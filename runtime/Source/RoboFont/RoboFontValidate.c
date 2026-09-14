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

struct robo_font_validation_result {
    uint32_t cookie;
    uint32_t refs;
    robo_font_validation_issue_t issues[ROBO_FONT_VALIDATION_MAX_ISSUES];
    uint32_t issue_count;
    uint8_t has_fatal;
    robo_font_validation_stage_t completed_stage;
    uint32_t work_units;
};

static const char *k_issue_code_strings[] = {
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

static const char *k_severity_strings[] = {
    "info",
    "warning",
    "error",
    "fatal"
};

static const char *k_stage_strings[] = {
    "unknown",
    "container",
    "metadata",
    "features",
    "glyph_color_var"
};

static void copy_message(char *dst, size_t cap, const char *src)
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

static uint32_t clamp_u32(uint32_t value, uint32_t min_v, uint32_t max_v)
{
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static uint64_t clamp_u64(uint64_t value, uint64_t min_v, uint64_t max_v)
{
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static void add_issue(robo_font_validation_result_t *result,
                      robo_font_validation_issue_code_t code,
                      robo_font_validation_severity_t severity,
                      robo_font_validation_stage_t stage,
                      robo_font_tag_t table_tag,
                      uint32_t byte_offset,
                      uint32_t glyph_id,
                      robo_font_tag_t axis_tag,
                      uint8_t recoverable,
                      const char *message)
{
    robo_font_validation_issue_t *issue;

    if (!result || result->cookie != RESULT_COOKIE) {
        return;
    }
    if (severity == ROBO_FONT_VALIDATION_SEVERITY_FATAL) {
        result->has_fatal = 1u;
    }
    if (result->issue_count >= ROBO_FONT_VALIDATION_MAX_ISSUES) {
        return;
    }
    issue = &result->issues[result->issue_count++];
    issue->code = code;
    issue->severity = severity;
    issue->stage = stage;
    issue->table_tag = table_tag;
    issue->byte_offset = byte_offset;
    issue->glyph_id = glyph_id;
    issue->axis_tag = axis_tag;
    issue->recoverable = recoverable;
    copy_message(issue->message, sizeof(issue->message), message);
}

static int consume_work(robo_font_validation_result_t *result,
                        const robo_font_validation_policy_t *policy,
                        uint32_t units)
{
    if (!result || !policy) {
        return 0;
    }
    if (result->work_units + units > policy->limits.max_work_budget) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_WORK_BUDGET,
                  policy->reject_oversized ? ROBO_FONT_VALIDATION_SEVERITY_FATAL
                                           : ROBO_FONT_VALIDATION_SEVERITY_ERROR,
                  result->completed_stage ? result->completed_stage
                                          : ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->reject_oversized ? 0u : 1u,
                  "validation work budget exceeded");
        return 0;
    }
    result->work_units += units;
    return 1;
}

static int should_stop(const robo_font_validation_result_t *result,
                       const robo_font_validation_policy_t *policy)
{
    return policy && policy->stop_on_fatal && result && result->has_fatal;
}

static robo_font_validation_severity_t limit_severity(
    const robo_font_validation_policy_t *policy)
{
    return policy && policy->reject_oversized ? ROBO_FONT_VALIDATION_SEVERITY_FATAL
                                              : ROBO_FONT_VALIDATION_SEVERITY_ERROR;
}

static void report_checksum_mismatch(robo_font_validation_result_t *result,
                                     const robo_font_validation_policy_t *policy,
                                     robo_font_tag_t tag)
{
    robo_font_validation_severity_t severity;
    uint8_t recoverable = 1u;

    if (!result || !policy) {
        return;
    }
    switch (policy->checksum_policy) {
    case ROBO_FONT_CHECKSUM_ACCEPT:
        severity = ROBO_FONT_VALIDATION_SEVERITY_INFO;
        break;
    case ROBO_FONT_CHECKSUM_WARN:
        severity = ROBO_FONT_VALIDATION_SEVERITY_WARNING;
        break;
    case ROBO_FONT_CHECKSUM_REJECT:
    default:
        severity = ROBO_FONT_VALIDATION_SEVERITY_FATAL;
        recoverable = 0u;
        break;
    }
    add_issue(result,
              ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH,
              severity,
              ROBO_FONT_VALIDATION_STAGE_CONTAINER,
              tag,
              ISSUE_OFFSET_NA,
              ISSUE_GLYPH_NA,
              0u,
              recoverable,
              "table checksum mismatch");
}

static robo_result_t stage_container(robo_font_sfnt_t *sfnt,
                                     const robo_font_validation_policy_t *policy,
                                     robo_font_validation_result_t *result)
{
    const robo_font_sfnt_header_t *header;
    uint32_t table_count;
    uint32_t i;

    if (!sfnt || !policy || !result) {
        return ROBO_INVALID_ARGUMENT;
    }

    header = robo_font_sfnt_header(sfnt);
    table_count = robo_font_sfnt_table_count(sfnt);
    if (!consume_work(result, policy, 1u)) {
        return ROBO_PARSE_ERROR;
    }

    if (table_count > policy->limits.max_tables) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_TABLE_COUNT,
                  limit_severity(policy),
                  ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->reject_oversized ? 0u : 1u,
                  "table count exceeds validation limit");
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    if (header && !robo_font_sfnt_search_fields_consistent(header)) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_TABLE_COUNT,
                  policy->level >= ROBO_FONT_VALIDATION_STRICT
                      ? ROBO_FONT_VALIDATION_SEVERITY_WARNING
                      : ROBO_FONT_VALIDATION_SEVERITY_INFO,
                  ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  1u,
                  "SFNT searchRange fields inconsistent");
    }

    for (i = 0u; i < table_count; i++) {
        robo_font_sfnt_table_record_t rec;

        if (!consume_work(result, policy, 1u)) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_sfnt_table_at(sfnt, i, &rec) != ROBO_OK) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_INTERNAL,
                      ROBO_FONT_VALIDATION_SEVERITY_FATAL,
                      ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                      0u,
                      ISSUE_OFFSET_NA,
                      ISSUE_GLYPH_NA,
                      0u,
                      0u,
                      "failed to enumerate SFNT table");
            return ROBO_PARSE_ERROR;
        }
        if (rec.length > policy->limits.max_table_size) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_TABLE_OFFSET,
                      limit_severity(policy),
                      ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                      rec.tag,
                      rec.offset,
                      ISSUE_GLYPH_NA,
                      0u,
                      policy->reject_oversized ? 0u : 1u,
                      "table length exceeds validation limit");
            if (should_stop(result, policy)) {
                return ROBO_PARSE_ERROR;
            }
        }
    }

    if (policy->level >= ROBO_FONT_VALIDATION_STRICT ||
        policy->checksum_policy != ROBO_FONT_CHECKSUM_ACCEPT) {
        for (i = 0u; i < table_count; i++) {
            robo_font_sfnt_table_record_t rec;

            if (!consume_work(result, policy, 2u)) {
                return ROBO_PARSE_ERROR;
            }
            if (robo_font_sfnt_table_at(sfnt, i, &rec) != ROBO_OK) {
                continue;
            }
            if (robo_font_sfnt_verify_table_checksum(sfnt, rec.tag) != ROBO_OK) {
                report_checksum_mismatch(result, policy, rec.tag);
                if (should_stop(result, policy)) {
                    return ROBO_PARSE_ERROR;
                }
            }
        }
    }

    result->completed_stage = ROBO_FONT_VALIDATION_STAGE_CONTAINER;
    return ROBO_OK;
}

