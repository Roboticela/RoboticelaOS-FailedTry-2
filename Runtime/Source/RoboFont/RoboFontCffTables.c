/*
 * CFF charset, encoding, FDSelect (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void FontCffCharsetClear(FontCffCharset *c)
{
    if (!c) {
        return;
    }
    RoboFree(c->sids);
    memset(c, 0, sizeof(*c));
}

Result FontCffCharsetParse(FontSpan table,
                                          size_t offset,
                                          uint32_t nGlyphs,
                                          int isCid,
                                          FontCffCharset *out)
{
    FontReader r;
    uint8_t format;
    uint32_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontCffCharsetClear(out);
    if (nGlyphs == 0u || nGlyphs > RoboFontCffMaxGlyphs) {
        return RoboParseError;
    }

    /* Predefined charsets: offset 0/1/2 are not table offsets when top.charset <= 2 */
    if (offset <= 2u) {
        /* Structural stub: only .notdef + sequential SIDs for testing predefined */
        out->format = (uint8_t)offset;
        out->glyphCount = nGlyphs;
        out->sids = (uint16_t *)RoboCalloc(nGlyphs, sizeof(uint16_t));
        if (!out->sids) {
            return RoboOutOfMemory;
        }
        out->sids[0] = 0u;
        for (i = 1u; i < nGlyphs; i++) {
            out->sids[i] = isCid ? (uint16_t)i : (uint16_t)i; /* simplified */
        }
        return RoboOk;
    }

    if (offset >= table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU8(&r, &format) != RoboOk) {
        return RoboParseError;
    }
    out->format = format;
    out->glyphCount = nGlyphs;
    out->sids = (uint16_t *)RoboCalloc(nGlyphs, sizeof(uint16_t));
    if (!out->sids) {
        return RoboOutOfMemory;
    }
    out->sids[0] = 0u;

    if (format == 0u) {
        for (i = 1u; i < nGlyphs; i++) {
            uint16_t sid;
            if (isCid) {
                uint16_t cid;
                if (FontReadU16be(&r, &cid) != RoboOk) {
                    FontCffCharsetClear(out);
                    return RoboParseError;
                }
                out->sids[i] = cid;
            } else {
                if (FontReadU16be(&r, &sid) != RoboOk) {
                    FontCffCharsetClear(out);
                    return RoboParseError;
                }
                out->sids[i] = sid;
            }
        }
        return RoboOk;
    }

    if (format == 1u || format == 2u) {
        uint32_t gid = 1u;
        while (gid < nGlyphs) {
            uint16_t first;
            uint32_t nLeft;
            uint32_t k;
            if (FontReadU16be(&r, &first) != RoboOk) {
                FontCffCharsetClear(out);
                return RoboParseError;
            }
            if (format == 1u) {
                uint8_t nl;
                if (FontReadU8(&r, &nl) != RoboOk) {
                    FontCffCharsetClear(out);
                    return RoboParseError;
                }
                nLeft = nl;
            } else {
                uint16_t nl;
                if (FontReadU16be(&r, &nl) != RoboOk) {
                    FontCffCharsetClear(out);
                    return RoboParseError;
                }
                nLeft = nl;
            }
            if (nLeft + 1u > nGlyphs - gid) {
                FontCffCharsetClear(out);
                return RoboParseError;
            }
            for (k = 0u; k <= nLeft; k++) {
                out->sids[gid++] = (uint16_t)(first + k);
            }
        }
        return RoboOk;
    }

    FontCffCharsetClear(out);
    return RoboParseError;
}

Result FontCffCharsetSid(const FontCffCharset *c,
                                        uint32_t gid,
                                        uint16_t *outSid)
{
    if (!c || !outSid) {
        return RoboInvalidArgument;
    }
    if (gid >= c->glyphCount || !c->sids) {
        return RoboParseError;
    }
    *outSid = c->sids[gid];
    return RoboOk;
}

void FontCffEncodingClear(FontCffEncoding *e)
{
    if (!e) {
        return;
    }
    RoboFree(e->codes);
    RoboFree(e->suppCodes);
    RoboFree(e->suppSids);
    memset(e, 0, sizeof(*e));
}

