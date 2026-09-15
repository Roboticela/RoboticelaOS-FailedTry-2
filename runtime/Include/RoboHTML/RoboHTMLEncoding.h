/*
 * RoboHTML encoding boundary (Phase 8.1) — architecture stub.
 *
 * Phase 8.1 defines ONLY the boundary:
 *   - encoding identity (labels) and a static metadata table,
 *   - declared-vs-actual recording slots on the document,
 *   - the status model future decoders will report through.
 *
 * No detector, no decoder, no transcoding is implemented here. The future
 * encoding phase plugs into robo_html_document_input_encoding_changed /
 * _actual below without changing this ABI.
 *
 * Roboticela rule: UTF-8 is the runtime-native encoding. The table below
 * admits only labels the runtime will actually support; the full decoder
 * arrives in a later phase — until then attachment of sources declaring
 * other encodings still succeeds (metadata only) but parsing phases must
 * treat such documents as not-yet-decodable.
 */

#ifndef ROBO_HTML_ENCODING_H
#define ROBO_HTML_ENCODING_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encodings the runtime admits at the boundary. The numeric values are
 * stable identity tokens (do not renumber); the label table maps names.
 */
typedef enum robo_html_encoding {
    ROBO_HTML_ENCODING_UNKNOWN = 0,
    ROBO_HTML_ENCODING_UTF_8 = 1,
    ROBO_HTML_ENCODING_UTF_16LE = 2,
    ROBO_HTML_ENCODING_UTF_16BE = 3,
    ROBO_HTML_ENCODING_WINDOWS_1252 = 4,
    ROBO_HTML_ENCODING_SHIFT_JIS = 5,
    ROBO_HTML_ENCODING_UTF_16 = 6 /* alias handled at the boundary */
} robo_html_encoding_t;

/** Non-zero if @p enc is a defined encoding enumerator. */
int robo_html_encoding_is_known(robo_html_encoding_t enc);

/** Canonical label for @p enc; "unknown" when undefined. Never NULL. */
const char *robo_html_encoding_label(robo_html_encoding_t enc);

/**
 * Resolve an ASCII encoding label (case-insensitive) to its token.
 * Unknown label → ROBO_HTML_ENCODING_UNKNOWN (not an error: callers may
 * choose to record it as unsupported via the diagnostics boundary).
 */
robo_html_encoding_t robo_html_encoding_from_label(const char *label);

/**
 * Boundary status for encoding handling (future decoder reports through
 * this; 8.1 uses it to record declared/actual state transitions).
 */
typedef enum robo_html_encoding_status {
    ROBO_HTML_ENCODING_STATUS_OK = 0,
    /** Declared label unknown to the runtime (documented, not fatal). */
    ROBO_HTML_ENCODING_STATUS_UNSUPPORTED_LABEL = 1,
    /** Declared label is not valid ASCII / too long. */
    ROBO_HTML_ENCODING_STATUS_INVALID_LABEL = 2,
    /** Byte content does not match the declared/actual encoding. */
    ROBO_HTML_ENCODING_STATUS_DECODE_MISMATCH = 3,
    /** Malformed byte sequence (future decoder fills this). */
    ROBO_HTML_ENCODING_STATUS_MALFORMED = 4
} robo_html_encoding_status_t;

/** Non-zero if @p status is a defined encoding status enumerator. */
int robo_html_encoding_status_is_known(robo_html_encoding_status_t status);

/** Human-readable encoding status name; never NULL. */
const char *robo_html_encoding_status_name(robo_html_encoding_status_t status);

/**
 * Boundary record: what the source declared vs what the boundary decided.
 * Owned by the document; copies only — never borrows source memory.
 */
typedef struct robo_html_encoding_state {
    robo_html_encoding_t declared;   /* from source metadata / future <meta> */
    robo_html_encoding_t actual;     /* boundary decision (8.1: declared or UTF-8) */
    robo_html_encoding_status_t status;
} robo_html_encoding_state_t;

/** Reset @p st to UNKNOWN/UNKNOWN/OK. Safe on NULL. */
void robo_html_encoding_state_init(robo_html_encoding_state_t *st);

/**
 * Validate and normalize a declared label into @p out state fields.
 * Pure boundary helper — no I/O, no decoding.
 * @return ROBO_OK (label known), ROBO_NOT_SUPPORTED (label valid ASCII but
 *         not admitted), ROBO_PARSE_ERROR (label not ASCII / too long /
 *         NULL label with non-NULL out is still ROBO_INVALID_ARGUMENT).
 */
robo_result_t robo_html_encoding_declare(robo_html_encoding_state_t *st,
                                         const char *label,
                                         robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ENCODING_H */