static robo_result_t stage_metadata(robo_font_sfnt_t *sfnt,
                                    const robo_font_validation_policy_t *policy,
                                    robo_font_validation_result_t *result)
{
    int has_head;
    int has_maxp;
    uint32_t num_glyphs = 0u;
    robo_result_t res;

    if (!sfnt || !policy || !result) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!consume_work(result, policy, 1u)) {
        return ROBO_PARSE_ERROR;
    }

    has_head = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_HEAD);
    has_maxp = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_MAXP);

    if (!has_head || !has_maxp) {
        robo_font_validation_severity_t severity =
            policy->level >= ROBO_FONT_VALIDATION_STRICT
                ? ROBO_FONT_VALIDATION_SEVERITY_FATAL
                : ROBO_FONT_VALIDATION_SEVERITY_WARNING;
        if (!has_head) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_MISSING_REQUIRED,
                      severity,
                      ROBO_FONT_VALIDATION_STAGE_METADATA,
                      ROBO_FONT_TAG_HEAD,
                      ISSUE_OFFSET_NA,
                      ISSUE_GLYPH_NA,
                      0u,
                      severity != ROBO_FONT_VALIDATION_SEVERITY_FATAL,
                      "missing required head table");
        }
        if (!has_maxp) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_MISSING_REQUIRED,
                      severity,
                      ROBO_FONT_VALIDATION_STAGE_METADATA,
                      ROBO_FONT_TAG_MAXP,
                      ISSUE_OFFSET_NA,
                      ISSUE_GLYPH_NA,
                      0u,
                      severity != ROBO_FONT_VALIDATION_SEVERITY_FATAL,
                      "missing required maxp table");
        }
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    res = robo_font_sfnt_num_glyphs(sfnt, &num_glyphs);
    if (res == ROBO_OK) {
        if (num_glyphs > policy->limits.max_glyphs) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_GLYPH_COUNT,
                      limit_severity(policy),
                      ROBO_FONT_VALIDATION_STAGE_METADATA,
                      ROBO_FONT_TAG_MAXP,
                      ISSUE_OFFSET_NA,
                      num_glyphs,
                      0u,
                      policy->reject_oversized ? 0u : 1u,
                      "glyph count exceeds validation limit");
            if (should_stop(result, policy)) {
                return ROBO_PARSE_ERROR;
            }
        }
    } else if (policy->level >= ROBO_FONT_VALIDATION_STRICT) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_PARSE_FAILED,
                  ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                  ROBO_FONT_VALIDATION_STAGE_METADATA,
                  ROBO_FONT_TAG_MAXP,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  1u,
                  "unable to read glyph count from maxp");
    }

    result->completed_stage = ROBO_FONT_VALIDATION_STAGE_METADATA;
    return ROBO_OK;
}

