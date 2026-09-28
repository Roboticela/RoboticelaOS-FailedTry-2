/**
 * FolderIcon — Roboticela OS
 * Apple Big Sur-quality two-panel manila folder icon rendered entirely in SVG.
 *
 * ViewBox 512x512  ·  Safe area 44→468
 *   Back panel   y 60→336  (tab notch: y 60→96, x 44→214  |  body: x 44→468)
 *   Front panel  y 204→468, full width
 *   Opening gap  y 96→204  — where content peeks live
 *
 * Layers (bottom → top):
 *   1. Drop shadow filter
 *   2. Back panel (darker gradient)
 *   3. Opening-gap inner shadow (recessed feel)
 *   4. Content peeks clipped to folder bounds
 *   5. Front panel (lighter gradient) + rim highlight
 *   6. System glyph on front panel (overrides peeks)
 *   7. Badges (outside shadow)
 */

import { useId } from "react";
import type { CSSProperties } from "react";

// ─────────────────────────────────────────────────────────── Types ────────────

export type FolderColor =
  | "blue" | "gray" | "green" | "purple"
  | "red"  | "yellow" | "orange" | "pink";

export type SystemFolderKind =
  | "applications" | "documents" | "downloads"
  | "desktop" | "home";

export type FolderBadge = "shared" | "icloud" | "locked" | "alias";

export interface PeekItem {
  /** Colour used for the thumbnail accent header */
  accentColor?: string;
  /** Render as a tiny folder glyph instead of a file card */
  isFolder?: boolean;
}

export interface FolderIconProps {
  /** Folder tint. Default: "blue" */
  color?: FolderColor;
  /** Fixed system glyph — overrides content peeks */
  system?: SystemFolderKind;
  /** Content peeks (0-3 items) */
  peeks?: PeekItem[];
  /** Active badge overlays */
  badges?: FolderBadge[];
  /** Interaction state */
  state?: "rest" | "hover" | "selected" | "open" | "drag-over";
  /** Use dark-mode gradient palette */
  darkMode?: boolean;
  /** Rendered width & height in px. Default: 64 */
  size?: number;
  className?: string;
  style?: CSSProperties;
  "aria-label"?: string;
}

// ──────────────────────────────────────────────────────── Colour Palette ──────

interface ColorStop { frontTop: string; frontBottom: string; backTop: string; backBottom: string }
type ColorDef = { light: ColorStop; dark: ColorStop };

const COLORS: Record<FolderColor, ColorDef> = {
  blue:   { light: { frontTop:"#7DC4FF", frontBottom:"#3B9EFF", backTop:"#5CA8E8", backBottom:"#2070CC" },
             dark:  { frontTop:"#5AC8FA", frontBottom:"#0A84FF", backTop:"#3AAADA", backBottom:"#0060D0" } },
  gray:   { light: { frontTop:"#CACACC", frontBottom:"#8E8E93", backTop:"#ACACAE", backBottom:"#6A6A6E" },
             dark:  { frontTop:"#9D9DA0", frontBottom:"#636366", backTop:"#808083", backBottom:"#48484A" } },
  green:  { light: { frontTop:"#72E896", frontBottom:"#30D158", backTop:"#52C878", backBottom:"#20A840" },
             dark:  { frontTop:"#32D74B", frontBottom:"#1BAD36", backTop:"#20B038", backBottom:"#0D8826" } },
  purple: { light: { frontTop:"#DA8FFF", frontBottom:"#AF52DE", backTop:"#BA6EE0", backBottom:"#8838B8" },
             dark:  { frontTop:"#BF5AF2", frontBottom:"#8944AB", backTop:"#9E40D4", backBottom:"#6828A0" } },
  red:    { light: { frontTop:"#FF7F7C", frontBottom:"#FF3B30", backTop:"#DE5C58", backBottom:"#C41818" },
             dark:  { frontTop:"#FF453A", frontBottom:"#C0190E", backTop:"#D83028", backBottom:"#960E06" } },
  yellow: { light: { frontTop:"#FFE566", frontBottom:"#FFCC00", backTop:"#E0C244", backBottom:"#CCA800" },
             dark:  { frontTop:"#FFD60A", frontBottom:"#C8A800", backTop:"#D4B000", backBottom:"#9E8200" } },
  orange: { light: { frontTop:"#FFB347", frontBottom:"#FF8C00", backTop:"#E09228", backBottom:"#CC6A00" },
             dark:  { frontTop:"#FF9F0A", frontBottom:"#CC6800", backTop:"#D47E06", backBottom:"#9A4E00" } },
  pink:   { light: { frontTop:"#FF8FA8", frontBottom:"#FF2D55", backTop:"#E06E88", backBottom:"#C21040" },
             dark:  { frontTop:"#FF375F", frontBottom:"#D40038", backTop:"#D02048", backBottom:"#A0001C" } },
};

