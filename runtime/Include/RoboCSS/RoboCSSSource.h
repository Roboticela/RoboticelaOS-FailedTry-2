/*
 * RoboCSS source model (Phase 9.1).
 *
 * CSS arrives as bytes and nothing else: this module never opens a file,
 * resolves a URL or speaks a protocol, exactly as RoboHTML's source model does
 * not. Where the bytes come from is the caller's business; what RoboCSS needs
 * is a byte range, a way to name it in a diagnostic, and a position inside it.
 *
 * Four source kinds:
 *
 *   EMPTY    an explicit empty source — valid, and stylesheets parse from it
 *   MEMORY   borrowed caller bytes plus a length
 *   STRING   a borrowed NUL-terminated UTF-8 string
 *   COPY     bytes RoboCSS copied and therefore owns
 *
 * The COPY kind exists because a stylesheet outlives the buffer it was built
 * from in the common case (a `<style>` element's text is not permanent), while
 * a caller that already owns the bytes for long enough should not pay for a
 * copy. Both are explicit at the call site — robo_css_source_set_memory versus
 * robo_css_source_copy — so no API has to guess which one it got, and
 * robo_css_source_is_owned answers the question afterwards.
 *
 * Encoding: bytes are kept exactly as given, always. `@charset` handling,
 * decode-with-replacement and encoding detection belong to the tokenizer
 * boundary (9.2) and are not done here — this phase never rewrites a byte. The
 * helpers below only *describe* what is there.
 *
 * Positions: byte offsets are authoritative. Line and column are derived on
 * demand from the bytes (no index is built and nothing is cached, so no cache
 * can go stale), with a column counted in Unicode scalar values.
 */

#ifndef ROBO_CSS_SOURCE_H
#define ROBO_CSS_SOURCE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How a source holds its bytes. */
typedef enum robo_css_source_kind {
    /** Nothing configured; there is no source at all. */
    ROBO_CSS_SOURCE_KIND_NONE = 0,
    /** An explicit empty source. */
    ROBO_CSS_SOURCE_KIND_EMPTY = 1,
    /** Borrowed bytes plus a length. */
    ROBO_CSS_SOURCE_KIND_MEMORY = 2,
    /** A borrowed NUL-terminated string. */
    ROBO_CSS_SOURCE_KIND_STRING = 3,
    /** A copy RoboCSS allocated and owns. */
    ROBO_CSS_SOURCE_KIND_COPY = 4
} robo_css_source_kind_t;

/** Number of defined source kinds. */
#define ROBO_CSS_SOURCE_KIND_COUNT 5u

/** Non-zero if @p kind is a defined source kind enumerator. */
int robo_css_source_kind_is_known(robo_css_source_kind_t kind);

/** Human-readable source kind name; never NULL. */
const char *robo_css_source_kind_name(robo_css_source_kind_t kind);

/** Non-zero when a source of @p kind would keep an owned copy of its bytes. */
int robo_css_source_kind_is_owning(robo_css_source_kind_t kind);

/**
 * A CSS source.
 *
 * `bytes`/`length` are the content; `name` is a copied label used in
 * diagnostics only ("Sheet 1", "<style>", a path — RoboCSS does not interpret
 * it). The struct is a plain value with no hidden pointer to a heap header,
 * but a COPY source *does* own `bytes`: it must be disposed exactly once, and
 * copying the struct by value would duplicate that ownership. Treat a COPY
 * source as move-only, or share it by pointer.
 */
typedef struct robo_css_source {
    robo_css_source_kind_t kind;
    /** Content bytes: borrowed for MEMORY/STRING, owned for COPY, NULL for NONE/EMPTY. */
    const uint8_t *bytes;
    /** Content length in bytes (STRING derives it; EMPTY/NONE are 0). */
    size_t length;
    /** Diagnostic label, ASCII-safe, always NUL-terminated. */
    char name[ROBO_CSS_NAME_MAX];
} robo_css_source_t;

/** Clear @p source to KIND_NONE. Safe on NULL. */
void robo_css_source_init(robo_css_source_t *source);

/**
 * Dispose @p source: free owned bytes when the kind is COPY, then reset to
 * KIND_NONE. Safe on NULL and safe to call twice.
 */
void robo_css_source_dispose(robo_css_source_t *source);

/**
 * Configure @p source as an explicit empty source (KIND_EMPTY). Disposes any
 * bytes it already owned. NULL @p source → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_css_source_set_empty(robo_css_source_t *source,
                                       robo_css_diag_t *diag);

/**
 * Configure @p source as borrowed memory. @p bytes may be NULL only when
 * @p length is 0. The bytes must stay valid for as long as the source is used;
 * RoboCSS never frees them and never copies them.
 */
robo_result_t robo_css_source_set_memory(robo_css_source_t *source,
                                        const uint8_t *bytes,
                                        size_t length,
                                        robo_css_diag_t *diag);