Result FontCffEncodingParse(FontSpan table,
                                           size_t offset,
                                           uint32_t nGlyphs,
                                           FontCffEncoding *out)
{
    FontReader r;
    uint8_t formatByte;
    uint8_t format;
    int hasSupp;
    uint32_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontCffEncodingClear(out);

    if (offset <= 1u) {
        out->isPredefined = 1;
        out->format = (uint8_t)offset;
        return RoboOk;
    }

    if (offset >= table.size || nGlyphs == 0u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU8(&r, &formatByte) != RoboOk) {
        return RoboParseError;
    }
    format = (uint8_t)(formatByte & 0x7Fu);
    hasSupp = (formatByte & 0x80u) != 0;
    out->format = formatByte;

    if (format == 0u) {
        uint8_t nCodes;
        if (FontReadU8(&r, &nCodes) != RoboOk) {
            return RoboParseError;
        }
        out->nCodes = nCodes;
        if (nCodes > 0u) {
            out->codes = (uint8_t *)RoboCalloc(nCodes, 1u);
            if (!out->codes) {
                return RoboOutOfMemory;
            }
            for (i = 0u; i < nCodes; i++) {
                if (FontReadU8(&r, &out->codes[i]) != RoboOk) {
                    FontCffEncodingClear(out);
                    return RoboParseError;
                }
            }
        }
    } else if (format == 1u) {
        uint8_t nRanges;
        uint32_t filled = 0u;
        uint8_t *expanded = NULL;
        uint32_t cap = nGlyphs > 0u ? nGlyphs : 1u;
        if (FontReadU8(&r, &nRanges) != RoboOk) {
            return RoboParseError;
        }
        expanded = (uint8_t *)RoboCalloc(cap, 1u);
        if (!expanded) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < nRanges; i++) {
            uint8_t first;
            uint8_t nLeft;
            uint32_t k;
            if (FontReadU8(&r, &first) != RoboOk ||
                FontReadU8(&r, &nLeft) != RoboOk) {
                RoboFree(expanded);
                return RoboParseError;
            }
            for (k = 0u; k <= (uint32_t)nLeft; k++) {
                if (filled >= cap) {
                    RoboFree(expanded);
                    return RoboParseError;
                }
                expanded[filled++] = (uint8_t)(first + k);
            }
        }
        out->codes = expanded;
        out->nCodes = filled;
    } else {
        return RoboParseError;
    }

    if (hasSupp) {
        uint8_t nSups;
        if (FontReadU8(&r, &nSups) != RoboOk) {
            FontCffEncodingClear(out);
            return RoboParseError;
        }
        out->nSupplements = nSups;
        if (nSups > 0u) {
            out->suppCodes = (uint8_t *)RoboCalloc(nSups, 1u);
            out->suppSids = (uint16_t *)RoboCalloc(nSups, sizeof(uint16_t));
            if (!out->suppCodes || !out->suppSids) {
                FontCffEncodingClear(out);
                return RoboOutOfMemory;
            }
            for (i = 0u; i < nSups; i++) {
                if (FontReadU8(&r, &out->suppCodes[i]) != RoboOk ||
                    FontReadU16be(&r, &out->suppSids[i]) != RoboOk) {
                    FontCffEncodingClear(out);
                    return RoboParseError;
                }
            }
        }
    }
    (void)nGlyphs;
    return RoboOk;
}

void FontCffFdselectClear(FontCffFdselect *f)
{
    if (!f) {
        return;
    }
    RoboFree(f->fds);
    memset(f, 0, sizeof(*f));
}

Result FontCffFdselectParse(FontSpan table,
                                           size_t offset,
                                           uint32_t nGlyphs,
                                           uint32_t fdCount,
                                           FontCffFdselect *out)
{
    FontReader r;
    uint8_t format;
    uint32_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontCffFdselectClear(out);
    if (nGlyphs == 0u || fdCount == 0u || fdCount > RoboFontCffMaxFdarray) {
        return RoboParseError;
    }
    if (offset >= table.size) {
        return RoboParseError;
    }

    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU8(&r, &format) != RoboOk) {
        return RoboParseError;
    }
    out->format = format;
    out->nGlyphs = nGlyphs;
    out->fds = (uint8_t *)RoboCalloc(nGlyphs, 1u);
    if (!out->fds) {
        return RoboOutOfMemory;
    }

    if (format == 0u) {
        for (i = 0u; i < nGlyphs; i++) {
            if (FontReadU8(&r, &out->fds[i]) != RoboOk) {
                FontCffFdselectClear(out);
                return RoboParseError;
            }
            if ((uint32_t)out->fds[i] >= fdCount) {
                FontCffFdselectClear(out);
                return RoboParseError;
            }
        }
        return RoboOk;
    }

    if (format == 3u) {
        uint16_t nRanges;
        uint16_t first;
        if (FontReadU16be(&r, &nRanges) != RoboOk) {
            FontCffFdselectClear(out);
            return RoboParseError;
        }
        out->nRanges = nRanges;
        if (nRanges == 0u) {
            FontCffFdselectClear(out);
            return RoboParseError;
        }
        if (FontReadU16be(&r, &first) != RoboOk) {
            FontCffFdselectClear(out);
            return RoboParseError;
        }
        for (i = 0u; i < nRanges; i++) {
            uint8_t fd;
            uint16_t next;
            uint32_t g;
            if (FontReadU8(&r, &fd) != RoboOk ||
                FontReadU16be(&r, &next) != RoboOk) {
                FontCffFdselectClear(out);
                return RoboParseError;
            }
            if ((uint32_t)fd >= fdCount || next < first || next > nGlyphs) {
                FontCffFdselectClear(out);
                return RoboParseError;
            }
            for (g = first; g < next; g++) {
                out->fds[g] = fd;
            }
            first = next;
        }
        if (first != nGlyphs) {
            FontCffFdselectClear(out);
            return RoboParseError;
        }
        return RoboOk;
    }

    FontCffFdselectClear(out);
    return RoboNotSupported;
}

Result FontCffFdselectGet(const FontCffFdselect *f,
                                         uint32_t gid,
                                         uint8_t *outFd)
{
    if (!f || !outFd) {
        return RoboInvalidArgument;
    }
    if (gid >= f->nGlyphs || !f->fds) {
        return RoboParseError;
    }
    *outFd = f->fds[gid];
    return RoboOk;
}
