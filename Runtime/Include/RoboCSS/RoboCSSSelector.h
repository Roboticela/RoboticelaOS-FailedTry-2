/*
 * RoboCSS selector model (Phase 9.1).
 *
 * Selectors are the largest thing Phase 9.1 must not implement and the one it
 * must model most carefully, because the shape chosen here decides what 9.4
 * (matching) and 9.6 (specificity) can be written against without redesign.
 *
 * The model is the CSS Selectors structure, bottom-up:
 *
 *   simple selector     one condition: `div`, `*`, `.card`, `#main`,
 *                       `[type="text"]`, `:hover`, `::before`
 *   compound selector   simple selectors that all apply to one element:
 *                       `input[type="text"]:disabled`
 *   complex selector    compound selectors joined by combinators:
 *                       `form > input + label`
 *   selector list       complex selectors joined by commas: `h1, h2, h3`
 *
 * A compound selector owns its simple selectors; a complex selector owns its
 * compound selectors and its combinators; a selector list owns its complex
 * selectors. Appending *moves* the child in, exactly as component values do,
 * so there is one ownership rule in the whole subsystem rather than one per
 * model. Simple selectors are plain values holding borrowed slices, so they are
 * copied rather than moved.
 *
 * Two things are deliberately absent:
 *
 *   - Matching. `CssSimpleSelectorKind` says what a condition *is*;
 *     nothing here decides whether an element satisfies it. That is 9.4, and it
 *     needs the DOM view in RoboCSSStyle.h rather than a DOM copy here.
 *   - Specificity. A specificity is a property of a *specific* selector list
 *     under a specific spec revision; computing one is 9.6. A placeholder field
 *     now would be a number nobody computed, which is worse than no field.
 *
 * What *is* here beyond storage is the vocabulary an implementer needs to be
 * unambiguous: the attribute operators (`=`, `~=`, `|=`, `^=`, `$=`, `*=`) as
 * an enum rather than as magic strings, the `i`/`s` case-sensitivity flags, and
 * combinators including the column combinator that modern CSS added.
 *
 * A functional pseudo-class's argument (`:not(...)`, `:nth-child(...)`) is kept
 * as one borrowed slice of raw text: whether it contains a selector list, an
 * An+B expression or a language range depends on which pseudo-class it is, and
 * splitting it here would be parsing.
 *
 * Phase 9.4 added what parsing needed and nothing else:
 *
 *   names        a selector list owns one byte arena for the *resolved*
 *                spelling of every name in it. Escapes are resolved once, by
 *                the parser, through the tokenizer's own value rule (9.2) — so
 *                `.foo\:bar` carries the six bytes `foo:bar` and matching never
 *                has to re-resolve. A hand-built selector keeps whatever slice
 *                it was given, which is why the accessors borrow rather than
 *                interpret.
 *   nested       `:not(<selector list>)` is a real, owned selector list held by
 *                the *compound* that owns the pseudo-class, addressed by index
 *                from the simple selector. Keeping the index (not a pointer) on
 *                the simple selector is what preserves 9.1's one copy rule: a
 *                simple selector still owns nothing, so copying one is still
 *                safe. A copy's index is cleared by
 *                CssCompoundSelectorAppend, because an index only means
 *                something inside the compound that issued it.
 *   nth          `:nth-child(2n+1)`'s An+B, parsed once. `a` and `b` are
 *                integers and `n` runs over the non-negative integers; the
 *                argument text stays on the selector as the "as written" form.
 *
 * What 9.4 still does not put here: specificity, and any notion of which
 * element a selector "won". Matching lives in RoboCSSMatcher.h and reads these
 * structures; it never fills them and never mutates them.
 */

#ifndef ROBO_CSS_SELECTOR_H
#define ROBO_CSS_SELECTOR_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How deep functional pseudo-class arguments may nest before a selector is
 * rejected: `:not(:not(:not(...)))` inside itself, up to this many levels.
 *
 * The bound exists for the same reason RoboCssMaxNesting does in the value
 * model — parsing a nested argument is a recursive call, and adversarial input
 * must cost bounded stack rather than a crash. It is deliberately much smaller
 * than the value-model bound: real stylesheets nest `:not()` one or two deep,
 * so eight is generous, and anything past it is malformed rather than merely
 * unusual.
 */
