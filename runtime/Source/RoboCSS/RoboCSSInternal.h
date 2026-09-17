/*
 * RoboCSS internal helpers (Phase 9.1).
 *
 * Private to the subsystem: nothing here is exported, and no public header
 * includes this one. It holds the two things every RoboCSS translation unit
 * would otherwise re-implement — ASCII classification that does not depend on
 * the host locale, and one growable-array rule — plus the UTF-8 scan the source
 * model derives positions and scalar counts from.
 *
 * The growable-array rule is deliberately the only one in the subsystem. Every
 * collection (value children, declarations, selectors, rules, sheets) keeps its
 * own typed pointer/count/capacity triple and all of them grow through
 * robo_css_grow. The collections are *not* exposed as a shared container type
 * because they hold owning elements and are appended by moving into them: a
 * generic container would invite a caller to copy one in, and the ownership
 * rules in this subsystem's headers say they must not.
 */

#ifndef ROBO_CSS_INTERNAL_H
#define ROBO_CSS_INTERNAL_H

#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSCascade.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboCSS/RoboCSSTypes.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* ASCII, independent of the host locale                               */
/* ------------------------------------------------------------------ */

/** Non-zero for space, tab, LF, FF, CR (the CSS/HTML whitespace set). */
int robo_css_ascii_is_space(uint8_t c);

/** Non-zero for `0`–`9`. */
int robo_css_ascii_is_digit(uint8_t c);

/** Non-zero for `a`–`z` and `A`–`Z`. */
int robo_css_ascii_is_alpha(uint8_t c);

/** Lowercase an ASCII letter; any other byte is returned unchanged. */
uint8_t robo_css_ascii_lower(uint8_t c);

/**
 * ASCII case-insensitive comparison of @p length bytes at @p bytes against the
 * NUL-terminated @p ascii. A NUL inside @p bytes can never match.
 * @return 1 on equality, 0 otherwise (including for NULL arguments).
 */
int robo_css_bytes_equal_ascii_ci(const uint8_t *bytes, size_t length, const char *ascii);

/** Byte-exact variant of the above. */
int robo_css_bytes_equal_ascii(const uint8_t *bytes, size_t length, const char *ascii);

/**
 * Copy @p src into the fixed buffer @p dst of @p capacity bytes, NUL-terminated
 * and truncated when necessary. A NULL @p src clears the buffer, so a label is
 * always a valid string.
 */
void robo_css_copy_label(char *dst, size_t capacity, const char *src);

/* ------------------------------------------------------------------ */
/* UTF-8 scanning (never rewriting)                                    */
/* ------------------------------------------------------------------ */

/**
 * Number of bytes in the well-formed scalar at @p bytes, or 1 when the bytes
 * there cannot start or complete a well-formed scalar — overlong forms,
 * surrogates and anything above U+10FFFF are all "one ill-formed position".
 * Returns 0 only for @p available == 0.
 */
size_t robo_css_utf8_scalar_length(const uint8_t *bytes, size_t available);

/**
 * Non-zero for a byte a CSS newline is made of: LF, CR, FF. The tokenizer
 * treats these as one code point, and a CRLF pair as one break, without ever
 * rewriting the caller's bytes (see the input-preprocessing note in
 * RoboCSSTokenizer.h).
 */
int robo_css_is_newline_byte(uint8_t b);

/**
 * Bytes of the newline at @p bytes: 2 for a CRLF pair, 1 for a lone LF/CR/FF,
 * 0 when the bytes there are not a newline or @p available == 0.
 */
size_t robo_css_newline_length(const uint8_t *bytes, size_t available);

/* ------------------------------------------------------------------ */
/* CSS escapes                                                         */
/* ------------------------------------------------------------------ */

/**
 * One escape, as CSS Syntax's "consume an escaped code point" resolves it.
 *
 * An escape is a backslash that is not followed by a newline: the newline case
 * is not an escape at all (it is a parse error, and the backslash is a
 * delimiter on its own). A hexadecimal escape runs to six digits and swallows
 * one following whitespace — a whole CRLF pair counts as one — and yields
 * U+FFFD when what it spells is zero, a surrogate, or above U+10FFFF, which is
 * the specification's replacement rule and not a local invention.
 */
typedef struct robo_css_escape {
    /** 1 when a valid escape starts at the bytes looked at; 0 otherwise. */
    int valid;
    /** 1 when the escape was written as hexadecimal digits; 0 for a literal
     *  escape, whose character is already spelled in the source. */
    int is_hex;
    /** Bytes the escape occupies, backslash included (0 when not valid). */
    size_t length;
    /**
     * The code point it yields; 0 when not valid.
     *
     * Always filled, including for a literal escape (`\.` yields `.`, and a
     * literal non-ASCII escape yields the character the source already spells),
     * so a consumer can decide between re-encoding this and copying the source
     * bytes without having to ask which kind of escape it got.
     */
    uint32_t code_point;
} robo_css_escape_t;

/**
 * Inspect the bytes at @p bytes/@p available as a CSS escape.
 * Never reads past @p available; @p bytes is not dereferenced when it is 0.
 */
robo_css_escape_t robo_css_escape_scan(const uint8_t *bytes, size_t available);

/**
 * Append the UTF-8 encoding of @p code_point to @p out, which holds @p length
 * bytes of @p capacity. A code point that cannot be encoded (a surrogate, or
 * above U+10FFFF) is written as U+FFFD, which is what CSS does with it.
 *
 * Writes nothing and reports 1 when @p code_point does not fit — the caller
 * decides whether that is a failure, so this never silently truncates a scalar.
 * @return 1 when the bytes were written, 0 when they did not fit or an argument
 *         is unusable.
 */
