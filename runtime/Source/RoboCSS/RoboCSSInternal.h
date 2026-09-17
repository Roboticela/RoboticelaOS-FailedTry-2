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

#endif /* ROBO_CSS_INTERNAL_H */