#define RoboCssMaxSelectorNesting 8u

/**
 * "This simple selector has no parsed argument list" — the value
 * `nestedIndex` holds until CssCompoundSelectorAttachNested sets it.
 */
#define RoboCssNestedNone ((size_t)-1)

/** Forward declaration: a compound owns nested argument lists of this type. */
typedef struct CssSelectorList CssSelectorList;

/** The kinds of simple selector. */
typedef enum CssSimpleSelectorKind {
    /** No selector: a cleared record. */
    RoboCssSimpleSelectorNone = 0,
    /** `div`, `input` — a type (tag name) condition. */
    RoboCssSimpleSelectorType = 1,
    /** `*` — matches any element. */
    RoboCssSimpleSelectorUniversal = 2,
    /** `.card` */
    RoboCssSimpleSelectorClass = 3,
    /** `#main` */
    RoboCssSimpleSelectorId = 4,
    /** `[type="text"]` */
    RoboCssSimpleSelectorAttribute = 5,
    /** `:hover`, `:not(...)`, `:nth-child(2n+1)` */
    RoboCssSimpleSelectorPseudoClass = 6,
    /** `::before`, `::marker` */
    RoboCssSimpleSelectorPseudoElement = 7
} CssSimpleSelectorKind;

/** Number of defined simple selector kinds. */
#define RoboCssSimpleSelectorKindCount 8u

/** Non-zero if @p kind is a defined simple selector kind. */
int CssSimpleSelectorKindIsKnown(CssSimpleSelectorKind kind);

/** Human-readable simple selector kind name; never NULL. */
const char *CssSimpleSelectorKindName(CssSimpleSelectorKind kind);

/** How an attribute selector compares. */
typedef enum CssAttributeMatch {
    /** No comparison: `[disabled]` — presence only. */
    RoboCssAttributeMatchNone = 0,
    /** `[a=b]` */
    RoboCssAttributeMatchEquals = 1,
    /** `[a~=b]` — one whitespace-separated word equals b. */
    RoboCssAttributeMatchIncludes = 2,
    /** `[a|=b]` — equals b or begins with `b-`. */
    RoboCssAttributeMatchDash = 3,
    /** `[a^=b]` */
    RoboCssAttributeMatchPrefix = 4,
    /** `[a$=b]` */
    RoboCssAttributeMatchSuffix = 5,
    /** `[a*=b]` */
    RoboCssAttributeMatchSubstring = 6
} CssAttributeMatch;

/** Number of defined attribute match operators. */
#define RoboCssAttributeMatchCount 7u

/** Non-zero if @p match is a defined attribute match enumerator. */
int CssAttributeMatchIsKnown(CssAttributeMatch match);

/** Human-readable attribute match name; never NULL (e.g. "equals", "none"). */
const char *CssAttributeMatchName(CssAttributeMatch match);

/** How an attribute selector treats ASCII case. */
typedef enum CssAttributeCase {
    /** The document language decides (HTML: some attributes are case-insensitive). */
    RoboCssAttributeCaseDefault = 0,
    /** `[a=b i]` — the value compares ASCII case-insensitively. */
    RoboCssAttributeCaseInsensitive = 1,
    /** `[a=b s]` — the value compares case-sensitively. */
    RoboCssAttributeCaseSensitive = 2
} CssAttributeCase;

/** Number of defined attribute case flags. */
#define RoboCssAttributeCaseCount 3u

/** Non-zero if @p flag is a defined attribute case enumerator. */
int CssAttributeCaseIsKnown(CssAttributeCase flag);

/** Human-readable attribute case name; never NULL. */
const char *CssAttributeCaseName(CssAttributeCase flag);

