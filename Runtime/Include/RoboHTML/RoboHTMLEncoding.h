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
typedef enum HtmlEncoding {
    RoboHtmlEncodingUnknown = 0,
    RoboHtmlEncodingUtf8 = 1,
    RoboHtmlEncodingUtf16LE = 2,
    RoboHtmlEncodingUtf16BE = 3,
    RoboHtmlEncodingWindows1252 = 4,
    RoboHtmlEncodingShiftJis = 5,
    RoboHtmlEncodingUtf16 = 6 /* alias handled at the boundary */
} HtmlEncoding;

/** Non-zero if @p enc is a defined encoding enumerator. */
int HtmlEncodingIsKnown(HtmlEncoding enc);

/** Canonical label for @p enc; "unknown" when undefined. Never NULL. */
const char *HtmlEncodingLabel(HtmlEncoding enc);

/**
 * Resolve an ASCII encoding label (case-insensitive) to its token.
 * Unknown label → RoboHtmlEncodingUnknown (not an error: callers may
 * choose to record it as unsupported via the diagnostics boundary).
 */
HtmlEncoding HtmlEncodingFromLabel(const char *label);

/**
 * Boundary status for encoding handling (future decoder reports through
 * this; 8.1 uses it to record declared/actual state transitions).
 */
typedef enum HtmlEncodingStatus {
    RoboHtmlEncodingStatusOk = 0,
    /** Declared label unknown to the runtime (documented, not fatal). */
    RoboHtmlEncodingStatusUnsupportedLabel = 1,
    /** Declared label is not valid ASCII / too long. */
    RoboHtmlEncodingStatusInvalidLabel = 2,
    /** Byte content does not match the declared/actual encoding. */
    RoboHtmlEncodingStatusDecodeMismatch = 3,
    /** Malformed byte sequence (future decoder fills this). */
    RoboHtmlEncodingStatusMalformed = 4
} HtmlEncodingStatus;

/** Non-zero if @p status is a defined encoding status enumerator. */
int HtmlEncodingStatusIsKnown(HtmlEncodingStatus status);

/** Human-readable encoding status name; never NULL. */
const char *HtmlEncodingStatusName(HtmlEncodingStatus status);

/**
 * Boundary record: what the source declared vs what the boundary decided.
 * Owned by the document; copies only — never borrows source memory.
 */
typedef struct HtmlEncodingState {
    HtmlEncoding declared;   /* from source metadata / future <meta> */
    HtmlEncoding actual;     /* boundary decision (8.1: declared or UTF-8) */
    HtmlEncodingStatus status;
} HtmlEncodingState;

/** Reset @p st to UNKNOWN/UNKNOWN/OK. Safe on NULL. */
void HtmlEncodingStateInit(HtmlEncodingState *st);

/**
 * Validate and normalize a declared label into @p out state fields.
 * Pure boundary helper — no I/O, no decoding.
 * @return RoboOk (label known), RoboNotSupported (label valid ASCII but
 *         not admitted), RoboParseError (label not ASCII / too long /
 *         NULL label with non-NULL out is still RoboInvalidArgument).
 */
Result HtmlEncodingDeclare(HtmlEncodingState *st,
                                         const char *label,
                                         HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ENCODING_H */
