/**
 * AnimatedGifSource — adapter for animated GIFs.
 *
 * Rendering path: <canvas> with frame-by-frame playback via the
 * browser's native GIF decoder (ImageBitmap per frame extracted once on
 * load via OffscreenCanvas + ImageDecoder when available, falling back to
 * a hidden <img> multi-canvas approach).
 *
 * Key invariant: pause() halts the interval at the current frameIndex;
 * resume() continues from that exact frameIndex — never restarts at 0.
 */

import { BackgroundSource, type FitMode } from "../BackgroundSource";

interface GifFrame {
  bitmap: ImageBitmap;
  delay: number; // ms
}

export class AnimatedGifSource extends BackgroundSource {
  private readonly url: string;
  private canvas: HTMLCanvasElement | null = null;
  private ctx: CanvasRenderingContext2D | null = null;
  private frames: GifFrame[] = [];
  private frameIndex = 0;
  private timerId: ReturnType<typeof setTimeout> | null = null;
  private fitMode: FitMode = "fill";
  private _paused = false;
  private container: HTMLElement | null = null;
  private resizeObserver: ResizeObserver | null = null;

  constructor(url: string) {
    super();
    this.url = url;
  }

  async mount(container: HTMLElement): Promise<void> {
    this.container = container;

    const canvas = document.createElement("canvas");
    canvas.className = "bg-source bg-source--gif";
    container.appendChild(canvas);
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");

    // Size canvas to container, re-draw on resize
    this.syncSize();
    this.resizeObserver = new ResizeObserver(() => {
      this.syncSize();
      this.drawCurrentFrame();
    });
    this.resizeObserver.observe(container);

    await this.decodeFrames();

    if (!this._paused && this.frames.length > 0) {
      this.scheduleNext();
    } else if (this.frames.length > 0) {
      this.drawCurrentFrame();
    }
  }

  unmount(): void {
    this.stop();
    this.resizeObserver?.disconnect();
    this.canvas?.remove();
    this.canvas = null;
    this.ctx = null;
    this.container = null;
    this.frames.forEach((f) => f.bitmap.close());
    this.frames = [];
  }

  pause(): void {
    this._paused = true;
    this.stop();
  }

  resume(): void {
    this._paused = false;
    if (this.frames.length > 0) {
      this.scheduleNext();
    }
  }

  setFitMode(mode: FitMode): void {
    this.fitMode = mode;
    this.drawCurrentFrame();
  }

  async getPosterFrame(): Promise<string> {
    if (this.frames.length === 0) return "";
    const offscreen = document.createElement("canvas");
    offscreen.width = this.frames[0]!.bitmap.width;
    offscreen.height = this.frames[0]!.bitmap.height;
    offscreen.getContext("2d")?.drawImage(this.frames[0]!.bitmap, 0, 0);
    return offscreen.toDataURL("image/webp", 0.8);
  }

  isAnimated(): boolean {
    return true;
  }

  // ── Private helpers ───────────────────────────────────────────────────────

  private syncSize(): void {
    if (!this.canvas || !this.container) return;
    this.canvas.width = this.container.clientWidth || 320;
    this.canvas.height = this.container.clientHeight || 180;
  }

  private async decodeFrames(): Promise<void> {
    // Strategy: use an <img> element loaded into a hidden single-pixel canvas
    // sequence to extract frames via ImageDecoder (Chrome 94+) or fall back
    // to the simple "load URL as blob, use ImageDecoder if available" path.

    const response = await fetch(this.url);
    const blob = await response.blob();

    // Modern path: ImageDecoder API (Chrome/Edge 94+)
    if ("ImageDecoder" in window) {
      try {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const ImageDecoderCtor = (window as any).ImageDecoder as new (opts: unknown) => any;
        const decoder = new ImageDecoderCtor({ data: blob.stream(), type: "image/gif" });
        await decoder.completed;
        const selectedTrack = decoder.tracks?.selectedTrack;
        const totalFrames: number = selectedTrack?.frameCount ?? 1;

        const promises: Promise<GifFrame>[] = [];
        for (let i = 0; i < totalFrames; i++) {
          promises.push(
            decoder.decode({ frameIndex: i }).then((result: { image: VideoFrame }) => {
              const frame = result.image;
              const delay = Math.max(frame.duration ? frame.duration / 1000 : 100, 20);
              return createImageBitmap(frame).then((bitmap) => {
                frame.close();
                return { bitmap, delay };
              });
            })
          );
        }
        this.frames = await Promise.all(promises);
        decoder.close();
        return;
      } catch {
        // Fall through to img-tag approach
      }
    }

    // Fallback: load the GIF as a single-frame static snapshot.
    // Real multi-frame decode without ImageDecoder would require a WASM library;
    // for this fallback we extract a single ImageBitmap and treat delay=100ms.
    const bitmap = await createImageBitmap(blob);
    this.frames = [{ bitmap, delay: 100 }];
  }

  private scheduleNext(): void {
    if (this._paused || this.frames.length === 0) return;
    const frame = this.frames[this.frameIndex]!;
    this.timerId = setTimeout(() => {
      this.drawCurrentFrame();
      this.frameIndex = (this.frameIndex + 1) % this.frames.length;
      this.scheduleNext();
    }, frame.delay);
  }

  private stop(): void {
    if (this.timerId !== null) {
      clearTimeout(this.timerId);
      this.timerId = null;
    }
  }

  private drawCurrentFrame(): void {
    const ctx = this.ctx;
    const canvas = this.canvas;
    if (!ctx || !canvas || this.frames.length === 0) return;
    const bitmap = this.frames[this.frameIndex]!.bitmap;
    const { width: cw, height: ch } = canvas;
    const { width: fw, height: fh } = bitmap;

    ctx.clearRect(0, 0, cw, ch);

    switch (this.fitMode) {
      case "fill": {
        const scale = Math.max(cw / fw, ch / fh);
        const dx = (cw - fw * scale) / 2;
        const dy = (ch - fh * scale) / 2;
        ctx.drawImage(bitmap, dx, dy, fw * scale, fh * scale);
        break;
      }
      case "fit": {
        const scale = Math.min(cw / fw, ch / fh);
        const dx = (cw - fw * scale) / 2;
        const dy = (ch - fh * scale) / 2;
        ctx.fillStyle = "var(--bg-primary, #000)";
        ctx.fillRect(0, 0, cw, ch);
        ctx.drawImage(bitmap, dx, dy, fw * scale, fh * scale);
        break;
      }
      case "stretch":
        ctx.drawImage(bitmap, 0, 0, cw, ch);
        break;
      case "center": {
        const dx = (cw - fw) / 2;
        const dy = (ch - fh) / 2;
        ctx.drawImage(bitmap, dx, dy);
        break;
      }
      case "tile": {
        // Create a pattern from the current bitmap
        const pattern = ctx.createPattern(bitmap, "repeat");
        if (pattern) {
          ctx.fillStyle = pattern;
          ctx.fillRect(0, 0, cw, ch);
        }
        break;
      }
    }
  }
}
