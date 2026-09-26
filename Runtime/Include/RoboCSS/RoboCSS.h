/*
 * RoboCSS module lifecycle (Phase 9.1).
 *
 * Module state machine, the same shape RoboHTML and RoboWindow use:
 *
 *   uninitialized → initialized → uninitialized
 *
 *   - CssInit validates its foundation (RoboBase is up; RoboMem needs no
 *     initialization of its own) and refuses double initialization with
 *     RoboAlreadyExists.
 *   - CssShutdown destroys every live stylesheet in slot order and
 *     releases the module table. It is idempotent.
 *   - every stylesheet entry point requires the module to be initialized and
 *     answers RoboInitError otherwise — deterministically, never by crash.
 *
 * The module owns one thing: the stylesheet slot table. Property tables, style
 * contexts, parsers and every model object belong to their callers, which is
 * why shutdown has nothing else to free.
 *
 * Capabilities, not marketing: CssGetCapabilities reports what this
 * build actually does. The model flags are 1, and so are the behaviour flags of
 * every phase that has landed — the tokenizer (9.2), the grammar and inline
 * parsing (9.3), selector parsing and matching (9.4), the cascade (9.5),
 * specificity (9.6), the inheritance behind a cascade (9.7), computed values
 * (9.8) and colours (9.9). The flags that remain 0 are default styles and
 * loading, and each of those names work this build does not do. A caller that
 * wants to know whether CSS will style anything should read this rather than
 * infer it from a version number.
 *
 * A flag is 1 only when the thing it names works end to end. The rule has real
 * teeth at the boundaries: `cascade` waited for `specificity` rather than
 * claiming an ordering it could not complete, and `computedValues` was 0 while
 * `inheritance` was already 1, because a *resolved* style is a different thing
 * from a computed one and for a phase only the second existed. Both are 1 now,
 * and neither claims anything about layout: a computed value here is a length, a
 * percentage, a colour or a keyword, not a used one.
 */

#ifndef ROBO_CSS_H
#define ROBO_CSS_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Null-terminated RoboCSS version string (static; do not free). */
const char *CssVersionString(void);

/** Numeric RoboCSS version components; NULL out → RoboInvalidArgument. */
Result CssGetVersion(I32 *outMajor,
                                   I32 *outMinor,
                                   I32 *outPatch);

/**
 * Initialize RoboCSS.
 * @return RoboOk; RoboAlreadyExists if already initialized;
 *         RoboInitError when RoboBase is down.
 */
Result CssInit(void);

/** Shut down RoboCSS: destroy every live stylesheet. Idempotent. */
void CssShutdown(void);

/** @return 1 if the module is initialized, else 0. */
int CssIsInitialized(void);

/** Number of live stylesheets (0 when uninitialized). */
size_t CssLiveStylesheetCount(void);

/**
 * What this build of RoboCSS can do.
 *
 * The model flags describe the data types of this phase; the behaviour flags
 * describe the phases that have not run. A flag is 1 only when the thing it
 * names works end to end, so no caller can be misled by a half-built feature
 * claiming to be present.
 */
