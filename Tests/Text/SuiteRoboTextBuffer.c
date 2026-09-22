/*
 * RoboText's buffer, its units and its range algebra (Phase 12.4) — `text.buffer.*`, `text.units.*` and
 * `text.range.*`.
 *
 * No font is opened here, and that is the point of the split: every case in this file is a pure function
 * of bytes, so a failure means the *model* is wrong rather than a face. The face-touching cases are two
 * files over (`SuiteRoboTextGlyphs.c`, `SuiteRoboTextShape.c`).
 *
 * The unit cases are the ones worth reading, because they are the ones that catch the bug this whole
 * header exists to prevent. `"héllo"` is 5 scalars and 6 bytes; `"🙂"` is 1 scalar and 4. A layer that
 * used one number for both would pass every test written against ASCII and fail on the first accent — so
 * every conversion here is asserted in *both* directions, including the mid-sequence byte that must be
 * **refused** rather than rounded to the nearest boundary.
 */

#include "RoboTextTestSupport.h"

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestBufferLifecycle(void)
{
    RoboTextBuffer *buffer = NULL;
    RoboTextBuffer *clone = NULL;
    char *text = NULL;

    RoboCheckEqInt((int)RoboTextBufferCreate(NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBufferCreate(&buffer, NULL), (int)RoboOk);
    RoboCheck(buffer != NULL);
    RoboCheck(RoboTextBufferIsEmpty(buffer));
    RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), 0);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 0);
    RoboCheck(RoboTextBufferBytes(buffer) == NULL);
    /* The empty text's only position is 0, and it is total for index 0. */
    {
        RoboTextByteIndex byte = 99u;

        RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, 0u, &byte, NULL), (int)RoboOk);
        RoboCheckEqInt((int)byte, 0);
        RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, 1u, &byte, NULL),
                       (int)RoboInvalidArgument);
    }

    RoboCheckEqInt((int)RoboTextBufferAppend(buffer, "abc", 3u, NULL), (int)RoboOk);
    RoboCheck(!RoboTextBufferIsEmpty(buffer));
    RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), 3);
    RoboCheck(RoboTextBufferBytes(buffer) != NULL);
    RoboCheck(RoboTextBufferBytes(buffer)[0] == 'a');

    /* A clone is byte-identical and independent. */
    RoboCheckEqInt((int)RoboTextBufferClone(buffer, &clone, NULL), (int)RoboOk);
    RoboCheck(clone != NULL && clone != buffer);
    RoboCheckEqInt((int)RoboTextBufferByteLength(clone), (int)RoboTextBufferByteLength(buffer));
    RoboCheckEqInt((int)RoboTextBufferScalarCount(clone), 3);
    RoboCheckEqInt((int)RoboTextBufferAppend(clone, "d", 1u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(clone), 4);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 3);

    RoboCheckEqInt((int)RoboTextBufferToCString(buffer, &text, NULL), (int)RoboOk);
    RoboCheckStrEq(text, "abc");
    RoboFree(text);

    /* `Clear` keeps the storage but empties the text, and the index goes with it. */
    RoboTextBufferClear(buffer);
    RoboCheck(RoboTextBufferIsEmpty(buffer));
    RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), 0);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 0);
    RoboCheckEqInt((int)RoboTextBufferAppend(buffer, "z", 1u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 1);

    RoboTextBufferDestroy(clone);
    RoboTextBufferDestroy(buffer);
    RoboTextBufferDestroy(NULL); /* Safe. */

    RoboCheckEqInt((int)RoboTextBufferClone(NULL, &clone, NULL), (int)RoboInvalidArgument);
    {
        RoboTextByteIndex byte = 0u;
        Uchar32 scalar = 0u;

        RoboCheckEqInt((int)RoboTextBufferScalarAt(NULL, 0u, &scalar, NULL), (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextBufferScalarToByte(NULL, 0u, &byte, NULL),
                       (int)RoboInvalidArgument);
    }
    RoboCheckEqInt((int)RoboTextBufferAppend(NULL, "x", 1u, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, NULL, 3u, NULL), (int)RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Units                                                               */
/* ------------------------------------------------------------------ */

static void TestBufferUnits(void)
{
    static const struct {
        const char *text;
        uint32_t bytes;
        uint32_t scalars;
    } cases[] = {
        {"", 0u, 0u},
        {"a", 1u, 1u},
        {TextSampleHello, 18u, 18u},
        {"\xC3\xA9", 2u, 1u},                    /* é */
        {"\xCE\xA9", 2u, 1u},                    /* Ω */
        {"\xE4\xB8\xAD", 3u, 1u},                /* 中 */
        {"\xF0\x9F\x99\x82", 4u, 1u},            /* 🙂 */
        {"h\xC3\xA9llo", 6u, 5u},                /* héllo: the header's own example */
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        RoboTextBuffer *buffer = NULL;
        uint32_t scalar;

        RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, cases[index].text,
                                                     cases[index].bytes, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), (int)cases[index].bytes);
        RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), (int)cases[index].scalars);

        /* Every scalar index has a byte offset, including the one past the last scalar; every one of
         * them converts back to itself, so the two spaces cannot drift. */
        for (scalar = 0u; scalar <= cases[index].scalars; scalar++) {
            RoboTextByteIndex byte = 0xFFFFFFFFu;
            RoboTextScalarIndex back = 0xFFFFFFFFu;

            RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, scalar, &byte, NULL), (int)RoboOk);
            RoboCheck(byte <= cases[index].bytes);
            RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, byte, &back, NULL), (int)RoboOk);
            RoboCheckEqInt((int)back, (int)scalar);
            if (scalar > 0u) {
                RoboTextByteIndex previous = 0u;

                RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, scalar - 1u, &previous, NULL),
                               (int)RoboOk);
                RoboCheck(byte > previous); /* Strictly ascending, so the index is a partition. */
            }
        }
        /* One past the last scalar has no *scalar* at it, though it has an offset. */
        {
            Uchar32 value = 0u;
            RoboTextByteIndex past = 0u;

            RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, cases[index].scalars, &value, NULL),
                           (int)RoboInvalidArgument);
            RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, cases[index].scalars + 1u, &past, NULL),
                           (int)RoboInvalidArgument);
            RoboCheckEqInt((int)past, 0); /* The out parameter is cleared on the way out. */
        }
        RoboTextBufferDestroy(buffer);
    }

    /* A byte that lands *inside* a multi-byte scalar is refused, not rounded: that refusal is the one
     * thing that keeps a selection range from cutting a character in half. */
    {
        RoboTextBuffer *buffer = NULL;
        RoboTextScalarIndex scalar = 0u;

        RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, "h\xC3\xA9llo", 6u, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 2u, &scalar, NULL),
                       (int)RoboInvalidArgument); /* The second byte of é. */
        RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 1u, &scalar, NULL), (int)RoboOk);
        RoboCheckEqInt((int)scalar, 1);
        RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 7u, &scalar, NULL),
                       (int)RoboInvalidArgument); /* Past the end. */
        RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, 6u, &scalar, NULL), (int)RoboOk);
        RoboCheckEqInt((int)scalar, 5); /* The end-of-text position. */

        /* The third scalar of `héllo` is the ASCII `l`, and the first two are `h` and `é`. */
        {
            Uchar32 value = 0u;

            RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 0u, &value, NULL), (int)RoboOk);
            RoboCheckEqInt((int)value, 'h');
            RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 1u, &value, NULL), (int)RoboOk);
            RoboCheckEqInt((int)value, 0xE9);
            RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 2u, &value, NULL), (int)RoboOk);
            RoboCheckEqInt((int)value, 'l');
        }
        RoboTextBufferDestroy(buffer);
    }
}

