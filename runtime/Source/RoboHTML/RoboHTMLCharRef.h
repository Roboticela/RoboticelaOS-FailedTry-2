/*
 * Internal character-reference engine (Phase 8.2) — not a public API.
 */
#ifndef ROBO_HTML_CHAR_REF_H
#define ROBO_HTML_CHAR_REF_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RoboHTML-internal parse-error codes for character references (bounded
 * set surfaced through robo_html_diag_t with the ENCODING domain). */
typedef enum robo_html_charref_error {
    ROBO_HTML_CHARREF_OK = 0,
    /** No ';' before an attribute-value stop character (legacy path). */
    ROBO_HTML_CHARREF_MISSING_SEMICOLON = 1,
    /** Unknown entity name — emitted as literal '&' text. */
    ROBO_HTML_CHARREF_UNKNOWN_NAMED = 2,
    /** Empty/invalid numeric digits — emitted as literal '&' text. */
    ROBO_HTML_CHARREF_INVALID_NUMERIC = 3,
    /** Out of range / surrogate / control — mapped per HTML rules. */
    ROBO_HTML_CHARREF_OUT_OF_RANGE = 4
} robo_html_charref_error_t;

/**
 * Attempt to consume a character reference at @p bytes[@p offset].
 * @p offset must point at '&'.
 *
 * @param bytes        source (borrowed)
 * @param length       source byte length
 * @param offset       position of '&' (in/out; advanced past the reference
 *                     or past '&' alone when no reference matched)
 * @param in_attribute non-zero when expanding inside an attribute value
 *                     (enables the legacy semicolonless longest-prefix rule
 *                     with the following-character window)
 * @param out_utf8     output buffer (>= ROBO_HTML_CHARREF_MAX_BYTES)
 * @param out_len      bytes written (1..ROBO_HTML_CHARREF_MAX_BYTES)
 * @param out_error    first error encountered (ROBO_HTML_CHARREF_OK when none)
 *
 * @return non-zero when a reference was consumed (@p offset advanced and
 *         output written); 0 when no reference matched — in that case the
 *         caller emits '&' as a literal character and re-scans from
 *         offset+1 (which this call has NOT consumed).
 */
int robo_html_charref_expand(const char *bytes,
                             size_t length,
                             size_t *offset,
                             int in_attribute,
                             char *out_utf8,
                             size_t *out_len,
                             robo_html_charref_error_t *out_error);

/** Max UTF-8 bytes one reference can expand to (2 code points × 4). */
#define ROBO_HTML_CHARREF_MAX_BYTES 8u

/** Map a numeric reference value per the HTML numeric-reference rules. */
uint32_t robo_html_charref_map_value(uint32_t value,
                                     robo_html_charref_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_CHAR_REF_H */
