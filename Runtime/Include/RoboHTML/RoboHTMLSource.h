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

typedef enum HtmlSourceKind {
    RoboHtmlSourceKindNone = 0,
    RoboHtmlSourceKindEmpty = 1,
    RoboHtmlSourceKindMemory = 2,
    RoboHtmlSourceKindString = 3,
    RoboHtmlSourceKindStream = 4
} HtmlSourceKind;

/** Non-zero if @p kind is a defined source kind enumerator. */
int HtmlSourceKindIsKnown(HtmlSourceKind kind);

/** Human-readable source kind name; never NULL. */
const char *HtmlSourceKindName(HtmlSourceKind kind);

/**
 * Source descriptor (plain struct; no heap). Borrowing view over
 * application-owned bytes.
 */
typedef struct HtmlSource {
    HtmlSourceKind kind;
    /** Borrowed bytes for MEMORY; C string for STRING; else NULL. */
    const uint8_t *bytes;
    /** Byte length for MEMORY (STRING derives its length internally). */
    size_t length;
    /** Optional copied declared-encoding label ("" = unset). */
    char encoding[RoboHtmlEncodingLabelMax];
    /** Optional copied source label for diagnostics (e.g. URL), may be "". */
    char name[RoboHtmlUrlMax];
} HtmlSource;

/** Clear @p src to KIND_NONE / empty. Safe on NULL. */
void HtmlSourceInit(HtmlSource *src);

/**
 * Configure @p src as an explicit empty source (kind EMPTY).
 * @p diag optional; RoboInvalidArgument when @p src is NULL.
 */
Result HtmlSourceEmpty(HtmlSource *src,
                                     HtmlDiag *diag);

/**
 * Configure @p src as a borrowed memory buffer of @p length bytes.
 * @p bytes may be NULL only when @p length == 0 (then kind = MEMORY with
 * zero length is accepted as empty-equivalent).
 */
Result HtmlSourceMemory(HtmlSource *src,
                                      const uint8_t *bytes,
                                      size_t length,
                                      HtmlDiag *diag);

/**
 * Configure @p src as a borrowed NUL-terminated UTF-8 C string.
 * @p utf8 NULL → empty source. Byte length derived via bounded scan.
 */
Result HtmlSourceString(HtmlSource *src,
                                      const char *utf8,
                                      HtmlDiag *diag);

/**
 * Record a declared encoding label (copied, ASCII, <=
 * RoboHtmlEncodingLabelMax-1 bytes). Falls back to RoboParseError
 * with the ENCODING domain when the label is not representable.
 * The label is stored verbatim; decode/detection belongs to later phases.
 */
Result HtmlSourceSetDeclaredEncoding(HtmlSource *src,
                                                     const char *label,
                                                     HtmlDiag *diag);

/** Borrowed declared-encoding label ("" when unset); NULL src → NULL. */
const char *HtmlSourceDeclaredEncoding(const HtmlSource *src);

/**
 * Record a source name/URL label (copied, <= RoboHtmlUrlMax-1 bytes).
 * Non-UTF-8 → RoboParseError. Diagnostics label only.
 */
Result HtmlSourceSetName(HtmlSource *src,
                                        const char *name,
                                        HtmlDiag *diag);

/** Borrowed source name ("" when unset); NULL src → NULL. */
const char *HtmlSourceName(const HtmlSource *src);

/** Byte length of the source content (STRING scans; 0 when none/empty). */
size_t HtmlSourceLength(const HtmlSource *src);

/** Non-zero when the source carries no bytes (NONE or zero-length). */
int HtmlSourceIsEmpty(const HtmlSource *src);

/**
 * Snapshot (copy) the borrowed view into a caller-provided output buffer.
 * RoboHTML never keeps borrowed bytes; this helper documents the escape
 * hatch for callers that must extend lifetime.
 * @p out capacity must be >= HtmlSourceLength(src).
 */
Result HtmlSourceCopyBytes(const HtmlSource *src,
                                          uint8_t *out,
                                          size_t outCapacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_SOURCE_H */
