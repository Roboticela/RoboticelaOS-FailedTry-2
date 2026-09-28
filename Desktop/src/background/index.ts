/**
 * Format Detection & Adapter Selection
 *
 * Routing priority (highest to lowest):
 *   1. selectAdapterFromFile(file)  — uses File.type (MIME) + reads SVG text for animation check
 *   2. selectAdapter(url)           — extension from pathname; blob:/data: URLs
 *                                     fall back to Content-Type sniffing via fetch
 *   3. selectAdapterSync(url)       — synchronous extension-only path (no SVG support)
 */

import { type BackgroundSource } from "./BackgroundSource";
import { ImageSource } from "./adapters/ImageSource";
import { VideoSource } from "./adapters/VideoSource";
import { AnimatedGifSource } from "./adapters/AnimatedGifSource";
import { AnimatedSvgSource } from "./adapters/AnimatedSvgSource";
import { GenerativeSource, type GenerativeConfig } from "./adapters/GenerativeSource";

export type { BackgroundSource, FitMode } from "./BackgroundSource";
export { GenerativeSource } from "./adapters/GenerativeSource";
export type { GenerativeConfig } from "./adapters/GenerativeSource";

// ── Internal helpers ──────────────────────────────────────────────────────────

function getExtension(url: string): string {
  try {
    const path = new URL(url).pathname;
    const dot = path.lastIndexOf(".");
    return dot === -1 ? "" : path.slice(dot + 1).toLowerCase();
  } catch {
    const clean = url.split("?")[0]!;
    const dot = clean.lastIndexOf(".");
    return dot === -1 ? "" : clean.slice(dot + 1).toLowerCase();
  }
}

/** Returns true if the SVG markup contains SMIL or CSS animations. */
async function isSvgTextAnimated(text: string): Promise<boolean> {
  return (
    /<animate[\s>]/i.test(text) ||
    /<animateTransform[\s>]/i.test(text) ||
    /<animateMotion[\s>]/i.test(text) ||
    /@keyframes/i.test(text) ||
    /animation\s*:/i.test(text)
  );
}

async function isSvgUrlAnimated(url: string): Promise<boolean> {
  try {
    const text = await fetch(url).then((r) => r.text());
    return isSvgTextAnimated(text);
  } catch {
    return false;
  }
}

/**
 * Map a MIME type + URL to the correct adapter.
 * `text` is pre-fetched SVG markup when available (saves a double-fetch).
 */
async function adapterFromMime(
  url: string,
  mime: string,
  svgText?: string
): Promise<BackgroundSource> {
  const type = mime.split(";")[0]!.trim().toLowerCase();

  if (type === "image/gif") return new AnimatedGifSource(url);
  if (type === "video/mp4" || type === "video/webm" || type === "video/quicktime" || type === "video/ogg")
    return new VideoSource(url);
  if (type === "image/svg+xml") {
    const text = svgText ?? (await fetch(url).then((r) => r.text()).catch(() => ""));
    const animated = await isSvgTextAnimated(text);
    return animated ? new AnimatedSvgSource(url) : new ImageSource(url);
  }
  if (
    type === "image/jpeg" ||
    type === "image/png" ||
    type === "image/webp" ||
    type === "image/avif" ||
    type === "image/bmp" ||
    type === "image/tiff" ||
    type.startsWith("image/")
  ) {
    return new ImageSource(url);
  }

  // Unknown MIME — attempt extension fallback before giving up
  const ext = getExtension(url);
  if (ext) {
    const byExt = await selectAdapterByExtension(url, ext);
    if (byExt) return byExt;
  }

  throw new Error(`Unsupported media type: ${mime}`);
}

