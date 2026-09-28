/*
 * The desktop's stylesheet, generated from the tokens — one source of truth for both the
 * development preview and the compiled artifact.
 *
 * Guest documents are **application content only**. Native chrome owns the desk; this sheet
 * styles Overview and Input Lab bodies. Flex longhands are in play (Phase 18.3 / layout 19.3).
 * Silent no-ops (`border-radius`, `box-shadow`, `transform`, `transition`, `gap`) stay out.
 */

// @ts-nocheck
import { tokensFor } from "./tokens";

function px(value) {
  return typeof value === "number" ? `${value}px` : value;
}

function declarationsToBody(declarations) {
  const body = Object.entries(declarations)
    .filter(([, value]) => value !== undefined && value !== null && value !== "")
    .map(([name, value]) => {
      const property = name === "background" ? "background-color" : name;
      return `  ${property}: ${px(value)};`;
    })
    .join("\n");

  return body;
}

function rule(selector, declarations) {
  return `.${selector} {\n${declarationsToBody(declarations)}\n}`;
}

function elementRule(selector, declarations) {
  return `${selector} {\n${declarationsToBody(declarations)}\n}`;
}

/** Every class rule states display — this build has no user-agent sheet. */
function blockRule(selector, declarations) {
  return rule(selector, { display: "block", ...declarations });
}

function flexRule(selector, declarations) {
  return rule(selector, { display: "flex", ...declarations });
}

/**
 * The whole sheet for one appearance.
 *
 * `windows` is ignored: geometry lives on native windows now, not CSS slots.
 */
export function renderDesktopStyleSheet({
  appearance = "light",
} = {}) {
  const t = tokensFor(appearance);
  const rules = [];

  rules.push(
    elementRule("html, body", {
      display: "block",
      margin: 0,
      padding: 0,
      "background-color": t.bgPrimary,
      color: t.textPrimary,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      "font-weight": t.type.uiText.weight,
    })
  );

  /* ---- Application root ---------------------------------------------------- */

  rules.push(
    flexRule("appRoot", {
      "flex-direction": "column",
      "align-items": "stretch",
      padding: t.space.xl,
      "background-color": t.bgPrimary,
      color: t.textPrimary,
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      "font-weight": t.type.uiText.weight,
      "min-height": 200,
    })
  );
  rules.push(
    flexRule("appHeader", {
      "flex-direction": "column",
      "align-items": "flex-start",
      "margin-bottom": t.space.md,
    })
  );
  rules.push(
    blockRule("brandMark", {
      "font-size": 22,
      "line-height": 28,
      "font-weight": 600,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("appEyebrow", {
      "font-size": 12,
      "line-height": 16,
      "font-weight": 500,
      color: t.textSecondary,
      "margin-top": t.space.xs,
    })
  );
  rules.push(
    blockRule("appLead", {
      "font-size": t.type.uiText.size,
      "line-height": t.type.uiText.lineHeight,
      color: t.textSecondary,
      "margin-bottom": t.space.lg,
    })
  );
  rules.push(
    flexRule("phaseList", {
      "flex-direction": "column",
      "align-items": "stretch",
      "margin-bottom": t.space.lg,
    })
  );
  rules.push(
    flexRule("phaseRow", {
      "flex-direction": "row",
      "align-items": "baseline",
      "justify-content": "space-between",
      padding: `${t.space.sm} 0`,
      "border-bottom": `1px solid ${t.borderDefault}`,
    })
  );
  rules.push(
    blockRule("phaseLabel", {
      "font-size": t.type.uiSmall.size,
      "line-height": t.type.uiSmall.lineHeight,
      "font-weight": 600,
      color: t.textPrimary,
      "margin-right": t.space.md,
    })
  );
  rules.push(
    blockRule("phaseDetail", {
      "font-size": t.type.uiCaption.size,
      "line-height": t.type.uiCaption.lineHeight,
      color: t.textSecondary,
    })
  );
  rules.push(
    flexRule("appActions", {
      "flex-direction": "row",
      "align-items": "center",
      "flex-wrap": "wrap",
      "margin-top": t.space.md,
      "margin-bottom": t.space.md,
    })
  );
  rules.push(
    flexRule("appRow", {
      "flex-direction": "row",
      "align-items": "center",
      "flex-wrap": "wrap",
      "margin-top": t.space.sm,
    })
  );
  rules.push(
    blockRule("appStatus", {
      "margin-top": t.space.lg,
      padding: `${t.space.sm} ${t.space.md}`,
      "background-color": t.bgSecondary,
      border: `1px solid ${t.borderDefault}`,
      "font-size": 12,
      "line-height": 18,
      "font-weight": 500,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("appStatusActive", {
      "margin-top": t.space.lg,
      padding: `${t.space.sm} ${t.space.md}`,
      "background-color": t.brandPrimarySubtle,
      border: `1px solid ${t.brandPrimary}`,
      "font-size": 12,
      "line-height": 18,
      "font-weight": 600,
      color: t.brandPrimary,
    })
  );
  rules.push(
    blockRule("jsBadge", {
      "margin-top": t.space.sm,
      "font-size": 11,
      "line-height": 16,
      color: t.textSecondary,
    })
  );
  rules.push(
    blockRule("jsReady", {
      "margin-top": t.space.sm,
      "font-size": 11,
      "line-height": 16,
      "font-weight": 600,
      color: t.accentTeal,
    })
  );

  /* ---- Type and controls --------------------------------------------------- */

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

  const buttonBase = {
    display: "inline-block",
    width: 132,
    padding: `6px ${t.space.md}`,
    "margin-top": t.space.md,
    "margin-right": t.space.sm,
    "font-size": t.type.uiSmall.size,
    "line-height": t.type.uiSmall.lineHeight,
    "font-weight": 600,
  };
  rules.push(
    blockRule("button", {
      ...buttonBase,
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
      color: t.textPrimary,
    })
  );
  rules.push(
    blockRule("buttonPrimary", {
      ...buttonBase,
      background: t.brandPrimary,
      border: `1px solid ${t.brandPrimaryActive}`,
      color: "#FFFFFF",
    })
  );
  rules.push(
    blockRule("buttonActive", {
      ...buttonBase,
      background: t.brandPrimaryActive,
      border: `1px solid ${t.brandPrimaryActive}`,
      color: "#FFFFFF",
    })
  );
  rules.push(
    blockRule("buttonDisabled", {
      ...buttonBase,
      background: t.bgTertiary,
      border: `1px solid ${t.borderDefault}`,
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
  rules.push(blockRule("sliderFill", { height: 4, width: 132, background: t.brandPrimary }));

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

  return rules.join("\n\n");
}