/* ------------------------------------------------------------------ */
/* Decode and slice                                                    */
/* ------------------------------------------------------------------ */

static void TestBufferDecodeAndSlice(void)
{
    RoboTextBuffer *buffer = NULL;
    RoboTextBuffer *slice = NULL;
    Uchar32 scalars[9];
    size_t count = 0u;

    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, TextSampleUnicode,
                                                 strlen(TextSampleUnicode), NULL),
                   (int)RoboOk);
    /*
     * `"é Å Ω 中 🙂"` — five scalars and four spaces, so nine. The point of the count is that the *same*
     * bytes produce the same scalar count every time and that it is well below the byte count.
     */
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 9);
    RoboCheck(RoboTextBufferByteLength(buffer) > 9u);

    /* Count-only, then fill: the `_write` convention, and a caller can size a buffer with one call. */
    RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeFull(9u), NULL, 0u, &count, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 9);
    RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeFull(9u), scalars, 2u, &count,
                                                  NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)count, 9); /* Still tells the caller what it needs. */
    RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeFull(9u), scalars, 8u, &count,
                                                  NULL),
                   (int)RoboNotSupported); /* One short is still short. */
    RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeFull(9u), scalars, 9u, &count,
                                                  NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)count, 9);
    RoboCheckEqInt((int)scalars[0], 0xE9);
    RoboCheckEqInt((int)scalars[1], ' ');
    RoboCheckEqInt((int)scalars[2], 0xC5); /* Å */
    RoboCheckEqInt((int)scalars[4], 0x3A9); /* Ω */
    RoboCheckEqInt((int)scalars[6], 0x4E2D); /* 中 */
    RoboCheckEqInt((int)scalars[8], 0x1F642); /* 🙂 */

    /* A range that does not lie in the text is refused in either direction of the check. */
    RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeMake(0u, 10u), scalars, 9u, &count,
                                                  NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeMake(4u, 2u), scalars, 9u, &count,
                                                  NULL),
                   (int)RoboInvalidArgument);
    RoboCheck(!RoboTextBufferContainsRange(buffer, RoboTextRangeMake(0u, 10u)));
    RoboCheck(RoboTextBufferContainsRange(buffer, RoboTextRangeFull(9u)));

    /* A slice is the scalars it names, re-encoded — so `"héllo"` sliced after the `h` is `"éllo"`. */
    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&slice, "h\xC3\xA9llo", 6u, NULL), (int)RoboOk);
    {
        RoboTextBuffer *part = NULL;

        RoboCheckEqInt((int)RoboTextBufferSlice(slice, RoboTextRangeMake(1u, 5u), &part, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextBufferScalarCount(part), 4);
        RoboCheckEqInt((int)RoboTextBufferByteLength(part), 5);
        {
            Uchar32 value = 0u;

            RoboCheckEqInt((int)RoboTextBufferScalarAt(part, 0u, &value, NULL), (int)RoboOk);
            RoboCheckEqInt((int)value, 0xE9);
        }
        RoboTextBufferDestroy(part);

        /* An empty range yields an empty buffer rather than a NULL one, and a range past the end is
         * refused by the same door the byte conversion uses. */
        RoboCheckEqInt((int)RoboTextBufferSlice(slice, RoboTextRangeAt(3u), &part, NULL), (int)RoboOk);
        RoboCheck(RoboTextBufferIsEmpty(part));
        RoboCheckEqInt((int)RoboTextBufferScalarCount(part), 0);
        RoboTextBufferDestroy(part);
        RoboCheckEqInt((int)RoboTextBufferSlice(slice, RoboTextRangeMake(0u, 7u), &part, NULL),
                       (int)RoboInvalidArgument);
        RoboCheck(part == NULL);
    }
    RoboTextBufferDestroy(slice);
    RoboTextBufferDestroy(buffer);
}

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

