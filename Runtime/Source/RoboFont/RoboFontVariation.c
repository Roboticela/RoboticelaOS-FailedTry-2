#include "RoboFont/RoboFontVariation.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define MODEL_COOKIE 0x56524D31u /* 'VRM1' */
#define COORDS_COOKIE 0x56434F31u /* 'VCO1' */

typedef struct FontAvarSegment {
    float fromCoord;
    float toCoord;
} FontAvarSegment;

typedef struct FontAvarAxis {
    uint16_t segmentCount;
    FontAvarSegment *segments; /* owned by model */
} FontAvarAxis;

struct FontVariationModel {
    uint32_t cookie;
    uint32_t refs;
    uint32_t capabilities;
    uint16_t axisCount;
    uint16_t instanceCount;
    uint16_t instanceSize;
    uint8_t hasAvar;
    uint8_t hasPsNameId;
    FontVariationAxis *axes;
    FontVariationInstance *instances;
    float *instanceCoordBlob; /* instanceCount * axisCount */
    FontAvarAxis *avar; /* axisCount when hasAvar */
    uint16_t statAxisCount;
    FontTag *statTags;
};

struct FontVariationCoords {
    uint32_t cookie;
    uint32_t refs;
    uint16_t axisCount;
    float *user;
    float *normalized;
    float *adjusted;
    const FontVariationModel *model; /* retained */
};

static float FixedToFloat(int32_t v)
{
    return (float)((double)v / 65536.0);
}

static float F2dot14ToFloat(int16_t v)
{
    return (float)((double)v / 16384.0);
}

static int FloatOk(float v)
{
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return ((bits.u >> 23) & 0xFFu) != 0xFFu;
}

FontVarAxisKind FontVariationAxisKind(FontTag tag)
{
    if (tag == RoboFontTagWght || tag == RoboFontTagWdth ||
        tag == RoboFontTagItal || tag == RoboFontTagSlnt ||
        tag == RoboFontTagOpsz) {
        return RoboFontVarAxisRegistered;
    }
    if (tag == 0u) {
        return RoboFontVarAxisUnknown;
    }
    return RoboFontVarAxisCustom;
}

int FontVariationTagIsRegistered(FontTag tag)
{
    return FontVariationAxisKind(tag) == RoboFontVarAxisRegistered;
}

Result FontVariationClampUser(float minV,
                                             float defV,
                                             float maxV,
                                             float requested,
                                             float *outClamped)
{
    float lo;
    float hi;
    float v;

    (void)defV;
    if (!outClamped || !FloatOk(minV) || !FloatOk(maxV) || !FloatOk(requested)) {
        return RoboInvalidArgument;
    }
    lo = minV < maxV ? minV : maxV;
    hi = minV < maxV ? maxV : minV;
    v = requested;
    if (v < lo) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }
    *outClamped = v;
    return RoboOk;
}

Result FontVariationNormalize(float minV,
                                            float defV,
                                            float maxV,
                                            float user,
                                            float *outNorm)
{
    float lo;
    float hi;
    float v;
    float den;

    if (!outNorm || !FloatOk(minV) || !FloatOk(defV) || !FloatOk(maxV) ||
        !FloatOk(user)) {
        return RoboInvalidArgument;
    }
    lo = minV < maxV ? minV : maxV;
    hi = minV < maxV ? maxV : minV;
    v = user;
    if (v < lo) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }
    if (v == defV) {
        *outNorm = 0.0f;
        return RoboOk;
    }
    if (v < defV) {
        den = defV - minV;
        if (den == 0.0f || !FloatOk(den)) {
            *outNorm = 0.0f;
            return RoboOk;
        }
        *outNorm = (v - defV) / den;
        return RoboOk;
    }
    den = maxV - defV;
    if (den == 0.0f || !FloatOk(den)) {
        *outNorm = 0.0f;
        return RoboOk;
    }
    *outNorm = (v - defV) / den;
    return RoboOk;
}

void FontVariationRequestInit(FontVariationRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->namedInstanceIndex = 0xFFFFFFFFu;
    req->weight = RoboFontWeightNormal;
    req->stretch = RoboFontStretchNormal;
    req->style = RoboFontStyleNormal;
    req->applyStyleAxes = 1u;
}

void FontOutlineRequestInit(FontOutlineRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
}

void FontMetricsVariationContextInit(FontMetricsVariationContext *ctx)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = RoboFontMetricsVarStatic;
}

void FontMetricsVariationContextSet(FontMetricsVariationContext *ctx,
                                             const FontVariationCoords *coords)
{
    if (!ctx) {
        return;
    }
    ctx->coords = coords;
    ctx->state = coords ? RoboFontMetricsVarPending : RoboFontMetricsVarStatic;
}

