/*
 * RoboHTML encoding boundary (Phase 8.1).
 * Labels + state transitions only; no decoding in this phase.
 */

#include "RoboHTML/RoboHTMLEncoding.h"

#include <stddef.h>
#include <string.h>

typedef struct HtmlEncodingEntry {
    HtmlEncoding id;
    const char *label;
} HtmlEncodingEntry;

/* Static label table. Aliases map to the same token. */
static const HtmlEncodingEntry gEncodings[] = {
    { RoboHtmlEncodingUtf8, "UTF-8" },
    { RoboHtmlEncodingUtf8, "utf8" },
    { RoboHtmlEncodingUtf16LE, "UTF-16LE" },
    { RoboHtmlEncodingUtf16BE, "UTF-16BE" },
    { RoboHtmlEncodingUtf16, "UTF-16" },
    { RoboHtmlEncodingWindows1252, "windows-1252" },
    { RoboHtmlEncodingWindows1252, "cp1252" },
    { RoboHtmlEncodingShiftJis, "Shift_JIS" },
    { RoboHtmlEncodingUnknown, NULL }
};

static int AsciiCiEqual(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int HtmlEncodingIsKnown(HtmlEncoding enc)
{
    switch (enc) {
    case RoboHtmlEncodingUnknown:
    case RoboHtmlEncodingUtf8:
    case RoboHtmlEncodingUtf16LE:
    case RoboHtmlEncodingUtf16BE:
    case RoboHtmlEncodingUtf16:
    case RoboHtmlEncodingWindows1252:
    case RoboHtmlEncodingShiftJis:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlEncodingLabel(HtmlEncoding enc)
{
    switch (enc) {
    case RoboHtmlEncodingUtf8:
        return "UTF-8";
    case RoboHtmlEncodingUtf16LE:
        return "UTF-16LE";
    case RoboHtmlEncodingUtf16BE:
        return "UTF-16BE";
    case RoboHtmlEncodingUtf16:
        return "UTF-16";
    case RoboHtmlEncodingWindows1252:
        return "windows-1252";
    case RoboHtmlEncodingShiftJis:
        return "Shift_JIS";
    case RoboHtmlEncodingUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

HtmlEncoding HtmlEncodingFromLabel(const char *label)
{
    size_t i;

    if (!label) {
        return RoboHtmlEncodingUnknown;
    }
    for (i = 0u; gEncodings[i].label != NULL; i++) {
        if (AsciiCiEqual(gEncodings[i].label, label)) {
            return gEncodings[i].id;
        }
    }
    return RoboHtmlEncodingUnknown;
}

int HtmlEncodingStatusIsKnown(HtmlEncodingStatus status)
{
    switch (status) {
    case RoboHtmlEncodingStatusOk:
    case RoboHtmlEncodingStatusUnsupportedLabel:
    case RoboHtmlEncodingStatusInvalidLabel:
    case RoboHtmlEncodingStatusDecodeMismatch:
    case RoboHtmlEncodingStatusMalformed:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlEncodingStatusName(HtmlEncodingStatus status)
{
    switch (status) {
    case RoboHtmlEncodingStatusOk:
        return "ok";
    case RoboHtmlEncodingStatusUnsupportedLabel:
        return "unsupported-label";
    case RoboHtmlEncodingStatusInvalidLabel:
        return "invalid-label";
    case RoboHtmlEncodingStatusDecodeMismatch:
        return "decode-mismatch";
    case RoboHtmlEncodingStatusMalformed:
        return "malformed";
    default:
        return "unknown";
    }
}

void HtmlEncodingStateInit(HtmlEncodingState *st)
{
    if (!st) {
        return;
    }
    st->declared = RoboHtmlEncodingUnknown;
    st->actual = RoboHtmlEncodingUnknown;
    st->status = RoboHtmlEncodingStatusOk;
}

Result HtmlEncodingDeclare(HtmlEncodingState *st,
                                         const char *label,
                                         HtmlDiag *diag)
{
    HtmlEncoding id;
    size_t n;
    size_t i;

    if (!st) {
        return RoboHtmlDiag(diag, RoboHtmlDomainEncoding,
                              RoboInvalidArgument, "state is NULL");
    }
    if (!label) {
        return RoboHtmlDiag(diag, RoboHtmlDomainEncoding,
                              RoboInvalidArgument, "label is NULL");
    }
    n = strlen(label);
    if (n == 0u || n >= RoboHtmlEncodingLabelMax) {
        st->status = RoboHtmlEncodingStatusInvalidLabel;
        return RoboHtmlDiag(diag, RoboHtmlDomainEncoding,
                              RoboParseError,
                              "encoding label empty or too long");
    }
    for (i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)label[i];
        if (c < 0x20u || c >= 0x7Fu) {
            st->status = RoboHtmlEncodingStatusInvalidLabel;
            return RoboHtmlDiag(diag, RoboHtmlDomainEncoding,
                                  RoboParseError,
                                  "encoding label contains non-ASCII");
        }
    }
    id = HtmlEncodingFromLabel(label);
    if (id == RoboHtmlEncodingUnknown) {
        st->status = RoboHtmlEncodingStatusUnsupportedLabel;
        return RoboHtmlDiag(diag, RoboHtmlDomainEncoding,
                              RoboNotSupported,
                              "encoding label not admitted by the runtime");
    }
    st->declared = id;
    /* Boundary decision for 8.1: UTF-8 is the runtime-native encoding. */
    if (st->actual == RoboHtmlEncodingUnknown) {
        st->actual = RoboHtmlEncodingUtf8;
    }
    st->status = RoboHtmlEncodingStatusOk;
    return RoboOk;
}
