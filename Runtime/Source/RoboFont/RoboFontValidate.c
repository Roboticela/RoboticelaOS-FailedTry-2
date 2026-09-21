/*
 * RoboFontValidate — font security & validation (Goal 5.20).
 */

#include "RoboFont/RoboFontValidate.h"

#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontColor.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontVariation.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define RESULT_COOKIE 0x56414C31u /* 'VAL1' */

#define DEFAULT_MAX_TABLE_SIZE (16u * 1024u * 1024u)
#define DEFAULT_MAX_BITMAP_PAYLOAD (4u * 1024u * 1024u)

#define ISSUE_OFFSET_NA UINT32_MAX
#define ISSUE_GLYPH_NA UINT32_MAX

struct FontValidationResult {
    uint32_t cookie;
    uint32_t refs;
    FontValidationIssue issues[RoboFontValidationMaxIssues];
    uint32_t issueCount;
    uint8_t hasFatal;
    FontValidationStage completedStage;
    uint32_t workUnits;
};

static const char *kIssueCodeStrings[] = {
    "ok",
    "source_too_large",
    "invalid_signature",
    "table_count",
    "table_offset",
    "table_overlap",
    "duplicate_tag",
    "checksum_mismatch",
    "collection_faces",
    "glyph_count",
    "missing_required",
    "parse_failed",
    "unsupported",
    "color_limit",
    "variation_limit",
    "name_limit",
    "unicode_invalid",
    "work_budget",
    "internal"
};

static const char *kSeverityStrings[] = {
    "info",
    "warning",
    "error",
    "fatal"
};

static const char *kStageStrings[] = {
    "unknown",
    "container",
    "metadata",
    "features",
    "glyph_color_var"
};