static robo_result_t stage_features(robo_font_sfnt_t *sfnt,
                                    const robo_font_validation_policy_t *policy,
                                    robo_font_validation_result_t *result)
{
    robo_font_span_t span;

    if (!sfnt || !policy || !result) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!consume_work(result, policy, 1u)) {
        return ROBO_PARSE_ERROR;
    }

    if (robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_NAME)) {
        robo_font_name_table_t name_table;

        memset(&name_table, 0, sizeof(name_table));
        if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_NAME, &span) == ROBO_OK) {
            if (span.size > (size_t)policy->limits.max_allocation) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_NAME_LIMIT,
                          limit_severity(policy),
                          ROBO_FONT_VALIDATION_STAGE_FEATURES,
                          ROBO_FONT_TAG_NAME,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->reject_oversized ? 0u : 1u,
                          "name table exceeds allocation limit");
            } else if (robo_font_name_parse(span, &name_table) != ROBO_OK) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_PARSE_FAILED,
                          ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                          ROBO_FONT_VALIDATION_STAGE_FEATURES,
                          ROBO_FONT_TAG_NAME,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          1u,
                          "name table failed structural parse");
            } else if (name_table.count > policy->limits.max_name_records) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_NAME_LIMIT,
                          limit_severity(policy),
                          ROBO_FONT_VALIDATION_STAGE_FEATURES,
                          ROBO_FONT_TAG_NAME,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->reject_oversized ? 0u : 1u,
                          "name record count exceeds validation limit");
            }
            robo_font_name_table_clear(&name_table);
        }
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    if (robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CMAP)) {
        robo_font_cmap_t cmap;

        memset(&cmap, 0, sizeof(cmap));
        if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CMAP, &span) == ROBO_OK) {
            if (robo_font_cmap_parse(span, &cmap) != ROBO_OK) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_PARSE_FAILED,
                          ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                          ROBO_FONT_VALIDATION_STAGE_FEATURES,
                          ROBO_FONT_TAG_CMAP,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          1u,
                          "cmap table failed structural parse");
            }
            robo_font_cmap_clear(&cmap);
        } else {
            add_issue(result,
                      ROBO_FONT_VALIDATION_PARSE_FAILED,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_FEATURES,
                      ROBO_FONT_TAG_CMAP,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "cmap table bytes unavailable");
        }
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    result->completed_stage = ROBO_FONT_VALIDATION_STAGE_FEATURES;
    return ROBO_OK;
}

static void check_payload_table(robo_font_sfnt_t *sfnt,
                                const robo_font_validation_policy_t *policy,
                                robo_font_validation_result_t *result,
                                robo_font_tag_t tag,
                                uint32_t max_payload,
                                const char *message)
{
    robo_font_span_t span;

    if (!robo_font_sfnt_has_table(sfnt, tag)) {
        return;
    }
    if (robo_font_sfnt_table_bytes(sfnt, tag, &span) == ROBO_OK &&
        span.size > (size_t)max_payload) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_COLOR_LIMIT,
                  limit_severity(policy),
                  ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                  tag,
                  0u,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->reject_oversized ? 0u : 1u,
                  message);
    }
}

