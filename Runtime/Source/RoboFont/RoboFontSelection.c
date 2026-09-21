#include "RoboFont/RoboFontSelection.h"

#include "RoboFont/RoboFontVariation.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

static int SelectFloatOk(float v)
{
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return ((bits.u >> 23) & 0xFFu) != 0xFFu;
}

void FontSelectRequestInit(FontSelectRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->weight = RoboFontWeightNormal;
    req->stretch = RoboFontStretchNormal;
    req->style = RoboFontStyleNormal;
    req->flags = RoboFontSelectFlagAllowFullName;
}

Result FontSelectRequestValidate(const FontSelectRequest *req)
{
    size_t flen;

    if (!req) {
        return RoboInvalidArgument;
    }
    if (!req->family) {
        return RoboInvalidArgument;
    }
    flen = req->familyLen != 0u ? req->familyLen : strlen(req->family);
    if (flen == 0u || flen > RoboFontSelectMaxFamilyBytes) {
        return RoboInvalidArgument;
    }
    if (req->aliasCount > RoboFontSelectMaxAliases) {
        return RoboInvalidArgument;
    }
    if (req->aliasCount != 0u && !req->aliases) {
        return RoboInvalidArgument;
    }
    if (!FontWeightIsValid(req->weight)) {
        return RoboInvalidArgument;
    }
    if (!FontStretchIsValid(req->stretch)) {
        return RoboInvalidArgument;
    }
    if (req->style != RoboFontStyleNormal && req->style != RoboFontStyleItalic &&
        req->style != RoboFontStyleOblique && req->style != RoboFontStyleUnknown) {
        return RoboInvalidArgument;
    }
    if (req->axisCount > RoboFontStyleMaxAxes) {
        return RoboInvalidArgument;
    }
    {
        uint32_t i;
        for (i = 0u; i < req->axisCount; i++) {
            if (req->axes[i].tag == 0u || !SelectFloatOk(req->axes[i].value)) {
                return RoboInvalidArgument;
            }
        }
    }
    if (req->hasOpticalSize && !SelectFloatOk(req->opticalSize)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

void FontSelectionInit(FontSelection *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    FontStyleDescInit(&out->requested);
    FontStyleDescInit(&out->selected);
    out->cookie = RoboFontSelectionCookie;
}

void FontSelectionClear(FontSelection *sel)
{
    if (!sel) {
        return;
    }
    if (sel->cookie == RoboFontSelectionCookie && sel->face) {
        FontFaceRelease(sel->face);
        sel->face = NULL;
    }
    FontSelectionInit(sel);
}

uint32_t FontSelectStretchDist(FontStretch requested,
                                       FontStretch candidate)
{
    int d;
    uint32_t wrong = 0u;

    d = (int)candidate - (int)requested;
    if (d < 0) {
        d = -d;
    }
    if (requested < RoboFontStretchNormal) {
        /* Prefer condensed (≤ requested) before expanded. */
        if (candidate > requested) {
            wrong = 1u;
        }
    } else if (requested > RoboFontStretchNormal) {
        if (candidate < requested) {
            wrong = 1u;
        }
    }
    return wrong * 10000u + (uint32_t)d;
}

uint32_t FontSelectWeightDist(FontWeight requested,
                                      FontWeight candidate)
{
    int d;
    uint32_t wrong = 0u;

    d = (int)candidate - (int)requested;
    if (d < 0) {
        d = -d;
    }
    if (requested < 400u) {
        if (candidate > requested) {
            wrong = 1u;
        }
    } else if (requested > 500u) {
        if (candidate < requested) {
            wrong = 1u;
        }
    } else {
        /* 400..500: prefer lighter (≤ requested) first. */
        if (candidate > requested) {
            wrong = 1u;
        }
    }
    return wrong * 10000u + (uint32_t)d;
}

static uint32_t AbsI32(int32_t v)
{
    if (v < 0) {
        return (uint32_t)(-(int64_t)v);
    }
    return (uint32_t)v;
}

uint32_t FontSelectStyleDist(FontStyle requested,
                                     FontStyle candidate,
                                     uint8_t reqHasAngle,
                                     int32_t reqAngleMdeg,
                                     uint8_t candHasAngle,
                                     int32_t candAngleMdeg)
{
    uint32_t anglePen = 0u;

    if (reqHasAngle && candHasAngle) {
        anglePen = AbsI32(reqAngleMdeg - candAngleMdeg) / 100u; /* ~0.1 deg units */
        if (anglePen > 5000u) {
            anglePen = 5000u;
        }
    } else if (reqHasAngle != candHasAngle) {
        anglePen = 50u;
    }

    if (requested == RoboFontStyleUnknown) {
        requested = RoboFontStyleNormal;
    }
    if (candidate == RoboFontStyleUnknown) {
        candidate = RoboFontStyleNormal;
    }

    if (requested == RoboFontStyleNormal) {
        if (candidate == RoboFontStyleNormal) {
            return 0u;
        }
        if (candidate == RoboFontStyleOblique) {
            return 200u + anglePen;
        }
        return 300u; /* italic */
    }
    if (requested == RoboFontStyleItalic) {
        if (candidate == RoboFontStyleItalic) {
            return 0u;
        }
        if (candidate == RoboFontStyleOblique) {
            return 100u + anglePen;
        }
        return 1000u; /* normal */
    }
    /* OBLIQUE request */
    if (candidate == RoboFontStyleOblique) {
        return anglePen;
    }
    if (candidate == RoboFontStyleItalic) {
        return 150u;
    }
    return 1000u;
}

int FontMatchScoreCompare(const FontMatchScore *a,
                                  const FontMatchScore *b)
{
    if (!a || !b) {
        return a ? -1 : (b ? 1 : 0);
    }
    if (a->eligible != b->eligible) {
        return a->eligible ? -1 : 1; /* eligible better */
    }
    if (!a->eligible) {
        return (a->faceId < b->faceId) ? -1 : (a->faceId > b->faceId) ? 1 : 0;
    }
    if (a->familyRank != b->familyRank) {
        return (a->familyRank < b->familyRank) ? -1 : 1;
    }
    if (a->stretchDist != b->stretchDist) {
        return (a->stretchDist < b->stretchDist) ? -1 : 1;
    }
    if (a->styleDist != b->styleDist) {
        return (a->styleDist < b->styleDist) ? -1 : 1;
    }
    if (a->weightDist != b->weightDist) {
        return (a->weightDist < b->weightDist) ? -1 : 1;
    }
    /* More exact dimension bits are better (higher exactBits). */
    if (a->exactBits != b->exactBits) {
        return (a->exactBits > b->exactBits) ? -1 : 1;
    }
    if (a->wwsFlag != b->wwsFlag) {
        return (a->wwsFlag > b->wwsFlag) ? -1 : 1;
    }
    if (a->faceId != b->faceId) {
        return (a->faceId < b->faceId) ? -1 : 1;
    }
    return 0;
}

static size_t ReqFamilyLen(const FontSelectRequest *req)
{
    if (req->familyLen != 0u) {
        return req->familyLen;
    }
    return strlen(req->family);
}

static int Utf8CasefoldEq(const char *a, size_t al, const char *b, size_t bl)
{
    int cmp = 1;
    if (!a || !b) {
        return 0;
    }
    if (UnicodeCasefoldCompareUtf8(a, al, b, bl, RoboUnicodeReplace, &cmp) !=
        RoboOk) {
        return 0;
    }
    return cmp == 0;
}

static int FieldMatches(const FontMetaField *f, const char *name, size_t nlen)
{
    if (!f || f->avail != RoboFontNamePresent || !f->utf8) {
        return 0;
    }
    return Utf8CasefoldEq(f->utf8, f->utf8Len, name, nlen);
}

static int MatchNameAgainstRequest(const FontMetaField *f,
                                      const FontSelectRequest *req,
                                      int *outViaAlias)
{
    uint32_t i;
    size_t flen = ReqFamilyLen(req);

    *outViaAlias = 0;
    if (FieldMatches(f, req->family, flen)) {
        return 1;
    }
    for (i = 0u; i < req->aliasCount; i++) {
        const char *al = req->aliases[i];
        size_t alen;
        if (!al) {
            continue;
        }
        alen = strlen(al);
        if (alen == 0u || alen > RoboFontSelectMaxFamilyBytes) {
            continue;
        }
        if (FieldMatches(f, al, alen)) {
            *outViaAlias = 1;
            return 1;
        }
    }
    return 0;
}

static uint32_t FamilyKindRank(FontSelectFamilyKind kind, uint32_t flags)
{
    int preferWws = (flags & RoboFontSelectFlagPreferWws) != 0;

    switch (kind) {
    case RoboFontSelectFamilyTypographic:
        return preferWws ? 1u : 0u;
    case RoboFontSelectFamilyWws:
        return preferWws ? 0u : 1u;
    case RoboFontSelectFamilyLegacy:
        return 2u;
    case RoboFontSelectFamilyFull:
        return 3u;
    case RoboFontSelectFamilyPostscript:
        return 4u;
    case RoboFontSelectFamilyAlias:
        return 5u;
    default:
        return 100u;
    }
}

static void TryFamily(const FontMetaField *f,
                       FontSelectFamilyKind kind,
                       const FontSelectRequest *req,
                       FontSelectFamilyKind *bestKind,
                       uint32_t *bestRank,
                       int *found)
{
    int viaAlias = 0;
    uint32_t rank;
    FontSelectFamilyKind useKind;

    if (!MatchNameAgainstRequest(f, req, &viaAlias)) {
        return;
    }
    useKind = viaAlias ? RoboFontSelectFamilyAlias : kind;
    /* Alias on a stronger field still uses that field's kind for ranking when
     * the primary family string matched via alias — keep field kind unless
     * only alias path: we still prefer typographic/WWS/legacy kinds. */
    if (!viaAlias) {
        useKind = kind;
    } else {
        /* Alias matched this field: rank as the field kind (better than generic alias). */
        useKind = kind;
    }
    rank = FamilyKindRank(useKind, req->flags);
    if (!*found || rank < *bestRank) {
        *found = 1;
        *bestRank = rank;
        *bestKind = useKind;
    }
}

static Result ResolveFamily(const FontSelectRequest *req,
                                    const FontMetadata *meta,
                                    FontSelectFamilyKind *outKind,
                                    uint32_t *outRank)
{
    int found = 0;
    FontSelectFamilyKind kind = RoboFontSelectFamilyNone;
    uint32_t rank = 100u;

    if (!meta) {
        *outKind = RoboFontSelectFamilyNone;
        *outRank = 100u;
        return RoboOk;
    }

    TryFamily(&meta->typographicFamilyName, RoboFontSelectFamilyTypographic, req,
               &kind, &rank, &found);
    TryFamily(&meta->wwsFamilyName, RoboFontSelectFamilyWws, req, &kind, &rank,
               &found);
    TryFamily(&meta->familyName, RoboFontSelectFamilyLegacy, req, &kind, &rank,
               &found);
    if ((req->flags & RoboFontSelectFlagAllowFullName) != 0u) {
        TryFamily(&meta->fullName, RoboFontSelectFamilyFull, req, &kind, &rank,
                   &found);
    }
    if ((req->flags & RoboFontSelectFlagAllowPostscript) != 0u) {
        TryFamily(&meta->postscriptName, RoboFontSelectFamilyPostscript, req, &kind,
                   &rank, &found);
    }

    *outKind = found ? kind : RoboFontSelectFamilyNone;
    *outRank = found ? rank : 100u;
    return RoboOk;
}

Result FontSelectScoreFace(const FontSelectRequest *req,
                                          FontFace *face,
                                          FontMatchScore *outScore)
{
    const FontMetadata *meta;
    const FontClassification *cls;
    FontSelectFamilyKind fkind;
    uint32_t frank;
    Result res;

    if (!req || !face || !outScore) {
        return RoboInvalidArgument;
    }
    memset(outScore, 0, sizeof(*outScore));
    outScore->faceId = GetFontFaceId(face);

    res = FontSelectRequestValidate(req);
    if (res != RoboOk) {
        return res;
    }

    res = FontFaceParseMetadata(face);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceParseClassification(face);
    if (res != RoboOk) {
        return res;
    }

    meta = FontFaceMetadata(face);
    cls = FontFaceClassification(face);
    if (!cls) {
        return RoboParseError;
    }

    (void)ResolveFamily(req, meta, &fkind, &frank);
    if (fkind == RoboFontSelectFamilyNone) {
        outScore->eligible = 0u;
        return RoboOk;
    }

    outScore->eligible = 1u;
    outScore->familyKind = fkind;
    outScore->familyRank = frank;
    outScore->stretchDist =
        FontSelectStretchDist(req->stretch, cls->stretch);
    outScore->styleDist = FontSelectStyleDist(
        req->style, cls->style, req->hasObliqueAngle, req->obliqueAngleMillidegrees,
        cls->hasItalicAngle, cls->italicAngleMillidegrees);
    outScore->weightDist = FontSelectWeightDist(req->weight, cls->weight);
    outScore->exactBits = 0u;
    if (req->stretch == cls->stretch) {
        outScore->exactBits |= 1u;
    }
    if (req->style == cls->style ||
        (req->style == RoboFontStyleUnknown && cls->style == RoboFontStyleNormal)) {
        outScore->exactBits |= 2u;
    }
    if (req->weight == cls->weight) {
        outScore->exactBits |= 4u;
    }

    /* Variable-font axes can satisfy weight/stretch/style without another face. */
    {
        const FontVariationModel *vm = NULL;
        if (FontFaceParseVariation(face) == RoboOk) {
            vm = FontFaceVariation(face);
        }
        if (vm) {
            if (FontVariationCanSatisfyWeight(vm, req->weight)) {
                outScore->weightDist = 0u;
                outScore->exactBits |= 4u;
            }
            if (FontVariationCanSatisfyStretch(vm, req->stretch)) {
                outScore->stretchDist = 0u;
                outScore->exactBits |= 1u;
            }
            if (req->style != RoboFontStyleNormal &&
                FontVariationCanSatisfyStyle(vm, req->style)) {
                outScore->styleDist = 0u;
                outScore->exactBits |= 2u;
            }
            if (req->hasNamedInstance &&
                req->namedInstanceIndex <
                    FontVariationModelInstanceCount(vm)) {
                /* Prefer faces that expose the requested named instance. */
                outScore->exactBits |= 8u;
            }
        }
    }

    outScore->wwsFlag = cls->wwsFlag;
    return RoboOk;
}

Result FontSelect(const FontSelectRequest *req,
                               FontFace *const *candidates,
                               uint32_t candidateCount,
                               FontSelection *out)
{
    uint32_t i;
    FontFace *bestFace = NULL;
    FontMatchScore best;
    uint8_t haveBest = 0u;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    /* Accept zeroed or previously initialized out; never free a garbage face. */
    if (out->cookie == RoboFontSelectionCookie) {
        FontSelectionClear(out);
    } else {
        FontSelectionInit(out);
    }

    res = FontSelectRequestValidate(req);
    if (res != RoboOk) {
        return res;
    }
    if (candidateCount > RoboFontSelectMaxCandidates) {
        return RoboInvalidArgument;
    }
    if (candidateCount != 0u && !candidates) {
        return RoboInvalidArgument;
    }

    out->requested.weight = req->weight;
    out->requested.stretch = req->stretch;
    out->requested.style =
        (req->style == RoboFontStyleUnknown) ? RoboFontStyleNormal : req->style;
    out->requested.axisCount = req->axisCount;
    if (req->axisCount > 0u) {
        memcpy(out->requested.axes, req->axes,
               sizeof(FontAxisSetting) * req->axisCount);
    }

    memset(&best, 0, sizeof(best));

    for (i = 0u; i < candidateCount; i++) {
        FontFace *face = candidates[i];
        FontMatchScore score;

        if (!face) {
            continue;
        }
        res = FontSelectScoreFace(req, face, &score);
        if (res != RoboOk) {
            /* Skip malformed candidates rather than failing the whole select. */
            continue;
        }
        if (!score.eligible) {
            continue;
        }
        if (!haveBest || FontMatchScoreCompare(&score, &best) < 0) {
            best = score;
            bestFace = face;
            haveBest = 1u;
        }
    }

    if (!haveBest || !bestFace) {
        out->found = 0u;
        return RoboOk;
    }

    {
        const FontClassification *cls =
            FontFaceClassification(bestFace);
        FontFaceRetain(bestFace);
        out->found = 1u;
        out->face = bestFace;
        out->faceId = best.faceId;
        out->score = best;
        out->familyKind = best.familyKind;
        if (cls) {
            out->selected = cls->styleDesc;
            /* When axes satisfy the request, surface the requested numerics. */
            if (FontFaceVariation(bestFace)) {
                const FontVariationModel *vm =
                    FontFaceVariation(bestFace);
                if (FontVariationCanSatisfyWeight(vm, req->weight)) {
                    out->selected.weight = req->weight;
                }
                if (FontVariationCanSatisfyStretch(vm, req->stretch)) {
                    out->selected.stretch = req->stretch;
                }
                if (req->style != RoboFontStyleNormal &&
                    FontVariationCanSatisfyStyle(vm, req->style)) {
                    out->selected.style = req->style;
                }
            }
        }
    }
    return RoboOk;
}

Result FontSelectionFuzzParse(const uint8_t *data, size_t size)
{
    FontSelectRequest req;
    FontSelection sel;
    FontMatchScore score;
    char family[64];
    size_t n;
    size_t i;

    FontSelectRequestInit(&req);
    FontSelectionInit(&sel);
    memset(&score, 0, sizeof(score));

    if (!data) {
        return RoboInvalidArgument;
    }
    n = size < sizeof(family) - 1u ? size : sizeof(family) - 1u;
    for (i = 0u; i < n; i++) {
        uint8_t c = data[i];
        if (c < 0x20u || c > 0x7Eu) {
            c = (uint8_t)('A' + (c % 26u));
        }
        family[i] = (char)c;
    }
    family[n] = '\0';
    if (n == 0u) {
        family[0] = 'X';
        family[1] = '\0';
    }
    req.family = family;
    if (size >= 3u) {
        req.weight = (FontWeight)(1u + (data[0] % 1000u));
        req.stretch = (FontStretch)(50u + (data[1] % 151u));
        req.style = (FontStyle)(data[2] % 3u);
    }
    if (size >= 8u) {
        req.hasObliqueAngle = 1u;
        req.obliqueAngleMillidegrees =
            (int32_t)(((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                      ((uint32_t)data[6] << 8) | data[7]);
    }
    (void)FontSelectRequestValidate(&req);
    (void)FontSelect(&req, NULL, 0u, &sel);
    FontSelectionClear(&sel);

    /* Distance helpers with extreme values. */
    (void)FontSelectStretchDist(50u, 200u);
    (void)FontSelectWeightDist(1u, 1000u);
    (void)FontSelectStyleDist(RoboFontStyleItalic, RoboFontStyleOblique, 1u,
                                      -12000, 1u, 9000);
    (void)FontMatchScoreCompare(&score, &score);
    return RoboOk;
}
