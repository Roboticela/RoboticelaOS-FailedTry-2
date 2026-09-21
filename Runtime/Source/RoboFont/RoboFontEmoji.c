/*
 * RoboFontEmoji — emoji font awareness & selection foundations (Goal 5.17).
 */

#include "RoboFont/RoboFontEmoji.h"

#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeCase.h"

#include <string.h>

#define INFO_COOKIE 0x45494E46u /* 'EINF' */

struct FontEmojiInfo {
    uint32_t cookie;
    uint32_t refs;
    FontEmojiKind kind;
    uint32_t capabilities;
    uint32_t emojiScalars;
    /* Borrowed, retained: coverage for queries (may be NULL). */
    const FontCoverage *coverage;
};

static int NameHasEmojiHint(const char *s)
{
    size_t n;
    size_t i;
    if (!s || !s[0]) {
        return 0;
    }
    n = strlen(s);
    /* Case-insensitive ASCII substring "emoji". */
    for (i = 0u; i + 5u <= n; i++) {
        char a0 = s[i];
        char a1 = s[i + 1u];
        char a2 = s[i + 2u];
        char a3 = s[i + 3u];
        char a4 = s[i + 4u];
        if ((a0 == 'e' || a0 == 'E') && (a1 == 'm' || a1 == 'M') &&
            (a2 == 'o' || a2 == 'O') && (a3 == 'j' || a3 == 'J') &&
            (a4 == 'i' || a4 == 'I')) {
            return 1;
        }
    }
    return 0;
}

static int UvsStatusIsMapping(FontCmapUvsStatus st)
{
    return st == RoboFontCmapUvsDefault || st == RoboFontCmapUvsNonDefault;
}

void FontEmojiAnalyzeOptionsInit(FontEmojiAnalyzeOptions *opt)
{
    if (!opt) {
        return;
    }
    memset(opt, 0, sizeof(*opt));
    opt->monoEmojiScalarThreshold = 8u;
    opt->incidentalEmojiScalarThreshold = 1u;
}