/**
 * Configure @p source as a borrowed NUL-terminated string. NULL @p utf8, and
 * an empty string, both configure an empty source. The recorded kind is
 * KIND_STRING, and the byte length is the distance to the NUL terminator.
 */
robo_result_t robo_css_source_set_string(robo_css_source_t *source,
                                        const char *utf8,
                                        robo_css_diag_t *diag);

/**
 * Configure @p source as an owned copy of @p bytes/@p length (KIND_COPY).
 * RoboCSS allocates the copy with RoboMem and frees it in
 * robo_css_source_dispose.
 * @return ROBO_OK; ROBO_OUT_OF_MEMORY when the copy cannot be allocated (the
 *         source is then left KIND_NONE, never half-configured).
 */
robo_result_t robo_css_source_copy(robo_css_source_t *source,
                                  const uint8_t *bytes,
                                  size_t length,
                                  robo_css_diag_t *diag);

/**
 * Record a diagnostic label (copied, at most ROBO_CSS_NAME_MAX-1 bytes).
 * An over-long or NULL label is truncated rather than rejected: a label is
 * diagnostics, and diagnostics must never fail a call that would otherwise
 * succeed.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT when @p source is NULL.
 */
robo_result_t robo_css_source_set_name(robo_css_source_t *source,
                                      const char *name,
                                      robo_css_diag_t *diag);

/** Borrowed label, always NUL-terminated ("" when unset); NULL source → NULL. */
const char *robo_css_source_name(const robo_css_source_t *source);

/** Borrowed content bytes (NULL when none); NULL source → NULL. */
const uint8_t *robo_css_source_bytes(const robo_css_source_t *source);

/** Content length in bytes (0 when none); NULL source → 0. */
size_t robo_css_source_length(const robo_css_source_t *source);

/** Non-zero when the source carries no bytes at all (NONE, EMPTY or length 0). */
int robo_css_source_is_empty(const robo_css_source_t *source);

/** Non-zero when the source owns its bytes (KIND_COPY). */
int robo_css_source_is_owned(const robo_css_source_t *source);

/** Non-zero when @p source was configured at all (kind != NONE). */
int robo_css_source_is_configured(const robo_css_source_t *source);

/** Borrowed view of the whole source (absent slice when it has no bytes). */
robo_css_slice_t robo_css_source_slice(const robo_css_source_t *source);

/* ------------------------------------------------------------------ */
/* Bounds, positions and ranges                                        */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p offset is a usable position in @p source: `0 <= offset <=
 * length`. The end offset is a valid position (one past the last byte) because
 * a zero-width range there is how "the cursor reached the end" is expressed.
 * NULL source → 0.
 */
int robo_css_source_offset_is_valid(const robo_css_source_t *source,
                                    size_t offset);

/**
 * Resolve @p offset in @p source to a position.
 *
 * Line counting treats CR, LF and CRLF each as one break; the offset must be
 * inside the source (see above). An offset landing on the LF of a CRLF pair
 * resolves to the break's own line, with the column it has in that line.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL source/output or an offset
 *         past the end.
 */
robo_result_t robo_css_source_position(const robo_css_source_t *source,
                                      size_t offset,
                                      robo_css_position_t *out_position);

/** Number of lines in @p source (0 when empty; a source without a trailing
 *  break still counts its last line). NULL source → 0. */
size_t robo_css_source_line_count(const robo_css_source_t *source);

/**
 * Non-zero when @p range is usable in @p source: `begin <= end` and
 * `end <= length`. NULL source → 0.
 */
int robo_css_source_range_is_valid(const robo_css_source_t *source,
                                   robo_css_range_t range);

/**
 * Borrowed view of the bytes @p range covers.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output or a range that is
 *         not valid in @p source. A valid empty range yields an empty slice.
 */
robo_result_t robo_css_source_range_slice(const robo_css_source_t *source,
                                         robo_css_range_t range,
                                         robo_css_slice_t *out_slice);

/* ------------------------------------------------------------------ */
/* Describing the bytes (no rewriting)                                 */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when every byte is a well-formed UTF-8 sequence.
 *
 * This only *reports*; it never repairs. Ill-formed input is preserved
 * byte-for-byte, and deciding whether to decode with replacement is the
 * tokenizer's business (9.2). A NONE/EMPTY source is trivially valid.
 */
int robo_css_source_is_valid_utf8(const robo_css_source_t *source);

/**
 * Number of Unicode scalar values the source decodes to, counting each
 * ill-formed byte as one scalar (the same "one position per maximal subpart"
 * rule a decoder would use when it cannot form a sequence). It never
 * substitutes U+FFFD and never changes the bytes.
 * NULL source → 0.
 */
size_t robo_css_source_scalar_count(const robo_css_source_t *source);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_SOURCE_H */