static robo_result_t stage_glyph_color_var(robo_font_sfnt_t *sfnt,
                                           const robo_font_validation_policy_t *policy,
                                           robo_font_validation_result_t *result)
{
    robo_font_color_info_t *color = NULL;
    robo_font_variation_model_t *variation = NULL;
    robo_font_cff_font_t *cff = NULL;
    robo_result_t res;

    if (!sfnt || !policy || !result) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!consume_work(result, policy, 1u)) {
        return ROBO_PARSE_ERROR;
    }

    check_payload_table(sfnt,
                        policy,
                        result,
                        ROBO_FONT_TAG_SVG,
                        policy->limits.max_svg_payload,
                        "SVG table payload exceeds validation limit");
    check_payload_table(sfnt,
                        policy,
                        result,
                        ROBO_FONT_TAG_SBIX,
                        policy->limits.max_bitmap_payload,
                        "sbix table payload exceeds validation limit");
    check_payload_table(sfnt,
                        policy,
                        result,
                        ROBO_FONT_TAG_CBDT,
                        policy->limits.max_bitmap_payload,
                        "CBDT table payload exceeds validation limit");

    if (robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_COLR) ||
        robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CPAL)) {
        if (!consume_work(result, policy, 8u)) {
            return ROBO_PARSE_ERROR;
        }
        res = robo_font_color_info_create(sfnt, NULL, &color);
        if (res == ROBO_OK && color) {
            if (robo_font_color_info_palette_count(color) >
                policy->limits.max_color_palettes) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_COLOR_LIMIT,
                          limit_severity(policy),
                          ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                          ROBO_FONT_TAG_CPAL,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->reject_oversized ? 0u : 1u,
                          "color palette count exceeds validation limit");
            }
            robo_font_color_info_release(color);
            color = NULL;
        } else if (res != ROBO_NOT_FOUND) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_COLOR_LIMIT,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                      ROBO_FONT_TAG_COLR,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "color tables failed structural validation");
        }
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    if (robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_FVAR)) {
        if (!consume_work(result, policy, 8u)) {
            return ROBO_PARSE_ERROR;
        }
        res = robo_font_variation_model_create(sfnt, NULL, NULL, &variation);
        if (res == ROBO_OK && variation) {
            if (robo_font_variation_model_axis_count(variation) >
                policy->limits.max_variation_axes) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_VARIATION_LIMIT,
                          limit_severity(policy),
                          ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                          ROBO_FONT_TAG_FVAR,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->reject_oversized ? 0u : 1u,
                          "variation axis count exceeds validation limit");
            }
            if (robo_font_variation_model_instance_count(variation) >
                policy->limits.max_variation_instances) {
                add_issue(result,
                          ROBO_FONT_VALIDATION_VARIATION_LIMIT,
                          limit_severity(policy),
                          ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                          ROBO_FONT_TAG_FVAR,
                          0u,
                          ISSUE_GLYPH_NA,
                          0u,
                          policy->reject_oversized ? 0u : 1u,
                          "variation instance count exceeds validation limit");
            }
            robo_font_variation_model_release(variation);
            variation = NULL;
        } else if (res != ROBO_NOT_FOUND) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_VARIATION_LIMIT,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                      ROBO_FONT_TAG_FVAR,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "fvar table failed structural validation");
        }
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    if (robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF) ||
        robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF2)) {
        if (!consume_work(result, policy, 8u)) {
            return ROBO_PARSE_ERROR;
        }
        res = robo_font_cff_parse(sfnt, ROBO_FONT_CFF_FLAGS_REQUIRE_CHARSTRINGS, &cff);
        if (res != ROBO_OK) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_PARSE_FAILED,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                      robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF2) ? ROBO_FONT_TAG_CFF2
                                                                         : ROBO_FONT_TAG_CFF,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "CFF/CFF2 table failed structural probe");
        } else if (cff) {
            robo_font_cff_release(cff);
            cff = NULL;
        }
        if (should_stop(result, policy)) {
            return ROBO_PARSE_ERROR;
        }
    }

    if (robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GSUB) ||
        robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GPOS)) {
        if (policy->allow_unsupported_tables) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_UNSUPPORTED,
                      ROBO_FONT_VALIDATION_SEVERITY_INFO,
                      ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                      robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GSUB) ? ROBO_FONT_TAG_GSUB
                                                                         : ROBO_FONT_TAG_GPOS,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "layout tables present; shaping not validated");
        }
    }

    result->completed_stage = ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR;
    return ROBO_OK;
}

static robo_result_t validate_sfnt_internal(robo_font_sfnt_t *sfnt,
                                            const robo_font_validation_policy_t *policy,
                                            robo_font_validation_result_t *result)
{
    robo_result_t res;

    if (!sfnt || !policy || !result) {
        return ROBO_INVALID_ARGUMENT;
    }

    res = stage_container(sfnt, policy, result);
    if (res != ROBO_OK || should_stop(result, policy)) {
        return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
    }

    if (policy->level >= ROBO_FONT_VALIDATION_BASIC) {
        res = stage_metadata(sfnt, policy, result);
        if (res != ROBO_OK || should_stop(result, policy)) {
            return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
        }
    }

    if (policy->level >= ROBO_FONT_VALIDATION_STRICT) {
        res = stage_features(sfnt, policy, result);
        if (res != ROBO_OK || should_stop(result, policy)) {
            return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
        }
    }

    if (policy->level >= ROBO_FONT_VALIDATION_PARANOID) {
        res = stage_glyph_color_var(sfnt, policy, result);
        if (res != ROBO_OK || should_stop(result, policy)) {
            return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
        }
    }

    return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
}