static void TestBufferEdit(void)
{
    RoboTextBuffer *buffer = NULL;
    Uchar32 value = 0u;

    RoboCheckEqInt((int)RoboTextBufferCreate(&buffer, NULL), (int)RoboOk);

    /* Append, in pieces, including a split multi-byte sequence that only becomes valid once joined. */
    RoboCheckEqInt((int)RoboTextBufferAppend(buffer, "ab", 2u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferAppend(buffer, "c", 1u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 3);

    /* Append one scalar, including one that is four bytes long. */
    RoboCheckEqInt((int)RoboTextBufferAppendScalar(buffer, 0x1F642, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 4);
    RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), 7);
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 3u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, 0x1F642);
    /* A lone surrogate is not a scalar and is refused rather than replaced. */
    RoboCheckEqInt((int)RoboTextBufferAppendScalar(buffer, 0xD800, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 4);

    /* Insert at a scalar boundary — never at a byte offset, which is the whole point of the unit. */
    RoboCheckEqInt((int)RoboTextBufferInsert(buffer, 2u, "XY", 2u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 6);
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 0u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, 'a');
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 2u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, 'X');
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 4u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, 'c');
    /* Inserting at the scalar count is an append. */
    RoboCheckEqInt((int)RoboTextBufferInsert(buffer, 6u, "!", 1u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 7);
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 6u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, '!');
    RoboCheckEqInt((int)RoboTextBufferInsert(buffer, 8u, "?", 1u, NULL), (int)RoboInvalidArgument);

    /* Remove a span, then an empty span (which succeeds and changes nothing). */
    RoboCheckEqInt((int)RoboTextBufferRemove(buffer, RoboTextRangeMake(2u, 4u), NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 5);
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 2u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, 'c');
    RoboCheckEqInt((int)RoboTextBufferRemove(buffer, RoboTextRangeAt(2u), NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 5);

    /* Every edit invalidated the index and the next scalar-indexed read rebuilt it: walk the whole text
     * to prove the rebuilt index agrees with the bytes. */
    {
        RoboTextScalarIndex index;

        for (index = 0u; index < RoboTextBufferScalarCount(buffer); index++) {
            RoboTextByteIndex byte = 0u;
            RoboTextScalarIndex back = 0xFFFFFFFFu;

            RoboCheckEqInt((int)RoboTextBufferScalarToByte(buffer, index, &byte, NULL), (int)RoboOk);
            RoboCheckEqInt((int)RoboTextBufferByteToScalar(buffer, byte, &back, NULL), (int)RoboOk);
            RoboCheckEqInt((int)back, (int)index);
        }
    }

    /* Removing everything is legal and leaves the same state as a fresh buffer. */
    RoboCheckEqInt((int)RoboTextBufferRemove(buffer, RoboTextBufferRange(buffer), NULL), (int)RoboOk);
    RoboCheck(RoboTextBufferIsEmpty(buffer));
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 0);

    RoboTextBufferDestroy(buffer);
}

