/*
 * RoboCSS fundamental identities, shared value types and limits (Phase 9.1).
 *
 * RoboCSS is the style subsystem of the Roboticela runtime: CSS bytes in, a
 * stylesheet model out, and — in later phases — a computed style per DOM
 * element. Phase 9.1 builds the architecture and the data model only. Every
 * later subsystem is a declared boundary here: the tokenizer (9.2), the parser
 * (9.3), selectors (9.4), the cascade (9.5), specificity (9.6), inheritance
 * (9.7), computed values (9.8) and the property families (9.9+) all have their
 * types and entry points declared, and none of them is pretended to work.
 *
 * Identity rules (the same token discipline as RoboHTML and RoboWindow):
 *   - 0 always means "none" and is never a live identity.
 *   - Stylesheet ids come from a monotonic cursor and are never reused, so a
 *     destroyed id stays dead and a stale handle is detectable.
 *   - Property ids are *per table*: a property id is meaningful only against
 *     the table that issued it, and looking one up in another table is a
 *     programming error, not an error value to recover from.
 *
 * Slices are borrowed views over bytes somebody else owns. A slice never owns
 * its bytes and is never freed by RoboCSS; every API that produces one
 * documents which owner keeps those bytes alive.
 *
 * RoboCSS may depend only on RoboBase, RoboMem and RoboContainers, and later on
 * the RoboDOM boundary. It must never depend on RoboHTML (HTML and CSS are
 * peers that meet at the DOM, per DEPENDENCIES.md), RoboLayout, RoboPaint,
 * RoboRender, RoboWindow, RoboGraphics, RoboDisplay, or Chromium.
 */

#ifndef ROBO_CSS_TYPES_H
#define ROBO_CSS_TYPES_H

#include "RoboBase/RoboError.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Record lifetime                                                     */
/* ------------------------------------------------------------------ */

/**
 * Declare a RoboCSS record already cleared, e.g.
 *
 *     CssValue value = RoboCssCleared;
 *
 * RoboCSS models its syntax out of by-value records (a value, a token, a
 * declaration, a selector, a rule) rather than opaque pointers, because these
 * are inert description with no external identity. The price of that choice is
 * one rule, stated here once and depended on everywhere:
 *
 *     A record must be CLEARED before any call that may release what it
 *     already holds: every make/init/append/dispose call does exactly that.
 *
 * Those calls release first and then set, so reconfiguring a record never
 * leaks what it held before. That is what makes making a token over a value
 * that already has children safe, and it is the reason an uninitialized record
 * is not safe: the release step reads fields that still hold whatever was on
 * the stack, and a stale pointer there is a wild free.
 *
 * The cleared state is the all-zero byte pattern, and deliberately so: every
 * field's zero value is its empty value (each NONE enumerator is 0, a slice's
 * empty form is NULL/0, an empty range is 0/0, an empty collection is
 * NULL/0/0). So this macro and the matching init function produce exactly the
 * same record, and RoboCSS states the rule without needing a zero-length
 * constructor per type: declaring with this macro and declaring then calling
 * init are interchangeable.
 *
 * Zeroing a record by any other route (memset, a caller's own initializer) is
 * equally valid. Only leaving it uninitialized is not.
 */
#define RoboCssCleared {0}

/* ------------------------------------------------------------------ */
/* Borrowed byte views                                                 */
/* ------------------------------------------------------------------ */

/**
 * A borrowed view over @p length bytes.
 *
 * `bytes` may be NULL only when `length` is 0. A slice is *absent* when
 * `bytes` is NULL (nothing was recorded) and *empty* when `length` is 0 — the
 * same presence/emptiness distinction the HTML attribute model makes, and for
 * the same reason: "we looked and there is nothing here" is a different fact
 * from "nobody ever looked".
 */
typedef struct CssSlice {
    const uint8_t *bytes;
    size_t length;
} CssSlice;

/** An absent slice (NULL, length 0). */
CssSlice CssSliceEmpty(void);

/** A view over @p length bytes at @p bytes. */
CssSlice CssSliceFromBytes(const uint8_t *bytes, size_t length);

