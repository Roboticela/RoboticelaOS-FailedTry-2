/*
 * The clipboard (Phase 13.40/13.41) — see `RoboInputClipboard.h`.
 *
 * One value type and two operations. The operations are the interesting half: a copy reads characters through
 * the caller's source and an empty range refuses to touch what is already there, because "copy nothing" must
 * not mean "erase the clipboard".
 */

#include "RoboInput/RoboInputClipboard.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * Formats
 * ============================================================================================== */

static const char *const RiClipboardFormatNames[RoboInputClipboardFormatCount] = {
    "none", "text", "html", "rich-text", "image", "files"
};

int InputClipboardFormatIsKnown(InputClipboardFormat format)
{
    return (int)format >= 0 && (size_t)format < (size_t)RoboInputClipboardFormatCount;
}

const char *InputClipboardFormatName(InputClipboardFormat format)
{
    if (!InputClipboardFormatIsKnown(format)) {
        return "unknown";
    }
    return RiClipboardFormatNames[(size_t)format];
}

int InputClipboardFormatIsSupported(InputClipboardFormat format)
{
    /* Only plain text. The four others are *named* so a caller can ask and be told no, rather than inventing
     * its own list -- and so their numbers never move when one arrives. */
    return format == RoboInputClipboardFormatPlainText;
}

static const char *const RiClipboardResultNames[RoboInputClipboardResultCount] = {
    "copied", "nothing", "unavailable", "too-large"
};

int InputClipboardResultIsKnown(InputClipboardResult result)
{
    return (int)result >= 0 && (size_t)result < (size_t)RoboInputClipboardResultCount;
}

const char *InputClipboardResultName(InputClipboardResult result)
{
    if (!InputClipboardResultIsKnown(result)) {
        return "unknown";
    }
    return RiClipboardResultNames[(size_t)result];
}

/* ============================================================================================== *
 * The entry
 * ============================================================================================== */

void InputClipboardDataInit(InputClipboardData *data)
{
    if (!data) {
        return;
    }
    memset(data, 0, sizeof(*data));
}

int InputClipboardDataHasFormat(const InputClipboardData *data, InputClipboardFormat format)
{
    if (!data || !InputClipboardFormatIsKnown(format) || format == RoboInputClipboardFormatNone) {
        return 0;
    }
    return (data->formats & (uint32_t)(1u << (uint32_t)format)) != 0u ? 1 : 0;
}

int InputClipboardDataIsValid(const InputClipboardData *data)
{
    if (!data) {
        return 0;
    }
    if ((data->formats & ~((uint32_t)1u << RoboInputClipboardFormatCount)) != 0u) {
        /* Every bit in the mask must stand for a format this build defines. */
        uint32_t allowed = 0u;
        uint32_t index;

        for (index = 0u; index < RoboInputClipboardFormatCount; index++) {
            allowed |= (uint32_t)1u << index;
        }
        if ((data->formats & ~allowed) != 0u) {
            return 0;
        }
    }
    if (data->textLength >= RoboInputClipboardTextMax) {
        return 0;
    }
    if (data->text[data->textLength] != '\0') {
        return 0;
    }
    if (InputClipboardDataHasFormat(data, RoboInputClipboardFormatPlainText) && data->textLength == 0u) {
        /* Claiming text while carrying none is a record that disagrees with itself. */
        return 0;
    }
    return 1;
}

Result InputClipboardDataSetText(InputClipboardData *data,
                                 const char *text,
                                 size_t length,
                                 InputApplicationId owner,
                                 size_t *outLength,
                                 InputDiag *diag)
{
    uint32_t serial;

    if (outLength) {
        *outLength = length;
    }
    if (!data) {
        return RoboInputDiag(diag, RoboInputDomainClipboard, RoboInvalidArgument,
                             "a clipboard entry is required");
    }
    if (!text && length > 0u) {
        return RoboInputDiag(diag, RoboInputDomainClipboard, RoboInvalidArgument,
                             "text of a non-zero length is required");
    }
    if (length == 0u) {
        /* Clearing is legitimate and distinct from a refusal: the entry is emptied and the serial moves, so a
         * caller caching against it notices. */
        serial = data->serial + 1u;
        memset(data, 0, sizeof(*data));
        data->serial = serial;
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }
    if (length >= RoboInputClipboardTextMax) {
        return RoboInputDiag(diag, RoboInputDomainClipboard, RoboNotFound,
                             "the text does not fit the clipboard's buffer");
    }

    serial = data->serial + 1u;
    memset(data->text, 0, sizeof(data->text));
    memcpy(data->text, text, length);
    data->text[length] = '\0';
    data->textLength = length;
    data->formats = (uint32_t)1u << (uint32_t)RoboInputClipboardFormatPlainText;
    data->owner = owner;
    data->serial = serial;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

const char *InputClipboardDataText(const InputClipboardData *data)
{
    return data ? data->text : "";
}

int InputClipboardDataEqual(const InputClipboardData *a, const InputClipboardData *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->formats != b->formats || a->textLength != b->textLength || a->owner != b->owner) {
        return 0;
    }
    if (a->textLength == 0u) {
        return a->text[0] == b->text[0] ? 1 : 0;
    }
    return memcmp(a->text, b->text, a->textLength) == 0 ? 1 : 0;
}