int robo_css_utf8_append(uint8_t *out, size_t capacity, size_t *length, uint32_t code_point);

/* ------------------------------------------------------------------ */
/* Growable collections                                                */
/* ------------------------------------------------------------------ */

/**
 * Ensure @p *items can hold one more element.
 *
 * Grows to twice the current capacity from ROBO_CSS_COLLECTION_MIN_CAPACITY.
 * @return 1 when @p *items has room for @p count + 1 elements; 0 on allocation
 *         failure or arithmetic overflow, in which case nothing is changed.
 *         The caller keeps ownership of the block and owns the new one after
 *         the move, exactly as robo_realloc does.
 */
int robo_css_grow(void **items, size_t *capacity, size_t count, size_t element_size);

/* ------------------------------------------------------------------ */
/* Module slot table                                                   */
/* ------------------------------------------------------------------ */

/**
 * One stylesheet slot.
 *
 * Slots live in the module (RoboCSS.c) and are addressed by id; the slot struct
 * itself is private so that a stylesheet's storage cannot become part of any
 * public contract. A slot owns its source (when it is a COPY) and its rules,
 * and holds the last parse error as a value rather than as state.
 */
typedef struct robo_css_stylesheet_slot {
    int in_use;
    robo_css_stylesheet_id_t id;
    robo_css_origin_t origin;
    robo_css_stylesheet_state_t state;
    char name[ROBO_CSS_NAME_MAX];
    robo_css_source_t source;
    robo_css_rule_list_t rules;
    int has_error;
    robo_css_error_t error;
} robo_css_stylesheet_slot_t;

/** Resolve @p id to its slot, or NULL when it does not resolve. */
robo_css_stylesheet_slot_t *robo_css_slot_find(robo_css_stylesheet_id_t id);

/** Borrow a free slot, or NULL when the table is full. */
robo_css_stylesheet_slot_t *robo_css_slot_alloc(void);

/**
 * Take @p slot for a new stylesheet of @p origin and return its id.
 * The id comes from a monotonic cursor, so it is never reused.
 */
robo_css_stylesheet_id_t robo_css_slot_claim(robo_css_stylesheet_slot_t *slot,
                                             robo_css_origin_t origin);

/**
 * Return @p slot to the table: dispose its source and rules, reset it, and
 * mark it free. Safe on NULL.
 */
void robo_css_slot_release(robo_css_stylesheet_slot_t *slot);

/** Id of the slot at @p index in slot order; NONE when out of range. */
robo_css_stylesheet_id_t robo_css_slot_id_at(size_t index);

/** Number of live slots. */
size_t robo_css_slot_live_count(void);

/* ------------------------------------------------------------------ */
/* The grammar (9.3)                                                   */
/* ------------------------------------------------------------------ */

/*
 * The CSS Syntax grammar, living in RoboCSSParserGrammar.c.
 *
 * It is internal for one reason: Phase 9.1 fixed the two entry points a parse
 * can arrive through, and they are the public API. A stylesheet's source is
 * parsed by robo_css_stylesheet_parse (RoboCSSStyleSheet.h) and a `style`
 * attribute's text by robo_css_declaration_list_parse_text
 * (RoboCSSDeclaration.h); publishing a third `parse` here would be a second way
 * to produce the same thing, which is exactly what the 9.1 parser header says
 * it does not do.
 *
 * Both functions consume the *tokenizer*, never the source bytes: the parser
 * has no byte-level scanning of its own. Both are transactional — they build
 * into a local list and hand it over only on success — so a failed attempt
 * leaves the caller's object exactly as it was.
 *
 * Both report the first CSS problem through @p out_error, which is always
 * filled (kind NONE when the input was clean) and is never NULL-checked away.
 * A recovered problem is not a failed call: these return ROBO_OK whenever they
 * produced a result, and only ROBO_OUT_OF_MEMORY or ROBO_INVALID_ARGUMENT
 * otherwise. @p origin is recorded and never interpreted.
 */

/**
 * Parse @p source into @p out_rules (which is initialised first, and is left
 * empty when the parse could not run at all).
 */
robo_result_t robo_css_grammar_parse_stylesheet(robo_css_rule_list_t *out_rules,
                                                const robo_css_source_t *source,
                                                robo_css_origin_t origin,
                                                robo_css_error_t *out_error);

/**
 * Parse @p source as a declaration list into @p out_list — the whole of the
 * inline-style boundary (9.1.10). Declarations borrow @p source's bytes, so
 * @p source must outlive @p out_list.
 */
robo_result_t robo_css_grammar_parse_declarations(
    robo_css_declaration_list_t *out_list,
    const robo_css_source_t *source,
    robo_css_origin_t origin,
    robo_css_error_t *out_error);

/* ------------------------------------------------------------------ */
/* The cascade (9.5)                                                   */
/* ------------------------------------------------------------------ */

/*
 * The cascade's collection, living in RoboCSSCascade.c.
 *
 * Internal for the same reason the grammar is: Phase 9.1 fixed the doors a
 * resolution can arrive through, and robo_css_style_context_match_declarations
 * (RoboCSSStyle.h) is the only one. The context owns the sheets, the host and
 * the origins a cascade is a function of, so publishing a second way to collect
 * one would be publishing a way to collect a cascade from nowhere.
 *
 * `out` must already be initialised or cleared; it is initialised here as well,
 * because a collection replaces whatever the record held. On a failure that
 * produced no answer at all, `out` is left empty rather than partly filled.
 */
robo_result_t robo_css_cascade_collect(const robo_css_style_context_t *context,
                                       robo_css_element_ref_t element,
                                       robo_css_cascade_t *out,
                                       robo_css_error_t *out_error,
                                       robo_css_diag_t *diag);

#endif /* ROBO_CSS_INTERNAL_H */
