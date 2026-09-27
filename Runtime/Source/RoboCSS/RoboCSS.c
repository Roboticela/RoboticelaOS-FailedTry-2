/*
 * RoboCSS module lifecycle, slot table and version (Phase 9.1).
 *
 * The module owns exactly one thing: the stylesheet slot table. Everything else
 * in the subsystem — property tables, style contexts, parsers, values,
 * declarations, selectors, rules — belongs to the caller that created it, which
 * is why shutdown has nothing else to release and why "no hidden global state"
 * survives an architecture this wide.
 *
 * The table is a fixed array rather than a heap allocation: it is bounded by
 * RoboCssMaxStylesheets, and a bounded array is what lets
 * CssInit be a state transition with nothing that can fail.
 */

#include "RoboCSS/RoboCSS.h"

#include "RoboCSSInternal.h"
#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

static int gInitialized;
static CssStylesheetSlot gSlots[RoboCssMaxStylesheets];
static CssStylesheetId gNextStylesheetId = 1u;

const char *CssVersionString(void)
{
    return RoboCssVersionString;
}

Result CssGetVersion(I32 *outMajor,
                                   I32 *outMinor,
                                   I32 *outPatch)
{
    if (!outMajor || !outMinor || !outPatch) {
        return RoboInvalidArgument;
    }
    *outMajor = RoboCssVersionMajor;
    *outMinor = RoboCssVersionMinor;
    *outPatch = RoboCssVersionPatch;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Slot table                                                          */
/* ------------------------------------------------------------------ */

CssStylesheetSlot *CssSlotFind(CssStylesheetId id)
{
    size_t i;

    if (id == RoboCssStylesheetIdNone) {
        return NULL;
    }
    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        if (gSlots[i].inUse && gSlots[i].id == id) {
            return &gSlots[i];
        }
    }
    return NULL;
}

CssStylesheetSlot *CssSlotAlloc(void)
{
    size_t i;

    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        if (!gSlots[i].inUse) {
            return &gSlots[i];
        }
    }
    return NULL;
}

CssStylesheetId CssSlotClaim(CssStylesheetSlot *slot,
                                             CssOrigin origin)
{
    CssStylesheetId id;

    if (!slot) {
        return RoboCssStylesheetIdNone;
    }
    /* Zero the slot first: a recycled slot must not inherit the previous
     * stylesheet's rule list, source or error. */
    memset(slot, 0, sizeof(*slot));
    id = gNextStylesheetId++;
    slot->inUse = 1;
    slot->id = id;
    slot->origin = origin;
    slot->state = RoboCssStylesheetStateCreated;
    slot->name[0] = '\0';
    CssSourceInit(&slot->source);
    CssRuleListInit(&slot->rules);
    slot->hasError = 0;
    CssErrorInit(&slot->error);
    return id;
}

void CssSlotRelease(CssStylesheetSlot *slot)
{
    if (!slot) {
        return;
    }
    CssSourceDispose(&slot->source);
    CssRuleListDispose(&slot->rules);
    memset(slot, 0, sizeof(*slot));
    CssErrorInit(&slot->error);
}

CssStylesheetId CssSlotIdAt(size_t index)
{
    size_t i;
    size_t seen = 0u;

    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        if (!gSlots[i].inUse) {
            continue;
        }
        if (seen == index) {
            return gSlots[i].id;
        }
        seen++;
    }
    return RoboCssStylesheetIdNone;
}

size_t CssSlotLiveCount(void)
{
    size_t i;
    size_t live = 0u;

    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        if (gSlots[i].inUse) {
            live++;
        }
    }
    return live;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

Result CssInit(void)
{
    if (gInitialized) {
        return RoboAlreadyExists;
    }
    if (!BaseIsInitialized()) {
        LogError("robo_css: init refused; RoboBase not initialized");
        return RoboInitError;
    }
    /* Slots are static, so they start zeroed; the zero value of every field is
     * the disposed value (inUse 0, KIND_NONE source, empty rule list, no
     * error), which is what makes the table safe before the first claim. */
    gInitialized = 1;
    LogDebug("robo_css: module initialized (slots=%u)", RoboCssMaxStylesheets);
    return RoboOk;
}