static void ModelFree(FontVariationModel *m)
{
    uint32_t i;
    if (!m) {
        return;
    }
    if (m->instances) {
        for (i = 0u; i < m->instanceCount; i++) {
            m->instances[i].userCoords = NULL;
        }
        RoboFree(m->instances);
    }
    if (m->instanceCoordBlob) {
        RoboFree(m->instanceCoordBlob);
    }
    if (m->avar) {
        for (i = 0u; i < m->axisCount; i++) {
            if (m->avar[i].segments) {
                RoboFree(m->avar[i].segments);
            }
        }
        RoboFree(m->avar);
    }
    if (m->axes) {
        RoboFree(m->axes);
    }
    if (m->statTags) {
        RoboFree(m->statTags);
    }
    memset(m, 0, sizeof(*m));
    RoboFree(m);
}

void FontVariationModelRetain(FontVariationModel *m)
{
    if (!m || m->cookie != MODEL_COOKIE) {
        return;
    }
    m->refs++;
}

void FontVariationModelRelease(FontVariationModel *m)
{
    if (!m || m->cookie != MODEL_COOKIE) {
        return;
    }
    if (m->refs == 0u) {
        return;
    }
    m->refs--;
    if (m->refs != 0u) {
        return;
    }
    m->cookie = 0u;
    ModelFree(m);
}

static void CoordsFree(FontVariationCoords *c)
{
    if (!c) {
        return;
    }
    if (c->model) {
        FontVariationModelRelease((FontVariationModel *)c->model);
    }
    if (c->user) {
        RoboFree(c->user);
    }
    if (c->normalized) {
        RoboFree(c->normalized);
    }
    if (c->adjusted) {
        RoboFree(c->adjusted);
    }
    memset(c, 0, sizeof(*c));
    RoboFree(c);
}

void FontVariationCoordsRetain(FontVariationCoords *c)
{
    if (!c || c->cookie != COORDS_COOKIE) {
        return;
    }
    c->refs++;
}

void FontVariationCoordsRelease(FontVariationCoords *c)
{
    if (!c || c->cookie != COORDS_COOKIE) {
        return;
    }
    if (c->refs == 0u) {
        return;
    }
    c->refs--;
    if (c->refs != 0u) {
        return;
    }
    c->cookie = 0u;
    CoordsFree(c);
}

static Result ResolveName(const FontNameInfo *nameOpt,
                                  uint16_t nameId,
                                  char *out,
                                  uint8_t *outLen)
{
    const FontNameEntry *entry = NULL;
    FontNameLookupKind kind;
    size_t n = 0u;
    Result r;

    out[0] = '\0';
    *outLen = 0u;
    if (!nameOpt || nameId == 0xFFFFu) {
        return RoboOk;
    }
    r = FontNameInfoLookup(nameOpt, nameId, NULL, &entry, &kind);
    if (r != RoboOk || !entry || entry->decodeStatus != RoboFontNamePresent ||
        !entry->utf8) {
        return RoboOk;
    }
    n = entry->utf8Len;
    if (n >= RoboFontVarMaxNameBytes) {
        n = RoboFontVarMaxNameBytes - 1u;
    }
    memcpy(out, entry->utf8, n);
    out[n] = '\0';
    *outLen = (uint8_t)n;
    (void)kind;
    return RoboOk;
}

static int AxisRangeOk(float minV, float defV, float maxV)
{
    float lo;
    float hi;
    if (!FloatOk(minV) || !FloatOk(defV) || !FloatOk(maxV)) {
        return 0;
    }
    lo = minV < maxV ? minV : maxV;
    hi = minV < maxV ? maxV : minV;
    return defV >= lo && defV <= hi;
}

