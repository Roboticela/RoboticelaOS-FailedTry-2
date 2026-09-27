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
    /**
     * The flexbox family (18.3): the three closed vocabularies — `flex-direction`,
     * `flex-wrap`, and the alignment vocabulary `justify-content`, `align-items`,
     * `align-self` and `align-content` share — each carrying the classifications a
     * layout pass dispatches on, plus `flex-grow`/`flex-shrink` (a non-negative
     * number), `flex-basis` (a length, a percentage or `auto`/`content`), `order` (a
     * whole number) and the two shorthands `flex-flow` and `flex` as carried
     * declarations.
     *
     * What it does not claim is flex *layout*: nothing here measures a line,
     * distributes free space, resolves a main-axis position, or expands the two
     * shorthands into their halves — that is the layout phase that has a box, and the
     * values here are what it will read. `flex-wrap`'s `nowrap` and the alignment
     * `normal` are still values and not behaviours, exactly as `position: sticky` is.
     */
    int flexbox;
    /**
     * The grid vocabulary (18.4): `grid-auto-flow` as a closed set of four spellings
     * with its axis and density classifications, the gutter properties `row-gap` /
     * `column-gap` as a length family, and the remaining grid properties registered as
     * carried declarations so a document's grid declarations survive to the phase that
     * has a box.
     *
     * What it does not claim is the *track grammar*: `grid-template-columns`,
     * `grid-template-rows`, `grid-area` and their relatives take track lists and line
     * placements — a size, a `minmax()`, a repeat count, a line name, a `span` — whose
     * grammar is a phase of its own, so those declarations are PRESERVED rather than
     * read. Nor is grid *layout*: nothing here measures a track, places an item or
     * resolves a line number.
     */
    int grid;
    /**
     * The table vocabulary (18.5): the five closed sets a table box is described by —
     * `table-layout`, `border-collapse`, `caption-side`, `empty-cells` and `visibility`
     * — each a row per value with the classifications a table layout pass dispatches on,
     * plus `border-spacing` registered as a carried declaration.
     *
     * What it does not claim is table *layout*: nothing measures a column, distributes
     * a track, collapses two adjoining borders into one, or removes a `visibility:
     * collapse` row from the layout. The internal table display types (`display:
     * table`, `table-row`, `table-cell`, …) are **not** rows in this build's display
     * vocabulary — the internal-box model is the table layout phase's (19.6), which is
     * why this flag is named for the properties and not for the boxes they describe.
     * `border-spacing`'s `<length>{1,2}` pair is carried PRESERVED for the same reason.
     */
    int tables;
    /**
     * The multi-column vocabulary (18.6): the fragmentation container's properties —
     * `column-count` (`auto` or a positive integer), `column-width` (a length or
     * `auto`), `column-span` (`none` | `all`), `column-fill` (`auto` | `balance`), the
     * `column-rule-*` decoration (reusing the border family's width, style and colour
     * shapes), the two shorthands `column-rule` and `columns` as carried declarations,
     * and `column-gap` which 18.4 already registered.
     *
     * What it does not claim is fragmentation *layout*: nothing splits content into
     * columns, fills one to its height, balances a height, or decides how many columns
     * fit. A value here says what rule a fragmentation pass follows.
     */
    int multiColumn;
    /**
     * The writing-mode vocabulary (18.13): the three closed sets that decide which
     * physical edge is inline-start and how a character stands in a vertical line —
     * `direction` (`ltr` | `rtl`), `writing-mode` (`horizontal-tb`, `vertical-rl`,
     * `vertical-lr`, `sideways-rl`, `sideways-lr`) and `text-orientation` (`mixed` |
     * `upright` | `sideways`), each a row per value with the classifications a layout
     * or text pass dispatches on. `direction`'s shape was a bare KEYWORD until this
     * phase upgraded it in place.
     *
     * What it does not claim is a vertical *layout* or a vertical *shaper*: nothing
     * turns the writing mode into a run, a line, a caret or a fragment, and the two
     * `sideways-*` values are carried as values the build's text engine does not yet
     * draw. A value here says which axis is which; the module that has a box reads it.
     */
    int writingModes;
    /**
     * The transform vocabulary (18.7): the two closed sets a transformed box is
     * described by — `transform-style` (`flat` | `preserve-3d`) and
     * `backface-visibility` (`visible` | `hidden`) — each a row per value with the
     * classification a rendering pass dispatches on, and the closed set of
     * `<transform-function>` names (`CssTransformFunction`) a `transform` list is
     * built from, with its 3D and matrix classifications.
     *
     * What it does not claim is transform *layout* or a matrix library: nothing
     * composes a list, multiplies two matrices, resolves an origin or applies a
     * transform to a box. `transform`, `transform-origin`, `perspective`,
     * `perspective-origin`, `translate`, `rotate` and `scale` are registered as
     * carried declarations; the value says what an author wrote.
     */
    int transforms;
    /**
     * The transition vocabulary (18.8): the closed `transition-behavior` set
     * (`normal` | `allow-discrete`) and the keyword half of the timing-function
     * grammar (`ease`, `linear`, `ease-in`, `ease-out`, `ease-in-out`,
     * `step-start`, `step-end`) with its discrete-step classification.
     *
     * What it does not claim is a transition *engine* or a clock: no transition is
     * started, no easing curve sampled, no value interpolated and no timeline
     * advanced. `transition`, `transition-property`, `transition-duration`,
     * `transition-delay` and `transition-timing-function` are registered as carried
     * declarations, because a property list, a time list and a timing-function list
     * are grammars this phase names rather than parses.
     */
    int transitions;
    /**
     * The animation vocabulary (18.9): four closed sets — `animation-direction`
     * (`normal` | `reverse` | `alternate` | `alternate-reverse`),
     * `animation-fill-mode` (`none` | `forwards` | `backwards` | `both`),
     * `animation-play-state` (`running` | `paused`) and `animation-composition`
     * (`replace` | `add` | `accumulate`) — each a row per value with the
     * classifications a consumer dispatches on, plus `animation-iteration-count`
     * (`infinite`, or a non-negative number). The timing-function keywords are the
     * **transition** family's (§18.8) reused, not a second copy.
     *
     * What it does not claim is an animation *engine* or a clock, and it does not
     * parse `@keyframes`: no animation starts, no keyframe resolves, no curve is
     * sampled and no value is interpolated. `animation`, `animation-name`,
     * `animation-duration`, `animation-delay` and `animation-timing-function` are
     * registered as carried declarations.
     */
    int animations;
    /**
     * Media queries (18.10): the media-type vocabulary (`all` | `screen` |
     * `print`), the media-feature vocabulary with its kinds and range forms, the
     * `CssMediaContext` a caller fills in, and the condition/query matcher whose
     * answer is yes, no or **unknown** — a feature the context cannot answer is
     * unknown rather than a silent no.
     *
     * What it does not claim is the `@media` grammar: nothing here tokenizes a
     * prelude or attaches a condition to an at-rule (that is the parser's), and
     * nothing here owns a viewport — the caller copies its numbers into the context.
     * `@container` is 18.11's.
     */
    int mediaQueries;
    /**
     * Container queries (18.11): the `container-type` closed set (`normal` | `size`
     * | `inline-size`) with its classifications, and a matcher that evaluates a
     * container query's condition list — the **media-query** module's own
     * `CssMediaCondition` (§18.10) — against a container's size.
     *
     * What it does not claim is the `@container` grammar or an ancestor walk:
     * nothing here tokenizes a prelude or finds the query container — the caller
     * that has a box does that and hands the size here. And `inline-size`/`block-size`
     * as *feature names* are not built (a logical feature needs a writing mode a
     * container does not carry until 18.12). `container-name` and `container` are
     * carried declarations.
     */
    int containerQueries;
    /**
     * Logical properties (18.12): the logical side/axis vocabulary and
     * `CssLogicalResolveSide`, which maps a role (block-start, inline-start, …) to a
     * `CssBoxSide` under a `CssWritingMode` and `CssDirection` (§18.13). The box
     * model's logical longhands and shorthands — `margin-*`, `padding-*`, the insets
     * and `block-size`/`inline-size` — are registered as the **same shape** their
     * physical counterparts are.
     *
     * What it does not claim is logical-value *use*: nothing here maps an element's
     * computed style to physical values (that is the phase with an element, 19.10),
     * and the logical border longhands and logical `min-`/`max-` sizes are named as
     * not built. `sideways-*` resolves as its vertical counterpart.
     */
    int logicalProperties;
    /**
     * The font family (18.14): the twelve closed sets that describe a font *request*
     * — `font-style`, `font-kerning`, `font-optical-sizing`, the four
     * `font-synthesis-*` longhands (one vocabulary), `font-variant-caps` and
     * `font-variant-position` as typed shapes, and `font-stretch`, `font-size`,
     * `font-variant-numeric` and `font-variant-east-asian` as classified values
     * inside deliberately carried declarations — plus the `@font-face` descriptor
     * vocabulary and the `font-display` set, which is a descriptor and not a
     * property at all.
     *
     * What it does not claim is a font database or a text engine: nothing here opens
     * a font file, resolves a family name to a face, walks a coverage table or selects
     * a fallback (RoboFont, and 12.17's selection seam, do that), nothing shapes or
     * measures, and nothing synthesises a weight or a slope — `font-synthesis-*` is a
     * permission an author writes and RoboText's rule is that no synthesis happens.
     * `font-variant-ligatures`' five tags are **RoboText's** vocabulary (12.12) and
     * are registered here as a carried declaration rather than copied. A value in
     * this family says what an author asked for; the face that answers belongs to the
     * layer that has one.
     */
    int fonts;
    /**
     * The text family (18.15): the fourteen closed sets that describe a *rule* a text
     * pass follows — `text-align`, `text-align-last`, `text-justify`, `text-transform`,
     * `white-space`, `word-break`, `overflow-wrap`, `line-break`, `hyphens` and
     * `text-decoration-style` as typed shapes, and `text-decoration-line`,
     * `text-overflow`, `text-decoration-thickness`, `text-decoration-color` and
     * `text-underline-position` as classified values inside deliberately carried
     * declarations.
     *
     * What it does not claim is a line breaker, a justifier, a hyphenator or a text
     * transformer: nothing here wraps a line, distributes slack, finds a hyphenation
     * point or changes a character's case, and no function takes or returns a line, a
     * glyph id or a rectangle — `text-orientation` and `direction` belong to 18.13, and
     * where a line breaks and
     * which glyphs land on it belong to the phase that has a text engine (12.x). A
     * value in this family says what rule the text pass follows; walking it is the
     * layer that has a shaped line.
     */
    int text;
    /**
     * The image family (18.16): the `<image>` function vocabulary and the three
     * properties that control a chosen image — `image-rendering` and `object-fit` as
     * typed shapes, and `image-orientation`'s keyword pair inside a deliberately
     * carried declaration (`image-resolution` and `object-position` are carried too).
     *
     * What it does not claim is a loader, a decoder, a renderer, a gradient sampler
     * or a layout pass: no function here opens a file, resolves a URL, samples a
     * colour stop, scales a pixel or measures a box. A value in this family says
     * *which image* and *what rule* a renderer follows; the pixels belong to the phase
     * that draws them.
     */
    int images;
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
