/*
 * RoboUnicodeEmoji — UTS #51 RGI sequence matching (Goal 3.13).
 */

#include "RoboUnicode/RoboUnicodeEmoji.h"

#define ROBO_EMOJI_MATCH_MAX_CPS 32u

const char *robo_unicode_emoji_ucd_version_string(void)
{
    return robo_emoji_ucd_version_string();
}

const char *robo_unicode_emoji_version_string(void)
{
    return robo_emoji_version_string();
}

const char *robo_unicode_emoji_seq_kind_name(robo_unicode_emoji_seq_kind_t k)
{
    return robo_emoji_seq_kind_name(k);
}

robo_unicode_emoji_seq_kind_t robo_unicode_emoji_sequence_classify(
    const robo_uchar32_t *cps, size_t len)
{
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;
    size_t n;

    if (!cps || len == 0u) {
        return ROBO_EMOJI_SEQ_NONE;
    }
    n = robo_emoji_trie_match(cps, len, &kind);
    if (n != len) {
        return ROBO_EMOJI_SEQ_NONE;
    }
    return kind;
}

robo_result_t robo_unicode_emoji_sequence_match(const char *src,
                                                size_t src_len,
                                                size_t offset,
                                                robo_unicode_mode_t mode,
                                                size_t *out_end,
                                                robo_unicode_emoji_seq_kind_t *out_kind)
{
    robo_uchar32_t cps[ROBO_EMOJI_MATCH_MAX_CPS];
    size_t ends[ROBO_EMOJI_MATCH_MAX_CPS];
    size_t n = 0;
    size_t off = offset;
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;
    size_t matched;

    if (out_end) {
        *out_end = offset;
    }
    if (out_kind) {
        *out_kind = ROBO_EMOJI_SEQ_NONE;
    }
    if ((!src && src_len > 0u) || offset > src_len || !out_end || !out_kind) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (offset == src_len) {
        return ROBO_OK;
    }

    while (off < src_len && n < ROBO_EMOJI_MATCH_MAX_CPS) {
        robo_uchar32_t cp = 0;
        size_t next = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_next(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (next <= off) {
            return ROBO_PARSE_ERROR;
        }
        cps[n] = cp;
        ends[n] = next;
        n++;
        off = next;
    }

    matched = robo_emoji_trie_match(cps, n, &kind);
    if (matched == 0u) {
        *out_end = offset;
        *out_kind = ROBO_EMOJI_SEQ_NONE;
        return ROBO_OK;
    }
    *out_end = ends[matched - 1u];
    *out_kind = kind;
    return ROBO_OK;
}

robo_result_t robo_unicode_emoji_sequence_next(const char *src,
                                               size_t src_len,
                                               size_t offset,
                                               robo_unicode_mode_t mode,
                                               size_t *out_start,
                                               size_t *out_end,
                                               robo_unicode_emoji_seq_kind_t *out_kind)
{
    size_t off = offset;

    if (out_start) {
        *out_start = src_len;
    }
    if (out_end) {
        *out_end = src_len;
    }
    if (out_kind) {
        *out_kind = ROBO_EMOJI_SEQ_NONE;
    }
    if ((!src && src_len > 0u) || offset > src_len || !out_start || !out_end ||
        !out_kind) {
        return ROBO_INVALID_ARGUMENT;
    }

    while (off < src_len) {
        size_t end = off;
        robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;
        robo_result_t rc =
            robo_unicode_emoji_sequence_match(src, src_len, off, mode, &end, &kind);
        if (rc != ROBO_OK) {
            return rc;
        }
        if (kind != ROBO_EMOJI_SEQ_NONE && end > off) {
            *out_start = off;
            *out_end = end;
            *out_kind = kind;
            return ROBO_OK;
        }
        {
            robo_uchar32_t cp = 0;
            size_t next = 0;
            robo_unicode_status_t st =
                robo_unicode_utf8_next(src, src_len, off, mode, &cp, &next);
            if (st != ROBO_UNICODE_OK) {
                return ROBO_PARSE_ERROR;
            }
            if (next <= off) {
                return ROBO_PARSE_ERROR;
            }
            off = next;
        }
    }

    *out_start = src_len;
    *out_end = src_len;
    *out_kind = ROBO_EMOJI_SEQ_NONE;
    return ROBO_OK;
}