void robo_font_validation_limits_init(robo_font_validation_limits_t *lim)
{
    if (!lim) {
        return;
    }
    memset(lim, 0, sizeof(*lim));
    lim->max_source_size = ROBO_FONT_SFNT_MAX_SOURCE_SIZE;
    lim->max_tables = ROBO_FONT_SFNT_MAX_TABLES;
    lim->max_table_size = DEFAULT_MAX_TABLE_SIZE;
    lim->max_collection_faces = ROBO_FONT_VALIDATION_HARD_MAX_FACES;
    lim->max_glyphs = ROBO_FONT_TT_MAX_GLYPHS;
    lim->max_name_records = ROBO_FONT_NAME_MAX_RECORDS;
    lim->max_variation_axes = ROBO_FONT_VAR_MAX_AXES;
    lim->max_variation_instances = ROBO_FONT_VAR_MAX_INSTANCES;
    lim->max_color_palettes = ROBO_FONT_COLOR_MAX_PALETTES;
    lim->max_color_layers = ROBO_FONT_COLOR_MAX_LAYERS;
    lim->max_paint_depth = ROBO_FONT_COLOR_MAX_PAINT_DEPTH;
    lim->max_svg_payload = ROBO_FONT_COLOR_MAX_SVG_PAYLOAD;
    lim->max_bitmap_payload = DEFAULT_MAX_BITMAP_PAYLOAD;
    lim->max_recursion_depth = ROBO_FONT_TT_MAX_COMPOSITE_DEPTH;
    lim->max_allocation = ROBO_FONT_VALIDATION_HARD_MAX_ALLOC;
    lim->max_work_budget = ROBO_FONT_VALIDATION_HARD_MAX_WORK;
}

void robo_font_validation_limits_clamp(robo_font_validation_limits_t *lim)
{
    if (!lim) {
        return;
    }
    lim->max_source_size =
        clamp_u64(lim->max_source_size, 12u, ROBO_FONT_VALIDATION_HARD_MAX_SOURCE_SIZE);
    lim->max_tables = clamp_u32(lim->max_tables, 1u, ROBO_FONT_VALIDATION_HARD_MAX_TABLES);
    lim->max_table_size = clamp_u32(lim->max_table_size, 1024u, DEFAULT_MAX_TABLE_SIZE);
    lim->max_collection_faces =
        clamp_u32(lim->max_collection_faces, 1u, ROBO_FONT_VALIDATION_HARD_MAX_FACES);
    lim->max_glyphs = clamp_u32(lim->max_glyphs, 1u, ROBO_FONT_VALIDATION_HARD_MAX_GLYPHS);
    lim->max_name_records = clamp_u32(lim->max_name_records, 1u, ROBO_FONT_TT_MAX_NAME_RECORDS);
    lim->max_variation_axes = clamp_u32(lim->max_variation_axes, 1u, ROBO_FONT_VAR_MAX_AXES);
    lim->max_variation_instances =
        clamp_u32(lim->max_variation_instances, 1u, ROBO_FONT_VAR_MAX_INSTANCES);
    lim->max_color_palettes =
        clamp_u32(lim->max_color_palettes, 1u, ROBO_FONT_COLOR_MAX_PALETTES);
    lim->max_color_layers = clamp_u32(lim->max_color_layers, 1u, ROBO_FONT_COLOR_MAX_LAYERS);
    lim->max_paint_depth =
        clamp_u32(lim->max_paint_depth, 1u, ROBO_FONT_COLOR_MAX_PAINT_DEPTH);
    lim->max_svg_payload =
        clamp_u32(lim->max_svg_payload, 1024u, ROBO_FONT_COLOR_MAX_SVG_PAYLOAD);
    lim->max_bitmap_payload = clamp_u32(lim->max_bitmap_payload, 1024u, DEFAULT_MAX_BITMAP_PAYLOAD);
    lim->max_recursion_depth =
        clamp_u32(lim->max_recursion_depth, 1u, ROBO_FONT_TT_MAX_COMPOSITE_DEPTH);
    lim->max_allocation =
        clamp_u32(lim->max_allocation, 1024u, ROBO_FONT_VALIDATION_HARD_MAX_ALLOC);
    lim->max_work_budget =
        clamp_u32(lim->max_work_budget, 1u, ROBO_FONT_VALIDATION_HARD_MAX_WORK);
}

void robo_font_validation_policy_init(robo_font_validation_policy_t *pol)
{
    if (!pol) {
        return;
    }
    memset(pol, 0, sizeof(*pol));
    pol->level = ROBO_FONT_VALIDATION_BASIC;
    pol->checksum_policy = ROBO_FONT_CHECKSUM_WARN;
    pol->allow_unsupported_tables = 1u;
    pol->reject_oversized = 1u;
    pol->stop_on_fatal = 1u;
    robo_font_validation_limits_init(&pol->limits);
    robo_font_validation_limits_clamp(&pol->limits);
}

