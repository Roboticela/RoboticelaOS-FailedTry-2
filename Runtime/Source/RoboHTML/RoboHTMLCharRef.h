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
 * set surfaced through HtmlDiag with the ENCODING domain). */
typedef enum HtmlCharrefError {
    RoboHtmlCharrefOk = 0,
    /** No ';' before an attribute-value stop character (legacy path). */
    RoboHtmlCharrefMissingSemicolon = 1,
    /** Unknown entity name — emitted as literal '&' text. */
    RoboHtmlCharrefUnknownNamed = 2,
    /** Empty/invalid numeric digits — emitted as literal '&' text. */
    RoboHtmlCharrefInvalidNumeric = 3,
    /** Out of range / surrogate / control — mapped per HTML rules. */
    RoboHtmlCharrefOutOfRange = 4
} HtmlCharrefError;

/**
 * Attempt to consume a character reference at @p bytes[@p offset].
 * @p offset must point at '&'.
 *
 * @param bytes        source (borrowed)
 * @param length       source byte length
 * @param offset       position of '&' (in/out; advanced past the reference
 *                     or past '&' alone when no reference matched)
 * @param inAttribute non-zero when expanding inside an attribute value
 *                     (enables the legacy semicolonless longest-prefix rule
 *                     with the following-character window)
 * @param outUtf8     output buffer (>= RoboHtmlCharrefMaxBytes)
 * @param outLen      bytes written (1..RoboHtmlCharrefMaxBytes)
 * @param outError    first error encountered (RoboHtmlCharrefOk when none)
 *
 * @return non-zero when a reference was consumed (@p offset advanced and
 *         output written); 0 when no reference matched — in that case the
 *         caller emits '&' as a literal character and re-scans from
 *         offset+1 (which this call has NOT consumed).
 */
int HtmlCharrefExpand(const char *bytes,
                             size_t length,
                             size_t *offset,
                             int inAttribute,
                             char *outUtf8,
                             size_t *outLen,
                             HtmlCharrefError *outError);

/** Max UTF-8 bytes one reference can expand to (2 code points × 4). */
#define RoboHtmlCharrefMaxBytes 8u

/** Map a numeric reference value per the HTML numeric-reference rules. */
uint32_t HtmlCharrefMapValue(uint32_t value,
                                     HtmlCharrefError *err);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_CHAR_REF_H */
