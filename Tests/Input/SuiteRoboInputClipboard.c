/*
 * RoboInput clipboard suite (Phase 13.40/13.41) — `input.clipboard.*`.
 *
 * Four cases:
 *
 *   `vocabulary`   the six formats, the four results, and the one *supported* format -- the others are named
 *                  so a caller can ask and be told no, which is why they have numbers at all;
 *
 *   `entry`        the by-value entry: the serial that moves on every write, the do-nothing clear, a size
 *                  refusal that reports the length it needed, and the validator's two refusals;
 *
 *   `copy`         the read: an inverted range reads nothing, an **empty** range leaves the clipboard exactly
 *                  as it was (this is the case that matters -- a stray click must not destroy a copy), a
 *                  stale range is *unavailable* rather than empty, and too much text is reported as such;
 *
 *   `paste`        the write: the destination is not called for an empty clipboard, and when it is called it
 *                  gets the text **and the range never came from a glyph** -- the suite hands over a
 *                  character-level insertion and checks the bytes it received.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

/* A source over a fixed string, and the destination recorder. */
static const char *ClipboardFixtureText = "hello world";
static uint32_t ClipboardFixtureReads;
static int ClipboardFixtureStale;

static Result ClipboardReadRange(void *userdata,
                                 uint32_t start,
                                 uint32_t end,
                                 char *out,
                                 size_t capacity,
                                 size_t *outLength)
{
    size_t length = (size_t)(end - start);

    (void)userdata;
    ClipboardFixtureReads++;
    if (ClipboardFixtureStale) {
        return RoboNotFound;
    }
    if (length + 1u > capacity) {
        return RoboOutOfMemory;
    }
    memcpy(out, ClipboardFixtureText + start, length);
    out[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

static char ClipboardInserted[RoboInputClipboardTextMax];
static size_t ClipboardInsertedLength;
static uint32_t ClipboardInsertCalls;
static Result ClipboardInsertResult;

static Result ClipboardInsert(void *userdata, const char *text, size_t length)
{
    (void)userdata;
    ClipboardInsertCalls++;
    memset(ClipboardInserted, 0, sizeof(ClipboardInserted));
    if (length >= sizeof(ClipboardInserted)) {
        length = sizeof(ClipboardInserted) - 1u;
    }
    memcpy(ClipboardInserted, text, length);
    ClipboardInsertedLength = length;
    return ClipboardInsertResult;
}

static void ClipboardReset(void)
{
    ClipboardFixtureReads = 0u;
    ClipboardFixtureStale = 0;
    ClipboardInsertCalls = 0u;
    ClipboardInsertedLength = 0u;
    ClipboardInsertResult = RoboOk;
    memset(ClipboardInserted, 0, sizeof(ClipboardInserted));
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestClipboardVocabulary(void)
{
    RoboCheckEqInt(RoboInputClipboardFormatNone, 0);
    RoboCheckEqInt(RoboInputClipboardFormatPlainText, 1);
    RoboCheckEqInt(RoboInputClipboardFormatHtml, 2);
    RoboCheckEqInt(RoboInputClipboardFormatRichText, 3);
    RoboCheckEqInt(RoboInputClipboardFormatImage, 4);
    RoboCheckEqInt(RoboInputClipboardFormatFiles, 5);
    RoboCheckEqInt(RoboInputClipboardFormatCount, 6);
    RoboCheckStrEq(InputClipboardFormatName(RoboInputClipboardFormatPlainText), "text");
    RoboCheckStrEq(InputClipboardFormatName(RoboInputClipboardFormatRichText), "rich-text");
    RoboCheckStrEq(InputClipboardFormatName((InputClipboardFormat)9), "unknown");
    RoboCheck(!InputClipboardFormatIsKnown((InputClipboardFormat)6));

    /* One format is produced; the four others exist so a caller can be told no. */
    RoboCheck(InputClipboardFormatIsSupported(RoboInputClipboardFormatPlainText));
    RoboCheck(!InputClipboardFormatIsSupported(RoboInputClipboardFormatHtml));
    RoboCheck(!InputClipboardFormatIsSupported(RoboInputClipboardFormatRichText));
    RoboCheck(!InputClipboardFormatIsSupported(RoboInputClipboardFormatImage));
    RoboCheck(!InputClipboardFormatIsSupported(RoboInputClipboardFormatFiles));
    RoboCheck(!InputClipboardFormatIsSupported(RoboInputClipboardFormatNone));

    RoboCheckEqInt(RoboInputClipboardCopied, 0);
    RoboCheckEqInt(RoboInputClipboardNothing, 1);
    RoboCheckEqInt(RoboInputClipboardUnavailable, 2);
    RoboCheckEqInt(RoboInputClipboardTooLarge, 3);
    RoboCheckEqInt(RoboInputClipboardResultCount, 4);
    RoboCheckStrEq(InputClipboardResultName(RoboInputClipboardTooLarge), "too-large");
    RoboCheck(!InputClipboardResultIsKnown((InputClipboardResult)4));
}

/* ------------------------------------------------------------------ */
/* The entry                                                           */
/* ------------------------------------------------------------------ */

static void TestClipboardEntry(void)
{
    InputClipboardData data;
    InputDiag diag;
    size_t needed = 0u;

    InputDiagReset(&diag);
    InputClipboardDataInit(&data);
    RoboCheckEqInt((int)data.serial, 0);
    RoboCheckEqInt((int)data.textLength, 0);
    RoboCheckStrEq(InputClipboardDataText(&data), "");
    RoboCheck(!InputClipboardDataHasFormat(&data, RoboInputClipboardFormatPlainText));
    /* An empty entry is *valid*: it is what a clipboard nobody has written to is. */
    RoboCheck(InputClipboardDataIsValid(&data));
    RoboCheck(!InputClipboardDataHasFormat(NULL, RoboInputClipboardFormatPlainText));
    RoboCheck(!InputClipboardDataIsValid(NULL));

    /* A write installs the text and moves the serial, which is what a caller caches against. */
    RoboCheckEqInt(InputClipboardDataSetText(&data, "abc", 3u, 7u, &needed, &diag), RoboOk);
    RoboCheckEqInt((int)data.serial, 1);
    RoboCheckEqInt((int)data.textLength, 3);
    RoboCheckEqInt((int)data.owner, 7);
    RoboCheckStrEq(data.text, "abc");
    RoboCheck(InputClipboardDataHasFormat(&data, RoboInputClipboardFormatPlainText));
    RoboCheck(InputClipboardDataIsValid(&data));

    RoboCheckEqInt(InputClipboardDataSetText(&data, "de", 2u, 7u, NULL, &diag), RoboOk);
    RoboCheckEqInt((int)data.serial, 2);

    /* A clear empties it, moves the serial, and drops the format. */
    RoboCheckEqInt(InputClipboardDataSetText(&data, NULL, 0u, 0u, NULL, &diag), RoboOk);
    RoboCheckEqInt((int)data.serial, 3);
    RoboCheckEqInt((int)data.textLength, 0);
    RoboCheck(!InputClipboardDataHasFormat(&data, RoboInputClipboardFormatPlainText));

    /* Too much text is refused with the length it needed, and changes nothing. */
    {
        char big[RoboInputClipboardTextMax + 16];
        size_t index;

        for (index = 0u; index < sizeof(big) - 1u; index++) {
            big[index] = 'x';
        }
        big[sizeof(big) - 1u] = '\0';
        RoboCheckEqInt(InputClipboardDataSetText(&data, big, sizeof(big) - 1u, 0u, &needed, &diag),
                       RoboNotFound);
        RoboCheckEqInt((int)needed, (int)(sizeof(big) - 1u));
        RoboCheckEqInt((int)data.textLength, 0);
        RoboCheckEqInt((int)diag.domain, (int)RoboInputDomainClipboard);
    }

    /* A NULL entry, and text of a length without text. */
    RoboCheckEqInt(InputClipboardDataSetText(NULL, "x", 1u, 0u, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputClipboardDataSetText(&data, NULL, 5u, 0u, NULL, &diag), RoboInvalidArgument);

    /* Equality is by content. */
    {
        InputClipboardData a;
        InputClipboardData b;

        InputClipboardDataInit(&a);
        InputClipboardDataInit(&b);
        RoboCheck(InputClipboardDataEqual(&a, &b));
        (void)InputClipboardDataSetText(&a, "same", 4u, 1u, NULL, NULL);
        (void)InputClipboardDataSetText(&b, "same", 4u, 1u, NULL, NULL);
        RoboCheck(InputClipboardDataEqual(&a, &b));
        (void)InputClipboardDataSetText(&b, "other", 5u, 1u, NULL, NULL);
        RoboCheck(!InputClipboardDataEqual(&a, &b));
        RoboCheck(InputClipboardDataEqual(&a, &a));
        RoboCheck(!InputClipboardDataEqual(&a, NULL));
    }

    /* A record that claims text while carrying none is refused by the validator -- which is what makes it
     * usable as a check on anything a caller assembles by hand. */
    {
        InputClipboardData broken;

        InputClipboardDataInit(&broken);
        broken.formats = (uint32_t)1u << (uint32_t)RoboInputClipboardFormatPlainText;
        RoboCheck(!InputClipboardDataIsValid(&broken));
        broken.formats = 0x80000000u;
        RoboCheck(!InputClipboardDataIsValid(&broken));
    }
}

/* ------------------------------------------------------------------ */
/* Copy                                                                */
/* ------------------------------------------------------------------ */

static void TestClipboardCopy(void)
{
    InputClipboardTextSource source;
    InputClipboardData data;
    InputDiag diag;
    size_t length = 0u;

    InputDiagReset(&diag);
    ClipboardReset();
    source.userdata = NULL;
    source.readRange = ClipboardReadRange;

    /* A plain range. */
    InputClipboardDataInit(&data);
    RoboCheckEqInt((int)InputClipboardCopy(&source, 0u, 5u, 1u, &data, &length, &diag),
                   (int)RoboInputClipboardCopied);
    RoboCheckEqInt((int)length, 5);
    RoboCheckStrEq(data.text, "hello");
    RoboCheckEqInt((int)ClipboardFixtureReads, 1);

    /* An **empty** range leaves the clipboard exactly as it was: a stray click must not destroy a copy. */
    RoboCheckEqInt((int)InputClipboardCopy(&source, 5u, 5u, 1u, &data, &length, &diag),
                   (int)RoboInputClipboardNothing);
    RoboCheckStrEq(data.text, "hello");
    RoboCheckEqInt((int)ClipboardFixtureReads, 1);

    /* An inverted range reads nothing, and does not call the source at all. */
    RoboCheckEqInt((int)InputClipboardCopy(&source, 8u, 2u, 1u, &data, &length, &diag),
                   (int)RoboInputClipboardNothing);
    RoboCheckEqInt((int)ClipboardFixtureReads, 1);
    RoboCheckStrEq(data.text, "hello");

    /* No source at all is *unavailable*, not empty. */
    RoboCheckEqInt((int)InputClipboardCopy(NULL, 0u, 3u, 1u, &data, &length, &diag),
                   (int)RoboInputClipboardUnavailable);
    {
        InputClipboardTextSource empty;

        empty.userdata = NULL;
        empty.readRange = NULL;
        RoboCheckEqInt((int)InputClipboardCopy(&empty, 0u, 3u, 1u, &data, &length, &diag),
                       (int)RoboInputClipboardUnavailable);
    }

    /* A *stale* range -- an editor whose text changed under a selection -- is unavailable rather than empty,
     * and the clipboard keeps what it held. */
    ClipboardFixtureStale = 1;
    RoboCheckEqInt((int)InputClipboardCopy(&source, 0u, 5u, 1u, &data, &length, &diag),
                   (int)RoboInputClipboardUnavailable);
    RoboCheckStrEq(data.text, "hello");
    ClipboardFixtureStale = 0;

    /* The source reports what it could not fit, which is reported as *too large* rather than as a silent
     * truncation. */
    {
        char huge[RoboInputClipboardTextMax + 8];
        size_t index;

        for (index = 0u; index < sizeof(huge) - 1u; index++) {
            huge[index] = 'y';
        }
        huge[sizeof(huge) - 1u] = '\0';
        ClipboardFixtureText = huge;
        RoboCheckEqInt((int)InputClipboardCopy(&source, 0u, (uint32_t)(sizeof(huge) - 1u), 1u, &data,
                                               &length, &diag),
                       (int)RoboInputClipboardTooLarge);
        RoboCheck(length >= RoboInputClipboardTextMax);
        RoboCheckStrEq(data.text, "hello");
        ClipboardFixtureText = "hello world";
    }

    /* A NULL entry is unavailable rather than a crash. */
    RoboCheckEqInt((int)InputClipboardCopy(&source, 0u, 5u, 1u, NULL, &length, &diag),
                   (int)RoboInputClipboardUnavailable);
}

/* ------------------------------------------------------------------ */
/* Paste                                                               */
/* ------------------------------------------------------------------ */

static void TestClipboardPaste(void)
{
    InputClipboardData data;

    ClipboardReset();
    InputClipboardDataInit(&data);

    /* An empty clipboard does **not** call the destination: calling it with an empty string would delete the
     * selection, and pasting nothing means nothing happens. */
    RoboCheckEqInt((int)InputClipboardPaste(&data, ClipboardInsert, NULL), (int)RoboInputClipboardNothing);
    RoboCheckEqInt((int)ClipboardInsertCalls, 0);

    (void)InputClipboardDataSetText(&data, "pasted", 6u, 1u, NULL, NULL);
    RoboCheckEqInt((int)InputClipboardPaste(&data, ClipboardInsert, NULL),
                   (int)RoboInputClipboardCopied);
    RoboCheckEqInt((int)ClipboardInsertCalls, 1);
    /* The destination got the **characters**, and the suite checks the bytes: a clipboard that carried glyph
     * ids would arrive here as numbers rather than as text. */
    RoboCheckEqInt((int)ClipboardInsertedLength, 6);
    RoboCheckStrEq(ClipboardInserted, "pasted");

    /* A destination that refuses is reported, and a missing destination is unavailable. */
    ClipboardInsertResult = RoboInvalidArgument;
    RoboCheckEqInt((int)InputClipboardPaste(&data, ClipboardInsert, NULL),
                   (int)RoboInputClipboardUnavailable);
    RoboCheckEqInt((int)InputClipboardPaste(&data, NULL, NULL), (int)RoboInputClipboardUnavailable);
    RoboCheckEqInt((int)InputClipboardPaste(NULL, ClipboardInsert, NULL),
                   (int)RoboInputClipboardNothing);
}

/* ------------------------------------------------------------------ */
/* Dump                                                                */
/* ------------------------------------------------------------------ */

static void TestClipboardDump(void)
{
    InputClipboardData data;
    InputClipboardData other;
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    InputClipboardDataInit(&data);
    (void)InputClipboardDataSetText(&data, "hi", 2u, 3u, NULL, &diag);
    RoboCheckEqInt(InputClipboardDataWrite(&data, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "clipboard serial=1 owner=3 bytes=2 formats=text text=\"hi\"\n");

    InputClipboardDataInit(&other);
    RoboCheckEqInt(InputClipboardDataWrite(&other, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "clipboard serial=0 owner=0 bytes=0 formats=none text=\"\"\n");
    RoboCheckEqInt(InputClipboardDataWrite(&other, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    RoboCheckEqInt(InputClipboardDataWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "clipboard <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputClipboardDataWrite(&data, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputClipboard(void)
{
    TestRegister("input.clipboard.vocabulary", TestClipboardVocabulary);
    TestRegister("input.clipboard.entry", TestClipboardEntry);
    TestRegister("input.clipboard.copy", TestClipboardCopy);
    TestRegister("input.clipboard.paste", TestClipboardPaste);
    TestRegister("input.clipboard.dump", TestClipboardDump);
}