/** A view over a NUL-terminated string; NULL yields the absent slice. */
CssSlice CssSliceFromText(const char *utf8);

/** Non-zero when @p slice holds no bytes (NULL pointer or zero length). */
int CssSliceIsEmpty(CssSlice slice);

/** Non-zero when @p slice was never given a byte address (NULL pointer). */
int CssSliceIsAbsent(CssSlice slice);

/** Byte-exact equality. Two absent slices are equal. */
int CssSliceEquals(CssSlice a, CssSlice b);

/**
 * ASCII case-insensitive equality of two slices.
 *
 * Some CSS names fold and some do not, and the difference matters: property
 * names and attribute names are ASCII case-insensitive, class names and ids are
 * not. This is the comparison for the first kind, and it is deliberately a
 * slice-to-slice call rather than one that takes a C string, because both sides
 * of such a comparison are usually borrowed views (a declaration's name, an
 * attribute name from a host) rather than NUL-terminated text.
 *
 * Two absent slices are equal, as they are for CssSliceEquals; an absent
 * slice and an empty one are equal too (both hold no bytes).
 */
int CssSliceEqualsCi(CssSlice a, CssSlice b);

/** Byte-exact equality against a NUL-terminated ASCII string. */
int CssSliceEqualsAscii(CssSlice slice, const char *ascii);

/** ASCII case-insensitive equality against a NUL-terminated string. */
int CssSliceEqualsAsciiCi(CssSlice slice, const char *ascii);

/** Non-zero when @p slice begins with the ASCII @p prefix (case-sensitive). */
int CssSliceStartsWithAscii(CssSlice slice, const char *prefix);

/**
 * Copy the slice's bytes into @p out.
 * Requires @p outCapacity >= slice.length. The bytes are not NUL-terminated.
 * @return RoboOk; RoboInvalidArgument on a NULL destination or too little
 *         capacity.
 */
Result CssSliceCopyBytes(CssSlice slice,
                                        uint8_t *out,
                                        size_t outCapacity);

/**
 * Copy the slice into @p out as a NUL-terminated string.
 * Requires @p outCapacity >= slice.length + 1.
 * @return RoboOk; RoboInvalidArgument on a NULL destination or too little
 *         capacity.
 */
Result CssSliceCopyString(CssSlice slice,
                                         char *out,
                                         size_t outCapacity);

/**
 * Split @p text on runs of ASCII whitespace (space, tab, LF, FF, CR) into
 * up to @p capacity borrowed fields, writing the number written to
 * @p outCount when non-NULL. When @p outTruncated is non-NULL it is set to
 * 1 if @p text held more fields than @p capacity, else 0.
 *
 * This is the helper the element view uses to turn a `class` attribute into
 * class slices, and it is deliberately the only text-splitting rule RoboCSS
 * owns: no allocation, no trimming of a class name beyond the whitespace runs
 * themselves, and @p text is never modified.
 *
 * @return number of fields written (0 for NULL/empty/whitespace-only text).
 */
size_t CssSplitAsciiWhitespace(CssSlice text,
                                       CssSlice *outFields,
                                       size_t capacity,
                                       int *outTruncated);

/* ------------------------------------------------------------------ */
/* Positions and ranges                                                */
/* ------------------------------------------------------------------ */

/**
 * A location inside a CSS source.
 *
 * `offset` is a byte offset. `line` is 1-based and counts a CR, an LF or a
 * CRLF pair as exactly one line break. `column` is 1-based and counts Unicode
 * scalar values (not bytes) since the start of the current line, so a diag-
 * nostic column number means the same thing in an ASCII stylesheet and in one
 * written in Arabic or Japanese. `line == 0` means "no position was recorded"
 * — a model built by hand rather than by a parser, for instance.
 */
typedef struct CssPosition {
    size_t offset;
    uint32_t line;
    uint32_t column;
} CssPosition;

/** The "no position recorded" value: offset 0, line 0, column 0. */
CssPosition CssPositionNone(void);

/** A position with @p offset and no line/column information. */
CssPosition CssPositionAt(size_t offset);

/** Non-zero when @p position carries line/column information. */
int CssPositionHasLine(const CssPosition *position);