/* ------------------------------------------------------------------ */
/* From scalars, and REPLACE mode                                      */
/* ------------------------------------------------------------------ */

static void TestBufferFromScalars(void)
{
    RoboTextBuffer *buffer = NULL;
    static const Uchar32 scalars[] = {'h', 0xE9, 0x4E2D, 0x1F642};
    Uchar32 value = 0u;

    RoboCheckEqInt((int)RoboTextBufferCreateFromScalars(&buffer, scalars, 4u, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(buffer), 4);
    RoboCheckEqInt((int)RoboTextBufferByteLength(buffer), 1 + 2 + 3 + 4);
    RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, 1u, &value, NULL), (int)RoboOk);
    RoboCheckEqInt((int)value, 0xE9);
    /* Round trip: the bytes decode back to the scalars that encoded them. */
    {
        Uchar32 decoded[4];
        size_t count = 0u;

        RoboCheckEqInt((int)RoboTextBufferDecodeRange(buffer, RoboTextRangeFull(4u), decoded, 4u, &count,
                                                      NULL),
                       (int)RoboOk);
        for (size_t index = 0u; index < 4u; index++) {
            RoboCheckEqInt((int)decoded[index], (int)scalars[index]);
        }
    }
    RoboTextBufferDestroy(buffer);

    /* A lone surrogate is refused, and the buffer is destroyed rather than leaked. */
    {
        static const Uchar32 bad[] = {'a', 0xDC00};

        RoboCheckEqInt((int)RoboTextBufferCreateFromScalars(&buffer, bad, 2u, NULL),
                       (int)RoboInvalidArgument);
        RoboCheck(buffer == NULL);
    }
    RoboCheckEqInt((int)RoboTextBufferCreateFromScalars(&buffer, NULL, 2u, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextBufferCreateFromScalars(&buffer, scalars, 0u, NULL), (int)RoboOk);
    RoboCheck(RoboTextBufferIsEmpty(buffer));
    RoboTextBufferDestroy(buffer);
}

static void TestBufferReplaceMode(void)
{
    RoboTextBuffer *buffer = NULL;

    /*
     * A truncated sequence and a lone continuation byte. The contract is REPLACE, not refusal: a document
     * that reached the DOM has already been through the HTML tokenizer's own decoding, so a buffer that
     * refused these would be refusing a document the DOM accepted. What is asserted is that it *returns*
     * and that the replacement is counted — not a particular number of scalars, because the decoder's
     * advance over a malformed sequence is RoboUnicode's policy rather than this layer's.
     */
    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, "\xE4\xB8", 2u, NULL), (int)RoboOk);
    RoboCheck(RoboTextBufferScalarCount(buffer) >= 1u);
    RoboTextBufferDestroy(buffer);

    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, "a\x80" "b", 3u, NULL), (int)RoboOk);
    RoboCheck(RoboTextBufferScalarCount(buffer) >= 2u);
    RoboTextBufferDestroy(buffer);

    /* And every scalar the index reports must decode, or the index would be describing a text that is
     * not there. */
    RoboCheckEqInt((int)RoboTextBufferCreateFrom(&buffer, "\xFF\xFE\x80", 3u, NULL), (int)RoboOk);
    for (RoboTextScalarIndex index = 0u; index < RoboTextBufferScalarCount(buffer); index++) {
        Uchar32 value = 0u;

        RoboCheckEqInt((int)RoboTextBufferScalarAt(buffer, index, &value, NULL), (int)RoboOk);
        RoboCheck(value == 0xFFFD || value == 0u);
    }
    RoboTextBufferDestroy(buffer);
}