static Result ParseAvar(FontSpan span,
                                uint16_t axisCount,
                                FontAvarAxis **outAvar)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint16_t reserved;
    uint16_t acount;
    uint16_t ai;
    FontAvarAxis *avar;
    size_t i;

    *outAvar = NULL;
    if (span.size < 8u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &reserved) != RoboOk ||
        FontReadU16be(&r, &acount) != RoboOk) {
        return RoboParseError;
    }
    (void)minor;
    (void)reserved;
    if (major != 1u || acount != axisCount) {
        return RoboParseError;
    }
    avar = (FontAvarAxis *)RoboCalloc(axisCount, sizeof(*avar));
    if (!avar) {
        return RoboOutOfMemory;
    }
    for (ai = 0u; ai < axisCount; ai++) {
        uint16_t nseg;
        uint16_t s;
        if (FontReadU16be(&r, &nseg) != RoboOk) {
            goto fail;
        }
        if (nseg > RoboFontVarMaxAvarSegments) {
            goto fail;
        }
        avar[ai].segmentCount = nseg;
        if (nseg == 0u) {
            continue;
        }
        avar[ai].segments =
            (FontAvarSegment *)RoboCalloc(nseg, sizeof(FontAvarSegment));
        if (!avar[ai].segments) {
            goto fail;
        }
        for (s = 0u; s < nseg; s++) {
            int16_t fromI;
            int16_t toI;
            if (FontReadI16be(&r, &fromI) != RoboOk ||
                FontReadI16be(&r, &toI) != RoboOk) {
                goto fail;
            }
            avar[ai].segments[s].fromCoord = F2dot14ToFloat(fromI);
            avar[ai].segments[s].toCoord = F2dot14ToFloat(toI);
            if (!FloatOk(avar[ai].segments[s].fromCoord) ||
                !FloatOk(avar[ai].segments[s].toCoord)) {
                goto fail;
            }
            if (s > 0u &&
                avar[ai].segments[s].fromCoord < avar[ai].segments[s - 1u].fromCoord) {
                /* Non-monotonic — reject mapping for this axis (identity later). */
                RoboFree(avar[ai].segments);
                avar[ai].segments = NULL;
                avar[ai].segmentCount = 0u;
                /* Skip remaining pairs for this axis. */
                for (s = (uint16_t)(s + 1u); s < nseg; s++) {
                    int16_t junk;
                    if (FontReadI16be(&r, &junk) != RoboOk ||
                        FontReadI16be(&r, &junk) != RoboOk) {
                        goto fail;
                    }
                }
                break;
            }
            if (s > 0u &&
                avar[ai].segments[s].fromCoord == avar[ai].segments[s - 1u].fromCoord) {
                /* Duplicate fromCoord — drop axis map. */
                RoboFree(avar[ai].segments);
                avar[ai].segments = NULL;
                avar[ai].segmentCount = 0u;
                for (s = (uint16_t)(s + 1u); s < nseg; s++) {
                    int16_t junk;
                    if (FontReadI16be(&r, &junk) != RoboOk ||
                        FontReadI16be(&r, &junk) != RoboOk) {
                        goto fail;
                    }
                }
                break;
            }
        }
    }
    *outAvar = avar;
    return RoboOk;

fail:
    if (avar) {
        for (i = 0u; i < axisCount; i++) {
            if (avar[i].segments) {
                RoboFree(avar[i].segments);
            }
        }
        RoboFree(avar);
    }
    return RoboParseError;
}

static float AvarMapSegments(const FontAvarAxis *ax, float value)
{
    uint16_t i;
    uint16_t n;
    const FontAvarSegment *map;

    if (!ax || ax->segmentCount == 0u || !ax->segments) {
        return value;
    }
    n = ax->segmentCount;
    map = ax->segments;
    if (n < 2u) {
        if (n == 1u) {
            return value - map[0].fromCoord + map[0].toCoord;
        }
        return value;
    }
    for (i = 0u; i < n; i++) {
        if (value == map[i].fromCoord) {
            return map[i].toCoord;
        }
    }
    if (value <= map[0].fromCoord) {
        float den = map[1].fromCoord - map[0].fromCoord;
        if (den == 0.0f) {
            return map[0].toCoord;
        }
        return map[0].toCoord +
               (value - map[0].fromCoord) * (map[1].toCoord - map[0].toCoord) / den;
    }
    if (value >= map[n - 1u].fromCoord) {
        float den = map[n - 1u].fromCoord - map[n - 2u].fromCoord;
        if (den == 0.0f) {
            return map[n - 1u].toCoord;
        }
        return map[n - 2u].toCoord +
               (value - map[n - 2u].fromCoord) *
                   (map[n - 1u].toCoord - map[n - 2u].toCoord) / den;
    }
    for (i = 1u; i < n; i++) {
        if (value <= map[i].fromCoord) {
            float den = map[i].fromCoord - map[i - 1u].fromCoord;
            if (den == 0.0f) {
                return map[i].toCoord;
            }
            return map[i - 1u].toCoord +
                   (value - map[i - 1u].fromCoord) *
                       (map[i].toCoord - map[i - 1u].toCoord) / den;
        }
    }
    return value;
}

Result FontVariationAvarMap(const FontVariationModel *m,
                                           uint32_t axisIndex,
                                           float normalized,
                                           float *outAdjusted)
{
    if (!m || m->cookie != MODEL_COOKIE || !outAdjusted || !FloatOk(normalized)) {
        return RoboInvalidArgument;
    }
    if (axisIndex >= m->axisCount) {
        return RoboInvalidArgument;
    }
    if (!m->hasAvar || !m->avar) {
        *outAdjusted = normalized;
        return RoboOk;
    }
    *outAdjusted = AvarMapSegments(&m->avar[axisIndex], normalized);
    if (!FloatOk(*outAdjusted)) {
        *outAdjusted = normalized;
    }
    return RoboOk;
}