/** A combinator that joins two compound selectors. */
typedef enum CssCombinator {
    /** No combinator (the start of a complex selector). */
    RoboCssCombinatorNone = 0,
    /** Whitespace: any ancestor. */
    RoboCssCombinatorDescendant = 1,
    /** `>` — the parent. */
    RoboCssCombinatorChild = 2,
    /** `+` — the immediately preceding sibling. */
    RoboCssCombinatorNextSibling = 3,
    /** `~` — any preceding sibling. */
    RoboCssCombinatorSubsequentSibling = 4,
    /** `||` — the column combinator. */
    RoboCssCombinatorColumn = 5
} CssCombinator;

/** Number of defined combinators. */
#define RoboCssCombinatorCount 6u

/** Non-zero if @p combinator is a defined combinator enumerator. */
int CssCombinatorIsKnown(CssCombinator combinator);

/** Human-readable combinator name; never NULL (e.g. "child", "descendant"). */
const char *CssCombinatorName(CssCombinator combinator);

/** One simple selector. All slices borrow from the source. */
typedef struct CssSimpleSelector {
    CssSimpleSelectorKind kind;
    /** Extent of this simple selector in the source. */
    CssRange range;
    /** Type name / class / id / pseudo name, as written. */
    CssSlice name;
    /** ATTRIBUTE: the attribute's name, as written inside the brackets. */
    CssSlice attribute;
    /** ATTRIBUTE: the comparison value; absent for a presence-only selector. */
    CssSlice attributeValue;
    /** ATTRIBUTE: how that value compares. */
    CssAttributeMatch match;
    /** ATTRIBUTE: how that comparison treats ASCII case. */
    CssAttributeCase caseFlag;
    /** ATTRIBUTE: non-zero when the value was written as a quoted string. */
    int valueWasString;
    /** PSEUDO_CLASS/PSEUDO_ELEMENT: the raw text between the parentheses. */
    CssSlice argument;
    /**
     * PSEUDO_CLASS: index into the owning compound's nested argument lists of
     * this pseudo-class's parsed selector argument (`:not(...)`), or
     * RoboCssNestedNone when its argument is not a selector list, was not
     * parsed, or the selector was built by hand.
     *
     * An index rather than a list, so that this record keeps owning nothing. It
     * is meaningful only against the compound it was attached to; a copy made by
     * CssCompoundSelectorAppend has it cleared, and a reader must treat
     * an out-of-range index as "no argument" rather than trusting it.
     */
    size_t nestedIndex;
    /**
     * `:nth-child()` / `:nth-last-child()`: non-zero when the argument was
     * parsed as An+B and `nthA`/`nthB` are meaningful.
     */
    int nthParsed;
    /** The `a` coefficient of An+B (`0` for a bare integer such as `3`). */
    int nthA;
    /** The `b` offset of An+B. */
    int nthB;
} CssSimpleSelector;

/** Clear @p selector to KIND NONE. Safe on NULL. */
void CssSimpleSelectorInit(CssSimpleSelector *selector);

/** Disposing a simple selector clears it; it owns nothing. Safe on NULL. */
void CssSimpleSelectorDispose(CssSimpleSelector *selector);

/**
 * Begin a simple selector of @p kind with @p name.
 * UNIVERSAL carries no name, TYPE/CLASS/ID/PSEUDO_* do, and ATTRIBUTE carries
 * its attribute name instead (pass it through @p name and copy it to
 * `attribute`, or set it with CssSimpleSelectorSetAttribute).
 * @return RoboOk; RoboInvalidArgument for a NULL selector, a NONE or unknown
 *         kind, or a kind that requires a name given an empty one.
 */
Result CssSimpleSelectorMake(CssSimpleSelector *selector,
                                            CssSimpleSelectorKind kind,
                                            CssSlice name,
                                            CssDiag *diag);

/**
 * Configure an ATTRIBUTE simple selector's comparison.
 * @return RoboOk; RoboInvalidArgument for a NULL selector, a selector that
 *         is not ATTRIBUTE, an empty attribute name, an unknown match or case
 *         flag, or a comparison without a value.
 */