/**
 * A half-open byte range: `[begin, end)`. `begin == end` is an empty range,
 * which is a legal position marker (the cursor, an empty block).
 */
typedef struct CssRange {
    size_t begin;
    size_t end;
} CssRange;

/** The empty range at @p offset. */
CssRange CssRangeAt(size_t offset);

/** A range covering @p length bytes from @p begin (saturating). */
CssRange CssRangeSpan(size_t begin, size_t length);

/** Non-zero when @p range covers no bytes. */
int CssRangeIsEmpty(CssRange range);

/** Number of bytes @p range covers (0 when malformed, i.e. end < begin). */
size_t CssRangeLength(CssRange range);

/** Non-zero when @p offset lies inside @p range (`begin <= offset < end`). */
int CssRangeContains(CssRange range, size_t offset);

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

/** Maximum simultaneous live stylesheets (module slots). */
#define RoboCssMaxStylesheets 64u

/** Maximum length of a stylesheet/diagnostic label (copied). */
#define RoboCssNameMax 256u

/** Maximum stored length of a registered property name (copied). */
#define RoboCssPropertyNameMax 128u

/** Maximum registered properties in one property table. */
#define RoboCssPropertyTableMax 4096u

/** Initial capacity of every growable CSS collection. */
#define RoboCssCollectionMinCapacity 4u

/**
 * Maximum nesting depth the parser will build (9.3).
 *
 * "Depth" means one thing for rules and another for values, and the number
 * covers both: an at-rule's block inside an at-rule's block, and a function or
 * simple block inside a component value. It is a *budget*, not a limit on legal
 * CSS — 64 levels of `calc(calc(...))` or of nested `@media` is far past
 * anything an author writes and well inside a comfortable stack.
 *
 * Past the bound the parser neither recurses nor fails: it consumes the
 * construct flat and records a recoverable error, so adversarial input costs a
 * bounded amount of stack and no correctness, and the token stream still ends
 * at a defined place. Disposal walks the tree the parser built, so it recurses
 * exactly as deep as this bound allows and no deeper.
 */
#define RoboCssMaxNesting 64u

/* ------------------------------------------------------------------ */
/* Stylesheet origin                                                   */
/* ------------------------------------------------------------------ */

/**
 * Where a set of declarations came from.
 *
 * This is provenance, not priority. The cascade (9.5) decides what wins; this
 * vocabulary only lets the model record where a rule was written. INLINE is
 * deliberately *not* a stylesheet origin: an element's `style` attribute is
 * not a stylesheet and never becomes one here — the DOM keeps the attribute
 * and RoboCSS parses its text into declarations on request.
 */
typedef enum CssOrigin {
    RoboCssOriginNone = 0,
    /** Styles the runtime itself provides (the user-agent origin). */
    RoboCssOriginRuntime = 1,
    /** A stylesheet installed by the user. */
    RoboCssOriginUser = 2,
    /** A stylesheet belonging to the document or application being styled. */
    RoboCssOriginAuthor = 3,
    /** A `style` attribute's declarations. */
    RoboCssOriginInline = 4
} CssOrigin;

/** Number of defined origins (the length of a per-origin count array). */
#define RoboCssOriginCount 5u

/** Non-zero if @p origin is a defined origin enumerator. */
int CssOriginIsKnown(CssOrigin origin);

/** Human-readable origin name; never NULL; "unknown" when undefined. */
const char *CssOriginName(CssOrigin origin);

/** Non-zero for origins a stylesheet object can carry (RUNTIME/USER/AUTHOR). */
int CssOriginIsStylesheetOrigin(CssOrigin origin);

/** Non-zero for RoboCssOriginInline. */
int CssOriginIsInline(CssOrigin origin);

/* ------------------------------------------------------------------ */
/* Stylesheet state                                                    */
/* ------------------------------------------------------------------ */

/**
 * What a stylesheet object is.
 *
 * The three live states are deliberately few. CREATED means "exists, no parse
 * attempt yet"; PARSED means "a parse attempt finished and produced this rule
 * list" — a *recovered* parse is still PARSED, because CSS error recovery is
 * the normal case and the rules that survived are real rules; FAILED means
 * "the attempt could not produce a usable stylesheet at all", which
 * distinguishes "nothing was recovered" from "an empty stylesheet was
 * recovered". There is no DESTROYED state: a destroyed stylesheet is an id
 * that no longer resolves.
 */