Result FontVariationModelCreate(FontSfnt *sfnt,
                                               const FontStat *statOpt,
                                               const FontNameInfo *nameOpt,
                                               FontVariationModel **out)
{
    FontSpan fvarSpan;
    FontSpan avarSpan;
    FontReader r;
    FontVariationModel *m = NULL;
    uint16_t major;
    uint16_t minor;
    uint16_t axesOff;
    uint16_t reserved;
    uint16_t axisCount;
    uint16_t axisSize;
    uint16_t instanceCount;
    uint16_t instanceSize;
    uint16_t i;
    uint32_t j;
    Result res;
    int hasGvar = 0;
    int hasHvar = 0;
    int hasMvar = 0;
    int hasCff2 = 0;
    uint32_t caps = 0u;

    if (!out || !sfnt) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    res = FontSfntTableBytes(sfnt, RoboFontTagFvar, &fvarSpan);
    if (res != RoboOk) {
        return RoboNotFound;
    }
    if (fvarSpan.size < 16u) {
        return RoboParseError;
    }

    hasGvar = FontSfntHasTable(sfnt, RoboFontTagGvar);
    hasHvar = FontSfntHasTable(sfnt, RoboFontTagHvar);
    hasMvar = FontSfntHasTable(sfnt, RoboFontTagMvar);
    hasCff2 = FontSfntHasTable(sfnt, RoboFontTagCff2);

    FontReaderInitSpan(&r, fvarSpan);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &axesOff) != RoboOk ||
        FontReadU16be(&r, &reserved) != RoboOk ||
        FontReadU16be(&r, &axisCount) != RoboOk ||
        FontReadU16be(&r, &axisSize) != RoboOk ||
        FontReadU16be(&r, &instanceCount) != RoboOk ||
        FontReadU16be(&r, &instanceSize) != RoboOk) {
        return RoboParseError;
    }
    (void)minor;
    (void)reserved;
    if (major != 1u || axisCount == 0u || axisCount > RoboFontVarMaxAxes ||
        axisSize < 20u || instanceCount > RoboFontVarMaxInstances) {
        return RoboParseError;
    }
    if ((size_t)axesOff + (size_t)axisCount * (size_t)axisSize > fvarSpan.size) {
        return RoboParseError;
    }
    {
        size_t need = 4u + (size_t)axisCount * 4u;
        if (instanceSize < need) {
            return RoboParseError;
        }
    }

    m = (FontVariationModel *)RoboCalloc(1, sizeof(*m));
    if (!m) {
        return RoboOutOfMemory;
    }
    m->cookie = MODEL_COOKIE;
    m->refs = 1u;
    m->axisCount = axisCount;
    m->instanceCount = instanceCount;
    m->instanceSize = instanceSize;
    m->hasPsNameId = (instanceSize >= (uint16_t)(4u + axisCount * 4u + 2u)) ? 1u : 0u;
    m->axes = (FontVariationAxis *)RoboCalloc(axisCount, sizeof(*m->axes));
    if (!m->axes) {
        ModelFree(m);
        return RoboOutOfMemory;
    }

    for (i = 0u; i < axisCount; i++) {
        size_t off = (size_t)axesOff + (size_t)i * (size_t)axisSize;
        uint32_t tag;
        int32_t minF;
        int32_t defF;
        int32_t maxF;
        uint16_t flags;
        uint16_t nameId;
        FontReader ar;

        if (FontReaderSeek(&r, off) != RoboOk) {
            ModelFree(m);
            return RoboParseError;
        }
        ar = r;
        if (FontReadU32be(&ar, &tag) != RoboOk ||
            FontReadI32be(&ar, &minF) != RoboOk ||
            FontReadI32be(&ar, &defF) != RoboOk ||
            FontReadI32be(&ar, &maxF) != RoboOk ||
            FontReadU16be(&ar, &flags) != RoboOk ||
            FontReadU16be(&ar, &nameId) != RoboOk) {
            ModelFree(m);
            return RoboParseError;
        }
        for (j = 0u; j < i; j++) {
            if (m->axes[j].tag == tag) {
                ModelFree(m);
                return RoboParseError;
            }
        }
        m->axes[i].tag = tag;
        m->axes[i].minValue = FixedToFloat(minF);
        m->axes[i].defaultValue = FixedToFloat(defF);
        m->axes[i].maxValue = FixedToFloat(maxF);
        m->axes[i].flags = flags;
        m->axes[i].nameId = nameId;
        m->axes[i].kind = FontVariationAxisKind(tag);
        if (!AxisRangeOk(m->axes[i].minValue, m->axes[i].defaultValue,
                           m->axes[i].maxValue)) {
            ModelFree(m);
            return RoboParseError;
        }
        (void)ResolveName(nameOpt, nameId, m->axes[i].nameUtf8, &m->axes[i].nameLen);
    }

    if (instanceCount > 0u) {
        size_t instBase = (size_t)axesOff + (size_t)axisCount * (size_t)axisSize;
        size_t blobN = (size_t)instanceCount * (size_t)axisCount;

        if (instBase + (size_t)instanceCount * (size_t)instanceSize > fvarSpan.size) {
            ModelFree(m);
            return RoboParseError;
        }
        m->instances =
            (FontVariationInstance *)RoboCalloc(instanceCount, sizeof(*m->instances));
        m->instanceCoordBlob = (float *)RoboCalloc(blobN, sizeof(float));
        if (!m->instances || !m->instanceCoordBlob) {
            ModelFree(m);
            return RoboOutOfMemory;
        }
        for (i = 0u; i < instanceCount; i++) {
            size_t off = instBase + (size_t)i * (size_t)instanceSize;
            uint16_t subId;
            uint16_t iflags;
            uint16_t a;
            FontReader ir;

            if (FontReaderSeek(&r, off) != RoboOk) {
                ModelFree(m);
                return RoboParseError;
            }
            ir = r;
            if (FontReadU16be(&ir, &subId) != RoboOk ||
                FontReadU16be(&ir, &iflags) != RoboOk) {
                ModelFree(m);
                return RoboParseError;
            }
            m->instances[i].index = i;
            m->instances[i].subfamilyNameId = subId;
            m->instances[i].flags = iflags;
            m->instances[i].postscriptNameId = 0xFFFFu;
            m->instances[i].userCoords =
                m->instanceCoordBlob + (size_t)i * (size_t)axisCount;
            for (a = 0u; a < axisCount; a++) {
                int32_t cf;
                if (FontReadI32be(&ir, &cf) != RoboOk) {
                    ModelFree(m);
                    return RoboParseError;
                }
                m->instances[i].userCoords[a] = FixedToFloat(cf);
                if (!FloatOk(m->instances[i].userCoords[a])) {
                    ModelFree(m);
                    return RoboParseError;
                }
            }
            if (m->hasPsNameId) {
                uint16_t psid;
                if (FontReadU16be(&ir, &psid) == RoboOk) {
                    m->instances[i].postscriptNameId = psid;
                }
            }
            (void)ResolveName(nameOpt, subId, m->instances[i].nameUtf8,
                               &m->instances[i].nameLen);
        }
        caps |= RoboFontVarCapNamedInstances;
    }

    caps |= RoboFontVarCapFvar;
    if (hasGvar) {
        caps |= RoboFontVarCapGvar;
    }
    if (hasHvar) {
        caps |= RoboFontVarCapHvar;
    }
    if (hasMvar) {
        caps |= RoboFontVarCapMvar;
    }
    if (hasCff2) {
        caps |= RoboFontVarCapCff2;
    }

    if (FontSfntTableBytes(sfnt, RoboFontTagAvar, &avarSpan) == RoboOk) {
        FontAvarAxis *avar = NULL;
        if (ParseAvar(avarSpan, axisCount, &avar) == RoboOk && avar) {
            m->avar = avar;
            m->hasAvar = 1u;
            caps |= RoboFontVarCapAvar;
        }
        /* Malformed avar: keep fvar, ignore avar. */
    }

    if (statOpt && statOpt->axes && statOpt->designAxisCount > 0u) {
        uint16_t sc = statOpt->designAxisCount;
        uint16_t k;
        m->statTags = (FontTag *)RoboCalloc(sc, sizeof(FontTag));
        if (m->statTags) {
            m->statAxisCount = sc;
            for (k = 0u; k < sc; k++) {
                m->statTags[k] = statOpt->axes[k].tag;
            }
            caps |= RoboFontVarCapStat;
        }
    }

    m->capabilities = caps;
    *out = m;
    return RoboOk;
}

