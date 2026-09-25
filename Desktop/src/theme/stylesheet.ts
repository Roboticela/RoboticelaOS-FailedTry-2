/*
 * The desktop's stylesheet, generated from the tokens — one source of truth for both the
 * development preview and the compiled artifact.
 *
 * The sheet is a **string the build emits**, not a file somebody edits, and that is the point:
 * `Desktop/scripts/export-page.mjs` writes it into `RoboDesktop/Generated/DesktopPage.h` for the
 * guest, and the dev preview injects the same string into the page. Two copies of one stylesheet is
 * how a class gets renamed in the markup and styled only in the artifact.
 *
 * Every rule below names **one class**, and every class the markup writes has one. That is not
 * tidiness either: the scene's own drift check (`SuiteRoboDesktopScene.c`) walks the markup's
 * `class='…'` attributes and fails if the sheet styles no matching selector, so a variant that
 * wanted two classes (`class='win winFocused'`) would be a class the sheet cannot name. A variant
 * is therefore its own class — `win` and `winInactive` — and the component picks one.
 *
 * What the sheet may write, and why it is a short list
 *
 * This is the **compiled CSS of a document the RoboCSS/RoboLayout/RoboPaint build renders**, not a
 * browser's: `display: block` and `display: inline-block`, colours, `font-size`/`font-weight`/
 * `line-height`, `margin`, `padding`, a border per side, `background`, `width`/`height`, `opacity`,
 * and — since Phase 14.0 — `position` with its four insets. There is no flexbox, no grid, no
 * `border-radius`, no `box-shadow`, no `transition`, no custom property and no percentage; a
 * declaration outside the list compiles into a property the engine drops, which looks like a
 * styling mistake rather than a missing feature. The two consequences a reader will notice in the
 * rules below are therefore *deliberate*:
 *
 *   a **shadow** is a border on the window plus a hairline highlight, because `box-shadow` is
 *   Paint's and not this phase's, and a "depth" that is really a border is honest at a glance;
 *
 *   layers are ordered by **document order** rather than `z-index` (the token file says so), so
 *   what is on top is which component comes last.
 */

// Guest RoboCSS sheet generator — kept permissive so the export path stays stable.
// @ts-nocheck
import { tokensFor } from "./tokens";

/** One rule: a selector and a declaration map. Values are numbers (px) or strings. */
function px(value) {
  return typeof value === "number" ? `${value}px` : value;
}

/**
 * `rule` and `elementRule` differ in exactly one character, and it is the difference between a
 * stylesheet and a broken one: `rule("html, body", …)` writes `.html, body { … }` — a *class* selector
 * for `html` that matches nothing — so `html` keeps `display`'s initial `inline` while `body` is a
 * block, and the block pass refuses the document with "a block-level box participates in a context that
 * is not a block formatting context". Two doors rather than a flag because the failure is silent in
 * every direction that matters: the sheet still parses, the rule still applies to `body`, and the only
 * symptom is a page box with no height.
 */
function declarationsToBody(declarations) {
  const body = Object.entries(declarations)
    .filter(([, value]) => value !== undefined && value !== null && value !== "")
    .map(([name, value]) => {
      /*
       * `background` becomes `background-color`, deliberately. This build *does* expand the shorthand
       * (`RoboCSSBackground.c`), so either spelling reaches the same longhand — but the shorthand also
       * resets `background-image`, `-repeat`, `-position`, `-size` and `-attachment`, so a rule that
       * wanted a colour would silently declare five more things. A generated sheet writes the longhand
       * it means, and one place does the translation rather than fifty rules remembering it.
       */
      const property = name === "background" ? "background-color" : name;

      return `  ${property}: ${px(value)};`;
    })
    .join("\n");

  return body;
}

/** A rule for a class selector. */
function rule(selector, declarations) {
  return `.${selector} {\n${declarationsToBody(declarations)}\n}`;
}

/** A rule whose selector is written out in full — `html, body`, and anything else without a class. */
function elementRule(selector, declarations) {
  return `${selector} {\n${declarationsToBody(declarations)}\n}`;
}