/* ============================================================================================== *
 * Copy and paste
 * ============================================================================================== */

InputClipboardResult InputClipboardCopy(const InputClipboardTextSource *source,
                                        uint32_t start,
                                        uint32_t end,
                                        InputApplicationId owner,
                                        InputClipboardData *data,
                                        size_t *outLength,
                                        InputDiag *diag)
{
    char buffer[RoboInputClipboardTextMax];
    size_t length = 0u;
    Result status;

    if (outLength) {
        *outLength = 0u;
    }
    if (!data) {
        return RoboInputClipboardUnavailable;
    }
    if (end < start) {
        /* A selection is normalized before it gets here (12.21); an inverted range is a caller's bug, and the
         * honest answer is to read nothing. */
        return RoboInputClipboardNothing;
    }
    if (end == start) {
        /* An empty selection leaves the clipboard alone: replacing it with an empty string is how a stray
         * click destroys what the user copied. */
        return RoboInputClipboardNothing;
    }
    if (!source || !source->readRange) {
        return RoboInputClipboardUnavailable;
    }
    if (outLength) {
        *outLength = (size_t)(end - start);
    }

    memset(buffer, 0, sizeof(buffer));
    status = source->readRange(source->userdata, start, end, buffer, sizeof(buffer), &length);
    if (status == RoboNotFound) {
        /* The range is not in the current text: an editor whose text changed under a stale selection. */
        return RoboInputClipboardUnavailable;
    }
    if (status == RoboOutOfMemory) {
        /*
         * A bounded reader's `OutOfMemory` **is** "the buffer was too small": the source was handed
         * `RoboInputClipboardTextMax` and could not fit the range in it. Mapping it to anything else would
         * make a copy of a large selection indistinguishable from a broken editor, which is the difference
         * between "size your clipboard" and "something is wrong".
         */
        if (outLength) {
            *outLength = length >= RoboInputClipboardTextMax ? length : (size_t)(end - start);
        }
        return RoboInputClipboardTooLarge;
    }
    if (status != RoboOk) {
        (void)RoboInputDiag(diag, RoboInputDomainClipboard, status,
                            "the text source could not read the range");
        return RoboInputClipboardUnavailable;
    }
    if (length == 0u) {
        return RoboInputClipboardNothing;
    }
    if (length >= RoboInputClipboardTextMax) {
        if (outLength) {
            *outLength = length;
        }
        return RoboInputClipboardTooLarge;
    }
    /* A source that reported success while writing more than the range could hold is a caller's bug, and the
     * honest reading of it is that the copy is too large rather than that the text is trustworthy. */
    if (length > (size_t)(end - start)) {
        if (outLength) {
            *outLength = length;
        }
        return RoboInputClipboardTooLarge;
    }

    if (InputClipboardDataSetText(data, buffer, length, owner, NULL, diag) != RoboOk) {
        return RoboInputClipboardTooLarge;
    }
    if (outLength) {
        *outLength = length;
    }
    return RoboInputClipboardCopied;
}

InputClipboardResult InputClipboardPaste(const InputClipboardData *data,
                                         InputClipboardInsertFn insert,
                                         void *userdata)
{
    if (!data) {
        return RoboInputClipboardNothing;
    }
    if (!InputClipboardDataHasFormat(data, RoboInputClipboardFormatPlainText) || data->textLength == 0u) {
        /* No text: the destination is **not** called. Calling it with an empty string would delete the
         * selection, which is not what pasting an empty clipboard means -- it means nothing happens. */
        return RoboInputClipboardNothing;
    }
    if (!insert) {
        return RoboInputClipboardUnavailable;
    }
    if (insert(userdata, data->text, data->textLength) != RoboOk) {
        return RoboInputClipboardUnavailable;
    }
    return RoboInputClipboardCopied;
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputClipboardDataWrite(const InputClipboardData *data,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!data) {
        RiWriterText(&text, "clipboard <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "clipboard serial=");
    RiWriterU64(&text, (uint64_t)data->serial);
    RiWriterFieldU64(&text, " owner", (uint64_t)data->owner);
    RiWriterFieldU64(&text, " bytes", (uint64_t)data->textLength);
    RiWriterText(&text, " formats=");
    {
        int wrote = 0;

        for (index = 1u; index < RoboInputClipboardFormatCount; index++) {
            if (!InputClipboardDataHasFormat(data, (InputClipboardFormat)index)) {
                continue;
            }
            if (wrote) {
                RiWriterChar(&text, '+');
            }
            RiWriterText(&text, InputClipboardFormatName((InputClipboardFormat)index));
            wrote = 1;
        }
        if (!wrote) {
            RiWriterText(&text, "none");
        }
    }
    RiWriterText(&text, " text=\"");
    RiWriterText(&text, data->text);
    RiWriterText(&text, "\"\n");
    return RiWriterFinish(&text, outLength, diag);
}