robo_font_validation_level_t robo_font_validation_level_from_loader(
    robo_font_validate_policy_t loader_pol)
{
    switch (loader_pol) {
    case ROBO_FONT_VALIDATE_TRUSTED:
        return ROBO_FONT_VALIDATION_NONE;
    case ROBO_FONT_VALIDATE_NORMAL:
        return ROBO_FONT_VALIDATION_BASIC;
    case ROBO_FONT_VALIDATE_STRICT:
        return ROBO_FONT_VALIDATION_STRICT;
    case ROBO_FONT_VALIDATE_PARANOID:
        return ROBO_FONT_VALIDATION_PARANOID;
    default:
        return ROBO_FONT_VALIDATION_STRICT;
    }
}

void robo_font_validation_policy_from_loader(robo_font_validate_policy_t loader_pol,
                                             robo_font_validation_policy_t *out)
{
    if (!out) {
        return;
    }
    robo_font_validation_policy_init(out);
    out->level = robo_font_validation_level_from_loader(loader_pol);
    switch (loader_pol) {
    case ROBO_FONT_VALIDATE_TRUSTED:
        out->checksum_policy = ROBO_FONT_CHECKSUM_ACCEPT;
        break;
    case ROBO_FONT_VALIDATE_NORMAL:
        out->checksum_policy = ROBO_FONT_CHECKSUM_WARN;
        break;
    case ROBO_FONT_VALIDATE_STRICT:
        /* Diagnostics + head verify via TT; directory mismatches warn only. */
        out->checksum_policy = ROBO_FONT_CHECKSUM_WARN;
        break;
    case ROBO_FONT_VALIDATE_PARANOID:
        out->checksum_policy = ROBO_FONT_CHECKSUM_REJECT;
        break;
    default:
        out->level = ROBO_FONT_VALIDATION_STRICT;
        out->checksum_policy = ROBO_FONT_CHECKSUM_WARN;
        break;
    }
}

robo_font_validate_policy_t robo_font_validation_level_to_loader(
    robo_font_validation_level_t level)
{
    switch (level) {
    case ROBO_FONT_VALIDATION_NONE:
        return ROBO_FONT_VALIDATE_TRUSTED;
    case ROBO_FONT_VALIDATION_BASIC:
        return ROBO_FONT_VALIDATE_NORMAL;
    case ROBO_FONT_VALIDATION_STRICT:
        return ROBO_FONT_VALIDATE_STRICT;
    case ROBO_FONT_VALIDATION_PARANOID:
        return ROBO_FONT_VALIDATE_PARANOID;
    default:
        return ROBO_FONT_VALIDATE_STRICT;
    }
}

const char *robo_font_validation_issue_code_string(robo_font_validation_issue_code_t code)
{
    if ((unsigned)code >= sizeof(k_issue_code_strings) / sizeof(k_issue_code_strings[0])) {
        return "unknown";
    }
    return k_issue_code_strings[code];
}

const char *robo_font_validation_severity_string(robo_font_validation_severity_t sev)
{
    if ((unsigned)sev >= sizeof(k_severity_strings) / sizeof(k_severity_strings[0])) {
        return "unknown";
    }
    return k_severity_strings[sev];
}

const char *robo_font_validation_stage_string(robo_font_validation_stage_t stage)
{
    if (stage < ROBO_FONT_VALIDATION_STAGE_CONTAINER ||
        stage > ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR) {
        return k_stage_strings[0];
    }
    return k_stage_strings[stage];
}

robo_result_t robo_font_validation_result_create(robo_font_validation_result_t **out)
{
    robo_font_validation_result_t *result;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    result = (robo_font_validation_result_t *)robo_calloc(1, sizeof(*result));
    if (!result) {
        return ROBO_OUT_OF_MEMORY;
    }
    result->cookie = RESULT_COOKIE;
    result->refs = 1u;
    *out = result;
    return ROBO_OK;
}

void robo_font_validation_result_retain(robo_font_validation_result_t *result)
{
    if (!result || result->cookie != RESULT_COOKIE) {
        return;
    }
    result->refs++;
}

void robo_font_validation_result_release(robo_font_validation_result_t *result)
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
    robo_free(result);
}

int robo_font_validation_result_ok(const robo_font_validation_result_t *result)
{
    return result && result->cookie == RESULT_COOKIE && !result->has_fatal;
}

int robo_font_validation_result_has_fatal(const robo_font_validation_result_t *result)
{
    return result && result->cookie == RESULT_COOKIE && result->has_fatal;
}

uint32_t robo_font_validation_result_issue_count(const robo_font_validation_result_t *result)
{
    return result && result->cookie == RESULT_COOKIE ? result->issue_count : 0u;
}

