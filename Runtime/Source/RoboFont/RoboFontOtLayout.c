#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void FontGdefClear(FontGdef *g)
{
    if (!g) {
        return;
    }
    FontOtClassDefClear(&g->glyphClassDef);
    FontOtClassDefClear(&g->markAttachClassDef);
    memset(g, 0, sizeof(*g));
}

Result FontGdefParse(FontSpan span, FontGdef *out)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint16_t glyphClassOff;
    uint16_t attachOff;
    uint16_t ligOff;
    uint16_t markAttachOff;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontGdefClear(out);
    if (span.size < 12u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &glyphClassOff) != RoboOk ||
        FontReadU16be(&r, &attachOff) != RoboOk ||
        FontReadU16be(&r, &ligOff) != RoboOk ||
        FontReadU16be(&r, &markAttachOff) != RoboOk) {
        return RoboParseError;
    }
    if (major != 1u) {
        return RoboNotSupported;
    }
    out->majorVersion = major;
    out->minorVersion = minor;
    out->table = span;

    if (glyphClassOff != 0u) {
        size_t abs;
        if (FontOtResolveOffset16(span, 0u, glyphClassOff, &abs) != RoboOk ||
            FontOtClassDefParse(span, abs, &out->glyphClassDef) != RoboOk) {
            FontGdefClear(out);
            return RoboParseError;
        }
        out->hasGlyphClassDef = 1u;
    }
    if (attachOff != 0u) {
        size_t abs;
        if (FontOtResolveOffset16(span, 0u, attachOff, &abs) != RoboOk) {
            FontGdefClear(out);
            return RoboParseError;
        }
        /* AttachList: Coverage offset + count — validate header only. */
        if (abs + 4u > span.size) {
            FontGdefClear(out);
            return RoboParseError;
        }
        out->attachListOffset = abs;
        out->hasAttachList = 1u;
    }
    if (ligOff != 0u) {
        size_t abs;
        if (FontOtResolveOffset16(span, 0u, ligOff, &abs) != RoboOk ||
            abs + 4u > span.size) {
            FontGdefClear(out);
            return RoboParseError;
        }
        out->ligCaretListOffset = abs;
        out->hasLigCaretList = 1u;
    }
    if (markAttachOff != 0u) {
        size_t abs;
        if (FontOtResolveOffset16(span, 0u, markAttachOff, &abs) != RoboOk ||
            FontOtClassDefParse(span, abs, &out->markAttachClassDef) !=
                RoboOk) {
            FontGdefClear(out);
            return RoboParseError;
        }
        out->hasMarkAttachClassDef = 1u;
    }
    if (minor >= 2u) {
        uint16_t markSets;
        if (span.size < 14u || FontReadU16be(&r, &markSets) != RoboOk) {
            FontGdefClear(out);
            return RoboParseError;
        }
        if (markSets != 0u) {
            size_t abs;
            if (FontOtResolveOffset16(span, 0u, markSets, &abs) != RoboOk ||
                abs + 4u > span.size) {
                FontGdefClear(out);
                return RoboParseError;
            }
            out->markGlyphSetsOffset = abs;
            out->hasMarkGlyphSets = 1u;
        }
    }
    if (minor >= 3u) {
        uint32_t itemVar;
        if (span.size < 18u || FontReadU32be(&r, &itemVar) != RoboOk) {
            FontGdefClear(out);
            return RoboParseError;
        }
        if (itemVar != 0u) {
            size_t abs;
            if (FontOtResolveOffset32(span, 0u, itemVar, &abs) != RoboOk) {
                FontGdefClear(out);
                return RoboParseError;
            }
            out->itemVarStoreOffset = abs;
            out->hasItemVarStore = 1u;
        }
    }
    return RoboOk;
}

void FontOtLayoutClear(FontOtLayout *L)
{
    if (!L) {
        return;
    }
    FontOtScriptListClear(&L->scripts);
    FontOtFeatureListClear(&L->features);
    FontOtLookupListClear(&L->lookups);
    memset(L, 0, sizeof(*L));
}

static Result LayoutParse(FontSpan span,
                                  FontOtLayoutKind kind,
                                  FontOtLayout *out)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint16_t scriptOff;
    uint16_t featureOff;
    uint16_t lookupOff;
    size_t sabs;
    size_t fabs;
    size_t labs;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtLayoutClear(out);
    if (span.size < 10u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &scriptOff) != RoboOk ||
        FontReadU16be(&r, &featureOff) != RoboOk ||
        FontReadU16be(&r, &lookupOff) != RoboOk) {
        return RoboParseError;
    }
    if (major != 1u || minor > 1u) {
        return RoboNotSupported;
    }
    out->kind = kind;
    out->majorVersion = major;
    out->minorVersion = minor;
    out->table = span;

    if (FontOtResolveOffset16(span, 0u, scriptOff, &sabs) != RoboOk ||
        FontOtScriptListParse(span, sabs, &out->scripts) != RoboOk) {
        FontOtLayoutClear(out);
        return RoboParseError;
    }
    if (FontOtResolveOffset16(span, 0u, featureOff, &fabs) != RoboOk ||
        FontOtFeatureListParse(span, fabs, &out->features) != RoboOk) {
        FontOtLayoutClear(out);
        return RoboParseError;
    }
    if (FontOtResolveOffset16(span, 0u, lookupOff, &labs) != RoboOk ||
        FontOtLookupListParse(span, labs, &out->lookups) != RoboOk) {
        FontOtLayoutClear(out);
        return RoboParseError;
    }

    /* Validate lookup types are known for this table kind. */
    {
        uint16_t i;
        for (i = 0u; i < out->lookups.lookupCount; i++) {
            uint16_t t = out->lookups.lookups[i].lookupType;
            if (kind == RoboFontOtLayoutGsub) {
                if (!FontGsubLookupTypeIsKnown(t)) {
                    FontOtLayoutClear(out);
                    return RoboParseError;
                }
            } else if (!FontGposLookupTypeIsKnown(t)) {
                FontOtLayoutClear(out);
                return RoboParseError;
            }
        }
        /* Feature lookup indices must be in range. */
        for (i = 0u; i < out->features.featureCount; i++) {
            uint16_t j;
            for (j = 0u; j < out->features.features[i].lookupIndexCount; j++) {
                if (out->features.features[i].lookupIndices[j] >=
                    out->lookups.lookupCount) {
                    FontOtLayoutClear(out);
                    return RoboParseError;
                }
            }
        }
    }

    if (minor >= 1u) {
        uint32_t featVar;
        if (span.size < 14u || FontReadU32be(&r, &featVar) != RoboOk) {
            FontOtLayoutClear(out);
            return RoboParseError;
        }
        if (featVar != 0u) {
            size_t abs;
            if (FontOtResolveOffset32(span, 0u, featVar, &abs) != RoboOk) {
                FontOtLayoutClear(out);
                return RoboParseError;
            }
            out->featureVariationsOffset = abs;
        }
    }
    return RoboOk;
}

Result FontGsubParse(FontSpan span, FontOtLayout *out)
{
    return LayoutParse(span, RoboFontOtLayoutGsub, out);
}

Result FontGposParse(FontSpan span, FontOtLayout *out)
{
    return LayoutParse(span, RoboFontOtLayoutGpos, out);
}

int FontGsubLookupTypeIsKnown(uint16_t type)
{
    return type >= 1u && type <= 8u;
}

int FontGposLookupTypeIsKnown(uint16_t type)
{
    return type >= 1u && type <= 9u;
}