Result FontEmojiInfoCreate(
    const FontSfnt *sfnt,
    const FontCoverage *cov,
    const char *familyName,
    const char *fullName,
    const FontEmojiAnalyzeOptions *opt,
    FontEmojiInfo **out)
{
    FontEmojiAnalyzeOptions local;
    FontEmojiInfo *info;
    uint32_t caps = 0u;
    uint32_t emojiScalars = 0u;
    int hasColr = 0;
    int hasCpal = 0;
    int hasSbix = 0;
    int hasCbdt = 0;
    int hasCblc = 0;
    int hasSvg = 0;
    int hasGlyf = 0;
    int hasCff = 0;
    int nameHint = 0;
    uint32_t monoTh;
    uint32_t incTh;
    FontEmojiKind kind = RoboFontEmojiKindNone;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    if (!opt) {
        FontEmojiAnalyzeOptionsInit(&local);
        opt = &local;
    }
    monoTh = opt->monoEmojiScalarThreshold ? opt->monoEmojiScalarThreshold : 8u;
    incTh = opt->incidentalEmojiScalarThreshold
                 ? opt->incidentalEmojiScalarThreshold
                 : 1u;

    if (cov) {
        const FontCoverageStats *st = GetFontCoverageStats(cov);
        if (st) {
            emojiScalars = st->emojiScalars;
            if (st->emojiScalars > 0u) {
                caps |= RoboFontEmojiCapCoverage;
            }
            if (st->hasUvs) {
                caps |= RoboFontEmojiCapUvs;
                /* Probe whether FE0E / FE0F selector records exist at all. */
                {
                    FontCmapUvsResult u;
                    /* Use U+0020 as a harmless base; status NONE vs others
                     * distinguishes missing selector vs unsupported base. */
                    if (FontCoverageQueryUvs(cov, 0x20u, RoboFontEmojiVsText,
                                                     &u) == RoboOk) {
                        if (u.status != RoboFontCmapUvsNone) {
                            caps |= RoboFontEmojiCapVsText;
                        }
                    }
                    if (FontCoverageQueryUvs(cov, 0x20u, RoboFontEmojiVsEmoji,
                                                     &u) == RoboOk) {
                        if (u.status != RoboFontCmapUvsNone) {
                            caps |= RoboFontEmojiCapVsEmoji;
                        }
                    }
                }
            }
        }
        FontCoverageRetain((FontCoverage *)cov);
    }

    if (sfnt) {
        hasColr = FontSfntHasTable(sfnt, RoboFontTagColr);
        hasCpal = FontSfntHasTable(sfnt, RoboFontTagCpal);
        hasSbix = FontSfntHasTable(sfnt, RoboFontTagSbix);
        hasCbdt = FontSfntHasTable(sfnt, RoboFontTagCbdt);
        hasCblc = FontSfntHasTable(sfnt, RoboFontTagCblc);
        hasSvg = FontSfntHasTable(sfnt, RoboFontTagSvg);
        hasGlyf = FontSfntHasTable(sfnt, RoboFontTagGlyf);
        hasCff = FontSfntHasTable(sfnt, RoboFontTagCff) ||
                  FontSfntHasTable(sfnt, RoboFontTagCff2);

        if (hasColr && hasCpal) {
            caps |= RoboFontEmojiCapVectorColor | RoboFontEmojiCapColor;
        }
        if (hasSbix || (hasCbdt && hasCblc) || hasCbdt || hasCblc) {
            caps |= RoboFontEmojiCapBitmap | RoboFontEmojiCapColor;
        }
        if (hasSvg) {
            caps |= RoboFontEmojiCapSvg | RoboFontEmojiCapColor;
        }
        if (hasGlyf || hasCff) {
            caps |= RoboFontEmojiCapOutline;
        }
    }

    nameHint = NameHasEmojiHint(familyName) || NameHasEmojiHint(fullName);
    if (nameHint) {
        caps |= RoboFontEmojiCapNameHint;
    }

    if (opt->configuredAsEmoji) {
        caps |= RoboFontEmojiCapConfigured;
    }

    if ((caps & RoboFontEmojiCapColor) != 0u &&
        ((caps & RoboFontEmojiCapCoverage) != 0u || nameHint ||
         opt->configuredAsEmoji)) {
        /* Color tables + (emoji coverage, weak name hint, or explicit config). */
        if ((caps & RoboFontEmojiCapCoverage) != 0u || opt->configuredAsEmoji) {
            kind = RoboFontEmojiKindColor;
        } else {
            /* Color tables + name hint only — not classified as an emoji font. */
            kind = RoboFontEmojiKindNone;
            caps &= (uint32_t)~(RoboFontEmojiCapNameHint);
        }
    } else if (emojiScalars >= monoTh) {
        kind = RoboFontEmojiKindMonochrome;
        caps |= RoboFontEmojiCapMonochrome;
    } else if (emojiScalars >= incTh) {
        kind = RoboFontEmojiKindIncidental;
        /* Name hint alone cannot promote INCIDENTAL → MONOCHROME. */
        (void)nameHint;
    } else {
        kind = RoboFontEmojiKindNone;
        /* Strip name hint from "supports" story when no coverage. */
        if ((caps & RoboFontEmojiCapCoverage) == 0u) {
            caps &= (uint32_t)~(RoboFontEmojiCapNameHint);
        }
    }

    /*
     * Explicit configuration marks CAP_CONFIGURED. Preserve structural
     * COLOR/MONOCHROME kind when tables/coverage already establish them;
     * otherwise promote to KIND_CONFIGURED (e.g. ASCII-only mapped face).
     */
    if (opt->configuredAsEmoji) {
        if (kind == RoboFontEmojiKindNone ||
            kind == RoboFontEmojiKindIncidental) {
            kind = RoboFontEmojiKindConfigured;
            if ((caps & RoboFontEmojiCapColor) == 0u &&
                (caps & RoboFontEmojiCapCoverage) != 0u) {
                caps |= RoboFontEmojiCapMonochrome;
            }
        }
    }

    info = (FontEmojiInfo *)RoboCalloc(1, sizeof(*info));
    if (!info) {
        if (cov) {
            FontCoverageRelease((FontCoverage *)cov);
        }
        return RoboOutOfMemory;
    }
    info->cookie = INFO_COOKIE;
    info->refs = 1u;
    info->kind = kind;
    info->capabilities = caps;
    info->emojiScalars = emojiScalars;
    info->coverage = cov;
    *out = info;
    return RoboOk;
}

