#include "RoboFont/RoboFontInstance.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontInstance {
    uint32_t refs;
    FontInstanceId id;
    FontFace *face;
    FontPixelSize pixelSize;
    FontStyleDesc style;
    FontVariationCoords *coords;
    FontMetricsVariationState metricsVarState;
    FontMetrics metrics;
    uint8_t hasMetrics;
};

static uint64_t gNextInstanceId = 1u;

static Result InstanceFinish(FontFace *face,
                                     FontPixelSize pixelSize,
                                     const FontStyleDesc *local,
                                     const FontVariationRequest *varReq,
                                     FontInstance **out)
{
    FontInstance *inst;
    FontVariationCoords *coords = NULL;
    Result r;

    *out = NULL;

    r = FontFaceParseVariation(face);
    if (r == RoboOk) {
        const FontVariationModel *model = FontFaceVariation(face);
        FontVariationRequest req;
        if (varReq) {
            req = *varReq;
        } else {
            FontVariationRequestFromStyle(local, 0.0f, 0u, 0u, 0xFFFFFFFFu, 0u,
                                                   &req);
        }
        if (model) {
            r = FontVariationCoordsCreate(model, &req, &coords);
            if (r != RoboOk) {
                return r;
            }
        }
    } else if (r != RoboNotFound) {
        return r;
    }

    inst = (FontInstance *)RoboCalloc(1, sizeof(*inst));
    if (!inst) {
        if (coords) {
            FontVariationCoordsRelease(coords);
        }
        return RoboOutOfMemory;
    }
    inst->refs = 1u;
    inst->id = gNextInstanceId++;
    if (inst->id == 0u) {
        inst->id = gNextInstanceId++;
    }
    FontFaceRetain(face);
    inst->face = face;
    inst->pixelSize = pixelSize;
    inst->style = *local;
    inst->coords = coords;
    inst->metricsVarState =
        coords ? RoboFontMetricsVarPending : RoboFontMetricsVarStatic;
    FontMetricsClear(&inst->metrics);
    inst->hasMetrics = 0u;
    if (FontFaceMetricsInfo(face)) {
        FontMetrics lm;
        if (FontFaceLayoutMetrics(face, &lm) == RoboOk && lm.valid) {
            inst->metrics = lm;
            inst->hasMetrics = 1u;
        }
    }
    *out = inst;
    return RoboOk;
}

Result FontInstanceCreate(FontFace *face,
                                        FontPixelSize pixelSize,
                                        const FontStyleDesc *style,
                                        FontInstance **out)
{
    FontStyleDesc local;
    Result r;

    if (!out || !face || !(pixelSize > 0.0f)) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    if (style) {
        local = *style;
    } else {
        FontStyleDescInit(&local);
    }
    r = FontStyleDescValidate(&local);
    if (r != RoboOk) {
        return r;
    }
    return InstanceFinish(face, pixelSize, &local, NULL, out);
}

Result FontInstanceCreateVariation(
    FontFace *face,
    FontPixelSize pixelSize,
    const FontStyleDesc *style,
    const FontVariationRequest *varReq,
    FontInstance **out)
{
    FontStyleDesc local;
    Result r;

    if (!out || !face || !(pixelSize > 0.0f)) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    if (style) {
        local = *style;
    } else {
        FontStyleDescInit(&local);
    }
    r = FontStyleDescValidate(&local);
    if (r != RoboOk) {
        return r;
    }
    return InstanceFinish(face, pixelSize, &local, varReq, out);
}

void FontInstanceRetain(FontInstance *inst)
{
    if (!inst) {
        return;
    }
    inst->refs++;
}

void FontInstanceRelease(FontInstance *inst)
{
    if (!inst) {
        return;
    }
    if (inst->refs == 0u) {
        return;
    }
    inst->refs--;
    if (inst->refs != 0u) {
        return;
    }
    if (inst->coords) {
        FontVariationCoordsRelease(inst->coords);
        inst->coords = NULL;
    }
    FontFaceRelease(inst->face);
    inst->face = NULL;
    RoboFree(inst);
}

FontInstanceId GetFontInstanceId(const FontInstance *inst)
{
    return inst ? inst->id : 0u;
}

FontPixelSize FontInstancePixelSize(const FontInstance *inst)
{
    return inst ? inst->pixelSize : 0.0f;
}

const FontFace *FontInstanceFace(const FontInstance *inst)
{
    return inst ? inst->face : NULL;
}

Result FontInstanceStyle(const FontInstance *inst,
                                       FontStyleDesc *out)
{
    if (!inst || !out) {
        return RoboInvalidArgument;
    }
    *out = inst->style;
    return RoboOk;
}

const FontVariationCoords *FontInstanceVariationCoords(
    const FontInstance *inst)
{
    return inst ? inst->coords : NULL;
}

FontMetricsVariationState FontInstanceMetricsVariationState(
    const FontInstance *inst)
{
    return inst ? inst->metricsVarState : RoboFontMetricsVarStatic;
}

Result FontInstanceSetMetrics(FontInstance *inst,
                                             const FontMetrics *metrics)
{
    if (!inst || !metrics || !metrics->valid) {
        return RoboInvalidArgument;
    }
    inst->metrics = *metrics;
    inst->hasMetrics = 1u;
    return RoboOk;
}

Result FontInstanceSyncMetrics(FontInstance *inst)
{
    FontMetrics lm;
    if (!inst) {
        return RoboInvalidArgument;
    }
    if (FontFaceLayoutMetrics(inst->face, &lm) != RoboOk || !lm.valid) {
        return RoboNotFound;
    }
    inst->metrics = lm;
    inst->hasMetrics = 1u;
    inst->metricsVarState =
        inst->coords ? RoboFontMetricsVarPending : RoboFontMetricsVarStatic;
    return RoboOk;
}

Result FontInstanceMetrics(const FontInstance *inst,
                                         FontMetrics *out)
{
    if (!inst || !out) {
        return RoboInvalidArgument;
    }
    if (!inst->hasMetrics) {
        FontMetricsClear(out);
        return RoboNotFound;
    }
    *out = inst->metrics;
    return RoboOk;
}

Result FontInstanceUnitsToPixels(const FontInstance *inst,
                                                 FontUnit units,
                                                 float *outPixels)
{
    if (!inst || !outPixels) {
        return RoboInvalidArgument;
    }
    if (!inst->hasMetrics || !inst->metrics.valid) {
        return RoboNotFound;
    }
    return FontUnitsToPixels(units, inst->metrics.unitsPerEm, inst->pixelSize,
                                     outPixels);
}

Result FontInstanceScaleAdvance(const FontInstance *inst,
                                               uint16_t advanceWidth,
                                               FontRoundMode roundMode,
                                               float *outPixels)
{
    if (!inst || !outPixels) {
        return RoboInvalidArgument;
    }
    if (!inst->hasMetrics || !inst->metrics.valid) {
        return RoboNotFound;
    }
    return FontScaleFontUnits((FontUnit)advanceWidth,
                                      inst->metrics.unitsPerEm, inst->pixelSize,
                                      roundMode, outPixels);
}
