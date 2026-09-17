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
 *     robo_css_value_t value = ROBO_CSS_CLEARED;
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
#define ROBO_CSS_CLEARED {0}

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
typedef struct robo_css_slice {
    const uint8_t *bytes;
    size_t length;
} robo_css_slice_t;

/** An absent slice (NULL, length 0). */
robo_css_slice_t robo_css_slice_empty(void);

/** A view over @p length bytes at @p bytes. */
robo_css_slice_t robo_css_slice_from_bytes(const uint8_t *bytes, size_t length);

/** A view over a NUL-terminated string; NULL yields the absent slice. */
robo_css_slice_t robo_css_slice_from_text(const char *utf8);

/** Non-zero when @p slice holds no bytes (NULL pointer or zero length). */
int robo_css_slice_is_empty(robo_css_slice_t slice);

/** Non-zero when @p slice was never given a byte address (NULL pointer). */
int robo_css_slice_is_absent(robo_css_slice_t slice);

/** Byte-exact equality. Two absent slices are equal. */
int robo_css_slice_equals(robo_css_slice_t a, robo_css_slice_t b);

/** Byte-exact equality against a NUL-terminated ASCII string. */
int robo_css_slice_equals_ascii(robo_css_slice_t slice, const char *ascii);

/** ASCII case-insensitive equality against a NUL-terminated string. */
int robo_css_slice_equals_ascii_ci(robo_css_slice_t slice, const char *ascii);

/** Non-zero when @p slice begins with the ASCII @p prefix (case-sensitive). */
int robo_css_slice_starts_with_ascii(robo_css_slice_t slice, const char *prefix);

/**
 * Copy the slice's bytes into @p out.
 * Requires @p out_capacity >= slice.length. The bytes are not NUL-terminated.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT on a NULL destination or too little
 *         capacity.
 */
robo_result_t robo_css_slice_copy_bytes(robo_css_slice_t slice,
                                        uint8_t *out,
                                        size_t out_capacity);

/**
 * Copy the slice into @p out as a NUL-terminated string.
 * Requires @p out_capacity >= slice.length + 1.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT on a NULL destination or too little
 *         capacity.
 */
robo_result_t robo_css_slice_copy_string(robo_css_slice_t slice,
                                         char *out,
                                         size_t out_capacity);

/**
 * Split @p text on runs of ASCII whitespace (space, tab, LF, FF, CR) into
 * up to @p capacity borrowed fields, writing the number written to
 * @p out_count when non-NULL. When @p out_truncated is non-NULL it is set to
 * 1 if @p text held more fields than @p capacity, else 0.
 *
 * This is the helper the element view uses to turn a `class` attribute into
 * class slices, and it is deliberately the only text-splitting rule RoboCSS
 * owns: no allocation, no trimming of a class name beyond the whitespace runs
 * themselves, and @p text is never modified.
 *
 * @return number of fields written (0 for NULL/empty/whitespace-only text).
 */
size_t robo_css_split_ascii_whitespace(robo_css_slice_t text,
                                       robo_css_slice_t *out_fields,
                                       size_t capacity,
                                       int *out_truncated);

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
typedef struct robo_css_position {
    size_t offset;
    uint32_t line;
    uint32_t column;
} robo_css_position_t;

/** The "no position recorded" value: offset 0, line 0, column 0. */
robo_css_position_t robo_css_position_none(void);

/** A position with @p offset and no line/column information. */
robo_css_position_t robo_css_position_at(size_t offset);

/** Non-zero when @p position carries line/column information. */
int robo_css_position_has_line(const robo_css_position_t *position);

/**
 * A half-open byte range: `[begin, end)`. `begin == end` is an empty range,
 * which is a legal position marker (the cursor, an empty block).
 */
typedef struct robo_css_range {
    size_t begin;
    size_t end;
} robo_css_range_t;

/** The empty range at @p offset. */
robo_css_range_t robo_css_range_at(size_t offset);

/** A range covering @p length bytes from @p begin (saturating). */
robo_css_range_t robo_css_range_span(size_t begin, size_t length);

/** Non-zero when @p range covers no bytes. */
int robo_css_range_is_empty(robo_css_range_t range);

/** Number of bytes @p range covers (0 when malformed, i.e. end < begin). */
size_t robo_css_range_length(robo_css_range_t range);

/** Non-zero when @p offset lies inside @p range (`begin <= offset < end`). */
int robo_css_range_contains(robo_css_range_t range, size_t offset);

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

/** Maximum simultaneous live stylesheets (module slots). */
#define ROBO_CSS_MAX_STYLESHEETS 64u

/** Maximum length of a stylesheet/diagnostic label (copied). */
#define ROBO_CSS_NAME_MAX 256u

/** Maximum stored length of a registered property name (copied). */
#define ROBO_CSS_PROPERTY_NAME_MAX 128u

/** Maximum registered properties in one property table. */
#define ROBO_CSS_PROPERTY_TABLE_MAX 4096u

