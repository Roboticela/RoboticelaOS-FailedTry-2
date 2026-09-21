#include "RoboFont/RoboFontTt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void FontGlyfGlyphClear(FontGlyfGlyph *g)
{
    if (!g) {
        return;
    }
    RoboFree(g->endPts);
    RoboFree(g->flags);
    RoboFree(g->xCoords);
    RoboFree(g->yCoords);
    RoboFree(g->components);
    memset(g, 0, sizeof(*g));
}

static Result ParseSimple(FontReader *r,
                                  size_t glyphEnd,
                                  int16_t ncontours,
                                  FontGlyfGlyph *out)
{
    uint16_t i;
    uint16_t lastPt;
    uint16_t instr;
    uint16_t pointCount;
    size_t flagsStart;
    size_t coordPos;
    uint16_t filled;

    if (ncontours <= 0) {
        return RoboParseError;
    }
    out->kind = RoboFontGlyfSimple;
    out->contourCount = (uint16_t)ncontours;
    out->endPts = (uint16_t *)RoboCalloc((uint16_t)ncontours, sizeof(uint16_t));
    if (!out->endPts) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < out->contourCount; i++) {
        if (FontReaderPos(r) + 2u > glyphEnd ||
            FontReadU16be(r, &out->endPts[i]) != RoboOk) {
            return RoboParseError;
        }
        if (i > 0u && out->endPts[i] <= out->endPts[i - 1u]) {
            return RoboParseError;
        }
    }
    lastPt = out->endPts[out->contourCount - 1u];
    pointCount = (uint16_t)(lastPt + 1u);
    out->pointCount = pointCount;

    if (FontReaderPos(r) + 2u > glyphEnd ||
        FontReadU16be(r, &instr) != RoboOk) {
        return RoboParseError;
    }
    out->instructionLength = instr;
    if (FontCheckedRange(glyphEnd, FontReaderPos(r), instr) != RoboOk) {
        return RoboParseError;
    }
    out->instructions.data = r->span.data + FontReaderPos(r);
    out->instructions.size = instr;
    if (FontReaderSeek(r, FontReaderPos(r) + instr) != RoboOk) {
        return RoboParseError;
    }

    out->flags = (uint8_t *)RoboCalloc(pointCount, 1u);
    out->xCoords = (int16_t *)RoboCalloc(pointCount, sizeof(int16_t));
    out->yCoords = (int16_t *)RoboCalloc(pointCount, sizeof(int16_t));
    if (!out->flags || !out->xCoords || !out->yCoords) {
        return RoboOutOfMemory;
    }

    /* Expand flags */
    filled = 0u;
    flagsStart = FontReaderPos(r);
    (void)flagsStart;
    while (filled < pointCount) {
        uint8_t flag;
        uint8_t repeat = 0u;
        if (FontReaderPos(r) >= glyphEnd ||
            FontReadU8(r, &flag) != RoboOk) {
            return RoboParseError;
        }
        if ((flag & RoboFontGlyfFlagRepeat) != 0u) {
            if (FontReaderPos(r) >= glyphEnd ||
                FontReadU8(r, &repeat) != RoboOk) {
                return RoboParseError;
            }
        }
        out->flags[filled++] = flag;
        while (repeat > 0u) {
            if (filled >= pointCount) {
                return RoboParseError;
            }
            out->flags[filled++] = flag;
            repeat--;
        }
    }

    /* X coordinates (deltas → absolute) */
    {
        int16_t x = 0;
        for (i = 0u; i < pointCount; i++) {
            uint8_t f = out->flags[i];
            int16_t dx = 0;
            if ((f & RoboFontGlyfFlagXShort) != 0u) {
                uint8_t b;
                if (FontReaderPos(r) >= glyphEnd ||
                    FontReadU8(r, &b) != RoboOk) {
                    return RoboParseError;
                }
                dx = (int16_t)b;
                if ((f & RoboFontGlyfFlagXSameOrPositive) == 0u) {
                    dx = (int16_t)(-dx);
                }
            } else if ((f & RoboFontGlyfFlagXSameOrPositive) == 0u) {
                if (FontReaderPos(r) + 2u > glyphEnd ||
                    FontReadI16be(r, &dx) != RoboOk) {
                    return RoboParseError;
                }
            }
            x = (int16_t)(x + dx);
            out->xCoords[i] = x;
        }
    }

    /* Y coordinates */
    {
        int16_t y = 0;
        for (i = 0u; i < pointCount; i++) {
            uint8_t f = out->flags[i];
            int16_t dy = 0;
            if ((f & RoboFontGlyfFlagYShort) != 0u) {
                uint8_t b;
                if (FontReaderPos(r) >= glyphEnd ||
                    FontReadU8(r, &b) != RoboOk) {
                    return RoboParseError;
                }
                dy = (int16_t)b;
                if ((f & RoboFontGlyfFlagYSameOrPositive) == 0u) {
                    dy = (int16_t)(-dy);
                }
            } else if ((f & RoboFontGlyfFlagYSameOrPositive) == 0u) {
                if (FontReaderPos(r) + 2u > glyphEnd ||
                    FontReadI16be(r, &dy) != RoboOk) {
                    return RoboParseError;
                }
            }
            y = (int16_t)(y + dy);
            out->yCoords[i] = y;
        }
    }

    coordPos = FontReaderPos(r);
    (void)coordPos;
    /* Trailing padding to even boundary is allowed; must not exceed glyphEnd. */
    if (FontReaderPos(r) > glyphEnd) {
        return RoboParseError;
    }
    return RoboOk;
}