robo_result_t robo_font_validation_result_issue(const robo_font_validation_result_t *result,
                                                uint32_t index,
                                                robo_font_validation_issue_t *out)
{
    if (!result || result->cookie != RESULT_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= result->issue_count) {
        return ROBO_NOT_FOUND;
    }
    *out = result->issues[index];
    return ROBO_OK;
}

robo_font_validation_stage_t robo_font_validation_result_completed_stage(
    const robo_font_validation_result_t *result)
{
    return result && result->cookie == RESULT_COOKIE ? result->completed_stage
                                                     : (robo_font_validation_stage_t)0;
}

uint32_t robo_font_validation_result_work_units(const robo_font_validation_result_t *result)
{
    return result && result->cookie == RESULT_COOKIE ? result->work_units : 0u;
}

robo_result_t robo_font_validate_source(robo_font_source_t *source,
                                        uint32_t face_index,
                                        const robo_font_validation_policy_t *policy,
                                        robo_font_validation_result_t **out_result,
                                        robo_font_sfnt_t **out_sfnt_opt)
{
    robo_font_validation_result_t *result = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    uint32_t face_count = 0u;
    uint32_t sfnt_flags = ROBO_FONT_SFNT_FLAGS_NONE;
    size_t source_size;
    robo_result_t res;

    if (!source || !policy || !out_result) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_result = NULL;
    if (out_sfnt_opt) {
        *out_sfnt_opt = NULL;
    }

    res = robo_font_validation_result_create(&result);
    if (res != ROBO_OK) {
        return res;
    }

    source_size = robo_font_source_size(source);
    if (source_size > (size_t)policy->limits.max_source_size) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_SOURCE_TOO_LARGE,
                  limit_severity(policy),
                  ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  0u,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->reject_oversized ? 0u : 1u,
                  "font source exceeds validation size limit");
        *out_result = result;
        return ROBO_PARSE_ERROR;
    }

    if (robo_font_sfnt_probe_face_count_source(source, &face_count) == ROBO_OK &&
        face_count > policy->limits.max_collection_faces) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_COLLECTION_FACES,
                  limit_severity(policy),
                  ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  policy->reject_oversized ? 0u : 1u,
                  "collection face count exceeds validation limit");
        if (should_stop(result, policy)) {
            *out_result = result;
            return ROBO_PARSE_ERROR;
        }
    }

    /* Always parse without hard VERIFY_CHECKSUMS so mismatches become
     * structured diagnostics under checksum_policy (WARN/REJECT). */
    res = robo_font_sfnt_parse(source, face_index, sfnt_flags, &sfnt);
    if (res != ROBO_OK) {
        robo_font_validation_issue_code_t code = ROBO_FONT_VALIDATION_PARSE_FAILED;
        const char *message = "SFNT container parse failed";

        if (res == ROBO_NOT_SUPPORTED) {
            code = ROBO_FONT_VALIDATION_INVALID_SIGNATURE;
            message = "unsupported or invalid SFNT signature";
        } else if (res == ROBO_NOT_FOUND) {
            message = "face index not found in collection";
        }
        add_issue(result,
                  code,
                  ROBO_FONT_VALIDATION_SEVERITY_FATAL,
                  ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  0u,
                  message);
        *out_result = result;
        return ROBO_PARSE_ERROR;
    }

    (void)validate_sfnt_internal(sfnt, policy, result);

    if (out_sfnt_opt && !result->has_fatal) {
        *out_sfnt_opt = sfnt;
    } else if (sfnt) {
        robo_font_sfnt_release(sfnt);
    }

    *out_result = result;
    return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
}

robo_result_t robo_font_validate_sfnt(robo_font_sfnt_t *sfnt,
                                      const robo_font_validation_policy_t *policy,
                                      robo_font_validation_result_t **out_result)
{
    robo_font_validation_result_t *result = NULL;
    robo_result_t res;

    if (!sfnt || !policy || !out_result) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_result = NULL;

    res = robo_font_validation_result_create(&result);
    if (res != ROBO_OK) {
        return res;
    }

    (void)validate_sfnt_internal(sfnt, policy, result);
    *out_result = result;
    return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
}