Result CssSimpleSelectorSetAttribute(
    CssSimpleSelector *selector,
    CssSlice attribute,
    CssAttributeMatch match,
    CssSlice value,
    CssAttributeCase caseFlag,
    CssDiag *diag);

/** Record a pseudo-class/element's raw parenthesised argument (may be empty). */
Result CssSimpleSelectorSetArgument(
    CssSimpleSelector *selector,
    CssSlice argument,
    CssDiag *diag);

/**
 * The nested argument list index @p selector was given, or RoboCssNestedNone
 * for NULL, for a selector that has none, and for a selector whose index has not
 * been resolved against a compound (see CssCompoundSelectorNestedAt).
 */
size_t CssSimpleSelectorNestedIndex(const CssSimpleSelector *selector);

/**
 * Record the An+B form of a `:nth-child()`-family argument.
 * `a` and `b` are signed, which is what makes `-n+3` and `n-1` expressible;
 * `a == 0` is the bare-integer form (`3` means `0n+3`).
 * @return RoboOk; RoboInvalidArgument for a NULL selector or a selector
 *         whose kind is not PSEUDO_CLASS.
 */
Result CssSimpleSelectorSetNth(CssSimpleSelector *selector,
                                               int a,
                                               int b,
                                               CssDiag *diag);

/** Non-zero when @p selector carries a parsed An+B. */
int CssSimpleSelectorHasNth(const CssSimpleSelector *selector);

/** The parsed `a` coefficient (0 when there is none). */
int CssSimpleSelectorNthA(const CssSimpleSelector *selector);

/** The parsed `b` offset (0 when there is none). */
int CssSimpleSelectorNthB(const CssSimpleSelector *selector);

/**
 * Non-zero when @p selector's pseudo-class or pseudo-element is named @p ascii,
 * compared ASCII case-insensitively (CSS pseudo names are ASCII
 * case-insensitive). False for a NULL selector, a selector whose kind is not a
 * pseudo kind, and an unset name.
 */
int CssSimpleSelectorIsPseudoNamed(const CssSimpleSelector *selector,
                                            const char *ascii);

/** Non-zero when @p selector is a usable condition (kind is not NONE). */
int CssSimpleSelectorIsSet(const CssSimpleSelector *selector);

/** The compound selector that owns the simple selectors it holds. */
typedef struct CssCompoundSelector {
    CssSimpleSelector *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
    /** Extent of the whole compound selector. */
    CssRange range;
    /**
     * Owned nested argument lists, addressed by index from the simple selectors
     * above: `:not(...)`'s argument, and later any other functional form whose
     * argument is itself a selector list.
     *
     * They live here rather than on the simple selector for the ownership
     * reason stated at the top of this header — a simple selector owns nothing,
     * so it is still copied rather than moved. Each entry is a complete
     * `CssSelectorList` with its own name arena, so a nested list is
     * disposed exactly as a top-level one is.
     */
    CssSelectorList *nested; /* owned; NULL when nestedCount == 0 */
    size_t nestedCount;
    size_t nestedCapacity;
} CssCompoundSelector;

/** Clear @p compound to empty. Safe on NULL. */
void CssCompoundSelectorInit(CssCompoundSelector *compound);

/**
 * Dispose every nested argument list, then free the simple selector array and
 * the nested array. The simple selectors themselves own nothing, so there is
 * nothing to release element by element. Idempotent; safe on NULL.
 */
void CssCompoundSelectorDispose(CssCompoundSelector *compound);

/**
 * Append a *copy* of @p selector (simple selectors own nothing, so a copy is
 * safe and cheap). The first simple selector in a compound determines nothing;
 * order is preserved as written.
 *
 * The copy's `nestedIndex` is cleared: an index names a list inside the
 * compound that issued it, so carrying one into a different compound could only
 * ever be wrong. The parser attaches a list to the copy afterwards with
 * CssCompoundSelectorAttachNested.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unset
 *         selector; RoboOutOfMemory when the array cannot grow.
 */
