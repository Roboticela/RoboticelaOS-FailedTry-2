/*
 * RoboUnicodeEmoji — UTS #51 RGI sequence matching (Goal 3.13).
 */

#include "RoboUnicode/RoboUnicodeEmoji.h"

#define RoboEmojiMatchMaxCps 32u

const char *UnicodeEmojiUcdVersionString(void)
{
    return EmojiUcdVersionString();
}

const char *UnicodeEmojiVersionString(void)
{
    return EmojiVersionString();
}

const char *UnicodeEmojiSeqKindName(UnicodeEmojiSeqKind k)
{
    return EmojiSeqKindName(k);
}

UnicodeEmojiSeqKind UnicodeEmojiSequenceClassify(
    const Uchar32 *cps, size_t len)
{
    UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;
    size_t n;

    if (!cps || len == 0u) {
        return RoboEmojiSeqNone;
    }
    n = EmojiTrieMatch(cps, len, &kind);
    if (n != len) {
        return RoboEmojiSeqNone;
    }
    return kind;
}

Result UnicodeEmojiSequenceMatch(const char *src,
                                                size_t srcLen,
                                                size_t offset,
                                                UnicodeMode mode,
                                                size_t *outEnd,
                                                UnicodeEmojiSeqKind *outKind)
{
    Uchar32 cps[RoboEmojiMatchMaxCps];
    size_t ends[RoboEmojiMatchMaxCps];
    size_t n = 0;
    size_t off = offset;
    UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;
    size_t matched;

    if (outEnd) {
        *outEnd = offset;
    }
    if (outKind) {
        *outKind = RoboEmojiSeqNone;
    }
    if ((!src && srcLen > 0u) || offset > srcLen || !outEnd || !outKind) {
        return RoboInvalidArgument;
    }
    if (offset == srcLen) {
        return RoboOk;
    }

    while (off < srcLen && n < RoboEmojiMatchMaxCps) {
        Uchar32 cp = 0;
        size_t next = 0;
        UnicodeStatus st =
            UnicodeUtf8Next(src, srcLen, off, mode, &cp, &next);
        if (st != RoboUnicodeOk) {
            return RoboParseError;
        }
        if (next <= off) {
            return RoboParseError;
        }
        cps[n] = cp;
        ends[n] = next;
        n++;
        off = next;
    }

    matched = EmojiTrieMatch(cps, n, &kind);
    if (matched == 0u) {
        *outEnd = offset;
        *outKind = RoboEmojiSeqNone;
        return RoboOk;
    }
    *outEnd = ends[matched - 1u];
    *outKind = kind;
    return RoboOk;
}

Result UnicodeEmojiSequenceNext(const char *src,
                                               size_t srcLen,
                                               size_t offset,
                                               UnicodeMode mode,
                                               size_t *outStart,
                                               size_t *outEnd,
                                               UnicodeEmojiSeqKind *outKind)
{
    size_t off = offset;

    if (outStart) {
        *outStart = srcLen;
    }
    if (outEnd) {
        *outEnd = srcLen;
    }
    if (outKind) {
        *outKind = RoboEmojiSeqNone;
    }
    if ((!src && srcLen > 0u) || offset > srcLen || !outStart || !outEnd ||
        !outKind) {
        return RoboInvalidArgument;
    }

    while (off < srcLen) {
        size_t end = off;
        UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;
        Result rc =
            UnicodeEmojiSequenceMatch(src, srcLen, off, mode, &end, &kind);
        if (rc != RoboOk) {
            return rc;
        }
        if (kind != RoboEmojiSeqNone && end > off) {
            *outStart = off;
            *outEnd = end;
            *outKind = kind;
            return RoboOk;
        }
        {
            Uchar32 cp = 0;
            size_t next = 0;
            UnicodeStatus st =
                UnicodeUtf8Next(src, srcLen, off, mode, &cp, &next);
            if (st != RoboUnicodeOk) {
                return RoboParseError;
            }
            if (next <= off) {
                return RoboParseError;
            }
            off = next;
        }
    }

    *outStart = srcLen;
    *outEnd = srcLen;
    *outKind = RoboEmojiSeqNone;
    return RoboOk;
}