Result FontVariationModelCreateFromFace(FontFace *face,
                                                         FontVariationModel **out)
{
    const FontSfnt *sfnt;
    const FontNameInfo *name = NULL;
    FontStat stat;
    FontStat *statPtr = NULL;
    FontOt *ot = NULL;
    Result res;
    FontSpan span;

    if (!face || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    res = FontFaceParseSfnt(face);
    if (res == RoboNotSupported || res == RoboParseError || res == RoboNotFound) {
        /* No usable SFNT → not a variable face. */
        return RoboNotFound;
    }
    if (res != RoboOk) {
        return res;
    }
    sfnt = FontFaceSfnt(face);
    if (!sfnt) {
        return RoboParseError;
    }
    (void)FontFaceParseMetadata(face);
    name = FontFaceNameInfo(face);

    memset(&stat, 0, sizeof(stat));
    if (FontSfntTableBytes((FontSfnt *)sfnt, RoboFontTagStat, &span) ==
        RoboOk) {
        if (FontStatParse(span, &stat) == RoboOk) {
            statPtr = &stat;
        }
    }
    (void)ot;

    res = FontVariationModelCreate((FontSfnt *)sfnt, statPtr, name, out);
    if (statPtr) {
        FontStatClear(&stat);
    }
    return res;
}

uint32_t FontVariationModelCapabilities(const FontVariationModel *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->capabilities : 0u;
}

int FontVariationModelIsVariable(const FontVariationModel *m)
{
    return m && m->cookie == MODEL_COOKIE && m->axisCount > 0u;
}

uint32_t FontVariationModelAxisCount(const FontVariationModel *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->axisCount : 0u;
}

const FontVariationAxis *FontVariationModelAxis(
    const FontVariationModel *m,
    uint32_t index)
{
    if (!m || m->cookie != MODEL_COOKIE || index >= m->axisCount) {
        return NULL;
    }
    return &m->axes[index];
}

Result FontVariationModelFindAxis(const FontVariationModel *m,
                                                  FontTag tag,
                                                  uint32_t *outIndex,
                                                  const FontVariationAxis **outAxis)
{
    uint32_t i;
    if (!m || m->cookie != MODEL_COOKIE) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < m->axisCount; i++) {
        if (m->axes[i].tag == tag) {
            if (outIndex) {
                *outIndex = i;
            }
            if (outAxis) {
                *outAxis = &m->axes[i];
            }
            return RoboOk;
        }
    }
    return RoboNotFound;
}

