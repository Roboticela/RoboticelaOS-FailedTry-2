/**
 * Cursor Lab — hover every RoboticelaOS cursor state (SVG data-URI + CSS).
 * Mirrors System/Cursors masters; runtime paints the same shapes from .rcur.
 */

export type CursorLabItem = {
  id: string;
  title: string;
  category: "navigation" | "resize" | "editing" | "status";
  stateName: string;
  hotspot32: { x: number; y: number };
  fallback: string;
  /** Compact light-theme SVG (viewBox 0 0 256 256), rewritten to 32×32 for CSS. */
  svg: string;
};

const stroke = "#0F172A";
const fill = "#FFFFFF";

function svg(body: string): string {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 256 256" fill="none">${body}</svg>`;
}

export const CURSOR_LAB_ITEMS: CursorLabItem[] = [
  {
    id: "RoboDefault",
    title: "Default Arrow",
    category: "navigation",
    stateName: "default",
    hotspot32: { x: 9, y: 6 },
    fallback: "default",
    svg: svg(
      `<path d="M70 50 L70 204 L108.5 171 L130.5 220.5 L152.5 209.5 L125 160 L180 160 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboPointer",
    title: "Pointer Hand",
    category: "navigation",
    stateName: "pointer",
    hotspot32: { x: 11, y: 6 },
    fallback: "pointer",
    svg: svg(
      `<path d="M86 50 L110 48 L120 140 L155 125 L175 140 L165 175 L200 185 L185 220 L95 215 L75 150 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboLink",
    title: "Link Pointer",
    category: "navigation",
    stateName: "pointer",
    hotspot32: { x: 11, y: 5 },
    fallback: "pointer",
    svg: svg(
      `<path d="M89 41 L110 45 L120 130 L160 120 L180 140 L170 180 L200 190 L185 220 L100 215 L70 150 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboGrab",
    title: "Grabbing Hand",
    category: "navigation",
    stateName: "grabbing",
    hotspot32: { x: 16, y: 16 },
    fallback: "grabbing",
    svg: svg(
      `<path d="M90 80 L130 70 L170 90 L200 130 L195 190 L150 220 L95 210 L70 160 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboMove",
    title: "Four-Way Move",
    category: "navigation",
    stateName: "move",
    hotspot32: { x: 16, y: 16 },
    fallback: "move",
    svg: svg(
      `<path d="M128 16 L84 60 H112 V112 H60 V84 L16 128 L60 172 V144 H112 V196 H84 L128 240 L172 196 H144 V144 H196 V172 L240 128 L196 84 V112 H144 V60 H172 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboCrosshair",
    title: "Precision Crosshair",
    category: "navigation",
    stateName: "crosshair",
    hotspot32: { x: 16, y: 16 },
    fallback: "crosshair",
    svg: svg(
      `<path d="M118 20 H138 V118 H236 V138 H138 V236 H118 V138 H20 V118 H118 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboResizeEW",
    title: "Horizontal Resize",
    category: "resize",
    stateName: "resize-h",
    hotspot32: { x: 16, y: 16 },
    fallback: "ew-resize",
    svg: svg(
      `<path d="M20 128 L64 84 V112 H193 V84 L237 128 L193 172 V144 H64 V172 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboResizeNS",
    title: "Vertical Resize",
    category: "resize",
    stateName: "resize-v",
    hotspot32: { x: 16, y: 16 },
    fallback: "ns-resize",
    svg: svg(
      `<path d="M128 20 L84 64 H112 V193 H84 L128 237 L172 193 H144 V64 H172 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboResizeNWSE",
    title: "Diagonal Resize (NW-SE)",
    category: "resize",
    stateName: "resize-diagonal",
    hotspot32: { x: 16, y: 16 },
    fallback: "nwse-resize",
    svg: svg(
      `<path d="M51.6 51.6 H113.9 L94.1 71.4 L185.3 162.6 L205.1 142.9 V205.1 H142.9 L162.6 185.3 L71.4 94.1 L51.6 113.9 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboResizeNESW",
    title: "Diagonal Resize (NE-SW)",
    category: "resize",
    stateName: "resize-diag-nesw",
    hotspot32: { x: 16, y: 16 },
    fallback: "nesw-resize",
    svg: svg(
      `<path d="M51.6 204.4 V142.1 L71.4 161.9 L162.6 70.7 L142.9 50.9 H205.1 V113.2 L185.3 93.3 L94.1 184.6 L113.9 204.4 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboResizeSplitEW",
    title: "Split Window Resize (H)",
    category: "resize",
    stateName: "col-resize",
    hotspot32: { x: 16, y: 16 },
    fallback: "col-resize",
    svg: svg(
      `<path d="M20 128 L64 84 V112 H118 V20 H138 V112 H192 V84 L236 128 L192 172 V142 H138 V236 H118 V142 H64 V172 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboResizeSplitNS",
    title: "Split Window Resize (V)",
    category: "resize",
    stateName: "row-resize",
    hotspot32: { x: 16, y: 16 },
    fallback: "row-resize",
    svg: svg(
      `<path d="M128 20 L172 64 H144 V118 H236 V138 H144 V192 H172 L128 236 L84 192 H112 V138 H20 V118 H112 V64 H84 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/>`,
    ),
  },
  {
    id: "RoboTextVertical",
    title: "Text Select (I-Beam)",
    category: "editing",
    stateName: "text",
    hotspot32: { x: 16, y: 16 },
    fallback: "text",
    svg: svg(
      `<path d="M120 40 H136 V216 H120 Z" fill="${fill}" stroke="${stroke}" stroke-width="8"/><path d="M96 48 H160" stroke="${stroke}" stroke-width="10" stroke-linecap="round"/><path d="M96 208 H160" stroke="${stroke}" stroke-width="10" stroke-linecap="round"/>`,
    ),
  },
  {
    id: "RoboTextHorizontal",
    title: "Vertical Text Select",
    category: "editing",
    stateName: "vertical-text",
    hotspot32: { x: 16, y: 16 },
    fallback: "vertical-text",
    svg: svg(
      `<path d="M40 120 H216 V136 H40 Z" fill="${fill}" stroke="${stroke}" stroke-width="8"/><path d="M48 96 V160" stroke="${stroke}" stroke-width="10" stroke-linecap="round"/><path d="M208 96 V160" stroke="${stroke}" stroke-width="10" stroke-linecap="round"/>`,
    ),
  },
  {
    id: "RoboEdit",
    title: "Drawing Pencil",
    category: "editing",
    stateName: "pencil",
    hotspot32: { x: 5, y: 28 },
    fallback: "crosshair",
    svg: svg(
      `<path d="M40 220 L78 202 L212 67 L193 48 L58 182 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/><path d="M40 220 L61 210 L50 199 Z" fill="${stroke}"/>`,
    ),
  },
  {
    id: "RoboNotAllowed",
    title: "Not Allowed",
    category: "status",
    stateName: "not-allowed",
    hotspot32: { x: 16, y: 16 },
    fallback: "not-allowed",
    svg: svg(
      `<circle cx="128" cy="128" r="84" fill="none" stroke="#DC2626" stroke-width="16"/><line x1="70" y1="186" x2="186" y2="70" stroke="#DC2626" stroke-width="16" stroke-linecap="round"/>`,
    ),
  },
  {
    id: "RoboWait",
    title: "Wait Spinner",
    category: "status",
    stateName: "wait",
    hotspot32: { x: 16, y: 16 },
    fallback: "wait",
    svg: svg(
      `<circle cx="128" cy="128" r="100" fill="none" stroke="#8E8E93" stroke-width="18" stroke-opacity="0.35"/><circle cx="128" cy="128" r="100" fill="none" stroke="#3B9EFF" stroke-width="18" stroke-linecap="round" stroke-dasharray="197 704" transform="rotate(-90 128 128)"/>`,
    ),
  },
  {
    id: "RoboWorking",
    title: "Working in Background",
    category: "status",
    stateName: "progress",
    hotspot32: { x: 7, y: 5 },
    fallback: "progress",
    svg: svg(
      `<g transform="scale(0.55)"><path d="M70 50 L70 204 L108.5 171 L130.5 220.5 L152.5 209.5 L125 160 L180 160 Z" fill="${fill}" stroke="${stroke}" stroke-width="8" stroke-linejoin="round"/></g><circle cx="192" cy="192" r="40" fill="none" stroke="#8E8E93" stroke-width="14" stroke-opacity="0.35"/><circle cx="192" cy="192" r="40" fill="none" stroke="#3B9EFF" stroke-width="14" stroke-linecap="round" stroke-dasharray="70 251" transform="rotate(-90 192 192)"/>`,
    ),
  },
];

export function cursorCssValue(item: CursorLabItem): string {
  const uri = "data:image/svg+xml;utf8," + encodeURIComponent(item.svg.replace(/\s+/g, " ").trim());
  return `url("${uri}") ${item.hotspot32.x} ${item.hotspot32.y}, ${item.fallback}`;
}
