/*
 * RoboText's text buffer (Phase 12.4) — the canonical text representation, and the conversions between
 * the two index spaces a caller will otherwise confuse.
 *
 * ## Owned text, and a lazily built scalar index
 *
 * A buffer **owns** its UTF-8. That is what makes `Append`, `Insert` and `Remove` expressible at all,
 * and it is the shape a document's text needs: an editor edits text, and a text layer that could only
 * borrow a `const char *` would force every editor to reimplement the buffer above it.
 *
 * Random access by scalar index is what the rest of the layer needs — a run is a scalar range, a glyph
 * carries a scalar range — and UTF-8 gives it only by walking. So the buffer keeps a **scalar index**:
 * one `RoboTextByteIndex` per scalar, holding the byte offset at which that scalar begins, plus a
 * final entry for the end of the text. The array is built on first use and invalidated by any
 * mutation, which is the one piece of state in this module that a reader has to know about:
 *
 *   - it is `n + 1` entries for `n` scalars, so `end` of the last scalar and `start` of "past the end"
 *     are both readable without a branch;
 *   - `ScalarAt(i)` is then `UnicodeUtf8Decode` at one known offset rather than a walk from zero, so a
 *     caller that walks a document scalar by scalar is O(n) and one that asks for one scalar is O(1);
 *   - it is **not** built by a constructor, so a buffer that is only ever converted between units once
 *     pays for it once.
 *
 * The index is a correctness device as much as a speed one: `RoboTextBufferScalarToByte` is total for
 * every in-range index and refuses the rest, and a caller can therefore never obtain a byte offset
 * that is not on a scalar boundary. That is the property a selection range needs and the property that
 * a hand-rolled `strlen`-based conversion loses.
 *
 * ## What is deliberately not here
 *
 * No grapheme clusters (a later goal), no normalization, no case mapping, no iteration protocol, no
 * rope. A buffer is bytes, an index and the conversions between two units; everything above it is
 * `RoboTextRuns.h`'s and `RoboTextGlyphs.h`'s.
 *
 * The decoder mode is `RoboUnicodeReplace` throughout, and that is a decision rather than a default:
 * a document that reached the DOM has already been through the HTML tokenizer's own decoding, so a
 * lone surrogate or a truncated sequence is *already* a fact of the text and a buffer that refused it
 * would be refusing a document the DOM accepted. The replacement character is counted once, in the
 * scalar index, exactly as the decoder counts it.
 */
#ifndef ROBO_TEXT_BUFFER_H
#define ROBO_TEXT_BUFFER_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RoboTextBuffer RoboTextBuffer;

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

/** An empty owned buffer. RoboInvalidArgument for a NULL @p out; RoboOutOfMemory on failure. */
Result RoboTextBufferCreate(RoboTextBuffer **out, RoboTextDiag *diag);

/**
 * An owned buffer holding a copy of @p utf8.
 *
 * @p length is in bytes and @p utf8 may be NULL only with @p length 0. The bytes are **copied** —
 * a buffer that borrowed would be a buffer whose `Append` moved somebody else's string — and they are
 * validated as UTF-8 by decoding them once, so a caller learns about bad bytes at the door rather than
 * in the middle of a run.
 *
 * Decoding uses REPLACE mode, so this is a *check* rather than a refusal: bytes that are not valid
 * UTF-8 become U+FFFD, which is what the DOM above already did with them.
 */
Result RoboTextBufferCreateFrom(RoboTextBuffer **out,
                                const char *utf8,
                                size_t length,
                                RoboTextDiag *diag);

/**
 * An owned buffer holding @p scalars UTF-32, encoded to UTF-8.
 *
 * The door a test uses and a caller that built its text as code points uses. Every value must be a
 * Unicode scalar: a lone surrogate is refused (`RoboInvalidArgument`) rather than replaced, because a
 * caller holding `Uchar32` values has already made a decision about them and a silent U+FFFD would
 * lose it.
 */
Result RoboTextBufferCreateFromScalars(RoboTextBuffer **out,
                                       const Uchar32 *scalars,
                                       size_t count,
                                       RoboTextDiag *diag);

/** Release @p buffer. Safe on NULL. */
void RoboTextBufferDestroy(RoboTextBuffer *buffer);