static Result ParseComposite(FontReader *r,
                                     size_t glyphEnd,
                                     uint16_t numGlyphs,
                                     FontGlyfGlyph *out)
{
    uint16_t flags;
    uint16_t count = 0u;
    FontGlyfComponent *comps;
    size_t cap = 8u;

    out->kind = RoboFontGlyfComposite;
    comps = (FontGlyfComponent *)RoboCalloc(cap, sizeof(*comps));
    if (!comps) {
        return RoboOutOfMemory;
    }

    do {
        FontGlyfComponent *c;
        if (count >= RoboFontTtMaxComponentsPerGlyph) {
            RoboFree(comps);
            return RoboParseError;
        }
        if (count >= cap) {
            size_t ncap = cap * 2u;
            FontGlyfComponent *n =
                (FontGlyfComponent *)RoboCalloc(ncap, sizeof(*n));
            if (!n) {
                RoboFree(comps);
                return RoboOutOfMemory;
            }
            memcpy(n, comps, count * sizeof(*comps));
            RoboFree(comps);
            comps = n;
            cap = ncap;
        }
        c = &comps[count];
        memset(c, 0, sizeof(*c));
        c->xscale = 0x4000;
        c->yscale = 0x4000;

        if (FontReaderPos(r) + 4u > glyphEnd ||
            FontReadU16be(r, &flags) != RoboOk ||
            FontReadU16be(r, &c->glyphIndex) != RoboOk) {
            RoboFree(comps);
            return RoboParseError;
        }
        c->flags = flags;
        if (c->glyphIndex >= numGlyphs) {
            RoboFree(comps);
            return RoboParseError;
        }

        if ((flags & RoboFontGlyfCompArgsWords) != 0u) {
            int16_t a1;
            int16_t a2;
            if (FontReaderPos(r) + 4u > glyphEnd ||
                FontReadI16be(r, &a1) != RoboOk ||
                FontReadI16be(r, &a2) != RoboOk) {
                RoboFree(comps);
                return RoboParseError;
            }
            c->arg1 = a1;
            c->arg2 = a2;
        } else {
            uint8_t a1;
            uint8_t a2;
            if (FontReaderPos(r) + 2u > glyphEnd ||
                FontReadU8(r, &a1) != RoboOk ||
                FontReadU8(r, &a2) != RoboOk) {
                RoboFree(comps);
                return RoboParseError;
            }
            if ((flags & RoboFontGlyfCompArgsXy) != 0u) {
                c->arg1 = (int32_t)(int8_t)a1;
                c->arg2 = (int32_t)(int8_t)a2;
            } else {
                c->arg1 = a1;
                c->arg2 = a2;
            }
        }

        if ((flags & RoboFontGlyfCompHaveScale) != 0u) {
            int16_t s;
            if (FontReaderPos(r) + 2u > glyphEnd ||
                FontReadI16be(r, &s) != RoboOk) {
                RoboFree(comps);
                return RoboParseError;
            }
            c->xscale = s;
            c->yscale = s;
            c->hasTransform = 1u;
        } else if ((flags & RoboFontGlyfCompHaveXyScale) != 0u) {
            if (FontReaderPos(r) + 4u > glyphEnd ||
                FontReadI16be(r, &c->xscale) != RoboOk ||
                FontReadI16be(r, &c->yscale) != RoboOk) {
                RoboFree(comps);
                return RoboParseError;
            }
            c->hasTransform = 1u;
        } else if ((flags & RoboFontGlyfCompHave2X2) != 0u) {
            if (FontReaderPos(r) + 8u > glyphEnd ||
                FontReadI16be(r, &c->xscale) != RoboOk ||
                FontReadI16be(r, &c->scale01) != RoboOk ||
                FontReadI16be(r, &c->scale10) != RoboOk ||
                FontReadI16be(r, &c->yscale) != RoboOk) {
                RoboFree(comps);
                return RoboParseError;
            }
            c->hasTransform = 1u;
        }

        count++;
    } while ((flags & RoboFontGlyfCompMore) != 0u);

    /* Optional instructions after components */
    if ((flags & RoboFontGlyfCompHaveInstructions) != 0u) {
        uint16_t instr;
        if (FontReaderPos(r) + 2u > glyphEnd ||
            FontReadU16be(r, &instr) != RoboOk) {
            RoboFree(comps);
            return RoboParseError;
        }
        if (FontCheckedRange(glyphEnd, FontReaderPos(r), instr) != RoboOk) {
            RoboFree(comps);
            return RoboParseError;
        }
        out->compositeInstructionLength = instr;
        out->compositeInstructions.data = r->span.data + FontReaderPos(r);
        out->compositeInstructions.size = instr;
        if (FontReaderSeek(r, FontReaderPos(r) + instr) != RoboOk) {
            RoboFree(comps);
            return RoboParseError;
        }
    }

    if (FontReaderPos(r) > glyphEnd) {
        RoboFree(comps);
        return RoboParseError;
    }

    out->componentCount = count;
    out->components = comps;
    return RoboOk;
}