typedef struct CssCapabilities {
    /* --- model: present in 9.1 --- */
    /** The source model: bytes, positions, ranges (9.1). */
    int sourceModel;
    /** The token model: the token vocabulary and a token bound to a range (9.1). */
    int tokenModel;
    /** The component-value model: token/function/block/list trees (9.1). */
    int valueModel;
    /** Property identity: the table, registration and lookup (9.1). */
    int propertyRegistry;
    /** The declaration model, declaration lists, and importance (9.1). */
    int declarationModel;
    /** The selector model: simple/compound/complex selectors and lists (9.1). */
    int selectorModel;
    /** The rule model: style rules, at-rules and rule lists (9.1). */
    int ruleModel;
    /** The stylesheet model: origin, state, source, rules (9.1). */
    int stylesheetModel;
    /** The CSS error vocabulary and the parser cursor (9.1). */
    int errorModel;

    /* --- behaviour --- */
    /** The tokenizer state machine: CSS bytes in, tokens out (9.2). */
    int tokenizer;
    /**
     * The stylesheet/declaration grammar: tokens in, rules, declarations and
     * component values out (9.3).
     */
    int parser;
    /**
     * The selector grammar: a rule's prelude in, a selector list out, names
     * resolved and arguments (An+B, `:not()`) parsed (9.4).
     */
    int selectorParsing;
    /** Selector matching against an element host (9.4). */
    int selectorMatching;
    /**
     * The cascade order (9.5): collecting what applies to an element in cascade
     * order and resolving a property by origin, importance and document order.
     */
    int cascade;
    /**
     * Specificity (9.6): the third cascade key, a property of the selector that
     * matched. It is what turns 9.5's reported-undecided contest between two
     * candidates of one level from different selectors into a decision.
     */
    int specificity;
    /**
     * Inheritance (9.7): the properties that propagate by default, the
     * CSS-wide keywords, and a resolved style per element carrying where every
     * value came from. A resolved value is *not* a computed one: a length is
     * still `10px` here, and a colour is still `red`.
     */
    int inheritance;
    /** Computed values (9.8). */
    int computedValues;
    /**
     * Colours (9.9): the standard named set, all four hex lengths,
     * `rgb()`/`rgba()` and `hsl()`/`hsla()` in both argument syntaxes,
     * `transparent`, and `currentColor` as a contextual colour that the computed
     * style resolves against the element's — or its parent's — `color`.
     *
     * What it does not claim is the colour spaces this build has no
     * representation for (`hwb`, `lab`, `lch`, `oklab`, `oklch`, `color()`,
     * `color-mix`): those are carried and reported as deferred, not computed.
     */
    int colors;
    /**
     * Units and numbers (9.10): the closed unit vocabulary across its four
     * families (absolute, font-relative, viewport, and the small/large/dynamic
     * viewport variants), absolute conversion to the reference pixel, and a
     * typed numeric value — number, integer, percentage or dimension — with
     * deterministic equality and one canonical spelling.
     *
     * What it does not claim is resolution: `2em` and `50vh` are carried with
     * their unit and their number, and `CssUnitResolvePx` answers
     * "needs context" rather than inventing a font size or a viewport. `calc()`
     * is likewise deferred, not evaluated.
     */
    int units;
    /**
     * The box model (9.11): the four sides in CSS's order, the three shorthands
     * this build distributes (`margin`, `padding`, `border-width`) and the
     * longhand each side of each stands for, and typed computed values for the
     * twelve box longhands — a length, a percentage, `auto` for an edge offset,
     * the three width keywords for a border.
     *
     * What it does not claim is layout. A box here is a set of *values* with a
     * shape: no edge is measured, no margin collapses, `50%` stays a percentage
     * and `auto` stays the keyword it is. `border-width` is distributed although
     * nothing paints a border, because the four widths are the four values and a
     * shorthand no longhand ever saw is a declaration no later phase can consume.
     */
    int boxModel;
    /**
     * `display` (9.12): the closed vocabulary — `none`, `block`, `inline`,
     * `inline-block` — as a typed value carrying what CSS Display 3 calls its
     * outer and inner halves, with the classifications a layout pass asks
     * (`generatesBox`, `is_block_level`, `is_inline_level`, `is_inline_block`)
     * and a typed accessor on the computed value. Reading it for an element is
     * the style context's query door rather than an accessor on the style
     * object, because `display` does not inherit and an element that declares
     * nothing therefore has no entry: only the query door can answer the
     * property's initial value and say that is what it answered.
     *
     * What it does not claim is layout, and `display: none` is the boundary that
     * matters: it is represented as the *absence of a box* rather than as zero
     * geometry, so nothing here removes an element from a tree that does not
     * exist yet. The multi-keyword syntax (`display: inline flex`) and the values
     * this build has not added (`flex`, `grid`, `table`, `contents`) are refused
     * rather than approximated — the vocabulary's outer/inner columns are where
     * they will arrive.
     */
    int display;
    /**
     * Positioning (9.13): the closed `position` vocabulary — `static`,
     * `relative`, `absolute`, `fixed`, `sticky` — as a typed value carrying
     * whether its box stays in flow and whether it is a positioned box, with the
     * classifications a later phase dispatches on; the four inset properties
     * `top`, `right`, `bottom` and `left` as values (a length, a percentage, or
     * `auto`); and `z-index` as `auto` or a whole number.
     *
     * What it does not claim is geometry. No containing block is resolved, no
     * offset is applied, no stacking order exists, and `position: sticky` is
     * represented and classified but not *behaved* on — what a sticky box does
     * relative to a scrollport belongs to the phase that has one. `top: 20%`
     * stays a percentage because only layout knows what it is a percentage of,
     * and an `auto` inset stays the keyword: it is not zero, and turning it into
     * zero would place every absolutely positioned box against the top-left
     * corner of its containing block.
     */
    int positioning;
    /**
     * Backgrounds (9.14): the six longhands — `background-color` (a colour),
     * `background-image` (`none`, or a reference this build carries),
     * `background-position` (a pair of components, one per axis),
     * `background-size` (`auto`, `cover`, `contain`, or a pair),
     * `background-repeat` (a closed four-value vocabulary) and
     * `background-attachment` (a closed two-value one) — plus the `background`
     * shorthand, distributed over all six.
     *
     * What it does not claim is paint or layout. Nothing draws a background,
     * nothing loads an image, and nothing resolves a `background-position`
     * percentage against a box: `25% 50%` stays two percentages because only the
     * phase with a box can say 25% of what. Multiple background *layers* are
     * refused rather than half-read, since the shorthand grammar for them is a
     * list of layers and each layer is a whole set of longhands.
     */
    int backgrounds;
    /**
     * The border family (9.15): the four widths, four styles and four colours as
     * typed computed values, plus the five declarations that can set them — the
     * three positional rows (`border-width`, `border-style`, `border-color`), the
     * four side shorthands and `border` — distributed by a pass that resolves each
     * longhand against *every* declaration speaking for it through the cascade's
     * own ordering.
     *
     * What it does not claim is paint or geometry. Nothing draws a border, nothing
     * measures one, and `none` and `hidden` are typed values whose consequence —
     * a used width of zero whatever `border-width` says — belongs to the phase
     * with a box. `border-radius` is not a value this build has, and `border`
     * covers the three kinds this build represents rather than the four styles and
     * four colours a collapsing-table model would need.
     */
    int borders;
    /**
     * The overflow family (10.13): `overflow`, `overflow-x` and `overflow-y` as
     * typed values of a closed two-value vocabulary — `visible` and `hidden` —
     * each carrying whether it clips the axis it belongs to.
     *
     * Registered by a *layout* phase rather than by one of the 9.x value phases,
     * and for a reason that is worth reading here: `overflow` is the first property
     * in this build whose value changes geometry, so the phase that has a box chose
     * to make it a value that can be refused rather than a token list carried
     * through. The shorthand's own name takes the same shape, which is how
     * `overflow: hidden` reaches a consumer without the shorthand expansion this
     * build does not have.
     *
     * What it does not claim is scrolling or clipping. Nothing here scrolls, no
     * scrollbar exists, and the clipping boundary — the box's padding edge — belongs
     * to the phase that has the box. `scroll`, `auto` and `clip` are rows this build
     * has not added rather than words it does not know.
     */
    int overflow;
    /**
     * Parsing an element's `style` attribute — the 9.1 boundary, filled by the
     * 9.3 grammar. The DOM still owns the attribute; RoboCSS is handed its text.
     */
    int inlineStyles;
    /** A runtime default stylesheet (9.1 boundary, later content). */
    int defaultStyles;
    /** Loading stylesheets from anywhere: `@import`, files, URLs. */
    int stylesheetLoading;
    /**
     * Custom properties (18.1): a `--name` is stored as an opaque token
     * sequence, cascaded like any declaration and inherited down the tree. The
     * half 9.x already had, now stated as a capability of its own.
     */
    int customProperties;
    /**
     * `var()` substitution (18.1): a declaration carrying a reference is
     * replaced with the referenced value — or its fallback, or `unset` when
     * neither exists — before the value is computed.
     *
     * What it does not claim is a shorthand whose expansion needs the component
     * count a substitution would change: that is expanded by the inheritance
     * pass before this one runs, so such a shorthand keeps its own single entry
     * (RoboCSSVariable.h, `docs/Architecture.md`).
     */
    int variableSubstitution;
    /**
     * The CSS object model (18.2): a model object rendered back to canonical
     * text (`cssText`) and a mutable, self-contained style declaration
     * (`element.style`'s shape) that can be parsed, read, edited and removed.
     *
     * What it does not claim is a document binding: there is no `styleSheets`
     * list, no `getComputedStyle`, no live update of a style context when a
     * rule is inserted, and no `CSSStyleRule.parentStyleSheet` chain. Those
     * need a DOM, and the document layer is a peer of RoboCSS rather than a
     * part of it.
     */
    int cssom;
} CssCapabilities;

/** Zero every flag. Safe on NULL. */
void CssCapabilitiesInit(CssCapabilities *capabilities);

/**
 * Fill @p out with this build's capabilities.
 * @return RoboOk; RoboInvalidArgument for a NULL output.
 * Available whether or not the module is initialized: it describes the build.
 */
Result CssGetCapabilities(CssCapabilities *out);

/**
 * The runtime's default (user-agent) stylesheet, as CSS text.
 *
 * This is the boundary for 9.1.11 and nothing more: the runtime default styles
 * are ordinary CSS that will be parsed through the same path as any other
 * stylesheet, which is why the interface hands out *text* rather than a
 * pre-built table of declarations.
 *
 * Phase 9.1 ships no default styles, so this returns NULL and writes 0 to
 * @p outLength. A caller must therefore treat NULL as "no runtime styles"
 * rather than as an error — the boundary exists, the styles do not yet. The
 * length is written whenever @p outLength is non-NULL, including for the NULL
 * return.
 */
const char *CssDefaultStylesheetText(size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_H */