Result CssCompoundSelectorAppend(
    CssCompoundSelector *compound,
    const CssSimpleSelector *selector,
    CssDiag *diag);

/**
 * Move @p list into @p compound's nested lists and point simple selector
 * @p simpleIndex at it.
 *
 * The two-step shape (append the simple selector, then attach its argument) is
 * what keeps the index and the list in step: the index is written only after the
 * list exists, so no window exists in which a simple selector addresses a hole.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an out-of-range
 *         @p simpleIndex; RoboOutOfMemory when the array cannot grow (the
 *         list is then untouched, and the caller still owns it).
 */
Result CssCompoundSelectorAttachNested(
    CssCompoundSelector *compound,
    size_t simpleIndex,
    CssSelectorList *list,
    CssDiag *diag);

/** Number of nested argument lists @p compound owns (0 for NULL). */
size_t CssCompoundSelectorNestedCount(const CssCompoundSelector *compound);

/**
 * Borrowed nested argument list @p index, or NULL when out of range.
 *
 * Prefer this over reading `nestedIndex` directly: a simple selector copied
 * between compounds, or one whose index was never attached, addresses nothing,
 * and this returns NULL for that instead of dereferencing a hole.
 */
const CssSelectorList *CssCompoundSelectorNestedAt(
    const CssCompoundSelector *compound,
    size_t index);

/**
 * The nested list simple selector @p simpleIndex actually resolves to, or NULL
 * when it has none or its index does not resolve. The one call a matcher needs.
 */
const CssSelectorList *CssCompoundSelectorArgumentOf(
    const CssCompoundSelector *compound,
    size_t simpleIndex);

/** Number of simple selectors (0 for NULL). */
size_t CssCompoundSelectorCount(const CssCompoundSelector *compound);

/** Borrowed simple selector at @p index, or NULL when out of range. */
const CssSimpleSelector *CssCompoundSelectorAt(
    const CssCompoundSelector *compound,
    size_t index);

/** Non-zero when @p compound holds at least one simple selector. */
int CssCompoundSelectorIsSet(const CssCompoundSelector *compound);

/**
 * Tighten the compound's range to span its simple selectors, or to the empty
 * range at 0 when it has none. The parser sets this after filling a compound;
 * it is never computed automatically, so an explicit range is never overwritten.
 */
void CssCompoundSelectorSealRange(CssCompoundSelector *compound);

/** A complex selector: N compounds joined by N-1 combinators. */
typedef struct CssComplexSelector {
    CssCompoundSelector *compounds; /* owned */
    size_t compoundCount;
    size_t compoundCapacity;
    CssCombinator *combinators; /* owned; one fewer than the compounds */
    size_t combinatorCount;
    size_t combinatorCapacity;
    /** Extent of the whole complex selector. */
    CssRange range;
} CssComplexSelector;

/** Clear @p selector to empty. Safe on NULL. */
void CssComplexSelectorInit(CssComplexSelector *selector);

/** Dispose every compound and combinator. Idempotent; safe on NULL. */
void CssComplexSelectorDispose(CssComplexSelector *selector);

/**
 * Append @p compound by moving it in; @p compound is left empty.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an empty
 *         compound; RoboOutOfMemory when the array cannot grow (the
 *         compound is then untouched).
 */
Result CssComplexSelectorAppendCompound(
    CssComplexSelector *selector,
    CssCompoundSelector *compound,
    CssDiag *diag);

/** Append a combinator trailing the compound last appended. */
Result CssComplexSelectorAppendCombinator(
    CssComplexSelector *selector,
    CssCombinator combinator,
    CssDiag *diag);

/** Number of compounds (0 for NULL). */
size_t CssComplexSelectorCompoundCount(
    const CssComplexSelector *selector);

/** Borrowed compound at @p index, or NULL when out of range. */
const CssCompoundSelector *CssComplexSelectorCompoundAt(
    const CssComplexSelector *selector,
    size_t index);

/** Number of combinators (0 for NULL). */
size_t CssComplexSelectorCombinatorCount(
    const CssComplexSelector *selector);