Result FontTtParseGlyphAt(FontSpan glyf,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint16_t numGlyphs,
                                          FontGlyfGlyph *out)
{
    FontReader r;
    FontGlyfHeader hdr;
    size_t glyphEnd;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontGlyfGlyphClear(out);
    if (length == 0u) {
        out->kind = RoboFontGlyfEmpty;
        out->offset = offset;
        out->length = 0u;
        return RoboOk;
    }
    if (FontCheckedRange(glyf.size, offset, length) != RoboOk) {
        return RoboParseError;
    }
    if (length < 10u) {
        return RoboParseError;
    }

    out->offset = offset;
    out->length = length;
    glyphEnd = (size_t)offset + (size_t)length;
    FontReaderInit(&r, glyf.data, glyf.size);
    if (FontReaderSeek(&r, offset) != RoboOk) {
        return RoboParseError;
    }
    if (FontReadI16be(&r, &hdr.numberOfContours) != RoboOk ||
        FontReadI16be(&r, &hdr.xMin) != RoboOk ||
        FontReadI16be(&r, &hdr.yMin) != RoboOk ||
        FontReadI16be(&r, &hdr.xMax) != RoboOk ||
        FontReadI16be(&r, &hdr.yMax) != RoboOk) {
        return RoboParseError;
    }
    if (hdr.xMin > hdr.xMax || hdr.yMin > hdr.yMax) {
        return RoboParseError;
    }
    out->header = hdr;

    if (hdr.numberOfContours >= 0) {
        res = ParseSimple(&r, glyphEnd, hdr.numberOfContours, out);
    } else if (hdr.numberOfContours == -1) {
        res = ParseComposite(&r, glyphEnd, numGlyphs, out);
    } else {
        return RoboParseError;
    }
    if (res != RoboOk) {
        FontGlyfGlyphClear(out);
    }
    return res;
}
