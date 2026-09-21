/*
 * RoboHTML source/input abstraction (Phase 8.1).
 *
 * Borrowing-only view over application-owned bytes. RoboHTML never frees
 * source memory and never extends its lifetime.
 */

#include "RoboHTML/RoboHTMLSource.h"

#include <stddef.h>
#include <string.h>

static size_t BoundedStrlen(const char *s)
{
    size_t n = 0u;
    if (!s) {
        return 0u;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int LabelAsciiOk(const char *s, size_t cap, size_t *outLen)
{
    size_t n = BoundedStrlen(s);
    size_t i;

    if (n == 0u || n >= cap) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20u || c >= 0x7Fu) {
            return 0;
        }
    }
    *outLen = n;
    return 1;
}

void HtmlSourceInit(HtmlSource *src)
{
    if (!src) {
        return;
    }
    memset(src, 0, sizeof(*src));
    src->kind = RoboHtmlSourceKindNone;
}

Result HtmlSourceEmpty(HtmlSource *src,
                                     HtmlDiag *diag)
{
    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL");
    }
    HtmlSourceInit(src);
    src->kind = RoboHtmlSourceKindEmpty;
    return RoboOk;
}

Result HtmlSourceMemory(HtmlSource *src,
                                      const uint8_t *bytes,
                                      size_t length,
                                      HtmlDiag *diag)
{
    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL");
    }
    if (!bytes && length != 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument,
                              "memory source: bytes NULL with non-zero length");
    }
    HtmlSourceInit(src);
    src->kind = RoboHtmlSourceKindMemory;
    src->bytes = bytes;
    src->length = bytes ? length : 0u;
    return RoboOk;
}

Result HtmlSourceString(HtmlSource *src,
                                      const char *utf8,
                                      HtmlDiag *diag)
{
    size_t n;

    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL");
    }
    HtmlSourceInit(src);
    if (!utf8) {
        src->kind = RoboHtmlSourceKindString;
        return RoboOk;
    }
    n = BoundedStrlen(utf8);
    src->kind = RoboHtmlSourceKindString;
    src->bytes = (const uint8_t *)utf8;
    src->length = n;
    return RoboOk;
}

Result HtmlSourceSetDeclaredEncoding(HtmlSource *src,
                                                     const char *label,
                                                     HtmlDiag *diag)
{
    size_t n;

    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL");
    }
    if (!label) {
        /* Clearing the label is allowed. */
        src->encoding[0] = '\0';
        return RoboOk;
    }
    if (!LabelAsciiOk(label, RoboHtmlEncodingLabelMax, &n)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainEncoding,
                              RoboParseError,
                              "encoding label empty, too long, or non-ASCII");
    }
    memcpy(src->encoding, label, n + 1u);
    return RoboOk;
}

const char *HtmlSourceDeclaredEncoding(const HtmlSource *src)
{
    if (!src) {
        return NULL;
    }
    return src->encoding;
}

Result HtmlSourceSetName(HtmlSource *src,
                                        const char *name,
                                        HtmlDiag *diag)
{
    size_t n;

    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL");
    }
    if (!name) {
        src->name[0] = '\0';
        return RoboOk;
    }
    n = BoundedStrlen(name);
    if (n >= RoboHtmlUrlMax) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboNotSupported, "source name too long");
    }
    memcpy(src->name, name, n + 1u);
    return RoboOk;
}

const char *HtmlSourceName(const HtmlSource *src)
{
    if (!src) {
        return NULL;
    }
    return src->name;
}

size_t HtmlSourceLength(const HtmlSource *src)
{
    if (!src) {
        return 0u;
    }
    if (src->kind == RoboHtmlSourceKindString && src->bytes) {
        return BoundedStrlen((const char *)src->bytes);
    }
    if (src->kind == RoboHtmlSourceKindEmpty ||
        src->kind == RoboHtmlSourceKindNone) {
        return 0u;
    }
    return src->length;
}

int HtmlSourceIsEmpty(const HtmlSource *src)
{
    if (!src) {
        return 1;
    }
    if (src->kind == RoboHtmlSourceKindNone ||
        src->kind == RoboHtmlSourceKindEmpty) {
        return 1;
    }
    return HtmlSourceLength(src) == 0u;
}

Result HtmlSourceCopyBytes(const HtmlSource *src,
                                          uint8_t *out,
                                          size_t outCapacity)
{
    size_t n;

    if (!src || !out) {
        return RoboInvalidArgument;
    }
    n = HtmlSourceLength(src);
    if (n > outCapacity) {
        return RoboNotSupported;
    }
    if (n > 0u) {
        memcpy(out, src->bytes, n);
    }
    return RoboOk;
}

int HtmlSourceKindIsKnown(HtmlSourceKind kind)
{
    switch (kind) {
    case RoboHtmlSourceKindNone:
    case RoboHtmlSourceKindEmpty:
    case RoboHtmlSourceKindMemory:
    case RoboHtmlSourceKindString:
    case RoboHtmlSourceKindStream:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlSourceKindName(HtmlSourceKind kind)
{
    switch (kind) {
    case RoboHtmlSourceKindNone:
        return "none";
    case RoboHtmlSourceKindEmpty:
        return "empty";
    case RoboHtmlSourceKindMemory:
        return "memory";
    case RoboHtmlSourceKindString:
        return "string";
    case RoboHtmlSourceKindStream:
        return "stream";
    default:
        return "unknown";
    }
}
