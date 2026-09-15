/*
 * Named character-reference table (Phase 8.2).
 *
 * Independent, compact table of the named references that occur in real
 * HTML documents, selected for the Roboticela runtime (not the full 2000+
 * WHATWG entities.json dump — the full table can be generated later from
 * the same structure without changing the lookup code).
 *
 * Entries are ASCII-lowercase, UNIQUE PREFIX-FREE sorted by name so a
 * single linear scan can find both exact matches and the longest prefix
 * match in one pass. `codepoints[0] == 0` marks a two-code-point entry
 * (legacy windowed semicolonless forms such as &notin → ¬+U+203E are NOT
 * included; Roboticela emits the base character plus a diagnostic).
 *
 * Keep sorted: RoboHTMLCharRef.c relies on it (asserted by a test).
 */
#ifndef ROBO_HTML_CHAR_REF_DATA_H
#define ROBO_HTML_CHAR_REF_DATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_html_charref_entry {
    const char *name; /* without & and ; */
    uint32_t cp1;
    uint32_t cp2; /* 0 when the entity maps to a single code point */
} robo_html_charref_entry_t;

extern const robo_html_charref_entry_t robo_html_charref_table[];
extern const size_t robo_html_charref_table_size;

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_CHAR_REF_DATA_H */