/**
 * A copy of @p buffer, byte-identical.
 *
 * A copy and not a view, because the point of it is to hand a caller something it may mutate; a view
 * would be `RoboTextBufferBytes` and its length, which is exactly what a caller that wants to *read*
 * should use.
 */
Result RoboTextBufferClone(const RoboTextBuffer *buffer, RoboTextBuffer **out, RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

/** Bytes held, excluding a terminator. Always 0 for NULL. */
RoboTextByteIndex RoboTextBufferByteLength(const RoboTextBuffer *buffer);

/** Scalars held. Always 0 for NULL. Walks or builds the index once per mutation. */
RoboTextScalarIndex RoboTextBufferScalarCount(const RoboTextBuffer *buffer);

/** Non-zero when the buffer holds no scalars. */
int RoboTextBufferIsEmpty(const RoboTextBuffer *buffer);

/** The UTF-8 bytes, **not** NUL-terminated. NULL when empty or NULL. */
const char *RoboTextBufferBytes(const RoboTextBuffer *buffer);

/**
 * The buffer's bytes as a NUL-terminated string, allocated from RoboMem.
 *
 * A copy, so the caller frees it with `RoboFree`. This exists because two layers above want a C string
 * (`string.h`, the PNG/dump paths) and because a caller that reached for `RoboTextBufferBytes` and
 * treated it as one would read past the end.
 */
Result RoboTextBufferToCString(const RoboTextBuffer *buffer, char **out, RoboTextDiag *diag);

/** The whole of the buffer as a scalar range: `[0, scalarCount)`. */
RoboTextRange RoboTextBufferRange(const RoboTextBuffer *buffer);

/** Non-zero when @p range lies inside the buffer. */
int RoboTextBufferContainsRange(const RoboTextBuffer *buffer, RoboTextRange range);

/* ------------------------------------------------------------------ */
/* Units                                                               */
/* ------------------------------------------------------------------ */

/**
 * The scalar at @p index.
 *
 * @return RoboOk and @p outScalar; RoboInvalidArgument for a NULL argument or an index past the last
 *         scalar; RoboOutOfMemory when the scalar index could not be built.
 */
Result RoboTextBufferScalarAt(const RoboTextBuffer *buffer,
                              RoboTextScalarIndex index,
                              Uchar32 *outScalar,
                              RoboTextDiag *diag);

/**
 * The byte offset at which scalar @p index begins.
 *
 * **Total for every in-range index**, including the index one past the last scalar — which is the end
 * of the text and therefore a legitimate caret position. That totality is the header's argument for
 * having a scalar index at all: the answer is always on a scalar boundary, so a caller can never
 * fabricate a byte offset that is not.
 */
Result RoboTextBufferScalarToByte(const RoboTextBuffer *buffer,
                                  RoboTextScalarIndex index,
                                  RoboTextByteIndex *outByte,
                                  RoboTextDiag *diag);

/**
 * The scalar index that begins at byte @p byte.
 *
 * @return RoboOk and @p outScalar; RoboInvalidArgument for a byte offset that is past the end or **not
 *         on a scalar boundary**, which is the refusal that keeps the two spaces from being mixed.
 */
Result RoboTextBufferByteToScalar(const RoboTextBuffer *buffer,
                                  RoboTextByteIndex byte,
                                  RoboTextScalarIndex *outScalar,
                                  RoboTextDiag *diag);

/**
 * The byte span @p range denotes.
 *
 * The endpoint inclusive and the other exclusive, so an empty range yields a zero-length span at the
 * right offset — a caret. The two endpoints are converted through the same door, so a range that
 * *ends* mid-sequence is refused by the second conversion rather than rounded.
 */
Result RoboTextBufferRangeToBytes(const RoboTextBuffer *buffer,
                                  RoboTextRange range,
                                  RoboTextByteRange *outBytes,
                                  RoboTextDiag *diag);

/**
 * The scalars of @p range, decoded into @p out.
 *
 * @p outCapacity is in scalars and @p outCount receives the count the range *needs*, whether or not it
 * fitted — the `_write` convention this tree uses everywhere, and the reason a caller can size a
 * buffer with one call and fill it with the next. @p out may be NULL to ask only for the count.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL buffer or a range not inside it; RoboNotSupported with
 *         @p outCount set to the required count when @p outCapacity is too small.
 */
Result RoboTextBufferDecodeRange(const RoboTextBuffer *buffer,
                                 RoboTextRange range,
                                 Uchar32 *out,
                                 size_t outCapacity,
                                 size_t *outCount,
                                 RoboTextDiag *diag);

/**
 * Copy @p range into @p out as a new owned buffer.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a range not inside the buffer;
 *         RoboOutOfMemory. The result is always an owned buffer the caller destroys, and an empty
 *         range yields an empty buffer rather than a NULL one.
 */
Result RoboTextBufferSlice(const RoboTextBuffer *buffer,
                           RoboTextRange range,
                           RoboTextBuffer **out,
                           RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

/** Append @p length bytes of UTF-8 to the end. RoboInvalidArgument for a NULL buffer. */
Result RoboTextBufferAppend(RoboTextBuffer *buffer,
                            const char *utf8,
                            size_t length,
                            RoboTextDiag *diag);

/** Append one scalar. RoboInvalidArgument for a non-scalar (a lone surrogate, or out of range). */
Result RoboTextBufferAppendScalar(RoboTextBuffer *buffer,
                                  Uchar32 scalar,
                                  RoboTextDiag *diag);

/**
 * Insert @p length bytes of UTF-8 at scalar @p at.
 *
 * @p at may be the scalar count, which is an append. A byte offset is not accepted: inserting at a byte
 * that is not a scalar boundary is the one thing a caller can do that would put the buffer in a state
 * its own index could not describe, so the unit is the scalar and the conversion happens here.
 */
Result RoboTextBufferInsert(RoboTextBuffer *buffer,
                            RoboTextScalarIndex at,
                            const char *utf8,
                            size_t length,
                            RoboTextDiag *diag);

/** Remove scalars `[range.start, range.end)`. An empty range removes nothing and succeeds. */
Result RoboTextBufferRemove(RoboTextBuffer *buffer, RoboTextRange range, RoboTextDiag *diag);

/** Remove every scalar; the storage is kept, exactly as the paint list's `clear` keeps its own. */
void RoboTextBufferClear(RoboTextBuffer *buffer);

/* ------------------------------------------------------------------ */
/* A borrowed view                                                     */
/* ------------------------------------------------------------------ */

/**
 * A read-only view over bytes the caller already owns.
 *
 * The reason this exists beside the owned buffer is the same reason `RoboFont` has `FontSpan`: a
 * layout pass measures a **DOM node's** bytes, and copying a document's text into an owned buffer in
 * order to measure it would be one allocation per text node per pass for no gain. A view converts units
 * with the same two doors (they decode; they do not cache), and it is what `RoboTextShape`'s
 * caller-facing door accepts.
 *
 * A view is a **value**, so there is nothing to free — which is why it is a struct here rather than an
 * opaque handle.
 */
typedef struct RoboTextView {
    const char *bytes;
    size_t length;
} RoboTextView;

/** A view over @p bytes. @p bytes may be NULL only with @p length 0. */
RoboTextView RoboTextViewMake(const char *bytes, size_t length);

/** Scalars in @p view. RoboInvalidArgument for NULL arguments. */
Result RoboTextViewScalarCount(RoboTextView view, RoboTextScalarIndex *out, RoboTextDiag *diag);

/** The scalar at @p index of @p view. RoboInvalidArgument past the last scalar. */
Result RoboTextViewScalarAt(RoboTextView view,
                            RoboTextScalarIndex index,
                            Uchar32 *out,
                            RoboTextDiag *diag);

/** The byte offset at which @p index begins; total for every in-range index, as the buffer's is. */
Result RoboTextViewScalarToByte(RoboTextView view,
                                RoboTextScalarIndex index,
                                RoboTextByteIndex *out,
                                RoboTextDiag *diag);

/** Decode @p range of @p view into @p out; same capacity convention as the buffer's door. */
Result RoboTextViewDecodeRange(RoboTextView view,
                               RoboTextRange range,
                               Uchar32 *out,
                               size_t outCapacity,
                               size_t *outCount,
                               RoboTextDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_BUFFER_H */