/**
 * A rule for one class that **states its display**.
 *
 * This is the one thing a compiled sheet for this engine must not leave out, and it is not obvious:
 * `display`'s initial value is `inline` (`RoboCssPropertyRegistry.c`), which is CSS's own initial value,
 * and a browser's user-agent sheet is what turns `html`, `body` and a `div` into block boxes. **This
 * build has no user-agent sheet** — every declaration in the document is the author's — so a rule that
 * does not say `display` lays its element out as an *inline* box: it has no block-stacked rectangle,
 * its parent's height stays zero, and the whole document renders as a mostly empty page.
 *
 * That is not a hypothetical either. The first version of this sheet omitted it, and the symptom was a
 * desktop whose background, chrome and windows were all missing from a white client area while the text
 * *inside* them was laid out — one placed box out of eighty-five, and `texts = 28` in the report saying
 * the text had been shaped. The generated sheets of the earlier phases state `display` on every rule for
 * exactly this reason; this helper is how this sheet does.
 */
function blockRule(selector, declarations) {
  return rule(selector, { display: "block", ...declarations });
}

/**
 * The whole sheet for one appearance.
 *
 * `appearance` is `"light"` or `"dark"` (`ColorPalette.md`'s two columns); anything else falls back
 * to light, and the record that comes back says which one was used so a caller cannot be wrong
 * about what it rendered.
 *
 * `windows` is the one part of the sheet that is **data rather than style**: each entry is a rectangle
 * in the shell's own state, and this function emits a `.windowSlotN` rule for it. That is not a
 * workaround for a missing feature, it is how a *static* compiled document carries per-window
 * geometry — the guest has no JavaScript yet, so "where each window is" has to be in the sheet the
 * document is rendered against, and it is generated from the same state the markup was. A drag at
 * runtime rewrites the slot's rule; the surface class beside it never changes, which is why a
 * window's *position* and a window's *appearance* are two classes on two nested elements.
 */