// ──────────────────────────────────────────────────────── SVG Geometry ────────

// Back panel — tab (x 44→214, y 60→96) + body (x 44→468, y 96→336)
// Squircle corners: cubic bezier with k≈0.75 (tighter than a circle's 0.552)
const BACK_PATH = [
  "M 60 60",
  "L 198 60",
  "C 210 60, 214 68, 214 76",      // tab top-right corner (r=16)
  "L 214 88",
  "C 214 96, 222 96, 234 96",      // shoulder: tab to body top
  "L 440 96",
  "C 461 96, 468 103, 468 124",    // body top-right corner (r=28)
  "L 468 308",
  "C 468 329, 461 336, 440 336",   // body bottom-right corner (r=28)
  "L 72 336",
  "C 51 336, 44 329, 44 308",      // body bottom-left corner (r=28)
  "L 44 76",
  "C 44 68, 48 60, 60 60",         // tab top-left corner (r=16)
  "Z",
].join(" ");

// Front panel — full-width rect y 204→468, squircle r=28
const FRONT_PATH_NORMAL = [
  "M 72 204",
  "L 440 204",
  "C 461 204, 468 211, 468 232",   // top-right corner
  "L 468 440",
  "C 468 461, 461 468, 440 468",   // bottom-right corner
  "L 72 468",
  "C 51 468, 44 461, 44 440",      // bottom-left corner
  "L 44 232",
  "C 44 211, 51 204, 72 204",      // top-left corner
  "Z",
].join(" ");

// Drag-over: front panel shifted up 8px (mouth "opens" wider)
const FRONT_PATH_DRAGOVER = [
  "M 72 196",
  "L 440 196",
  "C 461 196, 468 204, 468 224",
  "L 468 440",
  "C 468 461, 461 468, 440 468",
  "L 72 468",
  "C 51 468, 44 461, 44 440",
  "L 44 224",
  "C 44 204, 51 196, 72 196",
  "Z",
].join(" ");

// Clip union (back + front) — for constraining content peeks
const UNION_CLIP = `${BACK_PATH} ${FRONT_PATH_NORMAL}`;

// ─────────────────────────────────────────────────────── System Glyphs ────────

function GlyphHome() {
  return (
    <g transform="translate(256 348)" fill="white" fillOpacity={0.9}>
      <polygon points="0,-88 88,-12 -88,-12" />
      <rect x="-16" y="-88" width="26" height="38" rx="4" />
      <rect x="-68" y="-12" width="136" height="88" rx="10" />
      <rect x="-22" y="20"  width="44" height="56" rx="7" fill="rgba(0,0,0,0.22)" />
    </g>
  );
}

function GlyphDocuments() {
  return (
    <g transform="translate(256 344)">
      <rect x="-44" y="-82" width="88" height="116" rx="10"
            fill="white" fillOpacity={0.42} transform="rotate(6)" />
      <rect x="-48" y="-86" width="88" height="116" rx="10"
            fill="white" fillOpacity={0.60} transform="rotate(-4)" />
      <rect x="-48" y="-88" width="96" height="118" rx="10" fill="white" fillOpacity={0.92} />
      <polygon points="48,-88 48,-62 22,-88" fill="rgba(0,0,0,0.14)" />
      <rect x="-30" y="-34" width="60" height="8" rx="4" fill="rgba(0,0,0,0.18)" />
      <rect x="-30" y="-18" width="46" height="8" rx="4" fill="rgba(0,0,0,0.12)" />
      <rect x="-30" y="-2"  width="54" height="8" rx="4" fill="rgba(0,0,0,0.12)" />
    </g>
  );
}

