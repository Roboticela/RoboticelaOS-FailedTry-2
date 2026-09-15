/*
 * RoboHTML source/input abstraction (Phase 8.1).
 *
 * Defines how HTML source reaches the subsystem without binding RoboHTML
 * to any transport. Supported kinds:
 *
 *   MEMORY  — borrowed caller-owned buffer (bytes + length).
 *   STRING  — borrowed caller-owned NUL-terminated C string.
 *   STREAM  — future pull-based provider (not implemented in 8.1).
 *   EMPTY   — explicit empty source (valid; yields an empty document).
 *
 * Lifetime contract:
 *   - The source object borrows bytes; RoboHTML never frees them.
 *   - For MEMORY/STRING, bytes must stay valid while the source object is
 *     attached to a document (until detach/destroy or next attach).
 *   - Applications providing data copy into their own buffer first, or use
 *     robo_html_source_take to transfer an owned buffer (8.1 keeps the
 *     transfer semantics explicit: the caller stays owner until take).
 *
 * Encoding metadata travels with the source: a declared label, when set,
 * is recorded verbatim and validated at the encoding boundary; the actual
 * decode is implemented in the encoding phase, not here.
 */

#ifndef ROBO_HTML_SOURCE_H
#define ROBO_HTML_SOURCE_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_html_source_kind {
    ROBO_HTML_SOURCE_KIND_NONE = 0,
    ROBO_HTML_SOURCE_KIND_EMPTY = 1,
    ROBO_HTML_SOURCE_KIND_MEMORY = 2,
    ROBO_HTML_SOURCE_KIND_STRING = 3,
    ROBO_HTML_SOURCE_KIND_STREAM = 4
} robo_html_source_kind_t;

/** Non-zero if @p kind is a defined source kind enumerator. */
int robo_html_source_kind_is_known(robo_html_source_kind_t kind);

/** Human-readable source kind name; never NULL. */
const char *robo_html_source_kind_name(robo_html_source_kind_t kind);

/**
 * Source descriptor (plain struct; no heap). Borrowing view over
 * application-owned bytes.
 */
typedef struct robo_html_source {
    robo_html_source_kind_t kind;
    /** Borrowed bytes for MEMORY; C string for STRING; else NULL. */
    const uint8_t *bytes;
    /** Byte length for MEMORY (STRING derives its length internally). */
    size_t length;
    /** Optional copied declared-encoding label ("" = unset). */
    char encoding[ROBO_HTML_ENCODING_LABEL_MAX];
    /** Optional copied source label for diagnostics (e.g. URL), may be "". */
    char name[ROBO_HTML_URL_MAX];
} robo_html_source_t;

/** Clear @p src to KIND_NONE / empty. Safe on NULL. */
void robo_html_source_init(robo_html_source_t *src);

/**
 * Configure @p src as an explicit empty source (kind EMPTY).
 * @p diag optional; ROBO_INVALID_ARGUMENT when @p src is NULL.
 */
robo_result_t robo_html_source_empty(robo_html_source_t *src,
                                     robo_html_diag_t *diag);

/**
 * Configure @p src as a borrowed memory buffer of @p length bytes.
 * @p bytes may be NULL only when @p length == 0 (then kind = MEMORY with
 * zero length is accepted as empty-equivalent).
 */
robo_result_t robo_html_source_memory(robo_html_source_t *src,
                                      const uint8_t *bytes,
                                      size_t length,
                                      robo_html_diag_t *diag);

/**
 * Configure @p src as a borrowed NUL-terminated UTF-8 C string.
 * @p utf8 NULL → empty source. Byte length derived via bounded scan.
 */
robo_result_t robo_html_source_string(robo_html_source_t *src,
                                      const char *utf8,
                                      robo_html_diag_t *diag);

/**
 * Record a declared encoding label (copied, ASCII, <=
 * ROBO_HTML_ENCODING_LABEL_MAX-1 bytes). Falls back to ROBO_PARSE_ERROR
 * with the ENCODING domain when the label is not representable.
 * The label is stored verbatim; decode/detection belongs to later phases.
 */
robo_result_t robo_html_source_set_declared_encoding(robo_html_source_t *src,
                                                     const char *label,
                                                     robo_html_diag_t *diag);

/** Borrowed declared-encoding label ("" when unset); NULL src → NULL. */
const char *robo_html_source_declared_encoding(const robo_html_source_t *src);

/**
 * Record a source name/URL label (copied, <= ROBO_HTML_URL_MAX-1 bytes).
 * Non-UTF-8 → ROBO_PARSE_ERROR. Diagnostics label only.
 */
robo_result_t robo_html_source_set_name(robo_html_source_t *src,
                                        const char *name,
                                        robo_html_diag_t *diag);

/** Borrowed source name ("" when unset); NULL src → NULL. */
const char *robo_html_source_name(const robo_html_source_t *src);

/** Byte length of the source content (STRING scans; 0 when none/empty). */
size_t robo_html_source_length(const robo_html_source_t *src);

/** Non-zero when the source carries no bytes (NONE or zero-length). */
int robo_html_source_is_empty(const robo_html_source_t *src);

/**
 * Snapshot (copy) the borrowed view into a caller-provided output buffer.
 * RoboHTML never keeps borrowed bytes; this helper documents the escape
 * hatch for callers that must extend lifetime.
 * @p out capacity must be >= robo_html_source_length(src).
 */
robo_result_t robo_html_source_copy_bytes(const robo_html_source_t *src,
                                          uint8_t *out,
                                          size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_SOURCE_H */
