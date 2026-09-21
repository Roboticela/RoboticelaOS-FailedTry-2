#include "RoboFont/RoboFontStyle.h"

#include <string.h>

static int StyleFloatOk(float v)
{
    /* Freestanding-safe finite check (no math.h). */
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return ((bits.u >> 23) & 0xFFu) != 0xFFu;
}

int FontWeightIsValid(FontWeight w)
{
    return w >= 1u && w <= 1000u;
}

FontWeightCategory MakeFontWeightCategory(FontWeight w)
{
    if (!FontWeightIsValid(w)) {
        return RoboFontWeightCatUnknown;
    }
    if (w <= 149u) {
        return RoboFontWeightCatThin;
    }
    if (w <= 249u) {
        return RoboFontWeightCatExtraLight;
    }
    if (w <= 349u) {
        return RoboFontWeightCatLight;
    }
    if (w <= 449u) {
        return RoboFontWeightCatNormal;
    }
    if (w <= 549u) {
        return RoboFontWeightCatMedium;
    }
    if (w <= 649u) {
        return RoboFontWeightCatSemiBold;
    }
    if (w <= 749u) {
        return RoboFontWeightCatBold;
    }
    if (w <= 849u) {
        return RoboFontWeightCatExtraBold;
    }
    if (w <= 949u) {
        return RoboFontWeightCatBlack;
    }
    return RoboFontWeightCatExtraBlack;
}

int FontStretchIsValid(FontStretch s)
{
    return s >= 50u && s <= 200u;
}

int FontWidthClassIsValid(uint16_t wc)
{
    return wc >= 1u && wc <= 9u;
}

FontStretch FontStretchFromWidthClass(uint16_t usWidthClass)
{
    static const FontStretch map[10] = {
        RoboFontStretchNormal, /* unused index 0 */
        RoboFontStretchUltraCondensed,
        RoboFontStretchExtraCondensed,
        RoboFontStretchCondensed,
        RoboFontStretchSemiCondensed,
        RoboFontStretchNormal,
        RoboFontStretchSemiExpanded,
        RoboFontStretchExpanded,
        RoboFontStretchExtraExpanded,
        RoboFontStretchUltraExpanded};
    if (!FontWidthClassIsValid(usWidthClass)) {
        return RoboFontStretchNormal;
    }
    return map[usWidthClass];
}

FontWidthClass FontWidthClassFromStretch(FontStretch s)
{
    if (!FontStretchIsValid(s)) {
        return RoboFontWidthUnknown;
    }
    if (s < 56u) {
        return RoboFontWidthUltraCondensed;
    }
    if (s < 69u) {
        return RoboFontWidthExtraCondensed;
    }
    if (s < 81u) {
        return RoboFontWidthCondensed;
    }
    if (s < 94u) {
        return RoboFontWidthSemiCondensed;
    }
    if (s < 106u) {
        return RoboFontWidthNormal;
    }
    if (s < 119u) {
        return RoboFontWidthSemiExpanded;
    }
    if (s < 138u) {
        return RoboFontWidthExpanded;
    }
    if (s < 175u) {
        return RoboFontWidthExtraExpanded;
    }
    return RoboFontWidthUltraExpanded;
}

void FontStyleDescInit(FontStyleDesc *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->weight = RoboFontWeightNormal;
    out->stretch = RoboFontStretchNormal;
    out->style = RoboFontStyleNormal;
    out->axisCount = 0;
}

Result FontStyleDescValidate(const FontStyleDesc *desc)
{
    uint32_t i;

    if (!desc) {
        return RoboInvalidArgument;
    }
    if (!FontWeightIsValid(desc->weight)) {
        return RoboInvalidArgument;
    }
    if (!FontStretchIsValid(desc->stretch)) {
        return RoboInvalidArgument;
    }
    if (desc->style != RoboFontStyleNormal && desc->style != RoboFontStyleItalic &&
        desc->style != RoboFontStyleOblique && desc->style != RoboFontStyleUnknown) {
        return RoboInvalidArgument;
    }
    if (desc->axisCount > RoboFontStyleMaxAxes) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < desc->axisCount; i++) {
        if (desc->axes[i].tag == 0u) {
            return RoboInvalidArgument;
        }
        if (!StyleFloatOk(desc->axes[i].value)) {
            return RoboInvalidArgument;
        }
    }
    return RoboOk;
}
