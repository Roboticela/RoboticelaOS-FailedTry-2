// @ts-nocheck
/*
 * The desktop's design tokens — Phase 14.1's "one place a visual value comes from".
 *
 * Every value here is either read from `Documentation/Development/ColorPalette.md` or is a scale
 * this file *states* (spacing, radius, type, motion, layer). Nothing in the shell may write a
 * colour, a size or a duration of its own: a component names a token, and this file is where the
 * name is defined. That is what makes an appearance change a data change, which is the whole
 * reason the tokens exist rather than being a tidier way to write the same literals.
 *
 * Why there are **two** palettes and one is chosen at build time
 *
 * `ColorPalette.md` states a light and a dark value per token. The compiled artifact is a single
 * stylesheet of concrete values — RoboCSS in this build has no custom properties and no
 * `@media`, so `var(--brand-primary)` and a `prefers-color-scheme` block would both compile into
 * properties the engine does not read. The theme is therefore an *input* to the renderer
 * (`renderDesktopStyleSheet({ appearance })`), and the two palettes below are the two answers it
 * can be given. A runtime theme switch is then the same call with the other appearance, which is
 * a page regeneration rather than a second styling system.
 */

/** The palette's tokens, per appearance, with `ColorPalette.md`'s own names and values. */
export const palettes = {
  light: {
    brandPrimary: "#0E7AFF",
    brandPrimaryHover: "#0A6AE8",
    brandPrimaryActive: "#0757C7",
    brandPrimaryDisabled: "#B8D9FF",
    brandPrimarySubtle: "rgba(14,122,255,0.10)",
    brandPrimaryFocusRing: "rgba(14,122,255,0.25)",
    accentTeal: "#14B8A6",
    accentTealSubtle: "rgba(20,184,166,0.10)",
    success: "#30D158",
    warning: "#FF9F0A",
    danger: "#FF3B30",
    textPrimary: "#1D1D1F",
    textSecondary: "#6E6E73",
    bgPrimary: "#FFFFFF",
    bgSecondary: "#F5F5F7",
    bgTertiary: "#EDEDEF",
    borderDefault: "#D2D2D7",
  },
  dark: {
    brandPrimary: "#3B9EFF",
    brandPrimaryHover: "#5CADFF",
    brandPrimaryActive: "#2E8CE6",
    brandPrimaryDisabled: "#1E3A5C",
    brandPrimarySubtle: "rgba(59,158,255,0.15)",
    brandPrimaryFocusRing: "rgba(59,158,255,0.30)",
    accentTeal: "#2DD4C4",
    accentTealSubtle: "rgba(45,212,196,0.15)",
    success: "#30D158",
    warning: "#FF9F0A",
    danger: "#FF453A",
    textPrimary: "#FFFFFF",
    textSecondary: "#A1A1A6",
    bgPrimary: "#000000",
    bgSecondary: "#1C1C1E",
    bgTertiary: "#2C2C2E",
    borderDefault: "#38383A",
  },
};

/**
 * The spacing scale.
 *
 * Six steps and no seventh: a panel, a menu bar and a dock are all written from these, and a
 * component that needs a number between two of them has a layout problem that a token would hide.
 * The names are the ones the palette document and the component sheet both use.
 */
export const space = {
  xs: 4,
  sm: 8,
  md: 12,
  lg: 16,
  xl: 24,
  xxl: 32,
};

/**
 * Corner radii — and this build's honest limitation.
 *
 * `border-radius` is not read by RoboCSS or drawn by RoboPaint yet, so every radius here resolves
 * to a **square** corner today: the scale exists so that the day the property lands the shell
 * changes in this file rather than in forty rules. Writing `border-radius: 12px` in the sheet
 * would be a declaration the engine drops, which is worse than one this file names as pending.
 */
export const radius = {
  control: 6,
  card: 10,
  window: 12,
  dialog: 14,
  menu: 8,
};

/**
 * The type scale, in the desktop's own units.
 *
 * Sizes only: the *face* and the leading are Phase 12's, and a UI surface names a
 * `RoboTypographyRole` (`uiDisplay`/`uiText`/`uiCaption`/`uiMonospace`) rather than a family. The
 * measurements here are the document-side half of that binding — what the sheet asks RoboCSS for —
 * and the system pack answers with the face the role names.
 */
export const type = {
  uiDisplay: { size: 20, lineHeight: 28, weight: 600 },
  uiTitle: { size: 15, lineHeight: 22, weight: 600 },
  uiText: { size: 13, lineHeight: 20, weight: 400 },
  uiSmall: { size: 12, lineHeight: 18, weight: 400 },
  uiCaption: { size: 11, lineHeight: 16, weight: 500 },
  uiMonospace: { size: 12, lineHeight: 18, weight: 400 },
};

/**
 * Motion.
 *
 * Durations and easings, stated once. This build's compiled artifact is a *static* document — the
 * runtime has no JavaScript in the guest until Phase 16 — so nothing here animates yet; the scale
 * is what the compositor's transition phases (15.13) and the shell's own motion rules (14.8) will
 * read. A duration that no rule uses is a token with a name and no code behind it, and this file
 * says so rather than pretending: the shell writes no `transition` today because RoboCSS drops it.
 */
export const motion = {
  durationFast: 120,
  durationNormal: 200,
  durationSlow: 320,
  easingStandard: "cubic-bezier(0.32, 0.72, 0, 1)",
  easingEmphasized: "cubic-bezier(0.2, 0.9, 0.1, 1)",
  easingDecelerated: "cubic-bezier(0, 0, 0.2, 1)",
};

/**
 * The layer order, as **distances from the desktop's edges** rather than as `z-index` values.
 *
 * This is the shell's most important consequence of where the engine is: paint order is document
 * order, and this build reads no `z-index`, so what is on top is decided by the order the shell
 * writes its elements in. `layerOrder` names that order once — background, windows, chrome, overlays
 * — and the shell's composition reads it, so a new layer is an entry here and a component in the
 * right slot rather than a number somebody guesses. `zIndex` values are deliberately absent.
 */
export const layerOrder = ["wallpaper", "desktopIcons", "windows", "chrome", "overlays"];

/** The chrome's own measurements: a menu bar's height, a dock's, a title bar's. */
export const metrics = {
  menuBarHeight: 28,
  dockHeight: 68,
  dockItem: 48,
  dockIndicator: 4,
  titleBarHeight: 28,
  trafficLight: 12,
  trafficLightGap: 8,
  windowMinWidth: 240,
  windowMinHeight: 160,
  shadowStep: 1,
};

/** The desktop's bounding box, which is what a snap or a maximize resolves against. */
export const desktopBounds = {
  menuBar: metrics.menuBarHeight,
  dock: metrics.dockHeight,
};

/**
 * One appearance's tokens, with the scales that do not vary between appearances attached.
 *
 * A caller asks for `tokensFor("dark")` and writes `theme.textPrimary`; it never reaches into
 * `palettes`, and it never adds a value of its own. `appearance` comes back on the record so a
 * component can name the theme it was rendered under without a second lookup.
 */
export function tokensFor(appearance = "light") {
  const palette = palettes[appearance] ?? palettes.light;

  return {
    appearance: palettes[appearance] ? appearance : "light",
    ...palette,
    space,
    radius,
    type,
    motion,
    metrics,
    layerOrder,
  };
}