function GlyphDownloads() {
  return (
    <g transform="translate(256 344)" fill="white" fillOpacity={0.92}>
      <path d="M-76,28 L-76,72 Q-76,84 -62,84 L62,84 Q76,84 76,72 L76,28 Q76,16 62,16 L-62,16 Q-76,16 -76,28 Z" />
      <rect x="-13" y="-82" width="26" height="88" rx="10" />
      <polygon points="-44,8 44,8 0,68" />
    </g>
  );
}

function GlyphDesktop() {
  return (
    <g transform="translate(256 340)" fill="white" fillOpacity={0.9}>
      <rect x="-84" y="-82" width="168" height="112" rx="14" />
      <rect x="-70" y="-68" width="140" height="84"  rx="7"  fill="rgba(0,0,0,0.22)" />
      <rect x="-11" y="30"  width="22"  height="28"  rx="4" />
      <rect x="-44" y="56"  width="88"  height="14"  rx="7" />
    </g>
  );
}

function GlyphApplications() {
  const ACCENT = ["#FF453A","#FF9F0A","#32D74B","#0A84FF","#BF5AF2","#FF375F","#5AC8FA","#FFD60A"];
  const POS = [[-60,-60],[0,-60],[60,-60],[-60,0],[0,0],[60,0],[-60,60],[0,60]];
  return (
    <g transform="translate(256 340)">
      {POS.map(([cx, cy], i) => (
        <rect key={i} x={(cx??0)-22} y={(cy??0)-22} width="44" height="44"
              rx="10" fill={ACCENT[i%ACCENT.length]} fillOpacity={0.88} />
      ))}
    </g>
  );
}

// ─────────────────────────────────────────────────────── Content Peeks ────────

const PEEK_ACCENTS = ["#0A84FF","#FF9F0A","#32D74B","#BF5AF2","#FF375F"];

interface PeekPos { x: number; y: number; r: number }
const PEEK_LAYOUTS: Record<number, PeekPos[]> = {
  1: [{ x:256, y:152, r:0 }],
  2: [{ x:204, y:155, r:-6 }, { x:308, y:155, r:6 }],
  3: [{ x:172, y:158, r:-10 }, { x:256, y:148, r:0 }, { x:340, y:158, r:10 }],
};

function PeekCard({ x, y, r, accent, isFolder }: PeekPos & { accent: string; isFolder?: boolean }) {
  if (isFolder) {
    return (
      <g transform={`translate(${x},${y}) rotate(${r})`}>
        <rect x="-36" y="-28" width="72" height="42" rx="5" fill={accent} fillOpacity={0.78} />
        <rect x="-36" y="-40" width="28" height="16" rx="4" fill={accent} fillOpacity={0.62} />
        <rect x="-36" y="-14" width="72" height="52" rx="5" fill={accent} fillOpacity={0.9} />
      </g>
    );
  }
  return (
    <g transform={`translate(${x},${y}) rotate(${r})`}>
      <rect x="-38" y="-48" width="76" height="92" rx="8" fill="white" fillOpacity={0.95} />
      <rect x="-38" y="-48" width="76" height="26" rx="8" fill={accent} />
      <rect x="-38" y="-34" width="76" height="12"         fill={accent} />
      <polygon points="38,-48 38,-30 20,-48" fill="rgba(0,0,0,0.12)" />
      <rect x="-24" y="-4"  width="48" height="7" rx="3.5" fill="rgba(0,0,0,0.11)" />
      <rect x="-24" y="10"  width="36" height="7" rx="3.5" fill="rgba(0,0,0,0.08)" />
      <rect x="-24" y="24"  width="42" height="7" rx="3.5" fill="rgba(0,0,0,0.08)" />
    </g>
  );
}

// ─────────────────────────────────────────────────────────── Badges ──────────

function BadgeShared({ cx, cy }: { cx: number; cy: number }) {
  return (
    <g transform={`translate(${cx},${cy})`}>
      <circle r="32" fill="white" />
      <g fill="#0A84FF">
        <circle cx="-7" cy="-8" r="11" />
        <path d="M-22,14 Q-22,4 -7,4 Q8,4 8,14 Z" />
        <circle cx="9" cy="-10" r="9" fillOpacity={0.65} />
        <path d="M-3,13 Q-3,4 9,4 Q22,4 22,13 Z" fillOpacity={0.65} />
      </g>
    </g>
  );
}