uint32_t FontVariationModelInstanceCount(const FontVariationModel *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->instanceCount : 0u;
}

const FontVariationInstance *FontVariationModelInstance(
    const FontVariationModel *m,
    uint32_t index)
{
    if (!m || m->cookie != MODEL_COOKIE || index >= m->instanceCount || !m->instances) {
        return NULL;
    }
    return &m->instances[index];
}

int FontVariationModelHasAvar(const FontVariationModel *m)
{
    return m && m->cookie == MODEL_COOKIE && m->hasAvar;
}

uint32_t FontVariationModelStatAxisCount(const FontVariationModel *m)
{
    return m && m->cookie == MODEL_COOKIE ? m->statAxisCount : 0u;
}

FontTag FontVariationModelStatAxisTag(const FontVariationModel *m,
                                                        uint32_t index)
{
    if (!m || m->cookie != MODEL_COOKIE || index >= m->statAxisCount || !m->statTags) {
        return 0u;
    }
    return m->statTags[index];
}

static void SetAxisUser(float *user,
                          const FontVariationModel *m,
                          uint32_t axisI,
                          float v)
{
    float clamped = m->axes[axisI].defaultValue;
    (void)FontVariationClampUser(m->axes[axisI].minValue,
                                         m->axes[axisI].defaultValue,
                                         m->axes[axisI].maxValue, v, &clamped);
    user[axisI] = clamped;
}

static Result CoordsFinish(FontVariationCoords *c)
{
    uint32_t i;
    for (i = 0u; i < c->axisCount; i++) {
        float n = 0.0f;
        float a = 0.0f;
        if (FontVariationNormalize(c->model->axes[i].minValue,
                                          c->model->axes[i].defaultValue,
                                          c->model->axes[i].maxValue, c->user[i],
                                          &n) != RoboOk) {
            return RoboInvalidArgument;
        }
        c->normalized[i] = n;
        if (FontVariationAvarMap(c->model, i, n, &a) != RoboOk) {
            a = n;
        }
        c->adjusted[i] = a;
    }
    return RoboOk;
}

Result FontVariationCoordsCreateDefault(
    const FontVariationModel *model,
    FontVariationCoords **out)
{
    FontVariationRequest req;
    FontVariationRequestInit(&req);
    req.applyStyleAxes = 0u;
    return FontVariationCoordsCreate(model, &req, out);
}