/**
 * The combinator between compound @p index and compound `index + 1`.
 * @return NONE when out of range. A NULL output is tolerated.
 */
CssCombinator CssComplexSelectorCombinatorAt(
    const CssComplexSelector *selector,
    size_t index);

/**
 * Non-zero when @p selector is structurally coherent: at least one compound,
 * and exactly one fewer combinator than compounds. The parser builds in the
 * order compound → combinator → compound, so this cannot be enforced append by
 * append; it is checked here, once, by whoever wants to know.
 */
int CssComplexSelectorIsWellFormed(
    const CssComplexSelector *selector);

/** A selector list: the comma-separated complex selectors of one rule. */
struct CssSelectorList {
    CssComplexSelector *items; /* owned */
    size_t count;
    size_t capacity;
    /** Extent of the whole list, including the commas. */
    CssRange range;
    /**
     * Owned arena holding the resolved spelling of every name in this list, its
     * nested argument lists included. NULL until something is interned.
     *
     * Escapes are resolved once, here, so that matching compares the bytes CSS
     * meant rather than the bytes the author typed. The arena never moves after
     * the parser reserves it (9.4 reserves it up front from the prelude's
     * length), which is what lets slices into it stay valid for the list's life.
     */
    char *names;
    size_t namesLength;
    size_t namesCapacity;
};

/**
 * Reserve @p bytes of name arena up front.
 *
 * The parser calls this before interning anything so that interning can never
 * reallocate and invalidate a name it already handed out. Reserving twice keeps
 * the larger reservation. A caller that does not reserve still works: interning
 * grows the arena itself, it just cannot guarantee its earlier slices survive.
 * @return RoboOk; RoboInvalidArgument for NULL; RoboOutOfMemory.
 */
Result CssSelectorListReserveNames(CssSelectorList *list,
                                                  size_t bytes,
                                                  CssDiag *diag);

/**
 * Copy @p length bytes at @p bytes into @p list's name arena and return a slice
 * over the copy.
 *
 * A zero-length name interns as an *empty but present* slice, not as the absent
 * one: `[a=""]` is an attribute selector comparing against an empty value, and
 * `[a]` is a different selector that asks only whether the attribute is there.
 * Conflating the two here would make them the same selector one layer up.
 *
 * The absent slice means failure — a NULL list, a non-zero length with no bytes,
 * or an arena that cannot grow — and that is the one case a caller must check.
 * The parser does, and fails the parse rather than recording a name that points
 * nowhere.
 */
CssSlice CssSelectorListIntern(CssSelectorList *list,
                                               const uint8_t *bytes,
                                               size_t length);

/** Bytes used in @p list's name arena (0 for NULL). */
size_t CssSelectorListNamesLength(const CssSelectorList *list);

/** Borrowed arena contents, or NULL when nothing was interned. */
const char *CssSelectorListNames(const CssSelectorList *list);

/** Clear @p list to empty. Safe on NULL. */
void CssSelectorListInit(CssSelectorList *list);

/**
 * Dispose every complex selector — and, through them, their compounds' nested
 * argument lists — then free the array and the name arena. Idempotent; safe on
 * NULL.
 */
void CssSelectorListDispose(CssSelectorList *list);

/**
 * Append @p selector by moving it in; @p selector is left empty.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an empty
 *         selector; RoboOutOfMemory when the array cannot grow.
 */
Result CssSelectorListAppend(CssSelectorList *list,
                                            CssComplexSelector *selector,
                                            CssDiag *diag);

/** Number of complex selectors (0 for NULL). */
size_t CssSelectorListCount(const CssSelectorList *list);

/** Borrowed complex selector at @p index, or NULL when out of range. */
const CssComplexSelector *CssSelectorListAt(
    const CssSelectorList *list,
    size_t index);

/** Non-zero when @p list holds at least one complex selector. */
int CssSelectorListIsSet(const CssSelectorList *list);

/** Non-zero when every complex selector in @p list is well formed. */
int CssSelectorListIsWellFormed(const CssSelectorList *list);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_SELECTOR_H */