/** Extension-only routing used as a fallback. Returns null when ambiguous. */
async function selectAdapterByExtension(
  url: string,
  ext: string
): Promise<BackgroundSource | null> {
  if (["mp4", "webm", "mov", "ogg"].includes(ext)) return new VideoSource(url);
  if (ext === "gif") return new AnimatedGifSource(url);
  if (["jpg", "jpeg", "png", "webp", "avif", "bmp", "tiff", "tif"].includes(ext))
    return new ImageSource(url);
  if (ext === "svg") {
    const animated = await isSvgUrlAnimated(url);
    return animated ? new AnimatedSvgSource(url) : new ImageSource(url);
  }
  return null;
}

// ── Public API ────────────────────────────────────────────────────────────────

/**
 * Select the right adapter for a local File object.
 * Uses File.type (MIME) — reliable regardless of filename.
 * For SVG files the raw text is read once and reused for animation detection.
 */
export async function selectAdapterFromFile(file: File): Promise<BackgroundSource> {
  const url = URL.createObjectURL(file);

  // If MIME is missing (some OSes omit it), fall back to filename extension
  if (!file.type) {
    const ext = getExtension(file.name);
    const adapter = await selectAdapterByExtension(url, ext);
    if (adapter) return adapter;
    // Last resort: treat as static image
    return new ImageSource(url);
  }

  // For SVG: read text once now so animation detection doesn't double-fetch
  if (file.type === "image/svg+xml") {
    const text = await file.text();
    return adapterFromMime(url, file.type, text);
  }

  return adapterFromMime(url, file.type);
}

/**
 * Full async adapter selection from a URL string.
 *
 * Routing order:
 *   1. Extension from pathname (fast, covers most CDN / direct links)
 *   2. For blob:/data: URLs — fetch and inspect Content-Type header
 *   3. Fallback: treat as static image (never throws)
 */
export async function selectAdapter(url: string): Promise<BackgroundSource> {
  const isBlob = url.startsWith("blob:");
  const isData = url.startsWith("data:");

  // blob:/data: have no meaningful extension — must inspect content
  if (isBlob || isData) {
    try {
      const response = await fetch(url);
      const mime = response.headers.get("Content-Type") ?? "";

      // For text-based types (SVG), capture text once
      if (mime.includes("svg")) {
        const text = await response.text();
        return adapterFromMime(url, mime, text);
      }
      if (mime) return adapterFromMime(url, mime);
    } catch {
      // fetch failed (e.g. revoked blob) — fall through to ImageSource
    }
    // Graceful fallback: try as a static image
    return new ImageSource(url);
  }

  // Normal URL: try extension first (cheap)
  const ext = getExtension(url);
  const byExt = await selectAdapterByExtension(url, ext);
  if (byExt) return byExt;

  // No recognized extension — sniff via Content-Type
  try {
    const response = await fetch(url, { method: "HEAD" });
    const mime = response.headers.get("Content-Type") ?? "";
    if (mime) return adapterFromMime(url, mime);
  } catch {
    // HEAD failed (CORS etc) — attempt GET for text types
    try {
      const response = await fetch(url);
      const mime = response.headers.get("Content-Type") ?? "";
      if (mime.includes("svg")) {
        const text = await response.text();
        return adapterFromMime(url, mime, text);
      }
      if (mime) return adapterFromMime(url, mime);
    } catch {
      // Completely unreachable — last resort
    }
  }

  // Absolute last resort: static image (better than crashing)
  return new ImageSource(url);
}

/** Synchronous adapter selection for known non-SVG extensions only. */
export function selectAdapterSync(url: string): BackgroundSource | null {
  const ext = getExtension(url);
  if (["mp4", "webm", "mov"].includes(ext)) return new VideoSource(url);
  if (ext === "gif") return new AnimatedGifSource(url);
  if (["jpg", "jpeg", "png", "webp", "avif"].includes(ext)) return new ImageSource(url);
  return null;
}

/** Convenience: create a generative live scene with optional config. */
export function createGenerativeSource(config?: GenerativeConfig): BackgroundSource {
  return new GenerativeSource(config);
}