void CssShutdown(void)
{
    size_t i;
    size_t released = 0u;

    if (!gInitialized) {
        return;
    }
    for (i = 0u; i < (size_t)RoboCssMaxStylesheets; i++) {
        if (gSlots[i].inUse) {
            CssSlotRelease(&gSlots[i]);
            released++;
        }
    }
    gInitialized = 0;
    LogDebug("robo_css: module shut down (stylesheets released=%llu)",
                   (unsigned long long)released);
}

int CssIsInitialized(void)
{
    return gInitialized;
}

size_t CssLiveStylesheetCount(void)
{
    if (!gInitialized) {
        return 0u;
    }
    return CssSlotLiveCount();
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

void CssCapabilitiesInit(CssCapabilities *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

Result CssGetCapabilities(CssCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    CssCapabilitiesInit(out);

    /* The model this phase builds. Every one of these is backed by a header, an
     * implementation and a suite; none of them interprets CSS. */
    out->sourceModel = 1;
    out->tokenModel = 1;
    out->valueModel = 1;
    out->propertyRegistry = 1;
    out->declarationModel = 1;
    out->selectorModel = 1;
    out->ruleModel = 1;
    out->stylesheetModel = 1;
    out->errorModel = 1;

    /* Everything that would interpret it is 0, and stays 0 until its own phase
     * lands. A caller that reads this is never told a feature works because a
     * type for it exists. */
    /* 9.2 produced a tokenizer and 9.3 produced the grammar that consumes it:
     * both flags name something that now exists end to end, so both are 1, and
     * the suites assert 1. `inlineStyles` goes with the grammar rather than
     * with a phase of its own, because the boundary it names is
     * CssDeclarationListParseText, which is a declaration-list parse —
     * the same code path a block uses. Each later flag is 1 only once its own
     * phase has landed, which is what the two comments below record. */
    out->tokenizer = 1;
    out->parser = 1;
    out->inlineStyles = 1;
    /* 9.4 turns a prelude into selectors and matches them against a host's
     * elements. Both flags name something end to end: the selector grammar in
     * RoboCSSSelectorParser.c, and the matcher in RoboCSSMatcher.c. What is left
     * above them — specificity, inheritance, computed values — still has no
     * implementation, and says so below. */
    out->selectorParsing = 1;
    out->selectorMatching = 1;
    /* 9.5 collects what applies to an element — every origin, every matched
     * rule, the inline attribute — in cascade order, and answers the winner for
     * a property by level, specificity and document order. 9.6 supplied the
     * middle key and removed the reported-undecided outcome, so the two flags
     * below now name a complete ordering rather than half of one. */
    out->cascade = 1;
    /* 9.6 computes a selector's specificity and hands it to the cascade as
     * structured data. The cascade consumes it through one comparison function
     * and holds no arithmetic of its own, which is what makes this a flag rather
     * than a second implementation. */
    out->specificity = 1;
    /* 9.7 takes a cascade and the parent's resolved style and produces a
     * resolved style: every declared property, every inherited one, and every
     * inherited-by-default default, each carrying where its value came from. */
    out->inheritance = 1;
    /* 9.8 turns that resolved style into a computed one: every entry carries a
     * typed value — a length with its unit, a percentage, a colour, a keyword, a
     * family list, or the tokens themselves — normalised through the property's
     * declared value shape. What it does *not* do is resolve: `50%` stays a
     * percentage, `2em` stays relative, `var()` and `calc()` stay references, and
     * `currentColor` stays contextual, which is why this is a flag for computing
     * and not for laying anything out. */
    out->computedValues = 1;
    /* 9.9 gave colours a module of their own (RoboCSSColor.h): every spelling
     * CSS's ordinary colour syntax has, in one authoritative parser that the
     * value layer consumes rather than duplicates. `currentColor` is a typed
     * contextual colour and is resolved by the computed style against the
     * element's — or, on `color` itself, the parent's — computed `color`; the
     * colour spaces this build cannot represent are carried and *reported* as
     * deferred rather than approximated. */
    out->colors = 1;
    /* 9.10 gave numbers and units a module of their own (RoboCSSUnit.h): the
     * closed unit vocabulary with its families and its fixed factors, a typed
     * numeric value with deterministic equality, and one canonical spelling that
     * the value layer consumes rather than reformatting. Resolution is a
     * *question* — `CssUnitResolvePx` answers "needs context" for `2em`
     * with no font and `50vh` with no viewport — which is why this is a flag for
     * the value system and not for resolving anything against a layout. */
    out->units = 1;
    /* 9.11 gave the box model a vocabulary module of its own (RoboCSSBox.h):
     * the four sides in CSS's order, the three shorthands and their longhands,
     * and the component mapping — with expansion and *resolution* kept apart, so
     * which declaration wins an edge is still the cascade's answer and not a
     * second opinion held here. What it does not claim is layout: a box at this
     * phase is a set of values with a shape, `50%` stays a percentage and `auto`
     * stays the keyword, which is why this is a flag for a value family and not
     * for measuring anything. */
    out->boxModel = 1;
    /* 10.13 gave the overflow family its values (RoboCSSOverflow.h): the shorthand
     * and its two longhands on one closed vocabulary of two values, each carrying
     * whether it clips its axis. It is the first family in this table added by a
     * *layout* phase, because `overflow` is the first property whose value changes
     * geometry — and the reason it is a value with a shape rather than a preserved
     * token list is that the phase with a box dispatches on it, so an identifier it
     * does not know has to be a mistake. What it does not claim: nothing scrolls,
     * no scrollbar exists, and where the boundary is belongs to the box. */
    out->overflow = 1;
    /* 9.12 gave `display` a vocabulary of its own (RoboCSSDisplay.h): a closed set
     * of four values, each a row carrying the keyword it is spelled by and what
     * CSS Display 3 calls its outer and inner halves. The property was already a
     * registry row; what changed is that its value now has a shape that refuses an
     * identifier it does not know, so `display: bogus` is a mistake with a
     * diagnostic instead of a keyword carrying the text `bogus`. `display: none`
     * is the absence of a box rather than zero geometry, which is why this claims
     * a value family and not layout: nothing here removes an element from a tree
     * that does not exist yet. */
    out->display = 1;
     /* 9.13 gave positioning a vocabulary too (RoboCSSPosition.h): the five
     * `position` values, each a row carrying whether its box stays in flow and
     * whether it is a positioned box, plus the four inset properties and
     * `z-index`. The insets reuse the box model's EDGE_OFFSET shape because
     * `<length-percentage> | auto` is the grammar CSS gives both families, and
     * `z-index` is `auto` or a whole number. What it claims is a value and its
     * classifications; what it does not claim is geometry — no containing block
     * is computed, `top: 20%` stays a percentage, `auto` stays a keyword rather
     * than becoming zero, and `z-index` orders nothing. */
    out->positioning = 1;
    /* 9.14 gave the background family its values (RoboCSSBackground.h): the six
     * longhands, each with a shape of its own, plus `background` as a shorthand
     * the resolution pass distributes over all six. The distribution is the part
     * that is new in kind, because a component is assigned to a longhand by
     * *kind* rather than by position — a colour goes to the colour, a repeat
     * keyword to the repeat, `cover` after a `/` to the size. The two closed
     * vocabularies (repeat and attachment) are rows in the same table the display
     * and positioning shapes use. What it claims is a value and its distribution;
     * what it does not claim is paint — no image is fetched, no percentage is
     * resolved against a box, and no pixel is blended. */
    out->backgrounds = 1;
    /* Borders (9.15): twelve longhands and the five declarations that can set
     * them. The styles are a closed vocabulary read through a typed enumerator,
     * the colours reuse 9.9's family — `border-top-color: currentColor` and
     * `color: currentColor` are one computation — and one pass resolves each
     * longhand against every declaration speaking for it, by the cascade's own
     * ordering. What it claims is a value and its distribution; what it does not
     * claim is geometry — nothing draws a border, nothing measures one, and `none`
     * having a used width of zero is the phase-with-a-box's rule. */
    out->borders = 1;
    /* Custom properties and `var()` (18.1): a `--name` is an opaque token
     * sequence that cascades and inherits like any declaration (the half 9.x
     * had), and a declaration carrying a `var()` reference is replaced with the
     * referenced value before it is computed. What it does not claim is
     * substitution *before* shorthand expansion: a shorthand whose component
     * count a substitution would change is refused at expansion and takes
     * `unset` at computed-value time, which is a named boundary rather than a
     * half-read value. */
    out->customProperties = 1;
    out->variableSubstitution = 1;
    /* The CSS object model (18.2): a model object rendered back to text, and a
     * mutable style declaration that owns its bytes. What it deliberately does
     * not claim is a document binding -- there is no `styleSheets`, no
     * `getComputedStyle` and no live context update on an edit, because those
     * need the DOM and the document layer is a peer of RoboCSS. */
    out->cssom = 1;
    /* The flexbox family (18.3): three closed vocabularies -- the direction, the wrap
     * and the alignment set four properties share -- each carrying what a layout pass
     * dispatches on, plus the factor, basis and order shapes and the two shorthands as
     * carried declarations. What it does not claim is flex *layout*: no line is
     * measured and no free space is distributed, because a value says what rule a
     * layout pass follows and where the items land belongs to the phase with a box. */
    out->flexbox = 1;
    /* The grid vocabulary (18.4): one closed set (`grid-auto-flow`) with its axis and
     * density classifications, the gutters as a length family, and every other grid
     * property registered as a carried declaration. What it does not claim is the track
     * grammar -- a track list is a size, a `minmax()`, a repeat count and a line name,
     * which is a phase of its own -- so those declarations are PRESERVED rather than
     * read, and no track is measured or item placed here. */
    out->grid = 1;
    /* The table vocabulary (18.5): five closed sets -- the layout rule, the collapse,
     * the caption side, the empty-cell rule and visibility -- each a row per value with
     * the classifications a table layout pass reads, plus `border-spacing` carried as a
     * declaration. What it does not claim is table *layout*: no column is measured, no
     * border is collapsed and no collapsed row is removed, and the internal table display
     * types are not in this build's display vocabulary because the internal-box model is
     * 19.6's. */
    out->tables = 1;
    /* The multi-column family (18.6): the count, width, span and fill rule of a
     * fragmentation container, the `column-rule-*` decoration (the border family's
     * shapes reused rather than copied), the two shorthands as carried declarations and
     * the gutter 18.4 already registered. What it does not claim is fragmentation
     * *layout*: no content is split, no height is balanced and no column count is
     * derived from a width. */
    out->multiColumn = 1;
    /* The writing-mode family (18.13): the three closed sets that decide which physical
     * edge is inline-start and how a character stands -- `direction`, `writing-mode` and
     * `text-orientation` -- each a row per value with the classifications a consumer
     * reads, and `direction` upgraded from the bare KEYWORD shape it had since 9.7. What
     * it does not claim is vertical *layout* or a vertical *shaper*: no run, line, caret
     * or fragment is derived from a writing mode here, and the two `sideways-*` values are
     * carried as values the build's text engine does not yet draw. */
    out->writingModes = 1;
    /* The transform family (18.7): the two closed sets a transformed box is described
     * by -- `transform-style` and `backface-visibility` -- plus the closed set of
     * function names a `transform` list is built from, with the 3D and matrix
     * classifications a rendering pass reads. What it does not claim is transform
     * *layout* or a matrix library: no list is composed, no matrix multiplied and no
     * origin resolved; `transform` and its relatives are carried declarations. */
    out->transforms = 1;
    /* The transition family (18.8): the `transition-behavior` closed set and the
     * keyword half of the timing-function grammar, with its discrete-step
     * classification. What it does not claim is a transition *engine* or a clock: no
     * transition is started, no curve sampled and no value interpolated; the property
     * list, time list and timing-function list are carried declarations. */
    out->transitions = 1;
    /* The animation family (18.9): four closed sets -- direction, fill mode, play state
     * and composition -- each a row per value with the classifications a consumer reads,
     * plus the `animation-iteration-count` shape (`infinite` or a non-negative number,
     * where a fraction is legal). The timing-function keywords are the transition
     * family's reused. What it does not claim is an animation *engine* or a clock, and
     * no `@keyframes` at-rule is parsed: the name, time and timing-function lists are
     * carried declarations. */
    out->animations = 1;
    /* Media queries (18.10): the media-type and media-feature vocabularies and the
     * condition/query matcher, whose answer is yes, no or *unknown* -- a feature the
     * context cannot answer is unknown rather than a silent no. What it does not claim
     * is the `@media` grammar (no prelude is tokenized and no at-rule is attached) or a
     * viewport, which the caller copies into the context. */
    out->mediaQueries = 1;
    /* Container queries (18.11): the `container-type` closed set and a matcher that
     * evaluates a container query's condition list -- the media module's own condition
     * model -- against a container's size. What it does not claim is the `@container`
     * grammar or an ancestor walk: the caller finds the query container and hands its
     * size here, and a `normal` box answers unknown rather than matching. */
    out->containerQueries = 1;
    /* Logical properties (18.12): the side/axis vocabulary and `CssLogicalResolveSide`,
     * which maps a role to a physical `CssBoxSide` under a writing mode and direction.
     * The box model's logical longhands and shorthands are registered as the same shape
     * their physical counterparts are. What it does not claim is logical-value *use* --
     * no element's computed style is mapped to physical values -- and the logical border
     * longhands and logical min/max sizes are named as not built. */
    out->logicalProperties = 1;
    /* The font family (18.14): the twelve closed sets that describe a font *request*,
     * over the registry rows that carry the other half of the family, plus the
     * `@font-face` descriptor vocabulary and the `font-display` set (a descriptor, not
     * a property). What it does not claim is a font database or a text engine -- no
     * font file is opened, no family resolved, no glyph shaped -- and no synthesis:
     * `font-synthesis-*` is a permission an author writes, and nothing here or
     * downstream emboldens or slants a face. `font-variant-ligatures`' vocabulary is
     * RoboText's (12.12). */
    out->fonts = 1;
    /* The text family (18.15): the fourteen closed sets that describe a *rule* a text
     * pass follows, over the registry rows that carry the other half. What it does not
     * claim is a line breaker, a justifier, a hyphenator or a text transformer -- no
     * line is wrapped, no slack distributed, no hyphenation point found, no case
     * changed, and no function takes or returns a line, a glyph id or a rectangle.
     * `direction` and `text-orientation` are 18.13's. */
    out->text = 1;
    /* The image family (18.16): the `<image>` function vocabulary and the three
     * properties that control a chosen image, as typed shapes or carried
     * declarations. What it does not claim is a loader, a decoder, a renderer, a
     * gradient sampler or a layout pass -- no file is opened, no URL resolved, no
     * stop sampled, no pixel scaled and no box measured. */
    out->images = 1;
    /* The background family's 18.17 additions: `space`/`round`, `local`, the box
     * vocabulary `background-clip` and `background-origin` share, and the sixteen
     * blend modes. What it does not claim is multiple layers, a renderer that samples
     * a gradient or blends a pixel, or percentage resolution. */
    out->blendModes = 1;
    out->defaultStyles = 0;
    out->stylesheetLoading = 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Runtime default stylesheet boundary                                 */
/* ------------------------------------------------------------------ */

const char *CssDefaultStylesheetText(size_t *outLength)
{
    if (outLength) {
        *outLength = 0u;
    }
    /* No runtime styles exist yet. Returning NULL rather than an empty string
     * keeps "there is no default stylesheet" from being confusable with "the
     * default stylesheet is empty", which is a different (and temporary) state
     * a later phase will be in while the sheet is being written. */
    return NULL;
}