static void CopyMessage(char *dst, size_t cap, const char *src)
{
    size_t i;

    if (!dst || cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0u; i + 1u < cap && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static uint32_t ClampU32(uint32_t value, uint32_t minV, uint32_t maxV)
{
    if (value < minV) {
        return minV;
    }
    if (value > maxV) {
        return maxV;
    }
    return value;
}

static uint64_t ClampU64(uint64_t value, uint64_t minV, uint64_t maxV)
{
    if (value < minV) {
        return minV;
    }
    if (value > maxV) {
        return maxV;
    }
    return value;
}

static void AddIssue(FontValidationResult *result,
                      FontValidationIssueCode code,
                      FontValidationSeverity severity,
                      FontValidationStage stage,
                      FontTag tableTag,
                      uint32_t byteOffset,
                      uint32_t glyphId,
                      FontTag axisTag,
                      uint8_t recoverable,
                      const char *message)
{
    FontValidationIssue *issue;

    if (!result || result->cookie != RESULT_COOKIE) {
        return;
    }
    if (severity == RoboFontValidationSeverityFatal) {
        result->hasFatal = 1u;
    }
    if (result->issueCount >= RoboFontValidationMaxIssues) {
        return;
    }
    issue = &result->issues[result->issueCount++];
    issue->code = code;
    issue->severity = severity;
    issue->stage = stage;
    issue->tableTag = tableTag;
    issue->byteOffset = byteOffset;
    issue->glyphId = glyphId;
    issue->axisTag = axisTag;
    issue->recoverable = recoverable;
    CopyMessage(issue->message, sizeof(issue->message), message);
}

static int ConsumeWork(FontValidationResult *result,
                        const FontValidationPolicy *policy,
                        uint32_t units)
{
    if (!result || !policy) {
        return 0;
    }
    if (result->workUnits + units > policy->limits.maxWorkBudget) {
        AddIssue(result,
                  RoboFontValidationWorkBudget,
                  policy->rejectOversized ? RoboFontValidationSeverityFatal
                                           : RoboFontValidationSeverityError,
                  result->completedStage ? result->completedStage
                                          : RoboFontValidationStageContainer,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->rejectOversized ? 0u : 1u,
                  "validation work budget exceeded");
        return 0;
    }
    result->workUnits += units;
    return 1;
}

static int ShouldStop(const FontValidationResult *result,
                       const FontValidationPolicy *policy)
{
    return policy && policy->stopOnFatal && result && result->hasFatal;
}

static FontValidationSeverity LimitSeverity(
    const FontValidationPolicy *policy)
{
    return policy && policy->rejectOversized ? RoboFontValidationSeverityFatal
                                              : RoboFontValidationSeverityError;
}

static void ReportChecksumMismatch(FontValidationResult *result,
                                     const FontValidationPolicy *policy,
                                     FontTag tag)
{
    FontValidationSeverity severity;
    uint8_t recoverable = 1u;

    if (!result || !policy) {
        return;
    }
    switch (policy->checksumPolicy) {
    case RoboFontChecksumAccept:
        severity = RoboFontValidationSeverityInfo;
        break;
    case RoboFontChecksumWarn:
        severity = RoboFontValidationSeverityWarning;
        break;
    case RoboFontChecksumReject:
    default:
        severity = RoboFontValidationSeverityFatal;
        recoverable = 0u;
        break;
    }
    AddIssue(result,
              RoboFontValidationChecksumMismatch,
              severity,
              RoboFontValidationStageContainer,
              tag,
              ISSUE_OFFSET_NA,
              ISSUE_GLYPH_NA,
              0u,
              recoverable,
              "table checksum mismatch");
}

static Result StageContainer(FontSfnt *sfnt,
                                     const FontValidationPolicy *policy,
                                     FontValidationResult *result)
{
    const FontSfntHeader *header;
    uint32_t tableCount;
    uint32_t i;

    if (!sfnt || !policy || !result) {
        return RoboInvalidArgument;
    }

    header = GetFontSfntHeader(sfnt);
    tableCount = FontSfntTableCount(sfnt);
    if (!ConsumeWork(result, policy, 1u)) {
        return RoboParseError;
    }

    if (tableCount > policy->limits.maxTables) {
        AddIssue(result,
                  RoboFontValidationTableCount,
                  LimitSeverity(policy),
                  RoboFontValidationStageContainer,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->rejectOversized ? 0u : 1u,
                  "table count exceeds validation limit");
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    if (header && !FontSfntSearchFieldsConsistent(header)) {
        AddIssue(result,
                  RoboFontValidationTableCount,
                  policy->level >= RoboFontValidationStrict
                      ? RoboFontValidationSeverityWarning
                      : RoboFontValidationSeverityInfo,
                  RoboFontValidationStageContainer,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  1u,
                  "SFNT searchRange fields inconsistent");
    }

    for (i = 0u; i < tableCount; i++) {
        FontSfntTableRecord rec;

        if (!ConsumeWork(result, policy, 1u)) {
            return RoboParseError;
        }
        if (FontSfntTableAt(sfnt, i, &rec) != RoboOk) {
            AddIssue(result,
                      RoboFontValidationInternal,
                      RoboFontValidationSeverityFatal,
                      RoboFontValidationStageContainer,
                      0u,
                      ISSUE_OFFSET_NA,
                      ISSUE_GLYPH_NA,
                      0u,
                      0u,
                      "failed to enumerate SFNT table");
            return RoboParseError;
        }
        if (rec.length > policy->limits.maxTableSize) {
            AddIssue(result,
                      RoboFontValidationTableOffset,
                      LimitSeverity(policy),
                      RoboFontValidationStageContainer,
                      rec.tag,
                      rec.offset,
                      ISSUE_GLYPH_NA,
                      0u,
                      policy->rejectOversized ? 0u : 1u,
                      "table length exceeds validation limit");
            if (ShouldStop(result, policy)) {
                return RoboParseError;
            }
        }
    }

    if (policy->level >= RoboFontValidationStrict ||
        policy->checksumPolicy != RoboFontChecksumAccept) {
        for (i = 0u; i < tableCount; i++) {
            FontSfntTableRecord rec;

            if (!ConsumeWork(result, policy, 2u)) {
                return RoboParseError;
            }
            if (FontSfntTableAt(sfnt, i, &rec) != RoboOk) {
                continue;
            }
            if (FontSfntVerifyTableChecksum(sfnt, rec.tag) != RoboOk) {
                ReportChecksumMismatch(result, policy, rec.tag);
                if (ShouldStop(result, policy)) {
                    return RoboParseError;
                }
            }
        }
    }

    result->completedStage = RoboFontValidationStageContainer;
    return RoboOk;
}

static Result StageMetadata(FontSfnt *sfnt,
                                    const FontValidationPolicy *policy,
                                    FontValidationResult *result)
{
    int hasHead;
    int hasMaxp;
    uint32_t numGlyphs = 0u;
    Result res;

    if (!sfnt || !policy || !result) {
        return RoboInvalidArgument;
    }
    if (!ConsumeWork(result, policy, 1u)) {
        return RoboParseError;
    }

    hasHead = FontSfntHasTable(sfnt, RoboFontTagHead);
    hasMaxp = FontSfntHasTable(sfnt, RoboFontTagMaxp);

    if (!hasHead || !hasMaxp) {
        FontValidationSeverity severity =
            policy->level >= RoboFontValidationStrict
                ? RoboFontValidationSeverityFatal
                : RoboFontValidationSeverityWarning;
        if (!hasHead) {
            AddIssue(result,
                      RoboFontValidationMissingRequired,
                      severity,
                      RoboFontValidationStageMetadata,
                      RoboFontTagHead,
                      ISSUE_OFFSET_NA,
                      ISSUE_GLYPH_NA,
                      0u,
                      severity != RoboFontValidationSeverityFatal,
                      "missing required head table");
        }
        if (!hasMaxp) {
            AddIssue(result,
                      RoboFontValidationMissingRequired,
                      severity,
                      RoboFontValidationStageMetadata,
                      RoboFontTagMaxp,
                      ISSUE_OFFSET_NA,
                      ISSUE_GLYPH_NA,
                      0u,
                      severity != RoboFontValidationSeverityFatal,
                      "missing required maxp table");
        }
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    res = FontSfntNumGlyphs(sfnt, &numGlyphs);
    if (res == RoboOk) {
        if (numGlyphs > policy->limits.maxGlyphs) {
            AddIssue(result,
                      RoboFontValidationGlyphCount,
                      LimitSeverity(policy),
                      RoboFontValidationStageMetadata,
                      RoboFontTagMaxp,
                      ISSUE_OFFSET_NA,
                      numGlyphs,
                      0u,
                      policy->rejectOversized ? 0u : 1u,
                      "glyph count exceeds validation limit");
            if (ShouldStop(result, policy)) {
                return RoboParseError;
            }
        }
    } else if (policy->level >= RoboFontValidationStrict) {
        AddIssue(result,
                  RoboFontValidationParseFailed,
                  RoboFontValidationSeverityWarning,
                  RoboFontValidationStageMetadata,
                  RoboFontTagMaxp,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  1u,
                  "unable to read glyph count from maxp");
    }

    result->completedStage = RoboFontValidationStageMetadata;
    return RoboOk;
}

static Result StageFeatures(FontSfnt *sfnt,
                                    const FontValidationPolicy *policy,
                                    FontValidationResult *result)
{
    FontSpan span;

    if (!sfnt || !policy || !result) {
        return RoboInvalidArgument;
    }
    if (!ConsumeWork(result, policy, 1u)) {
        return RoboParseError;
    }

    if (FontSfntHasTable(sfnt, RoboFontTagName)) {
        FontNameTable nameTable;

        memset(&nameTable, 0, sizeof(nameTable));
        if (FontSfntTableBytes(sfnt, RoboFontTagName, &span) == RoboOk) {
            if (span.size > (size_t)policy->limits.maxAllocation) {
                AddIssue(result,
                          RoboFontValidationNameLimit,
                          LimitSeverity(policy),
                          RoboFontValidationStageFeatures,
                          RoboFontTagName,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->rejectOversized ? 0u : 1u,
                          "name table exceeds allocation limit");
            } else if (FontNameParse(span, &nameTable) != RoboOk) {
                AddIssue(result,
                          RoboFontValidationParseFailed,
                          RoboFontValidationSeverityWarning,
                          RoboFontValidationStageFeatures,
                          RoboFontTagName,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          1u,
                          "name table failed structural parse");
            } else if (nameTable.count > policy->limits.maxNameRecords) {
                AddIssue(result,
                          RoboFontValidationNameLimit,
                          LimitSeverity(policy),
                          RoboFontValidationStageFeatures,
                          RoboFontTagName,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->rejectOversized ? 0u : 1u,
                          "name record count exceeds validation limit");
            }
            FontNameTableClear(&nameTable);
        }
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    if (FontSfntHasTable(sfnt, RoboFontTagCmap)) {
        FontCmap cmap;

        memset(&cmap, 0, sizeof(cmap));
        if (FontSfntTableBytes(sfnt, RoboFontTagCmap, &span) == RoboOk) {
            if (FontCmapParse(span, &cmap) != RoboOk) {
                AddIssue(result,
                          RoboFontValidationParseFailed,
                          RoboFontValidationSeverityWarning,
                          RoboFontValidationStageFeatures,
                          RoboFontTagCmap,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          1u,
                          "cmap table failed structural parse");
            }
            FontCmapClear(&cmap);
        } else {
            AddIssue(result,
                      RoboFontValidationParseFailed,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageFeatures,
                      RoboFontTagCmap,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "cmap table bytes unavailable");
        }
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    result->completedStage = RoboFontValidationStageFeatures;
    return RoboOk;
}

static void CheckPayloadTable(FontSfnt *sfnt,
                                const FontValidationPolicy *policy,
                                FontValidationResult *result,
                                FontTag tag,
                                uint32_t maxPayload,
                                const char *message)
{
    FontSpan span;

    if (!FontSfntHasTable(sfnt, tag)) {
        return;
    }
    if (FontSfntTableBytes(sfnt, tag, &span) == RoboOk &&
        span.size > (size_t)maxPayload) {
        AddIssue(result,
                  RoboFontValidationColorLimit,
                  LimitSeverity(policy),
                  RoboFontValidationStageGlyphColorVar,
                  tag,
                  0u,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->rejectOversized ? 0u : 1u,
                  message);
    }
}

static Result StageGlyphColorVar(FontSfnt *sfnt,
                                           const FontValidationPolicy *policy,
                                           FontValidationResult *result)
{
    FontColorInfo *color = NULL;
    FontVariationModel *variation = NULL;
    FontCffFont *cff = NULL;
    Result res;

    if (!sfnt || !policy || !result) {
        return RoboInvalidArgument;
    }
    if (!ConsumeWork(result, policy, 1u)) {
        return RoboParseError;
    }

    CheckPayloadTable(sfnt,
                        policy,
                        result,
                        RoboFontTagSvg,
                        policy->limits.maxSvgPayload,
                        "SVG table payload exceeds validation limit");
    CheckPayloadTable(sfnt,
                        policy,
                        result,
                        RoboFontTagSbix,
                        policy->limits.maxBitmapPayload,
                        "sbix table payload exceeds validation limit");
    CheckPayloadTable(sfnt,
                        policy,
                        result,
                        RoboFontTagCbdt,
                        policy->limits.maxBitmapPayload,
                        "CBDT table payload exceeds validation limit");

    if (FontSfntHasTable(sfnt, RoboFontTagColr) ||
        FontSfntHasTable(sfnt, RoboFontTagCpal)) {
        if (!ConsumeWork(result, policy, 8u)) {
            return RoboParseError;
        }
        res = FontColorInfoCreate(sfnt, NULL, &color);
        if (res == RoboOk && color) {
            if (FontColorInfoPaletteCount(color) >
                policy->limits.maxColorPalettes) {
                AddIssue(result,
                          RoboFontValidationColorLimit,
                          LimitSeverity(policy),
                          RoboFontValidationStageGlyphColorVar,
                          RoboFontTagCpal,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->rejectOversized ? 0u : 1u,
                          "color palette count exceeds validation limit");
            }
            FontColorInfoRelease(color);
            color = NULL;
        } else if (res != RoboNotFound) {
            AddIssue(result,
                      RoboFontValidationColorLimit,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageGlyphColorVar,
                      RoboFontTagColr,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "color tables failed structural validation");
        }
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    if (FontSfntHasTable(sfnt, RoboFontTagFvar)) {
        if (!ConsumeWork(result, policy, 8u)) {
            return RoboParseError;
        }
        res = FontVariationModelCreate(sfnt, NULL, NULL, &variation);
        if (res == RoboOk && variation) {
            if (FontVariationModelAxisCount(variation) >
                policy->limits.maxVariationAxes) {
                AddIssue(result,
                          RoboFontValidationVariationLimit,
                          LimitSeverity(policy),
                          RoboFontValidationStageGlyphColorVar,
                          RoboFontTagFvar,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->rejectOversized ? 0u : 1u,
                          "variation axis count exceeds validation limit");
            }
            if (FontVariationModelInstanceCount(variation) >
                policy->limits.maxVariationInstances) {
                AddIssue(result,
                          RoboFontValidationVariationLimit,
                          LimitSeverity(policy),
                          RoboFontValidationStageGlyphColorVar,
                          RoboFontTagFvar,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->rejectOversized ? 0u : 1u,
                          "variation instance count exceeds validation limit");
            }
            FontVariationModelRelease(variation);
            variation = NULL;
        } else if (res != RoboNotFound) {
            AddIssue(result,
                      RoboFontValidationVariationLimit,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageGlyphColorVar,
                      RoboFontTagFvar,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "fvar table failed structural validation");
        }
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    if (FontSfntHasTable(sfnt, RoboFontTagCff) ||
        FontSfntHasTable(sfnt, RoboFontTagCff2)) {
        if (!ConsumeWork(result, policy, 8u)) {
            return RoboParseError;
        }
        res = FontCffParse(sfnt, RoboFontCffFlagsRequireCharstrings, &cff);
        if (res != RoboOk) {
            AddIssue(result,
                      RoboFontValidationParseFailed,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageGlyphColorVar,
                      FontSfntHasTable(sfnt, RoboFontTagCff2) ? RoboFontTagCff2
                                                                         : RoboFontTagCff,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "CFF/CFF2 table failed structural probe");
        } else if (cff) {
            FontCffRelease(cff);
            cff = NULL;
        }
        if (ShouldStop(result, policy)) {
            return RoboParseError;
        }
    }

    if (FontSfntHasTable(sfnt, RoboFontTagGsub) ||
        FontSfntHasTable(sfnt, RoboFontTagGpos)) {
        if (policy->allowUnsupportedTables) {
            AddIssue(result,
                      RoboFontValidationUnsupported,
                      RoboFontValidationSeverityInfo,
                      RoboFontValidationStageGlyphColorVar,
                      FontSfntHasTable(sfnt, RoboFontTagGsub) ? RoboFontTagGsub
                                                                         : RoboFontTagGpos,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "layout tables present; shaping not validated");
        }
    }

    result->completedStage = RoboFontValidationStageGlyphColorVar;
    return RoboOk;
}

static Result ValidateSfntInternal(FontSfnt *sfnt,
                                            const FontValidationPolicy *policy,
                                            FontValidationResult *result)
{
    Result res;

    if (!sfnt || !policy || !result) {
        return RoboInvalidArgument;
    }

    res = StageContainer(sfnt, policy, result);
    if (res != RoboOk || ShouldStop(result, policy)) {
        return result->hasFatal ? RoboParseError : RoboOk;
    }

    if (policy->level >= RoboFontValidationBasic) {
        res = StageMetadata(sfnt, policy, result);
        if (res != RoboOk || ShouldStop(result, policy)) {
            return result->hasFatal ? RoboParseError : RoboOk;
        }
    }

    if (policy->level >= RoboFontValidationStrict) {
        res = StageFeatures(sfnt, policy, result);
        if (res != RoboOk || ShouldStop(result, policy)) {
            return result->hasFatal ? RoboParseError : RoboOk;
        }
    }

    if (policy->level >= RoboFontValidationParanoid) {
        res = StageGlyphColorVar(sfnt, policy, result);
        if (res != RoboOk || ShouldStop(result, policy)) {
            return result->hasFatal ? RoboParseError : RoboOk;
        }
    }

    return result->hasFatal ? RoboParseError : RoboOk;
}

void FontValidationLimitsInit(FontValidationLimits *lim)
{
    if (!lim) {
        return;
    }
    memset(lim, 0, sizeof(*lim));
    lim->maxSourceSize = RoboFontSfntMaxSourceSize;
    lim->maxTables = RoboFontSfntMaxTables;
    lim->maxTableSize = DEFAULT_MAX_TABLE_SIZE;
    lim->maxCollectionFaces = RoboFontValidationHardMaxFaces;
    lim->maxGlyphs = RoboFontTtMaxGlyphs;
    lim->maxNameRecords = RoboFontNameMaxRecords;
    lim->maxVariationAxes = RoboFontVarMaxAxes;
    lim->maxVariationInstances = RoboFontVarMaxInstances;
    lim->maxColorPalettes = RoboFontColorMaxPalettes;
    lim->maxColorLayers = RoboFontColorMaxLayers;
    lim->maxPaintDepth = RoboFontColorMaxPaintDepth;
    lim->maxSvgPayload = RoboFontColorMaxSvgPayload;
    lim->maxBitmapPayload = DEFAULT_MAX_BITMAP_PAYLOAD;
    lim->maxRecursionDepth = RoboFontTtMaxCompositeDepth;
    lim->maxAllocation = RoboFontValidationHardMaxAlloc;
    lim->maxWorkBudget = RoboFontValidationHardMaxWork;
}

void FontValidationLimitsClamp(FontValidationLimits *lim)
{
    if (!lim) {
        return;
    }
    lim->maxSourceSize =
        ClampU64(lim->maxSourceSize, 12u, RoboFontValidationHardMaxSourceSize);
    lim->maxTables = ClampU32(lim->maxTables, 1u, RoboFontValidationHardMaxTables);
    lim->maxTableSize = ClampU32(lim->maxTableSize, 1024u, DEFAULT_MAX_TABLE_SIZE);
    lim->maxCollectionFaces =
        ClampU32(lim->maxCollectionFaces, 1u, RoboFontValidationHardMaxFaces);
    lim->maxGlyphs = ClampU32(lim->maxGlyphs, 1u, RoboFontValidationHardMaxGlyphs);
    lim->maxNameRecords = ClampU32(lim->maxNameRecords, 1u, RoboFontTtMaxNameRecords);
    lim->maxVariationAxes = ClampU32(lim->maxVariationAxes, 1u, RoboFontVarMaxAxes);
    lim->maxVariationInstances =
        ClampU32(lim->maxVariationInstances, 1u, RoboFontVarMaxInstances);
    lim->maxColorPalettes =
        ClampU32(lim->maxColorPalettes, 1u, RoboFontColorMaxPalettes);
    lim->maxColorLayers = ClampU32(lim->maxColorLayers, 1u, RoboFontColorMaxLayers);
    lim->maxPaintDepth =
        ClampU32(lim->maxPaintDepth, 1u, RoboFontColorMaxPaintDepth);
    lim->maxSvgPayload =
        ClampU32(lim->maxSvgPayload, 1024u, RoboFontColorMaxSvgPayload);
    lim->maxBitmapPayload = ClampU32(lim->maxBitmapPayload, 1024u, DEFAULT_MAX_BITMAP_PAYLOAD);
    lim->maxRecursionDepth =
        ClampU32(lim->maxRecursionDepth, 1u, RoboFontTtMaxCompositeDepth);
    lim->maxAllocation =
        ClampU32(lim->maxAllocation, 1024u, RoboFontValidationHardMaxAlloc);
    lim->maxWorkBudget =
        ClampU32(lim->maxWorkBudget, 1u, RoboFontValidationHardMaxWork);
}

void FontValidationPolicyInit(FontValidationPolicy *pol)
{
    if (!pol) {
        return;
    }
    memset(pol, 0, sizeof(*pol));
    pol->level = RoboFontValidationBasic;
    pol->checksumPolicy = RoboFontChecksumWarn;
    pol->allowUnsupportedTables = 1u;
    pol->rejectOversized = 1u;
    pol->stopOnFatal = 1u;
    FontValidationLimitsInit(&pol->limits);
    FontValidationLimitsClamp(&pol->limits);
}

FontValidationLevel FontValidationLevelFromLoader(
    FontValidatePolicy loaderPol)
{
    switch (loaderPol) {
    case RoboFontValidateTrusted:
        return RoboFontValidationNone;
    case RoboFontValidateNormal:
        return RoboFontValidationBasic;
    case RoboFontValidateStrict:
        return RoboFontValidationStrict;
    case RoboFontValidateParanoid:
        return RoboFontValidationParanoid;
    default:
        return RoboFontValidationStrict;
    }
}

void FontValidationPolicyFromLoader(FontValidatePolicy loaderPol,
                                             FontValidationPolicy *out)
{
    if (!out) {
        return;
    }
    FontValidationPolicyInit(out);
    out->level = FontValidationLevelFromLoader(loaderPol);
    switch (loaderPol) {
    case RoboFontValidateTrusted:
        out->checksumPolicy = RoboFontChecksumAccept;
        break;
    case RoboFontValidateNormal:
        out->checksumPolicy = RoboFontChecksumWarn;
        break;
    case RoboFontValidateStrict:
        /* Diagnostics + head verify via TT; directory mismatches warn only. */
        out->checksumPolicy = RoboFontChecksumWarn;
        break;
    case RoboFontValidateParanoid:
        out->checksumPolicy = RoboFontChecksumReject;
        break;
    default:
        out->level = RoboFontValidationStrict;
        out->checksumPolicy = RoboFontChecksumWarn;
        break;
    }
}

FontValidatePolicy FontValidationLevelToLoader(
    FontValidationLevel level)
{
    switch (level) {
    case RoboFontValidationNone:
        return RoboFontValidateTrusted;
    case RoboFontValidationBasic:
        return RoboFontValidateNormal;
    case RoboFontValidationStrict:
        return RoboFontValidateStrict;
    case RoboFontValidationParanoid:
        return RoboFontValidateParanoid;
    default:
        return RoboFontValidateStrict;
    }
}

const char *FontValidationIssueCodeString(FontValidationIssueCode code)
{
    if ((unsigned)code >= sizeof(kIssueCodeStrings) / sizeof(kIssueCodeStrings[0])) {
        return "unknown";
    }
    return kIssueCodeStrings[code];
}

const char *FontValidationSeverityString(FontValidationSeverity sev)
{
    if ((unsigned)sev >= sizeof(kSeverityStrings) / sizeof(kSeverityStrings[0])) {
        return "unknown";
    }
    return kSeverityStrings[sev];
}

const char *FontValidationStageString(FontValidationStage stage)
{
    if (stage < RoboFontValidationStageContainer ||
        stage > RoboFontValidationStageGlyphColorVar) {
        return kStageStrings[0];
    }
    return kStageStrings[stage];
}

Result FontValidationResultCreate(FontValidationResult **out)
{
    FontValidationResult *result;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    result = (FontValidationResult *)RoboCalloc(1, sizeof(*result));
    if (!result) {
        return RoboOutOfMemory;
    }
    result->cookie = RESULT_COOKIE;
    result->refs = 1u;
    *out = result;
    return RoboOk;
}

void FontValidationResultRetain(FontValidationResult *result)
{
    if (!result || result->cookie != RESULT_COOKIE) {
        return;
    }
    result->refs++;
}

void FontValidationResultRelease(FontValidationResult *result)
{
    if (!result || result->cookie != RESULT_COOKIE) {
        return;
    }
    if (result->refs == 0u) {
        return;
    }
    result->refs--;
    if (result->refs != 0u) {
        return;
    }
    result->cookie = 0u;
    RoboFree(result);
}

int FontValidationResultOk(const FontValidationResult *result)
{
    return result && result->cookie == RESULT_COOKIE && !result->hasFatal;
}

int FontValidationResultHasFatal(const FontValidationResult *result)
{
    return result && result->cookie == RESULT_COOKIE && result->hasFatal;
}

uint32_t FontValidationResultIssueCount(const FontValidationResult *result)
{
    return result && result->cookie == RESULT_COOKIE ? result->issueCount : 0u;
}

Result FontValidationResultIssue(const FontValidationResult *result,
                                                uint32_t index,
                                                FontValidationIssue *out)
{
    if (!result || result->cookie != RESULT_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    if (index >= result->issueCount) {
        return RoboNotFound;
    }
    *out = result->issues[index];
    return RoboOk;
}

FontValidationStage FontValidationResultCompletedStage(
    const FontValidationResult *result)
{
    return result && result->cookie == RESULT_COOKIE ? result->completedStage
                                                     : (FontValidationStage)0;
}

uint32_t FontValidationResultWorkUnits(const FontValidationResult *result)
{
    return result && result->cookie == RESULT_COOKIE ? result->workUnits : 0u;
}

Result FontValidateSource(FontSource *source,
                                        uint32_t faceIndex,
                                        const FontValidationPolicy *policy,
                                        FontValidationResult **outResult,
                                        FontSfnt **outSfntOpt)
{
    FontValidationResult *result = NULL;
    FontSfnt *sfnt = NULL;
    uint32_t faceCount = 0u;
    uint32_t sfntFlags = RoboFontSfntFlagsNone;
    size_t sourceSize;
    Result res;

    if (!source || !policy || !outResult) {
        return RoboInvalidArgument;
    }
    *outResult = NULL;
    if (outSfntOpt) {
        *outSfntOpt = NULL;
    }

    res = FontValidationResultCreate(&result);
    if (res != RoboOk) {
        return res;
    }

    sourceSize = FontSourceSize(source);
    if (sourceSize > (size_t)policy->limits.maxSourceSize) {
        AddIssue(result,
                  RoboFontValidationSourceTooLarge,
                  LimitSeverity(policy),
                  RoboFontValidationStageContainer,
                  0u,
                  0u,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->rejectOversized ? 0u : 1u,
                  "font source exceeds validation size limit");
        *outResult = result;
        return RoboParseError;
    }

    if (FontSfntProbeFaceCountSource(source, &faceCount) == RoboOk &&
        faceCount > policy->limits.maxCollectionFaces) {
        AddIssue(result,
                  RoboFontValidationCollectionFaces,
                  LimitSeverity(policy),
                  RoboFontValidationStageContainer,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->rejectOversized ? 0u : 1u,
                  "collection face count exceeds validation limit");
        if (ShouldStop(result, policy)) {
            *outResult = result;
            return RoboParseError;
        }
    }

    /* Always parse without hard VERIFY_CHECKSUMS so mismatches become
     * structured diagnostics under checksumPolicy (WARN/REJECT). */
    res = FontSfntParse(source, faceIndex, sfntFlags, &sfnt);
    if (res != RoboOk) {
        FontValidationIssueCode code = RoboFontValidationParseFailed;
        const char *message = "SFNT container parse failed";

        if (res == RoboNotSupported) {
            code = RoboFontValidationInvalidSignature;
            message = "unsupported or invalid SFNT signature";
        } else if (res == RoboNotFound) {
            message = "face index not found in collection";
        }
        AddIssue(result,
                  code,
                  RoboFontValidationSeverityFatal,
                  RoboFontValidationStageContainer,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  0u,
                  message);
        *outResult = result;
        return RoboParseError;
    }

    (void)ValidateSfntInternal(sfnt, policy, result);

    if (outSfntOpt && !result->hasFatal) {
        *outSfntOpt = sfnt;
    } else if (sfnt) {
        FontSfntRelease(sfnt);
    }

    *outResult = result;
    return result->hasFatal ? RoboParseError : RoboOk;
}

Result FontValidateSfnt(FontSfnt *sfnt,
                                      const FontValidationPolicy *policy,
                                      FontValidationResult **outResult)
{
    FontValidationResult *result = NULL;
    Result res;

    if (!sfnt || !policy || !outResult) {
        return RoboInvalidArgument;
    }
    *outResult = NULL;

    res = FontValidationResultCreate(&result);
    if (res != RoboOk) {
        return res;
    }

    (void)ValidateSfntInternal(sfnt, policy, result);
    *outResult = result;
    return result->hasFatal ? RoboParseError : RoboOk;
}

Result FontValidateFace(FontFace *face,
                                      const FontValidationPolicy *policy,
                                      FontValidationResult **outResult)
{
    FontValidationResult *result = NULL;
    const FontSfnt *sfntConst = NULL;
    FontSfnt *sfnt = NULL;
    Result res;

    if (!face || !policy || !outResult) {
        return RoboInvalidArgument;
    }
    *outResult = NULL;

    res = FontValidationResultCreate(&result);
    if (res != RoboOk) {
        return res;
    }

    res = FontFaceParseSfnt(face);
    if (res != RoboOk) {
        AddIssue(result,
                  RoboFontValidationParseFailed,
                  RoboFontValidationSeverityFatal,
                  RoboFontValidationStageContainer,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  0u,
                  "face SFNT parse failed");
        *outResult = result;
        return RoboParseError;
    }

    sfntConst = FontFaceSfnt(face);
    sfnt = (FontSfnt *)sfntConst;
    (void)ValidateSfntInternal(sfnt, policy, result);
    if (ShouldStop(result, policy)) {
        *outResult = result;
        return RoboParseError;
    }

    if (policy->level >= RoboFontValidationBasic) {
        res = FontFaceParseMetadata(face);
        if (res != RoboOk && res != RoboNotFound) {
            AddIssue(result,
                      RoboFontValidationParseFailed,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageMetadata,
                      RoboFontTagName,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "face metadata parse failed");
        }
        if (ShouldStop(result, policy)) {
            *outResult = result;
            return RoboParseError;
        }
    }

    if (policy->level >= RoboFontValidationParanoid) {
        res = FontFaceParseColor(face);
        if (res != RoboOk && res != RoboNotFound) {
            AddIssue(result,
                      RoboFontValidationColorLimit,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageGlyphColorVar,
                      RoboFontTagColr,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "face color parse failed");
        }
        res = FontFaceParseVariation(face);
        if (res != RoboOk && res != RoboNotFound) {
            AddIssue(result,
                      RoboFontValidationVariationLimit,
                      RoboFontValidationSeverityWarning,
                      RoboFontValidationStageGlyphColorVar,
                      RoboFontTagFvar,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "face variation parse failed");
        }
        if (ShouldStop(result, policy)) {
            *outResult = result;
            return RoboParseError;
        }
    }

    if (policy->level >= RoboFontValidationStrict) {
        res = FontFaceParseTruetype(face, RoboFontTtFlagsVerifyHeadChecksum);
        if (res != RoboOk) {
            AddIssue(result,
                      RoboFontValidationChecksumMismatch,
                      policy->checksumPolicy == RoboFontChecksumReject
                          ? RoboFontValidationSeverityFatal
                          : RoboFontValidationSeverityWarning,
                      RoboFontValidationStageMetadata,
                      RoboFontTagHead,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      policy->checksumPolicy != RoboFontChecksumReject,
                      "head table checksum verification failed");
        }
    }

    *outResult = result;
    return result->hasFatal ? RoboParseError : RoboOk;
}

Result FontValidateForLoad(FontFace *face,
                                          FontValidatePolicy loaderPol,
                                          FontValidationResult **outResultOpt)
{
    FontValidationPolicy policy;
    FontValidationResult *result = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    FontValidationPolicyFromLoader(loaderPol, &policy);
    res = FontValidateFace(face, &policy, &result);
    if (outResultOpt) {
        *outResultOpt = result;
    } else if (result) {
        FontValidationResultRelease(result);
    }
    if (res != RoboOk) {
        return RoboParseError;
    }
    if (result && result->hasFatal) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontValidateFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *source = NULL;
    FontValidationPolicy basic;
    FontValidationPolicy paranoid;
    FontValidationResult *result = NULL;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    if (size == 0u) {
        return RoboOk;
    }
    if (size > 256u * 1024u) {
        size = 256u * 1024u;
    }

    if (FontSourceCreateMemoryCopy(data, size, &source) != RoboOk) {
        return RoboOk;
    }

    FontValidationPolicyInit(&basic);
    basic.level = RoboFontValidationBasic;
    (void)FontValidateSource(source, 0u, &basic, &result, NULL);
    if (result) {
        FontValidationResultRelease(result);
        result = NULL;
    }

    FontValidationPolicyInit(&paranoid);
    paranoid.level = RoboFontValidationParanoid;
    paranoid.checksumPolicy = RoboFontChecksumWarn;
    (void)FontValidateSource(source, 0u, &paranoid, &result, NULL);
    if (result) {
        FontValidationResultRelease(result);
    }

    FontSourceRelease(source);
    return RoboOk;
}