/** Initial capacity of every growable CSS collection. */
#define ROBO_CSS_COLLECTION_MIN_CAPACITY 4u

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
typedef enum robo_css_origin {
    ROBO_CSS_ORIGIN_NONE = 0,
    /** Styles the runtime itself provides (the user-agent origin). */
    ROBO_CSS_ORIGIN_RUNTIME = 1,
    /** A stylesheet installed by the user. */
    ROBO_CSS_ORIGIN_USER = 2,
    /** A stylesheet belonging to the document or application being styled. */
    ROBO_CSS_ORIGIN_AUTHOR = 3,
    /** A `style` attribute's declarations. */
    ROBO_CSS_ORIGIN_INLINE = 4
} robo_css_origin_t;

/** Number of defined origins (the length of a per-origin count array). */
#define ROBO_CSS_ORIGIN_COUNT 5u

/** Non-zero if @p origin is a defined origin enumerator. */
int robo_css_origin_is_known(robo_css_origin_t origin);

/** Human-readable origin name; never NULL; "unknown" when undefined. */
const char *robo_css_origin_name(robo_css_origin_t origin);

/** Non-zero for origins a stylesheet object can carry (RUNTIME/USER/AUTHOR). */
int robo_css_origin_is_stylesheet_origin(robo_css_origin_t origin);

/** Non-zero for ROBO_CSS_ORIGIN_INLINE. */
int robo_css_origin_is_inline(robo_css_origin_t origin);

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
typedef enum robo_css_stylesheet_state {
    ROBO_CSS_STYLESHEET_STATE_NONE = 0,
    ROBO_CSS_STYLESHEET_STATE_CREATED = 1,
    ROBO_CSS_STYLESHEET_STATE_PARSED = 2,
    ROBO_CSS_STYLESHEET_STATE_FAILED = 3
} robo_css_stylesheet_state_t;

/** Number of defined stylesheet states. */
#define ROBO_CSS_STYLESHEET_STATE_COUNT 4u

/** Non-zero if @p state is a defined stylesheet state enumerator. */
int robo_css_stylesheet_state_is_known(robo_css_stylesheet_state_t state);

/** Human-readable stylesheet state name; never NULL. */
const char *robo_css_stylesheet_state_name(robo_css_stylesheet_state_t state);

/** Non-zero when @p state means a parse attempt has finished. */
int robo_css_stylesheet_state_is_settled(robo_css_stylesheet_state_t state);

/* ------------------------------------------------------------------ */
/* Diagnostic domains                                                  */
/* ------------------------------------------------------------------ */

/**
 * Which RoboCSS subsystem reported a failure.
 *
 * Kept beside the shared robo_result_t codes rather than replacing them: a
 * caller keeps switching on robo_result_t, and this domain refines it without
 * creating a second error framework (see RoboCSSDiagnostics.h).
 */
typedef enum robo_css_diagnostic_domain {
    ROBO_CSS_DOMAIN_NONE = 0,
    /** Module lifecycle: not initialized, already initialized, slot exhausted. */
    ROBO_CSS_DOMAIN_MODULE = 1,
    /** CSS source: configuration, bounds, positions, ranges. */
    ROBO_CSS_DOMAIN_SOURCE = 2,
    /** Token model: construction and type queries. */
    ROBO_CSS_DOMAIN_TOKEN = 3,
    /** Component values: construction, nesting, ownership. */
    ROBO_CSS_DOMAIN_VALUE = 4,
    /** Property identity: the table, registration, lookup. */
    ROBO_CSS_DOMAIN_PROPERTY = 5,
    /** Declarations: name/value/importance, and declaration lists. */
    ROBO_CSS_DOMAIN_DECLARATION = 6,
    /** Selectors: the simple/compound/complex data model. */
    ROBO_CSS_DOMAIN_SELECTOR = 7,
    /** Rules: style rules and at-rules. */
    ROBO_CSS_DOMAIN_RULE = 8,
    /** Stylesheets: slots, source attachment, rule storage, state. */
    ROBO_CSS_DOMAIN_STYLESHEET = 9,
    /** The parser boundary: cursor state and the not-yet-implemented parse. */
    ROBO_CSS_DOMAIN_PARSER = 10,
    /** Style resolution boundary: element views, the style context. */
    ROBO_CSS_DOMAIN_STYLE = 11,
    /** Computed-style container boundary. */
    ROBO_CSS_DOMAIN_COMPUTED = 12
} robo_css_diagnostic_domain_t;

/** Number of defined diagnostic domains. */
#define ROBO_CSS_DOMAIN_COUNT 13u

/** Non-zero if @p domain is a defined diagnostic domain enumerator. */
int robo_css_diagnostic_domain_is_known(robo_css_diagnostic_domain_t domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *robo_css_diagnostic_domain_name(robo_css_diagnostic_domain_t domain);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TYPES_H */