export function renderDesktopStyleSheet({
  appearance = "light",
  windows = [],
}: {
  appearance?: string;
  windows?: { slot: string; x: number; y: number; width: number; height: number }[];
} = {}) {
  const t = tokensFor(appearance);
  const rules = [];

  /*
   * The document's own box, and the only rule here with an element selector.
   *
   * `html, body` is where a browser's user-agent sheet would put the page background, the zero margins
   * and — the part that matters — `display: block` on both. Stating it here is what makes everything
   * below inherit a page box with a real height, and it is also where the type scale the shell's text is
   * measured with begins, so a document that declared nothing else would still be legible.
   */
  rules.push(
    elementRule("html, body", {
      display: "block",
      margin: 0,
      padding: 0,
      "background-color": t.bgSecondary,
      color: t.textPrimary,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      "font-weight": t.type.uiText.weight,
    })
  );

  /* ---- The desktop surface ------------------------------------------------------------- */

  /*
   * The root box is the **document's** box, not the desktop's, and in this build that difference is
   * load-bearing: the root's height is `auto`, its geometry comes from its content, and every surface
   * below it is out of flow — so the root's height is **zero**, and a layer that resolved `top: 0;
   * bottom: 0` against it would be zero pixels tall. That is not a hypothetical: it is what the first
   * version of this sheet did, and the rendered frame was a white client area with the borders, the
   * text and the traffic lights correctly drawn on top of it.
   *
   * So the root carries **what a document inherits** — the text colour and the type scale — and every
   * surface is anchored to the **initial containing block** with `position: fixed`, which is the
   * viewport and has both dimensions. The shell's chrome is pinned to the screen by its nature (a menu
   * bar does not scroll with a document), so this is the honest expression of what these layers are
   * rather than a workaround.
   */
  rules.push(
    blockRule("desktop", {
      color: t.textPrimary,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      "font-weight": t.type.uiText.weight,
    })
  );
  /* The wallpaper is two surfaces rather than a gradient, which this build cannot paint: a calm
   * desk grey and a soft brand sheen below the menu bar — the Apple desk atmosphere in what the
   * engine has. */
  rules.push(
    blockRule("wallpaper", {
      position: "fixed",
      top: 0,
      right: 0,
      bottom: 0,
      left: 0,
      background: "#B8C4D4",
    })
  );
  rules.push(
    blockRule("wallpaperSheen", {
      position: "fixed",
      top: t.metrics.menuBarHeight,
      right: 0,
      left: 0,
      height: 320,
      background: "rgba(255,255,255,0.28)",
    })
  );

  /* ---- The desktop's icons ------------------------------------------------------------- */

  rules.push(
    blockRule("desktopIcons", { position: "fixed", top: 64, left: t.space.xl, width: 320 })
  );
  /*
   * An icon is an inline-block whose contents are **inline-level**, and the two facts are one decision:
   * an `inline-block`'s inner formatting context is a flow root, and this build's classification refuses
   * a block-level child in a context that is not a block formatting context (`layout.classification`
   * states it, and the diagnostic names the box). So a tile and a label inside an inline-block are
   * `inline-block` themselves and are centred by the parent's `text-align` rather than by auto margins —
   * which is what a real sheet would do anyway, and what makes each icon a row of inline boxes.
   */
  rules.push(
    blockRule("desktopIcon", {
      display: "inline-block",
      width: 96,
      "margin-right": t.space.md,
      "margin-bottom": t.space.lg,
      "text-align": "center",
    })
  );
  rules.push(
    blockRule("desktopIconTile", {
      display: "inline-block",
      width: 44,
      height: 44,
      background: t.bgPrimary,
      border: `1px solid ${t.borderDefault}`,
    })
  );
  rules.push(
    blockRule("desktopIconTileActive", {
      display: "inline-block",
      width: 44,
      height: 44,
      background: t.brandPrimary,
      border: `1px solid ${t.brandPrimaryActive}`,
    })
  );
  rules.push(
    blockRule("desktopIconLabel", {
      display: "inline-block",
      "font-size": t.type.uiCaption.size,
      "line-height": t.type.uiCaption.lineHeight,
      "font-weight": t.type.uiCaption.weight,
      color: t.textPrimary,
    })
  );

  /* ---- The window layer, and a window --------------------------------------------------- */

  rules.push(
    blockRule("windows", { position: "fixed", top: 0, right: 0, bottom: 0, left: 0 })
  );
  /* A window's **surface**: what one looks like, and deliberately not where it is. The slot below
   * carries the rectangle, so a drag moves the slot and the surface's own rules never change. */
  rules.push(
    blockRule("window", {
      background: t.bgPrimary,
      border: `1px solid #C6C6C8`,
    })
  );
  rules.push(
    blockRule("windowInactive", {
      background: "#FAFAFA",
      border: `1px solid #D2D2D7`,
    })
  );

  /* One slot per window in the state, in paint order. The components name these, so the drift check
   * between the markup and this sheet covers them exactly like a hand-written class. */
  for (const window of windows) {
    rules.push(
      rule(window.slot, {
        position: "absolute",
        left: window.x,
        top: window.y,
        width: window.width,
        height: window.height,
      })
    );
  }
  rules.push(
    blockRule("titleBar", {
      position: "relative",
      height: t.metrics.titleBarHeight,
      background: "#E8E8ED",
      "border-bottom": `1px solid #D2D2D7`,
    })
  );
  rules.push(
    blockRule("titleBarInactive", {
      position: "relative",
      height: t.metrics.titleBarHeight,
      background: "#F0F0F5",
      "border-bottom": `1px solid #E5E5EA`,
    })
  );
  /* The window title is a block with the left padding the traffic lights need: they are absolutely
   * positioned inside the title bar, so this is what keeps the two from overlapping. */
  rules.push(
    blockRule("titleText", {
      "padding-left": 72,
      "padding-top": 6,
      "font-size": 13,
      "line-height": 16,
      "font-weight": 600,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("titleTextInactive", {
      "padding-left": 72,
      "padding-top": 6,
      "font-size": 13,
      "line-height": 16,
      "font-weight": 600,
      color: t.textSecondary,
    })
  );
  const light = {
    position: "absolute",
    top: (t.metrics.titleBarHeight - t.metrics.trafficLight) / 2,
    width: t.metrics.trafficLight,
    height: t.metrics.trafficLight,
  };
  /* Traffic lights — Apple red / amber / green, quiet borders. */
  rules.push(blockRule("trafficClose", { ...light, left: 12, background: "#FF5F57", border: `1px solid #E0443E` }));
  rules.push(
    blockRule("trafficMinimize", {
      ...light,
      left: 12 + t.metrics.trafficLight + t.metrics.trafficLightGap,
      background: "#FEBC2E",
      border: `1px solid #DEA123`,
    })
  );
  rules.push(
    blockRule("trafficZoom", {
      ...light,
      left: 12 + 2 * (t.metrics.trafficLight + t.metrics.trafficLightGap),
      background: "#28C840",
      border: `1px solid #1AAB29`,
    })
  );
  rules.push(blockRule("windowContent", { padding: t.space.xl }));
  rules.push(blockRule("windowStatus", { padding: t.space.sm, background: t.bgSecondary, "border-top": `1px solid ${t.borderDefault}` }));

  /* ---- The menu bar, and the dock ------------------------------------------------------- */

  rules.push(
    blockRule("menuBar", {
      position: "fixed",
      top: 0,
      right: 0,
      left: 0,
      height: t.metrics.menuBarHeight,
      background: "rgba(246,246,248,0.92)",
      "border-bottom": `1px solid rgba(0,0,0,0.08)`,
      padding: `5px ${t.space.lg}`,
    })
  );
  rules.push(
    blockRule("menuBarBrand", {
      display: "inline-block",
      "font-size": 13,
      "line-height": 18,
      "font-weight": 700,
      color: t.textPrimary,
      "margin-right": t.space.md,
    })
  );
  rules.push(
    blockRule("menuBarTitle", {
      display: "inline-block",
      "font-size": 13,
      "line-height": 18,
      "font-weight": 600,
      color: t.textPrimary,
      "margin-right": t.space.lg,
    })
  );
  rules.push(
    blockRule("menuBarMenus", {
      display: "inline-block",
      "font-size": 13,
      "line-height": 18,
      "font-weight": 400,
      color: t.textPrimary,
    })
  );
  /* The two right-hand items are absolutely positioned inside the bar with a **declared width**,
   * which is what makes a far-side anchor place them: with `width: auto` the box would fill the bar
   * and the text would sit at the bar's left edge. */
  rules.push(
    blockRule("menuBarWorkspaces", {
      position: "absolute",
      top: 5,
      right: 200,
      width: 72,
      height: 18,
      "font-size": t.type.uiCaption.size,
      "line-height": t.type.uiCaption.lineHeight,
      "font-weight": t.type.uiCaption.weight,
      color: t.textSecondary,
    })
  );
  rules.push(
    blockRule("menuBarStatus", {
      position: "absolute",
      top: 5,
      right: t.space.lg,
      width: 176,
      height: 18,
      "font-size": 12,
      "line-height": 18,
      "font-weight": 500,
      color: t.textPrimary,
    })
  );
  /*
   * The dock is lifted above the desk — Apple's floating pill — with a primary fill and a hairline
   * so it does not dissolve into the wallpaper.
   */
  rules.push(
    blockRule("dock", {
      position: "fixed",
      bottom: t.space.lg,
      left: 0,
      right: 0,
      "margin-left": "auto",
      "margin-right": "auto",
      width: 392,
      height: t.metrics.dockHeight,
      background: "rgba(255,255,255,0.72)",
      border: `1px solid rgba(0,0,0,0.12)`,
      padding: `10px ${t.space.sm}`,
    })
  );
  /* The dock's items, on one line for the same reason the desktop's icons are: the bar is an
   * `inline-block` row and its contents are inline-level (see the icons above). */
  rules.push(
    blockRule("dockItem", {
      display: "inline-block",
      width: t.metrics.dockItem,
      height: t.metrics.dockItem,
      "margin-right": t.space.sm,
      "text-align": "center",
    })
  );
  const dockIconBase = {
    display: "inline-block",
    width: 36,
    height: 36,
    "margin-top": 2,
    border: `1px solid rgba(0,0,0,0.08)`,
  };
  rules.push(blockRule("dockIcon", { ...dockIconBase, background: "#8E8E93" }));
  rules.push(blockRule("dockIconRunning", { ...dockIconBase, background: t.brandPrimary }));
  rules.push(blockRule("dockIconFinder", { ...dockIconBase, background: "#5AC8FA" }));
  rules.push(blockRule("dockIconLab", { ...dockIconBase, background: "#0E7AFF" }));
  rules.push(blockRule("dockIconOverview", { ...dockIconBase, background: "#AF52DE" }));
  rules.push(blockRule("dockIconFiles", { ...dockIconBase, background: "#FF9F0A" }));
  rules.push(blockRule("dockIconSettings", { ...dockIconBase, background: "#8E8E93" }));
  rules.push(blockRule("dockIconTerminal", { ...dockIconBase, background: "#1D1D1F" }));
  /* The running indicator: a dot narrower than its item, centred by the item's own `text-align`.
   * Absent entirely for an app that is not running, which is a fact the markup carries rather than a
   * hidden element. */
  rules.push(
    blockRule("dockIndicator", {
      display: "inline-block",
      width: 4,
      height: t.metrics.dockIndicator,
      "margin-top": 4,
      background: t.textPrimary,
    })
  );

  /* ---- Overlays: notifications, menus, dialogs ------------------------------------------ */

  rules.push(
    blockRule("overlays", { position: "fixed", top: 0, right: 0, bottom: 0, left: 0 })
  );
  rules.push(
    blockRule("notification", {
      position: "absolute",
      top: t.metrics.menuBarHeight + t.space.md,
      right: t.space.lg,
      width: 300,
      background: t.bgPrimary,
      border: `1px solid ${t.borderDefault}`,
      padding: t.space.md,
    })
  );
  rules.push(
    blockRule("notificationTitle", {
      "font-size": t.type.uiTitle.size,
      "line-height": t.type.uiTitle.lineHeight,
      "font-weight": t.type.uiTitle.weight,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("notificationBody", {
      "font-size": t.type.uiSmall.size,
      "line-height": t.type.uiSmall.lineHeight,
      "font-weight": t.type.uiSmall.weight,
      color: t.textSecondary,
    })
  );
  rules.push(
    blockRule("menu", {
      position: "absolute",
      top: t.metrics.menuBarHeight,
      left: 96,
      width: 232,
      background: t.bgPrimary,
      border: `1px solid ${t.borderDefault}`,
      padding: t.space.sm,
    })
  );
  rules.push(
    blockRule("menuItem", {
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textPrimary,
      padding: `6px ${t.space.sm}`,
    })
  );
  rules.push(
    blockRule("menuItemSelected", {
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      background: t.brandPrimarySubtle,
      color: t.textPrimary,
      padding: `6px ${t.space.sm}`,
    })
  );
  rules.push(
    blockRule("menuSeparator", { height: 1, background: t.borderDefault, "margin-top": t.space.xs, "margin-bottom": t.space.xs })
  );
  rules.push(
    blockRule("dialog", {
      position: "absolute",
      top: 132,
      left: 0,
      right: 0,
      "margin-left": "auto",
      "margin-right": "auto",
      width: 360,
      background: t.bgPrimary,
      border: `1px solid ${t.borderDefault}`,
      padding: t.space.xl,
    })
  );
  rules.push(
    blockRule("dialogTitle", {
      "font-size": t.type.uiDisplay.size,
      "line-height": t.type.uiDisplay.lineHeight,
      "font-weight": t.type.uiDisplay.weight,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("dialogBody", {
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textSecondary,
      "margin-top": t.space.sm,
    })
  );
  rules.push(
    blockRule("actions", { "margin-top": t.space.lg })
  );

  /* ---- Controls: the window content's own surfaces -------------------------------------- */

  rules.push(
    blockRule("heading", {
      "font-size": t.type.uiDisplay.size,
      "line-height": t.type.uiDisplay.lineHeight,
      "font-weight": t.type.uiDisplay.weight,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("body", {
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textSecondary,
      "margin-top": t.space.xs,
    })
  );
  rules.push(
    blockRule("caption", {
      "font-size": t.type.uiCaption.size,
      "line-height": t.type.uiCaption.lineHeight,
      "font-weight": t.type.uiCaption.weight,
      color: t.textSecondary,
      "margin-top": t.space.sm,
    })
  );
  rules.push(
    blockRule("mono", {
      "font-size": t.type.uiMonospace.size,
      "line-height": t.type.uiMonospace.lineHeight,
      color: t.textPrimary,
    })
  );
  /* The field, and the focus ring as the *border* it is: a second border on the same side would be
   * two declarations for one edge, so the focused variant widens the one and colours it — which is
   * also why the ring is visible at a glance rather than only in the palette. */
  rules.push(
    blockRule("field", {
      background: t.bgPrimary,
      border: `1px solid ${t.borderDefault}`,
      padding: `6px ${t.space.sm}`,
      "margin-top": t.space.sm,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("fieldFocused", {
      background: t.bgPrimary,
      border: `2px solid ${t.brandPrimary}`,
      padding: `5px 7px`,
      "margin-top": t.space.sm,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("fieldDisabled", {
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
      padding: `6px ${t.space.sm}`,
      "margin-top": t.space.sm,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: "#C7C7CC",
    })
  );
  rules.push(
    blockRule("fieldError", {
      background: t.bgPrimary,
      border: `1px solid ${t.danger}`,
      padding: `6px ${t.space.sm}`,
      "margin-top": t.space.sm,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("button", {
      display: "inline-block",
      width: 132,
      padding: `6px ${t.space.md}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
      "font-size": t.type.uiSmall.size,
      "line-height": t.type.uiSmall.lineHeight,
      "font-weight": 600,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("buttonPrimary", {
      display: "inline-block",
      width: 132,
      padding: `6px ${t.space.md}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
      background: t.brandPrimary,
      border: `1px solid ${t.brandPrimaryActive}`,
      "font-size": t.type.uiSmall.size,
      "line-height": t.type.uiSmall.lineHeight,
      "font-weight": 600,
      color: "#FFFFFF",
    })
  );
  rules.push(
    blockRule("buttonActive", {
      display: "inline-block",
      width: 132,
      padding: `6px ${t.space.md}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
      background: t.brandPrimaryActive,
      border: `1px solid ${t.brandPrimaryActive}`,
      "font-size": t.type.uiSmall.size,
      "line-height": t.type.uiSmall.lineHeight,
      "font-weight": 600,
      color: "#FFFFFF",
    })
  );
  rules.push(
    blockRule("buttonDisabled", {
      display: "inline-block",
      width: 132,
      padding: `6px ${t.space.md}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
      "font-size": t.type.uiSmall.size,
      "line-height": t.type.uiSmall.lineHeight,
      "font-weight": 600,
      color: "#C7C7CC",
    })
  );
  rules.push(
    blockRule("sliderTrack", {
      height: 4,
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
      "margin-top": t.space.lg,
    })
  );
  rules.push(
    blockRule("sliderFill", { height: 4, width: 132, background: t.brandPrimary })
  );
  rules.push(
    blockRule("sliderKnob", {
      position: "absolute",
      left: 132,
      width: 16,
      height: 16,
      background: t.bgPrimary,
      border: `1px solid ${t.brandPrimaryActive}`,
    })
  );
  rules.push(
    blockRule("toggle", {
      display: "inline-block",
      width: 36,
      height: 20,
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
    })
  );
  rules.push(
    blockRule("toggleOn", {
      display: "inline-block",
      width: 36,
      height: 20,
      background: "#30D158",
      border: `1px solid #28C840`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
    })
  );
  rules.push(
    blockRule("checkbox", {
      display: "inline-block",
      width: 16,
      height: 16,
      background: t.bgPrimary,
      border: `1px solid ${t.borderDefault}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
    })
  );
  rules.push(
    blockRule("checkboxOn", {
      display: "inline-block",
      width: 16,
      height: 16,
      background: t.brandPrimary,
      border: `1px solid ${t.brandPrimaryActive}`,
      "margin-top": t.space.md,
      "margin-right": t.space.sm,
    })
  );
  rules.push(blockRule("row", { "margin-top": t.space.sm }));
  rules.push(
    blockRule("sectionLabel", {
      "font-size": 11,
      "line-height": 16,
      "font-weight": 600,
      color: t.textSecondary,
      "margin-top": t.space.lg,
    })
  );
  rules.push(
    blockRule("labBadge", {
      display: "inline-block",
      background: t.bgSecondary,
      border: `1px solid ${t.borderDefault}`,
      padding: `4px ${t.space.sm}`,
      "margin-top": t.space.sm,
      "margin-right": t.space.sm,
      "font-size": 11,
      "line-height": 16,
      color: t.textPrimary,
    })
  );

  /* ---- The specimen: typography as the hero the phase asks for -------------------------- */

  rules.push(
    blockRule("specimenTitle", {
      "font-size": 28,
      "line-height": 36,
      "font-weight": 600,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("specimenBody", {
      "font-size": t.type.uiText.size,
      "line-height": 22,
      color: t.textPrimary,
      "margin-top": t.space.xs,
    })
  );
  rules.push(
    blockRule("specimenMeta", {
      "font-size": t.type.uiCaption.size,
      "line-height": t.type.uiCaption.lineHeight,
      "font-weight": t.type.uiCaption.weight,
      color: t.textSecondary,
      "margin-top": t.space.md,
    })
  );

  return `${rules.join("\n\n")}\n`;
}

/** The default sheet, for a caller that has no appearance in mind. */
export const desktopStyleSheet = renderDesktopStyleSheet();