Result FontEmojiInfoCreateFromFace(
    FontFace *face,
    const FontEmojiAnalyzeOptions *opt,
    FontEmojiInfo **out)
{
    Result res;
    const FontSfnt *sfnt = NULL;
    const FontCoverage *cov = NULL;
    const char *family = NULL;
    const char *full = NULL;
    const FontMetadata *meta = NULL;

    if (!face || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    res = FontFaceParseSfnt(face);
    if (res != RoboOk && res != RoboNotSupported) {
        return res;
    }
    sfnt = FontFaceSfnt(face);

    res = FontFaceParseCoverage(face);
    if (res == RoboOk) {
        cov = FontFaceCoverage(face);
    }

    if (FontFaceParseMetadata(face) == RoboOk) {
        meta = FontFaceMetadata(face);
        if (meta) {
            if (meta->familyName.utf8 && meta->familyName.utf8Len) {
                family = meta->familyName.utf8;
            }
            if (meta->fullName.utf8 && meta->fullName.utf8Len) {
                full = meta->fullName.utf8;
            }
        }
    }

    return FontEmojiInfoCreate(sfnt, cov, family, full, opt, out);
}

void FontEmojiInfoRetain(FontEmojiInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    info->refs++;
}

void FontEmojiInfoRelease(FontEmojiInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    if (info->coverage) {
        FontCoverageRelease((FontCoverage *)info->coverage);
    }
    info->cookie = 0u;
    RoboFree(info);
}

FontEmojiKind FontEmojiInfoKind(const FontEmojiInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->kind : RoboFontEmojiKindNone;
}

uint32_t FontEmojiInfoCapabilities(const FontEmojiInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->capabilities : 0u;
}

uint32_t FontEmojiInfoEmojiScalars(const FontEmojiInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->emojiScalars : 0u;
}

int FontEmojiInfoSupportsEmoji(const FontEmojiInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return 0;
    }
    return info->kind == RoboFontEmojiKindMonochrome ||
           info->kind == RoboFontEmojiKindColor ||
           info->kind == RoboFontEmojiKindConfigured ||
           (info->kind == RoboFontEmojiKindIncidental &&
            (info->capabilities & RoboFontEmojiCapCoverage) != 0u);
}

int FontEmojiInfoSupportsColorEmoji(const FontEmojiInfo *info)
{
    return info && info->cookie == INFO_COOKIE &&
           (info->capabilities & RoboFontEmojiCapColor) != 0u &&
           FontEmojiInfoSupportsEmoji(info);
}

int FontEmojiInfoSupportsMonochromeEmoji(const FontEmojiInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return 0;
    }
    if (!FontEmojiInfoSupportsEmoji(info)) {
        return 0;
    }
    /* Outline/mono emoji: coverage without requiring color tables. */
    return (info->capabilities & RoboFontEmojiCapCoverage) != 0u &&
           ((info->capabilities & RoboFontEmojiCapColor) == 0u ||
            (info->capabilities & RoboFontEmojiCapMonochrome) != 0u ||
            (info->capabilities & RoboFontEmojiCapOutline) != 0u);
}

int FontEmojiInfoSupportsVariationSelectors(
    const FontEmojiInfo *info)
{
    return info && info->cookie == INFO_COOKIE &&
           (info->capabilities & RoboFontEmojiCapUvs) != 0u;
}

int FontEmojiInfoIsClassifiedEmoji(const FontEmojiInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return 0;
    }
    return info->kind == RoboFontEmojiKindMonochrome ||
           info->kind == RoboFontEmojiKindColor ||
           info->kind == RoboFontEmojiKindConfigured;
}

const char *FontEmojiKindName(FontEmojiKind kind)
{
    switch (kind) {
    case RoboFontEmojiKindNone:
        return "none";
    case RoboFontEmojiKindIncidental:
        return "incidental";
    case RoboFontEmojiKindMonochrome:
        return "monochrome";
    case RoboFontEmojiKindColor:
        return "color";
    case RoboFontEmojiKindConfigured:
        return "configured";
    default:
        return "unknown";
    }
}

int FontEmojiCoversCodepoint(const FontEmojiInfo *info,
                                     uint32_t codePoint)
{
    if (!info || info->cookie != INFO_COOKIE || !info->coverage) {
        return 0;
    }
    return FontCoverageContains(info->coverage, codePoint);
}