function BadgeCloud({ cx, cy }: { cx: number; cy: number }) {
  return (
    <g transform={`translate(${cx},${cy})`}>
      <circle r="32" fill="white" />
      <g fill="#0A84FF">
        <path d="M-18,-6 Q-20,-22 -4,-24 Q4,-26 10,-16 Q16,-28 26,-18 Q38,-16 32,-4 Q20,-4 -6,-4 Z" />
        <path d="M0,2 L0,18" stroke="#0A84FF" strokeWidth="5" strokeLinecap="round" fill="none" />
        <path d="M-10,8 L0,20 L10,8" stroke="#0A84FF" strokeWidth="5"
              strokeLinecap="round" strokeLinejoin="round" fill="none" />
      </g>
    </g>
  );
}

function BadgeLocked({ cx, cy }: { cx: number; cy: number }) {
  return (
    <g transform={`translate(${cx},${cy})`}>
      <circle r="32" fill="white" />
      <rect x="-14" y="-2"  width="28" height="22" rx="5" fill="#8E8E93" />
      <path d="M-10,-2 Q-10,-22 0,-22 Q10,-22 10,-2"
            fill="none" stroke="#8E8E93" strokeWidth="7" strokeLinecap="round" />
      <circle cx="0" cy="9" r="4.5" fill="white" />
    </g>
  );
}

function BadgeAlias({ cx, cy }: { cx: number; cy: number }) {
  return (
    <g transform={`translate(${cx},${cy})`}>
      <circle r="28" fill="white" />
      <path d="M -8 -14 C -8 -20 14 -20 14 -6 L 14 4 L 22 -4 L 22 18 L 0 18 L 8 10 L 8 -2 C 8 -10 -2 -10 -2 -4 Z"
            fill="#8E8E93" />
    </g>
  );
}

// ──────────────────────────────────────────────────────── Main Component ──────