robo_result_t robo_font_validate_face(robo_font_face_t *face,
                                      const robo_font_validation_policy_t *policy,
                                      robo_font_validation_result_t **out_result)
{
    robo_font_validation_result_t *result = NULL;
    const robo_font_sfnt_t *sfnt_const = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_result_t res;

    if (!face || !policy || !out_result) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_result = NULL;

    res = robo_font_validation_result_create(&result);
    if (res != ROBO_OK) {
        return res;
    }

    res = robo_font_face_parse_sfnt(face);
    if (res != ROBO_OK) {
        add_issue(result,
                  ROBO_FONT_VALIDATION_PARSE_FAILED,
                  ROBO_FONT_VALIDATION_SEVERITY_FATAL,
                  ROBO_FONT_VALIDATION_STAGE_CONTAINER,
                  0u,
                  ISSUE_OFFSET_NA,
                  ISSUE_GLYPH_NA,
                  0u,
                  0u,
                  "face SFNT parse failed");
        *out_result = result;
        return ROBO_PARSE_ERROR;
    }

    sfnt_const = robo_font_face_sfnt(face);
    sfnt = (robo_font_sfnt_t *)sfnt_const;
    (void)validate_sfnt_internal(sfnt, policy, result);
    if (should_stop(result, policy)) {
        *out_result = result;
        return ROBO_PARSE_ERROR;
    }

    if (policy->level >= ROBO_FONT_VALIDATION_BASIC) {
        res = robo_font_face_parse_metadata(face);
        if (res != ROBO_OK && res != ROBO_NOT_FOUND) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_PARSE_FAILED,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_METADATA,
                      ROBO_FONT_TAG_NAME,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "face metadata parse failed");
        }
        if (should_stop(result, policy)) {
            *out_result = result;
            return ROBO_PARSE_ERROR;
        }
    }

    if (policy->level >= ROBO_FONT_VALIDATION_PARANOID) {
        res = robo_font_face_parse_color(face);
        if (res != ROBO_OK && res != ROBO_NOT_FOUND) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_COLOR_LIMIT,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                      ROBO_FONT_TAG_COLR,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "face color parse failed");
        }
        res = robo_font_face_parse_variation(face);
        if (res != ROBO_OK && res != ROBO_NOT_FOUND) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_VARIATION_LIMIT,
                      ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_GLYPH_COLOR_VAR,
                      ROBO_FONT_TAG_FVAR,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      1u,
                      "face variation parse failed");
        }
        if (should_stop(result, policy)) {
            *out_result = result;
            return ROBO_PARSE_ERROR;
        }
    }

    if (policy->level >= ROBO_FONT_VALIDATION_STRICT) {
        res = robo_font_face_parse_truetype(face, ROBO_FONT_TT_FLAGS_VERIFY_HEAD_CHECKSUM);
        if (res != ROBO_OK) {
            add_issue(result,
                      ROBO_FONT_VALIDATION_CHECKSUM_MISMATCH,
                      policy->checksum_policy == ROBO_FONT_CHECKSUM_REJECT
                          ? ROBO_FONT_VALIDATION_SEVERITY_FATAL
                          : ROBO_FONT_VALIDATION_SEVERITY_WARNING,
                      ROBO_FONT_VALIDATION_STAGE_METADATA,
                      ROBO_FONT_TAG_HEAD,
                      0u,
                      ISSUE_GLYPH_NA,
                      0u,
                      policy->checksum_policy != ROBO_FONT_CHECKSUM_REJECT,
                      "head table checksum verification failed");
        }
    }

    *out_result = result;
    return result->has_fatal ? ROBO_PARSE_ERROR : ROBO_OK;
}

robo_result_t robo_font_validate_for_load(robo_font_face_t *face,
                                          robo_font_validate_policy_t loader_pol,
                                          robo_font_validation_result_t **out_result_opt)
{
    robo_font_validation_policy_t policy;
    robo_font_validation_result_t *result = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_validation_policy_from_loader(loader_pol, &policy);
    res = robo_font_validate_face(face, &policy, &result);
    if (out_result_opt) {
        *out_result_opt = result;
    } else if (result) {
        robo_font_validation_result_release(result);
    }
    if (res != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (result && result->has_fatal) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_validate_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *source = NULL;
    robo_font_validation_policy_t basic;
    robo_font_validation_policy_t paranoid;
    robo_font_validation_result_t *result = NULL;

    if (!data && size != 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size == 0u) {
        return ROBO_OK;
    }
    if (size > 256u * 1024u) {
        size = 256u * 1024u;
    }

    if (robo_font_source_create_memory_copy(data, size, &source) != ROBO_OK) {
        return ROBO_OK;
    }

    robo_font_validation_policy_init(&basic);
    basic.level = ROBO_FONT_VALIDATION_BASIC;
    (void)robo_font_validate_source(source, 0u, &basic, &result, NULL);
    if (result) {
        robo_font_validation_result_release(result);
        result = NULL;
    }

    robo_font_validation_policy_init(&paranoid);
    paranoid.level = ROBO_FONT_VALIDATION_PARANOID;
    paranoid.checksum_policy = ROBO_FONT_CHECKSUM_WARN;
    (void)robo_font_validate_source(source, 0u, &paranoid, &result, NULL);
    if (result) {
        robo_font_validation_result_release(result);
    }

    robo_font_source_release(source);
    return ROBO_OK;
}