Result FontVariationCoordsCreate(const FontVariationModel *model,
                                                const FontVariationRequest *req,
                                                FontVariationCoords **out)
{
    FontVariationCoords *c;
    uint32_t i;
    uint32_t a;

    if (!model || model->cookie != MODEL_COOKIE || !req || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    c = (FontVariationCoords *)RoboCalloc(1, sizeof(*c));
    if (!c) {
        return RoboOutOfMemory;
    }
    c->cookie = COORDS_COOKIE;
    c->refs = 1u;
    c->axisCount = model->axisCount;
    c->user = (float *)RoboCalloc(model->axisCount, sizeof(float));
    c->normalized = (float *)RoboCalloc(model->axisCount, sizeof(float));
    c->adjusted = (float *)RoboCalloc(model->axisCount, sizeof(float));
    if (!c->user || !c->normalized || !c->adjusted) {
        CoordsFree(c);
        return RoboOutOfMemory;
    }
    FontVariationModelRetain((FontVariationModel *)model);
    c->model = model;

    for (i = 0u; i < model->axisCount; i++) {
        c->user[i] = model->axes[i].defaultValue;
    }

    if (req->hasNamedInstance) {
        const FontVariationInstance *inst;
        if (req->namedInstanceIndex >= model->instanceCount || !model->instances) {
            CoordsFree(c);
            return RoboNotFound;
        }
        inst = &model->instances[req->namedInstanceIndex];
        for (i = 0u; i < model->axisCount; i++) {
            SetAxisUser(c->user, model, i, inst->userCoords[i]);
        }
    }

    if (req->applyStyleAxes) {
        uint32_t idx;
        const FontVariationAxis *ax;
        if (FontVariationModelFindAxis(model, RoboFontTagWght, &idx, &ax) ==
            RoboOk) {
            SetAxisUser(c->user, model, idx, (float)req->weight);
        }
        if (FontVariationModelFindAxis(model, RoboFontTagWdth, &idx, &ax) ==
            RoboOk) {
            SetAxisUser(c->user, model, idx, (float)req->stretch);
        }
        if (req->style == RoboFontStyleItalic &&
            FontVariationModelFindAxis(model, RoboFontTagItal, &idx, &ax) ==
                RoboOk) {
            SetAxisUser(c->user, model, idx, ax->maxValue);
        } else if (req->style == RoboFontStyleOblique &&
                   FontVariationModelFindAxis(model, RoboFontTagSlnt, &idx,
                                                       &ax) == RoboOk) {
            /* Prefer negative slant when range spans negative values. */
            float slant = ax->minValue < 0.0f ? ax->minValue : ax->maxValue;
            SetAxisUser(c->user, model, idx, slant);
        } else if (req->style == RoboFontStyleNormal &&
                   FontVariationModelFindAxis(model, RoboFontTagItal, &idx,
                                                       &ax) == RoboOk) {
            SetAxisUser(c->user, model, idx, ax->minValue);
        }
    }

    if (req->hasOpticalSize || req->opticalSizeAuto) {
        uint32_t idx;
        const FontVariationAxis *ax;
        if (FontVariationModelFindAxis(model, RoboFontTagOpsz, &idx, &ax) ==
            RoboOk) {
            float v = req->hasOpticalSize ? req->opticalSize : ax->defaultValue;
            SetAxisUser(c->user, model, idx, v);
        }
    }

    if (req->axes && req->axisCount > 0u) {
        for (a = 0u; a < req->axisCount; a++) {
            uint32_t idx;
            if (req->axes[a].tag == 0u || !FloatOk(req->axes[a].value)) {
                continue;
            }
            if (FontVariationModelFindAxis(model, req->axes[a].tag, &idx, NULL) ==
                RoboOk) {
                SetAxisUser(c->user, model, idx, req->axes[a].value);
            }
        }
    }

    if (CoordsFinish(c) != RoboOk) {
        CoordsFree(c);
        return RoboInvalidArgument;
    }
    *out = c;
    return RoboOk;
}

uint32_t FontVariationCoordsAxisCount(const FontVariationCoords *c)
{
    return c && c->cookie == COORDS_COOKIE ? c->axisCount : 0u;
}

float FontVariationCoordsUser(const FontVariationCoords *c, uint32_t i)
{
    if (!c || c->cookie != COORDS_COOKIE || i >= c->axisCount || !c->user) {
        return 0.0f;
    }
    return c->user[i];
}

float FontVariationCoordsNormalized(const FontVariationCoords *c,
                                            uint32_t i)
{
    if (!c || c->cookie != COORDS_COOKIE || i >= c->axisCount || !c->normalized) {
        return 0.0f;
    }
    return c->normalized[i];
}

float FontVariationCoordsAdjusted(const FontVariationCoords *c, uint32_t i)
{
    if (!c || c->cookie != COORDS_COOKIE || i >= c->axisCount || !c->adjusted) {
        return 0.0f;
    }
    return c->adjusted[i];
}

Result FontVariationCoordsUserForTag(const FontVariationCoords *c,
                                                      FontTag tag,
                                                      float *out)
{
    uint32_t idx;
    if (!c || c->cookie != COORDS_COOKIE || !out || !c->model) {
        return RoboInvalidArgument;
    }
    if (FontVariationModelFindAxis(c->model, tag, &idx, NULL) != RoboOk) {
        return RoboNotFound;
    }
    *out = c->user[idx];
    return RoboOk;
}

void FontVariationRequestFromStyle(const FontStyleDesc *style,
                                            float opticalSize,
                                            uint8_t hasOpticalSize,
                                            uint8_t opticalSizeAuto,
                                            uint32_t namedInstanceIndex,
                                            uint8_t hasNamedInstance,
                                            FontVariationRequest *out)
{
    FontVariationRequestInit(out);
    if (!style || !out) {
        return;
    }
    out->applyStyleAxes = 1u;
    out->weight = style->weight;
    out->stretch = style->stretch;
    out->style = style->style;
    out->axes = style->axes;
    out->axisCount = style->axisCount;
    out->hasOpticalSize = hasOpticalSize;
    out->opticalSize = opticalSize;
    out->opticalSizeAuto = opticalSizeAuto;
    out->hasNamedInstance = hasNamedInstance;
    out->namedInstanceIndex = namedInstanceIndex;
}

int FontVariationCanSatisfyWeight(const FontVariationModel *m,
                                           FontWeight weight)
{
    const FontVariationAxis *ax;
    float w = (float)weight;
    if (FontVariationModelFindAxis(m, RoboFontTagWght, NULL, &ax) != RoboOk) {
        return 0;
    }
    return w >= ax->minValue && w <= ax->maxValue;
}

int FontVariationCanSatisfyStretch(const FontVariationModel *m,
                                            FontStretch stretch)
{
    const FontVariationAxis *ax;
    float s = (float)stretch;
    if (FontVariationModelFindAxis(m, RoboFontTagWdth, NULL, &ax) != RoboOk) {
        return 0;
    }
    return s >= ax->minValue && s <= ax->maxValue;
}

int FontVariationCanSatisfyStyle(const FontVariationModel *m,
                                          FontStyle style)
{
    if (style == RoboFontStyleItalic) {
        return FontVariationModelFindAxis(m, RoboFontTagItal, NULL, NULL) ==
               RoboOk;
    }
    if (style == RoboFontStyleOblique) {
        return FontVariationModelFindAxis(m, RoboFontTagSlnt, NULL, NULL) ==
                   RoboOk ||
               FontVariationModelFindAxis(m, RoboFontTagItal, NULL, NULL) ==
                   RoboOk;
    }
    return 1;
}

int FontOutlineVariationApplied(const FontOutline *o)
{
    (void)o;
    return 0;
}

Result FontVariationFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontVariationModel *model = NULL;
    FontVariationCoords *coords = NULL;
    FontVariationRequest req;
    float n = 0.0f;
    float a = 0.0f;
    Result r;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    if (size == 0u) {
        return RoboOk;
    }
    if (size > 256u * 1024u) {
        size = 256u * 1024u;
    }
    r = FontSourceCreateMemoryCopy(data, size, &src);
    if (r != RoboOk) {
        return RoboOk;
    }
    if (FontSfntParse(src, 0u, 0u, &sfnt) != RoboOk) {
        FontSourceRelease(src);
        return RoboOk;
    }
    if (FontVariationModelCreate(sfnt, NULL, NULL, &model) == RoboOk && model) {
        uint32_t i;
        (void)FontVariationNormalize(-1.0f, 0.0f, 1.0f,
                                            (float)(data[0] % 3u) - 1.0f, &n);
        for (i = 0u; i < FontVariationModelAxisCount(model) && i < 8u; i++) {
            (void)FontVariationAvarMap(model, i, n, &a);
        }
        FontVariationRequestInit(&req);
        req.applyStyleAxes = 1u;
        req.weight = (FontWeight)(100u + (data[0] % 9u) * 100u);
        if (FontVariationCoordsCreate(model, &req, &coords) == RoboOk && coords) {
            (void)FontVariationCoordsAdjusted(coords, 0u);
            FontVariationCoordsRelease(coords);
        }
        /* Pathological named instance index */
        req.hasNamedInstance = 1u;
        req.namedInstanceIndex = 0xFFFFFFFEu;
        (void)FontVariationCoordsCreate(model, &req, &coords);
        if (coords) {
            FontVariationCoordsRelease(coords);
        }
        FontVariationModelRelease(model);
    }
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
    return RoboOk;
}