Result FontEmojiQueryUvs(const FontEmojiInfo *info,
                                        uint32_t baseCodePoint,
                                        uint32_t variationSelector,
                                        FontCmapUvsResult *out)
{
    if (!info || info->cookie != INFO_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    if (!info->coverage) {
        return RoboNotSupported;
    }
    return FontCoverageQueryUvs(info->coverage, baseCodePoint,
                                        variationSelector, out);
}

Result FontEmojiProbePresentation(const FontEmojiInfo *info,
                                                 uint32_t baseCodePoint,
                                                 FontEmojiPresentationProbe *out)
{
    FontCmapUvsResult u;
    if (!info || info->cookie != INFO_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->baseCovered = FontEmojiCoversCodepoint(info, baseCodePoint);
    out->vsTextStatus = RoboFontCmapUvsNone;
    out->vsEmojiStatus = RoboFontCmapUvsNone;
    if (!info->coverage) {
        return RoboOk;
    }
    if (FontCoverageQueryUvs(info->coverage, baseCodePoint,
                                     RoboFontEmojiVsText, &u) == RoboOk) {
        out->vsTextStatus = u.status;
        out->hasVsTextMapping = UvsStatusIsMapping(u.status);
    }
    if (FontCoverageQueryUvs(info->coverage, baseCodePoint,
                                     RoboFontEmojiVsEmoji, &u) == RoboOk) {
        out->vsEmojiStatus = u.status;
        out->hasVsEmojiMapping = UvsStatusIsMapping(u.status);
    }
    return RoboOk;
}

void FontEmojiSequenceInit(FontEmojiSequence *seq)
{
    if (!seq) {
        return;
    }
    memset(seq, 0, sizeof(*seq));
    seq->rgiKind = RoboEmojiSeqNone;
}

Result FontEmojiSequenceSet(FontEmojiSequence *seq,
                                           const uint32_t *codepoints,
                                           uint32_t count)
{
    if (!seq) {
        return RoboInvalidArgument;
    }
    if (count > RoboFontEmojiMaxSequence) {
        return RoboInvalidArgument;
    }
    if (count != 0u && !codepoints) {
        return RoboInvalidArgument;
    }
    FontEmojiSequenceInit(seq);
    seq->codepoints = codepoints;
    seq->count = count;
    return RoboOk;
}

Result FontEmojiSequenceAnalyze(FontEmojiSequence *seq)
{
    uint32_t i;
    if (!seq) {
        return RoboInvalidArgument;
    }
    seq->hasVsText = 0;
    seq->hasVsEmoji = 0;
    seq->hasZwj = 0;
    seq->hasModifier = 0;
    seq->hasRegionalIndicator = 0;
    seq->hasKeycap = 0;
    seq->rgiKind = RoboEmojiSeqNone;

    if (!seq->codepoints || seq->count == 0u) {
        return RoboOk;
    }

    for (i = 0u; i < seq->count; i++) {
        uint32_t cp = seq->codepoints[i];
        if (cp == RoboFontEmojiVsText) {
            seq->hasVsText = 1;
        } else if (cp == RoboFontEmojiVsEmoji) {
            seq->hasVsEmoji = 1;
        } else if (cp == RoboFontEmojiZwj) {
            seq->hasZwj = 1;
        } else if (cp == RoboFontEmojiKeycap) {
            seq->hasKeycap = 1;
        } else if (UnicodeIsEmojiModifier(cp)) {
            seq->hasModifier = 1;
        } else if (UnicodeIsRegionalIndicator(cp)) {
            seq->hasRegionalIndicator = 1;
        }
    }

    seq->rgiKind = UnicodeEmojiSequenceClassify(
        (const Uchar32 *)seq->codepoints, (size_t)seq->count);
    return RoboOk;
}

FontEmojiSequenceSupport FontEmojiInfoSupportsSequence(
    const FontEmojiInfo *info,
    const FontEmojiSequence *seq)
{
    uint32_t i;
    int all = 1;
    int needVs = 0;
    int vsOk = 1;

    if (!info || info->cookie != INFO_COOKIE || !seq || !seq->codepoints ||
        seq->count == 0u) {
        return RoboFontEmojiSeqSupportNone;
    }

    /* Multi-codepoint RGI kinds that require shaping → UNKNOWN even if
     * components exist. */
    if (seq->rgiKind == RoboEmojiSeqZwj || seq->rgiKind == RoboEmojiSeqFlag ||
        seq->rgiKind == RoboEmojiSeqKeycap || seq->rgiKind == RoboEmojiSeqTag ||
        (seq->count > 1u && seq->hasZwj)) {
        /* Still verify components for callers that want a soft signal. */
        for (i = 0u; i < seq->count; i++) {
            uint32_t cp = seq->codepoints[i];
            if (cp == RoboFontEmojiZwj || cp == RoboFontEmojiVsText ||
                cp == RoboFontEmojiVsEmoji) {
                continue;
            }
            if (!FontEmojiCoversCodepoint(info, cp)) {
                return RoboFontEmojiSeqSupportNone;
            }
        }
        return RoboFontEmojiSeqSupportUnknown;
    }

    for (i = 0u; i < seq->count; i++) {
        uint32_t cp = seq->codepoints[i];
        if (cp == RoboFontEmojiVsText || cp == RoboFontEmojiVsEmoji) {
            needVs = 1;
            continue;
        }
        if (cp == RoboFontEmojiZwj) {
            continue;
        }
        if (!FontEmojiCoversCodepoint(info, cp)) {
            all = 0;
            break;
        }
    }
    if (!all) {
        return RoboFontEmojiSeqSupportNone;
    }

    if (needVs && info->coverage) {
        /* For base+VS pairs, check the first base against each VS in sequence. */
        uint32_t base = 0u;
        int haveBase = 0;
        for (i = 0u; i < seq->count; i++) {
            uint32_t cp = seq->codepoints[i];
            if (cp == RoboFontEmojiVsText || cp == RoboFontEmojiVsEmoji) {
                if (haveBase) {
                    FontCmapUvsResult u;
                    if (FontCoverageQueryUvs(info->coverage, base, cp, &u) !=
                            RoboOk ||
                        !UvsStatusIsMapping(u.status)) {
                        vsOk = 0;
                    }
                }
            } else if (cp != RoboFontEmojiZwj) {
                base = cp;
                haveBase = 1;
            }
        }
        if (vsOk) {
            return RoboFontEmojiSeqSupportComponentsAndVs;
        }
        return RoboFontEmojiSeqSupportComponents;
    }

    return RoboFontEmojiSeqSupportComponents;
}

int FontEmojiContentIsEmojiRelated(const uint32_t *codepoints,
                                             uint32_t count)
{
    uint32_t i;
    if (!codepoints || count == 0u) {
        return 0;
    }
    for (i = 0u; i < count; i++) {
        uint32_t cp = codepoints[i];
        if (UnicodeIsEmoji(cp) || UnicodeIsEmojiPresentation(cp) ||
            UnicodeIsExtendedPictographic(cp) ||
            UnicodeIsEmojiModifier(cp) ||
            UnicodeIsEmojiComponent(cp) ||
            UnicodeIsRegionalIndicator(cp) || cp == RoboFontEmojiZwj ||
            cp == RoboFontEmojiVsText || cp == RoboFontEmojiVsEmoji ||
            cp == RoboFontEmojiKeycap) {
            return 1;
        }
    }
    return 0;
}

/* ---- Selection helpers ---- */

void FontEmojiSelectRequestInit(FontEmojiSelectRequest *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->colorPreference = RoboFontEmojiColorAny;
    req->weight = 400;
    req->stretch = RoboFontStretchNormal;
    req->style = RoboFontStyleNormal;
    req->useGenericEmoji = 1;
}

void FontEmojiSelectionInit(FontEmojiSelection *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cookie = RoboFontEmojiSelectionCookie;
}

void FontEmojiSelectionClear(FontEmojiSelection *out)
{
    if (!out) {
        return;
    }
    if (out->cookie == RoboFontEmojiSelectionCookie && out->face) {
        FontFaceRelease(out->face);
    }
    memset(out, 0, sizeof(*out));
    out->cookie = RoboFontEmojiSelectionCookie;
}

static int FamilyListContains(const char *const *list, uint32_t n, const char *fam)
{
    uint32_t i;
    int cmp = 0;
    if (!fam || !list) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        if (!list[i]) {
            continue;
        }
        if (UnicodeCasefoldCompareUtf8(list[i], 0u, fam, 0u, RoboUnicodeReplace,
                                               &cmp) == RoboOk &&
            cmp == 0) {
            return 1;
        }
    }
    return 0;
}

static int ColorPrefOk(FontEmojiColorPreference pref,
                         const FontEmojiInfo *info)
{
    int isColor = FontEmojiInfoSupportsColorEmoji(info);
    int isMono = FontEmojiInfoSupportsMonochromeEmoji(info);
    switch (pref) {
    case RoboFontEmojiColorAny:
        return 1;
    case RoboFontEmojiColorPreferColor:
    case RoboFontEmojiColorPreferMono:
        return 1; /* preference only */
    case RoboFontEmojiColorRequireColor:
        return isColor;
    case RoboFontEmojiColorRequireMono:
        return isMono && !isColor;
    default:
        return 1;
    }
}

static uint32_t ScoreCandidate(const FontEmojiInfo *info,
                                const FontEmojiSelectRequest *req,
                                uint32_t famPrio,
                                int configuredFamily,
                                int inColorList,
                                int inMonoList,
                                FontFaceId faceId)
{
    uint32_t score = 0u;
    uint32_t i;
    int covered = 1;
    FontEmojiKind kind = FontEmojiInfoKind(info);

    if (configuredFamily || kind == RoboFontEmojiKindConfigured) {
        score += 1000000u;
    }
    if (kind == RoboFontEmojiKindColor) {
        score += 500000u;
    } else if (kind == RoboFontEmojiKindMonochrome) {
        score += 400000u;
    } else if (kind == RoboFontEmojiKindIncidental) {
        score += 50000u;
    }

    if (req->codepoints && req->codepointCount > 0u) {
        for (i = 0u; i < req->codepointCount; i++) {
            uint32_t cp = req->codepoints[i];
            if (cp == RoboFontEmojiZwj || cp == RoboFontEmojiVsText ||
                cp == RoboFontEmojiVsEmoji) {
                continue;
            }
            if (FontEmojiCoversCodepoint(info, cp)) {
                score += 1000u;
            } else {
                covered = 0;
            }
        }
        if (!covered && req->codepointCount > 0u) {
            /* Missing required coverage — heavily penalize but keep for ranking. */
            score = score / 10u;
        } else if (covered) {
            score += 200000u;
        }
    }

    if (FontEmojiInfoSupportsVariationSelectors(info)) {
        score += 5000u;
    }

    if (req->colorPreference == RoboFontEmojiColorPreferColor ||
        req->colorPreference == RoboFontEmojiColorRequireColor) {
        if (FontEmojiInfoSupportsColorEmoji(info)) {
            score += 80000u;
        }
        if (inColorList) {
            score += 40000u;
        }
    } else if (req->colorPreference == RoboFontEmojiColorPreferMono ||
               req->colorPreference == RoboFontEmojiColorRequireMono) {
        if (FontEmojiInfoSupportsMonochromeEmoji(info) &&
            !FontEmojiInfoSupportsColorEmoji(info)) {
            score += 80000u;
        }
        if (inMonoList) {
            score += 40000u;
        }
    }

    if (famPrio != 0xFFFFFFFFu) {
        score += 30000u - (famPrio < 30000u ? famPrio : 29999u);
    }

    /* Deterministic tie-break: lower faceId slightly preferred. */
    if (faceId < 10000u) {
        score += (uint32_t)(10000u - (uint32_t)faceId);
    }
    return score;
}

typedef struct {
    char names[RoboFontEmojiMaxCandidates][128];
    uint32_t count;
} EmojiFamilyList;

static void FamilyListAdd(EmojiFamilyList *dst, const char *name)
{
    uint32_t i;
    size_t n;
    if (!dst || !name || !name[0] || dst->count >= RoboFontEmojiMaxCandidates) {
        return;
    }
    n = strlen(name);
    if (n >= 128u) {
        n = 127u;
    }
    for (i = 0u; i < dst->count; i++) {
        int cmp = 0;
        if (UnicodeCasefoldCompareUtf8(dst->names[i], 0u, name, 0u,
                                               RoboUnicodeReplace, &cmp) == RoboOk &&
            cmp == 0) {
            return;
        }
    }
    memcpy(dst->names[dst->count], name, n);
    dst->names[dst->count][n] = '\0';
    dst->count++;
}

static void FamilyListAddMany(EmojiFamilyList *dst, const char *const *src,
                                 uint32_t n)
{
    uint32_t i;
    if (!src) {
        return;
    }
    for (i = 0u; i < n; i++) {
        FamilyListAdd(dst, src[i]);
    }
}

Result FontEmojiSelect(
    FontRegistry *registry,
    FontGenericResolver *generics,
    const FontEmojiSelectRequest *req,
    FontEmojiSelection *out)
{
    EmojiFamilyList families;
    FontGenericResult gres;
    uint32_t regN;
    uint32_t i;
    uint32_t bestScore = 0u;
    int haveBest = 0;
    const FontRegistration *bestRec = NULL;
    FontEmojiInfo *bestInfo = NULL;
    FontEmojiAnalyzeOptions aopt;

    if (!registry || !req || !out) {
        return RoboInvalidArgument;
    }
    if (req->codepointCount > RoboFontEmojiMaxSequence) {
        return RoboInvalidArgument;
    }
    if (req->codepointCount != 0u && !req->codepoints) {
        return RoboInvalidArgument;
    }

    if (out->cookie == RoboFontEmojiSelectionCookie) {
        FontEmojiSelectionClear(out);
    } else {
        FontEmojiSelectionInit(out);
    }

    memset(&families, 0, sizeof(families));
    FamilyListAddMany(&families, req->preferredFamilies, req->preferredFamilyCount);
    FamilyListAddMany(&families, req->colorFamilies, req->colorFamilyCount);
    FamilyListAddMany(&families, req->monoFamilies, req->monoFamilyCount);

    if (req->useGenericEmoji && generics) {
        FontGenericRequest greq;
        FontGenericRequestInit(&greq);
        greq.generic = RoboFontGenericEmoji;
        FontGenericResultInit(&gres);
        if (FontGenericResolve(generics, &greq, &gres) == RoboOk &&
            gres.resolved) {
            for (i = 0u; i < gres.familyCount; i++) {
                FamilyListAdd(&families, gres.families[i]);
            }
        }
        FontGenericResultClear(&gres);
    }

    regN = FontRegistryRegistrationCount(registry);
    for (i = 0u; i < regN; i++) {
        const FontRegistration *rec = NULL;
        FontEmojiInfo *info = NULL;
        int configured = 0;
        uint32_t famPrio;
        uint32_t score;
        int inColor;
        int inMono;

        if (FontRegistryRegistrationAt(registry, i, &rec) != RoboOk || !rec ||
            !rec->face) {
            continue;
        }

        configured = 0;
        {
            uint32_t fi;
            for (fi = 0u; fi < families.count; fi++) {
                int cmp = 0;
                if (UnicodeCasefoldCompareUtf8(families.names[fi], 0u, rec->family,
                                                       0u, RoboUnicodeReplace,
                                                       &cmp) == RoboOk &&
                    cmp == 0) {
                    configured = 1;
                    break;
                }
            }
        }

        FontEmojiAnalyzeOptionsInit(&aopt);
        aopt.configuredAsEmoji = configured;

        if (FontEmojiInfoCreateFromFace(rec->face, &aopt, &info) != RoboOk ||
            !info) {
            continue;
        }

        if (!FontEmojiInfoSupportsEmoji(info) && !configured) {
            FontEmojiInfoRelease(info);
            continue;
        }
        if (!ColorPrefOk(req->colorPreference, info)) {
            FontEmojiInfoRelease(info);
            continue;
        }

        famPrio = 0xFFFFFFFFu;
        {
            uint32_t fi;
            for (fi = 0u; fi < families.count; fi++) {
                int cmp = 0;
                if (UnicodeCasefoldCompareUtf8(families.names[fi], 0u, rec->family,
                                                       0u, RoboUnicodeReplace,
                                                       &cmp) == RoboOk &&
                    cmp == 0) {
                    famPrio = fi;
                    break;
                }
            }
        }
        inColor =
            FamilyListContains(req->colorFamilies, req->colorFamilyCount, rec->family);
        inMono =
            FamilyListContains(req->monoFamilies, req->monoFamilyCount, rec->family);

        score = ScoreCandidate(info, req, famPrio, configured, inColor, inMono,
                                rec->faceId);

        /* If preferred families were configured, skip faces outside the list
         * unless no preferred list exists. */
        if (families.count > 0u && famPrio == 0xFFFFFFFFu) {
            /* Still allow classified emoji fonts as soft fallback candidates
             * with lower score — only if request has codepoints to cover. */
            if (!req->codepoints || req->codepointCount == 0u) {
                FontEmojiInfoRelease(info);
                continue;
            }
            if (!FontEmojiInfoIsClassifiedEmoji(info)) {
                FontEmojiInfoRelease(info);
                continue;
            }
        }

        if (!haveBest || score > bestScore ||
            (score == bestScore && bestRec && rec->faceId < bestRec->faceId)) {
            if (bestInfo) {
                FontEmojiInfoRelease(bestInfo);
            }
            bestInfo = info;
            bestRec = rec;
            bestScore = score;
            haveBest = 1;
            info = NULL; /* ownership transferred to bestInfo */
        }
        if (info) {
            FontEmojiInfoRelease(info);
        }
    }

    if (!haveBest || !bestRec || !bestRec->face) {
        if (bestInfo) {
            FontEmojiInfoRelease(bestInfo);
        }
        out->found = 0;
        return RoboOk;
    }

    FontFaceRetain(bestRec->face);
    out->found = 1;
    out->face = bestRec->face;
    out->faceId = bestRec->faceId;
    out->kind = FontEmojiInfoKind(bestInfo);
    out->capabilities = FontEmojiInfoCapabilities(bestInfo);
    out->score = bestScore;
    {
        size_t n = strlen(bestRec->family);
        if (n >= sizeof(out->family)) {
            n = sizeof(out->family) - 1u;
        }
        memcpy(out->family, bestRec->family, n);
        out->family[n] = '\0';
    }
    FontEmojiInfoRelease(bestInfo);
    return RoboOk;
}

Result FontEmojiResolveFallback(
    FontRegistry *registry,
    FontGenericResolver *generics,
    FontFallbackResolver *fallback,
    FontFace *primary,
    const FontEmojiSelectRequest *req,
    FontEmojiSelection *emojiOut,
    FontFallbackResult *fallbackOut)
{
    FontEmojiSelection localSel;
    FontEmojiSelection *sel = emojiOut;
    FontFallbackRequest freq;
    FontGenericResult gres;
    const char *preferred[RoboFontFallbackMaxPreferred];
    uint32_t prefN = 0u;
    Result res;
    int ownSel = 0;

    if (!registry || !fallback || !primary || !req || !fallbackOut) {
        return RoboInvalidArgument;
    }
    if (!FontEmojiContentIsEmojiRelated(req->codepoints, req->codepointCount)) {
        /* Not emoji-related — let normal fallback handle without emoji policy. */
        FontFallbackRequestInit(&freq);
        freq.primary = primary;
        freq.codepoints = req->codepoints;
        freq.codepointCount = req->codepointCount;
        freq.weight = req->weight;
        freq.stretch = req->stretch;
        freq.style = req->style;
        return FontFallbackResolve(fallback, &freq, fallbackOut);
    }

    if (!sel) {
        FontEmojiSelectionInit(&localSel);
        sel = &localSel;
        ownSel = 1;
    }

    res = FontEmojiSelect(registry, generics, req, sel);
    if (res != RoboOk) {
        if (ownSel) {
            FontEmojiSelectionClear(&localSel);
        }
        return res;
    }

    /* Build preferred family list for fallback. */
    if (req->preferredFamilies) {
        uint32_t i;
        for (i = 0u; i < req->preferredFamilyCount &&
                     prefN < RoboFontFallbackMaxPreferred;
             i++) {
            preferred[prefN++] = req->preferredFamilies[i];
        }
    }
    if (generics && req->useGenericEmoji) {
        FontGenericRequest greq;
        FontGenericRequestInit(&greq);
        greq.generic = RoboFontGenericEmoji;
        FontGenericResultInit(&gres);
        if (FontGenericResolve(generics, &greq, &gres) == RoboOk && gres.resolved) {
            uint32_t i;
            for (i = 0u; i < gres.familyCount && prefN < RoboFontFallbackMaxPreferred;
                 i++) {
                preferred[prefN++] = gres.families[i];
            }
        }
        /* Keep gres alive until fallback returns — families borrowed. */
        FontFallbackRequestInit(&freq);
        freq.primary = primary;
        freq.codepoints = req->codepoints;
        freq.codepointCount = req->codepointCount;
        freq.weight = req->weight;
        freq.stretch = req->stretch;
        freq.style = req->style;
        freq.preferredFamilies = preferred;
        freq.preferredFamilyCount = prefN;
        res = FontFallbackResolve(fallback, &freq, fallbackOut);
        FontGenericResultClear(&gres);
    } else {
        FontFallbackRequestInit(&freq);
        freq.primary = primary;
        freq.codepoints = req->codepoints;
        freq.codepointCount = req->codepointCount;
        freq.weight = req->weight;
        freq.stretch = req->stretch;
        freq.style = req->style;
        freq.preferredFamilies = preferred;
        freq.preferredFamilyCount = prefN;
        res = FontFallbackResolve(fallback, &freq, fallbackOut);
    }

    if (ownSel) {
        FontEmojiSelectionClear(&localSel);
    }
    return res;
}

Result FontEmojiFuzzParse(const uint8_t *data, size_t size)
{
    FontEmojiSequence seq;
    uint32_t cps[RoboFontEmojiMaxSequence];
    uint32_t n = 0u;
    size_t i;
    FontEmojiInfo *info = NULL;
    FontEmojiAnalyzeOptions opt;
    FontEmojiPresentationProbe probe;
    FontEmojiSelectRequest req;

    if (!data || size == 0u) {
        return RoboOk;
    }

    /* Build a bounded codepoint sequence from input bytes. */
    for (i = 0u; i + 3u < size && n < RoboFontEmojiMaxSequence; i += 3u) {
        uint32_t cp = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1u] << 8) |
                      (uint32_t)data[i + 2u];
        cp &= 0x1FFFFFu;
        if (cp > 0x10FFFFu) {
            cp = 0xFFFD;
        }
        cps[n++] = cp;
    }

    (void)FontEmojiSequenceSet(&seq, cps, n);
    (void)FontEmojiSequenceAnalyze(&seq);
    (void)FontEmojiContentIsEmojiRelated(cps, n);
    (void)FontEmojiKindName((FontEmojiKind)(data[0] % 5u));

    FontEmojiAnalyzeOptionsInit(&opt);
    opt.configuredAsEmoji = (data[0] & 1u) != 0u;
    (void)FontEmojiInfoCreate(NULL, NULL, "FuzzEmoji", NULL, &opt, &info);
    if (info) {
        (void)FontEmojiInfoSupportsEmoji(info);
        (void)FontEmojiInfoSupportsColorEmoji(info);
        (void)FontEmojiInfoSupportsMonochromeEmoji(info);
        (void)FontEmojiInfoSupportsSequence(info, &seq);
        (void)FontEmojiProbePresentation(info, n ? cps[0] : 0x1F600u, &probe);
        FontEmojiInfoRelease(info);
    }

    /* Pathological large sequence count. */
    (void)FontEmojiSequenceSet(&seq, cps, RoboFontEmojiMaxSequence + 1u);

    FontEmojiSelectRequestInit(&req);
    req.codepoints = cps;
    req.codepointCount = n > RoboFontEmojiMaxSequence ? 0u : n;
    req.colorPreference =
        (FontEmojiColorPreference)(data[0] % 5u);
    (void)req;

    (void)FontOtFuzzParse(data, size);
    return RoboOk;
}
