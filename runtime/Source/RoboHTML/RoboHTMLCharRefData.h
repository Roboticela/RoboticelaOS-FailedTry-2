/*
 * Named character-reference table (Phase 8.2).
 *
 * Independent, compact table of the named references that occur in real
 * HTML documents, selected for the Roboticela runtime (not the full 2000+
 * WHATWG entities.json dump — the full table can be generated later from
 * the same structure without changing the lookup code).
 *
 * Entry names are the ASCII spelling *without* the leading '&' and
 * including the trailing ';' when the reference requires one, so both
 * legacy semicolonless forms ("amp") and terminated forms ("amp;") are
 * separate entries. Matching is byte-wise, therefore mixed-case names
 * ("Dagger", "OElig") are case-sensitive exactly as the catalogue spells
 * them.
 *
 * Name order carries NO meaning: the catalogue is grouped by category and
 * the engine (RoboHTMLCharRef.c) selects the longest matching name itself.
 * A non-zero `cp2` marks a two-code-point expansion (e.g. "NotEqualTilde;"
 * → U+2242 U+0338).
 *
 * The catalogue is a curated subset of the WHATWG named references; it can
 * grow to the full set without changing the lookup contract.
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