typedef enum CssStylesheetState {
    RoboCssStylesheetStateNone = 0,
    RoboCssStylesheetStateCreated = 1,
    RoboCssStylesheetStateParsed = 2,
    RoboCssStylesheetStateFailed = 3
} CssStylesheetState;

/** Number of defined stylesheet states. */
#define RoboCssStylesheetStateCount 4u

/** Non-zero if @p state is a defined stylesheet state enumerator. */
int CssStylesheetStateIsKnown(CssStylesheetState state);

/** Human-readable stylesheet state name; never NULL. */
const char *CssStylesheetStateName(CssStylesheetState state);

/** Non-zero when @p state means a parse attempt has finished. */
int CssStylesheetStateIsSettled(CssStylesheetState state);

/* ------------------------------------------------------------------ */
/* Diagnostic domains                                                  */
/* ------------------------------------------------------------------ */

/**
 * Which RoboCSS subsystem reported a failure.
 *
 * Kept beside the shared Result codes rather than replacing them: a
 * caller keeps switching on Result, and this domain refines it without
 * creating a second error framework (see RoboCSSDiagnostics.h).
 */
typedef enum CssDiagnosticDomain {
    RoboCssDomainNone = 0,
    /** Module lifecycle: not initialized, already initialized, slot exhausted. */
    RoboCssDomainModule = 1,
    /** CSS source: configuration, bounds, positions, ranges. */
    RoboCssDomainSource = 2,
    /** Token model: construction and type queries. */
    RoboCssDomainToken = 3,
    /** Component values: construction, nesting, ownership. */
    RoboCssDomainValue = 4,
    /** Property identity: the table, registration, lookup. */
    RoboCssDomainProperty = 5,
    /** Declarations: name/value/importance, and declaration lists. */
    RoboCssDomainDeclaration = 6,
    /** Selectors: the simple/compound/complex data model. */
    RoboCssDomainSelector = 7,
    /** Rules: style rules and at-rules. */
    RoboCssDomainRule = 8,
    /** Stylesheets: slots, source attachment, rule storage, state. */
    RoboCssDomainStylesheet = 9,
    /** The parser boundary: cursor state and the not-yet-implemented parse. */
    RoboCssDomainParser = 10,
    /** Style resolution boundary: element views, the style context. */
    RoboCssDomainStyle = 11,
    /** Computed-style container boundary. */
    RoboCssDomainComputed = 12,
    /** The property registry: classification and initial values (9.7). */
    RoboCssDomainPropertyRegistry = 13,
    /** Inheritance: the resolved-style representation and its resolution (9.7). */
    RoboCssDomainInherit = 14,
    /** The box model: sides, shorthands and their expansion (9.11). */
    RoboCssDomainBox = 15,
    /** The display vocabulary: the closed set and its classifications (9.12). */
    RoboCssDomainDisplay = 16,
    /** The positioning vocabulary: the closed set and its classifications (9.13). */
    RoboCssDomainPosition = 17,
    /** Background properties and the `background` shorthand (9.14). */
    RoboCssDomainBackground = 18,
    /** Border properties and the five distributing shorthands (9.15). */
    RoboCssDomainBorder = 19,
    /** The overflow vocabulary: the closed set and its classifications (10.13). */
    RoboCssDomainOverflow = 20,
    /** The CSS object model: serialization and style-declaration editing (18.2). */
    RoboCssDomainCssom = 21,
    /** The flexbox vocabulary: the three closed sets and their classifications (18.3). */
    RoboCssDomainFlex = 22
} CssDiagnosticDomain;

/** Number of defined diagnostic domains. */
#define RoboCssDomainCount 23u

/** Non-zero if @p domain is a defined diagnostic domain enumerator. */
int CssDiagnosticDomainIsKnown(CssDiagnosticDomain domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *CssDiagnosticDomainName(CssDiagnosticDomain domain);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TYPES_H */