export function FolderIcon({
  color = "blue",
  system,
  peeks = [],
  badges = [],
  state = "rest",
  darkMode = false,
  size = 64,
  className,
  style,
  "aria-label": ariaLabel,
}: FolderIconProps) {
  // Stable per-instance IDs for SVG defs (avoids gradient collisions in DOM)
  const rawId = useId();
  const uid = rawId.replace(/:/g, "_");

  const palette = COLORS[color]![darkMode ? "dark" : "light"]!;

  const isDragOver = state === "drag-over";
  const activeFrontPath = isDragOver ? FRONT_PATH_DRAGOVER : FRONT_PATH_NORMAL;

  const filterStyle: string | undefined =
    state === "selected" ? "brightness(1.05)" :
    state === "open"     ? "brightness(1.08)" :
    isDragOver           ? "brightness(1.10)" :
    undefined;

  // Badge layout — right-side badges stacked horizontally
  const rightBadges = badges.filter((b): b is Exclude<FolderBadge,"alias"> => b !== "alias");
  const BADGE_D = 68; // circle diameter
  const BADGE_G = 6;  // gap between badges
  const rowWidth = rightBadges.length * BADGE_D + Math.max(0, rightBadges.length - 1) * BADGE_G;
  const firstBadgeCx = 468 - rowWidth / 2;

  const peekCount = Math.min(peeks.length, 3);
  const peekLayout = PEEK_LAYOUTS[peekCount] ?? [];

  return (
    <svg
      viewBox="0 0 512 512"
      width={size}
      height={size}
      className={className}
      style={{ filter: filterStyle, display: "block", ...style }}
      aria-label={ariaLabel ?? `${system ?? color} folder`}
      role="img"
    >
      <defs>
        {/* Back panel gradient — slightly darker/desaturated */}
        <linearGradient id={`bg_${uid}`} x1="0.15" y1="0" x2="0.85" y2="1">
          <stop offset="0%"   stopColor={palette.backTop} />
          <stop offset="100%" stopColor={palette.backBottom} />
        </linearGradient>

        {/* Front panel gradient — full brightness */}
        <linearGradient id={`fg_${uid}`} x1="0.15" y1="0" x2="0.85" y2="1">
          <stop offset="0%"   stopColor={palette.frontTop} />
          <stop offset="100%" stopColor={palette.frontBottom} />
        </linearGradient>

        {/* Opening-gap inner-shadow: top dark -> transparent */}
        <linearGradient id={`gap_${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%"   stopColor="rgba(0,0,0,0.18)" />
          <stop offset="100%" stopColor="rgba(0,0,0,0.0)" />
        </linearGradient>

        {/* Front panel rim highlight */}
        <linearGradient id={`rim_${uid}`} x1="0" y1="0" x2="1" y2="0">
          <stop offset="0%"   stopColor="rgba(255,255,255,0.30)" />
          <stop offset="50%"  stopColor="rgba(255,255,255,0.18)" />
          <stop offset="100%" stopColor="rgba(255,255,255,0.06)" />
        </linearGradient>

        {/* Clip union: constrains content peeks within folder silhouette */}
        <clipPath id={`clip_${uid}`}>
          <path d={UNION_CLIP} />
        </clipPath>

        {/* Folder drop shadow */}
        <filter id={`shadow_${uid}`} x="-6%" y="-4%" width="112%" height="116%">
          <feDropShadow dx="0" dy="4" stdDeviation="8" floodColor="rgba(0,0,0,0.22)" />
        </filter>
      </defs>

      {/* Everything inside the shadow filter so the whole icon casts one shadow */}
      <g filter={`url(#shadow_${uid})`}>

        {/* 1 — Back panel */}
        <path d={BACK_PATH} fill={`url(#bg_${uid})`} />

        {/* 2 — Opening-gap inner shadow */}
        <rect x="44" y="96" width="424" height="112"
              fill={`url(#gap_${uid})`}
              clipPath={`url(#clip_${uid})`} />

        {/* 3 — Content peeks (rendered back-to-front) */}
        {!system && peekCount > 0 && (
          <g clipPath={`url(#clip_${uid})`}>
            {peeks.slice(0, peekCount).map((item, i) => {
              const pos = peekLayout[i];
              if (!pos) return null;
              return (
                <PeekCard
                  key={i}
                  x={pos.x} y={pos.y} r={pos.r}
                  accent={item.accentColor ?? PEEK_ACCENTS[i % PEEK_ACCENTS.length]!}
                  isFolder={item.isFolder}
                />
              );
            })}
          </g>
        )}

        {/* 4 — Front panel */}
        <path d={activeFrontPath} fill={`url(#fg_${uid})`} />

        {/* 5 — Rim highlight along top edge of front panel */}
        <path
          d={isDragOver
            ? "M 72 196 L 440 196 C 461 196, 468 204, 468 224"
            : "M 72 204 L 440 204 C 461 204, 468 211, 468 232"}
          fill="none"
          stroke={`url(#rim_${uid})`}
          strokeWidth="2.5"
          strokeLinecap="round"
        />

        {/* 6 — System glyph (fixed; overrides peek behaviour) */}
        {system === "home"         && <GlyphHome />}
        {system === "documents"    && <GlyphDocuments />}
        {system === "downloads"    && <GlyphDownloads />}
        {system === "desktop"      && <GlyphDesktop />}
        {system === "applications" && <GlyphApplications />}

      </g>

      {/* 7 — Badges (outside shadow — always sharp, never blurred) */}

      {/* Alias badge — bottom-LEFT */}
      {badges.includes("alias") && <BadgeAlias cx={100} cy={426} />}

      {/* Right-side badges — horizontally stacked, bottom-right */}
      {rightBadges.map((badge, i) => {
        const cx = firstBadgeCx + i * (BADGE_D + BADGE_G);
        const cy = 428;
        if (badge === "shared") return <BadgeShared key={badge} cx={cx} cy={cy} />;
        if (badge === "icloud") return <BadgeCloud  key={badge} cx={cx} cy={cy} />;
        if (badge === "locked") return <BadgeLocked key={badge} cx={cx} cy={cy} />;
        return null;
      })}
    </svg>
  );
}