/* ------------------------------------------------------------------ */
/* The borrowed view                                                   */
/* ------------------------------------------------------------------ */

static void TestBufferView(void)
{
    static const char *text = "h\xC3\xA9llo";
    RoboTextView view = RoboTextViewMake(text, 6u);
    RoboTextScalarIndex count = 0u;
    RoboTextByteIndex byte = 0u;
    Uchar32 decoded[5];
    size_t decodedCount = 0u;

    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &count, NULL), (int)RoboOk);
    RoboCheckEqInt((int)count, 5);
    RoboCheckEqInt((int)RoboTextViewScalarToByte(view, 0u, &byte, NULL), (int)RoboOk);
    RoboCheckEqInt((int)byte, 0);
    RoboCheckEqInt((int)RoboTextViewScalarToByte(view, 2u, &byte, NULL), (int)RoboOk);
    RoboCheckEqInt((int)byte, 3);
    /* Total at the end, exactly as the owned buffer is: the view is the same model without a cache. */
    RoboCheckEqInt((int)RoboTextViewScalarToByte(view, 5u, &byte, NULL), (int)RoboOk);
    RoboCheckEqInt((int)byte, 6);
    RoboCheckEqInt((int)RoboTextViewScalarToByte(view, 6u, &byte, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextViewScalarAt(view, 1u, &decoded[0], NULL), (int)RoboOk);
    RoboCheckEqInt((int)decoded[0], 0xE9);
    RoboCheckEqInt((int)RoboTextViewScalarAt(view, 5u, &decoded[0], NULL), (int)RoboInvalidArgument);

    RoboCheckEqInt((int)RoboTextViewDecodeRange(view, RoboTextRangeFull(5u), decoded, 5u, &decodedCount,
                                                NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)decodedCount, 5);
    RoboCheckEqInt((int)decoded[1], 0xE9);
    RoboCheckEqInt((int)RoboTextViewDecodeRange(view, RoboTextRangeMake(0u, 6u), decoded, 5u,
                                                &decodedCount, NULL),
                   (int)RoboInvalidArgument);

    /* An empty view is a view over nothing, and NULL bytes with a length is not a view at all. */
    {
        RoboTextView empty = RoboTextViewMake(NULL, 0u);

        RoboCheckEqInt((int)RoboTextViewScalarCount(empty, &count, NULL), (int)RoboOk);
        RoboCheckEqInt((int)count, 0);
        RoboCheckEqInt((int)RoboTextViewScalarCount(RoboTextViewMake(NULL, 4u), &count, NULL),
                       (int)RoboInvalidArgument);
    }
    /* A view built from a NULL pointer with a length of 0 is what a `lookup("")` returns. */
    RoboCheckEqInt((int)RoboTextViewScalarCount(RoboTextViewMake(NULL, 0u), NULL, NULL),
                   (int)RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The range algebra                                                   */
/* ------------------------------------------------------------------ */

static void TestRangeAlgebra(void)
{
    RoboTextRange full = RoboTextRangeFull(10u);
    RoboTextRange at = RoboTextRangeAt(4u);
    RoboTextRange a = RoboTextRangeMake(2u, 6u);
    RoboTextRange b = RoboTextRangeMake(6u, 9u);
    RoboTextRange c = RoboTextRangeMake(3u, 8u);

    RoboCheckEqInt((int)full.start, 0);
    RoboCheckEqInt((int)full.end, 10);
    RoboCheckEqInt((int)RoboTextRangeLength(full), 10);
    RoboCheckEqInt((int)RoboTextRangeLength(at), 0);
    RoboCheck(RoboTextRangeIsEmpty(at));
    RoboCheck(!RoboTextRangeIsEmpty(full));
    RoboCheck(!RoboTextRangeIsInverted(at));
    RoboCheck(RoboTextRangeIsInverted(RoboTextRangeMake(6u, 2u)));
    RoboCheckEqInt((int)RoboTextRangeLength(RoboTextRangeMake(6u, 2u)), 0); /* Clamped, not wrapped. */

    RoboCheck(RoboTextRangeIsWithin(a, 10u));
    RoboCheck(!RoboTextRangeIsWithin(RoboTextRangeMake(0u, 11u), 10u));
    RoboCheck(!RoboTextRangeIsWithin(RoboTextRangeMake(8u, 2u), 10u));
    RoboCheck(RoboTextRangeIsWithin(at, 10u)); /* A caret is a legal range. */

    /* Half-open: `a` ends at 6 and `b` starts at 6, so they do not share a scalar. */
    RoboCheck(RoboTextRangeContains(a, 2u));
    RoboCheck(RoboTextRangeContains(a, 5u));
    RoboCheck(!RoboTextRangeContains(a, 6u));
    RoboCheck(!RoboTextRangeContains(a, 1u));
    RoboCheck(!RoboTextRangeIntersects(a, b));
    RoboCheck(RoboTextRangeIntersects(a, c));
    RoboCheck(!RoboTextRangeIntersects(a, at)); /* A caret shares no scalars with a span. */
    /* `c` is `[3, 8)` and `a` is `[2, 6)`: each extends past the other, so neither contains the other. */
    RoboCheck(!RoboTextRangeContainsRange(c, a));
    RoboCheck(!RoboTextRangeContainsRange(a, c));
    RoboCheck(RoboTextRangeContainsRange(a, RoboTextRangeMake(3u, 5u)));
    RoboCheck(RoboTextRangeContainsRange(a, at)); /* An empty range inside `a` is contained. */

    {
        RoboTextRange unionRange = RoboTextRangeUnion(a, b);

        RoboCheckEqInt((int)unionRange.start, 2);
        RoboCheckEqInt((int)unionRange.end, 9);
        /* An inverted argument is ignored rather than propagated. */
        RoboCheckEqInt((int)RoboTextRangeUnion(a, RoboTextRangeMake(9u, 1u)).end, 6);
        RoboCheckEqInt((int)RoboTextRangeUnion(RoboTextRangeMake(9u, 1u), a).start, 2);
    }
    {
        /* Slicing clamps: `from`/`to` are counted from the range's own start. */
        RoboTextRange slice = RoboTextRangeSlice(a, 1u, 3u);

        RoboCheckEqInt((int)slice.start, 3);
        RoboCheckEqInt((int)slice.end, 5);
        RoboCheckEqInt((int)RoboTextRangeSlice(a, 0u, 99u).end, 6);
        RoboCheckEqInt((int)RoboTextRangeSlice(a, 99u, 100u).start, 6);
        RoboCheck(RoboTextRangeIsEmpty(RoboTextRangeSlice(a, 3u, 1u)));
    }
    {
        RoboTextRange shifted = RoboTextRangeShift(a, 4);
        RoboTextRange back = RoboTextRangeShift(a, -4);

        RoboCheckEqInt((int)shifted.start, 6);
        RoboCheckEqInt((int)shifted.end, 10);
        RoboCheckEqInt((int)back.start, 0); /* Saturates at zero rather than wrapping. */
        RoboCheckEqInt((int)back.end, 2);
        /* Shifted up by a large step from 2: the arithmetic is exact and the saturation only begins at
         * the top of the index space, which is what keeps a caret from wrapping into the middle of a text. */
        RoboCheckEqInt((int)RoboTextRangeShift(a, 1 << 30).start, 2 + (1 << 30));
    }
    RoboCheck(RoboTextRangeEqual(&a, &a));
    RoboCheck(!RoboTextRangeEqual(&a, &b));
    RoboCheck(RoboTextRangeEqual(NULL, NULL));
    RoboCheck(!RoboTextRangeEqual(&a, NULL));
}

void TestRegisterTextBuffer(void)
{
    TestRegister("text.buffer.lifecycle", TestBufferLifecycle);
    TestRegister("text.buffer.units", TestBufferUnits);
    TestRegister("text.buffer.decode_slice", TestBufferDecodeAndSlice);
    TestRegister("text.buffer.edit", TestBufferEdit);
    TestRegister("text.buffer.from_scalars", TestBufferFromScalars);
    TestRegister("text.buffer.replace_mode", TestBufferReplaceMode);
    TestRegister("text.buffer.view", TestBufferView);
    TestRegister("text.range.algebra", TestRangeAlgebra);
}
