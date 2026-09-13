/*
 * RoboI18N transliteration engine (Goal 4.25).
 *
 * Interprets compact tables from generate_translit.py (CLDR transforms).
 * No runtime XML/rule parsing. No Chromium/ICU linkage.
 */

#include "RoboI18N/RoboI18NTranslit.h"

#include "RoboI18N/RoboTranslitData.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboUnicode/RoboUnicodeNorm.h"

#include <string.h>

/* Static info cache filled on first query. */
static robo_i18n_translit_id_info_t g_infos[64];
static uint8_t g_infos_ready;

static const char *pool_str(uint32_t off)
{
    if (off == 0u) {
        return "";
    }
    return robo_translit_strpool + off;
}

static void ensure_infos(void)
{
    uint32_t i;
    if (g_infos_ready) {
        return;
    }
    if (robo_translit_id_count > 64u) {
        /* Table larger than cache — still usable via dynamic path in find. */
    }
    for (i = 0; i < robo_translit_id_count && i < 64u; i++) {
        const robo_translit_id_t *id = &robo_translit_ids[i];
        g_infos[i].id = pool_str(id->id_off);
        g_infos[i].direction = id->direction;
        g_infos[i].reversible = id->reversible;
        g_infos[i].source_script = (robo_unicode_script_t)id->source_script;
        g_infos[i].target_script = (robo_unicode_script_t)id->target_script;
    }
    g_infos_ready = 1;
}

uint32_t robo_i18n_translit_id_count(void)
{
    return robo_translit_id_count;
}

const char *robo_i18n_translit_cldr_version_string(void)
{
    return ROBO_TRANSLIT_DATA_CLDR_VERSION;
}

static int id_match(const char *needle, const robo_translit_id_t *id)
{
    const char *canon = pool_str(id->id_off);
    const char *aliases;
    if (strcmp(canon, needle) == 0) {
        return 1;
    }
    if (id->alias_off == 0u) {
        return 0;
    }
    aliases = pool_str(id->alias_off);
    while (*aliases) {
        if (strcmp(aliases, needle) == 0) {
            return 1;
        }
        aliases += strlen(aliases) + 1;
    }
    return 0;
}

robo_result_t robo_i18n_translit_find_id(const char *id, uint32_t *out_index)
{
    uint32_t i;
    if (!id || !out_index) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0; i < robo_translit_id_count; i++) {
        if (id_match(id, &robo_translit_ids[i])) {
            *out_index = i;
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_i18n_translit_id_info(uint32_t index,
                                         robo_i18n_translit_id_info_t *out)
{
    if (!out || index >= robo_translit_id_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    ensure_infos();
    if (index < 64u) {
        *out = g_infos[index];
    } else {
        const robo_translit_id_t *id = &robo_translit_ids[index];
        out->id = pool_str(id->id_off);
        out->direction = id->direction;
        out->reversible = id->reversible;
        out->source_script = (robo_unicode_script_t)id->source_script;
        out->target_script = (robo_unicode_script_t)id->target_script;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_transliterator_create(uint32_t id_index,
                                              const robo_i18n_locale_t *locale,
                                              robo_i18n_transliterator_t *out)
{
    (void)locale;
    if (!out || id_index >= robo_translit_id_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    ensure_infos();
    out->id_index = id_index;
    out->info = (id_index < 64u) ? &g_infos[id_index] : NULL;
    out->ready = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_transliterator_create_id(const char *id,
                                                 const robo_i18n_locale_t *locale,
                                                 robo_i18n_transliterator_t *out)
{
    uint32_t idx = 0;
    robo_result_t r = robo_i18n_translit_find_id(id, &idx);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_transliterator_create(idx, locale, out);
}

void robo_i18n_transliterator_destroy(robo_i18n_transliterator_t *tr)
{
    if (!tr) {
        return;
    }
    memset(tr, 0, sizeof(*tr));
}

static int set_contains(uint16_t set_id, uint32_t cp)
{
    const robo_translit_set_t *set;
    uint32_t i;
    if (set_id == 0u || set_id >= robo_translit_set_count) {
        return 1; /* no constraint */
    }
    set = &robo_translit_sets[set_id];
    for (i = 0; i < set->range_count; i++) {
        const robo_translit_range_t *rg =
            &robo_translit_ranges[set->range_begin + i];
        if (cp >= rg->start && cp <= rg->end) {
            return 1;
        }
    }
    return 0;
}

static int rule_matches_b(const robo_translit_rule_t *rule,
                          const uint32_t *cps,
                          size_t len,
                          size_t pos)
{
    size_t k;
    if (rule->key_len == 0u || pos + rule->key_len > len) {
        return 0;
    }
    if (rule->before_set != 0u) {
        if (pos == 0u) {
            /* Start of text: match if set contains any Cc/Zs/punctuation —
             * check space and NUL-range. */
            if (!set_contains(rule->before_set, (uint32_t)' ') &&
                !set_contains(rule->before_set, 0u) &&
                !set_contains(rule->before_set, (uint32_t)'.')) {
                /* Still allow if set is a large complement-like set
                 * (range_count high). */
                const robo_translit_set_t *set = &robo_translit_sets[rule->before_set];
                if (set->range_count < 8u) {
                    return 0;
                }
            }
        } else if (!set_contains(rule->before_set, cps[pos - 1u])) {
            return 0;
        }
    }
    for (k = 0; k < rule->key_len; k++) {
        if (cps[pos + k] != robo_translit_cps[rule->key_off + k]) {
            return 0;
        }
    }
    if (rule->after_set != 0u) {
        size_t after_pos = pos + rule->key_len;
        if (after_pos >= len) {
            /* End of text similar to start */
            const robo_translit_set_t *set = &robo_translit_sets[rule->after_set];
            if (set->range_count < 8u &&
                !set_contains(rule->after_set, (uint32_t)' ') &&
                !set_contains(rule->after_set, 0u)) {
                return 0;
            }
        } else if (!set_contains(rule->after_set, cps[after_pos])) {
            return 0;
        }
    }
    return 1;
}

static robo_result_t normalize_buf(uint32_t **cps,
                                   size_t *inout_len,
                                   size_t *inout_cap,
                                   robo_unicode_norm_form_t form)
{
    robo_uchar32_t *tmp = NULL;
    size_t n = 0;
    robo_result_t r =
        robo_unicode_normalize((const robo_uchar32_t *)*cps, *inout_len, form, &tmp,
                               &n);
    if (r != ROBO_OK) {
        return r;
    }
    robo_free(*cps);
    *cps = (uint32_t *)tmp;
    *inout_len = n;
    *inout_cap = n;
    return ROBO_OK;
}

static robo_result_t lower_buf(uint32_t **cps, size_t *inout_len, size_t *inout_cap)
{
    robo_uchar32_t *tmp = NULL;
    size_t n = 0;
    robo_result_t r = robo_unicode_case_map_cps((const robo_uchar32_t *)*cps,
                                                *inout_len, ROBO_UNICODE_CASE_LOWER,
                                                &tmp, &n);
    if (r != ROBO_OK) {
        return r;
    }
    if (!tmp) {
        *inout_len = 0;
        return ROBO_OK;
    }
    robo_free(*cps);
    *cps = (uint32_t *)tmp;
    *inout_len = n;
    *inout_cap = n;
    return ROBO_OK;
}

static robo_result_t hangul_decomp(uint32_t **cps, size_t *inout_len, size_t *inout_cap)
{
    enum {
        SBase = 0xAC00,
        LBase = 0x1100,
        VBase = 0x1161,
        TBase = 0x11A7,
        LCount = 19,
        VCount = 21,
        TCount = 28,
        NCount = VCount * TCount,
        SCount = LCount * NCount
    };
    uint32_t *out;
    size_t i, out_len = 0;
    size_t len = *inout_len;
    size_t cap = len * 3u + 8u;
    robo_result_t r;

    if (cap > ROBO_I18N_TRANSLIT_OUTPUT_MAX) {
        cap = ROBO_I18N_TRANSLIT_OUTPUT_MAX;
    }
    out = (uint32_t *)robo_malloc(cap * sizeof(uint32_t));
    if (!out) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0; i < len; i++) {
        uint32_t s = (*cps)[i];
        if (s >= SBase && s < SBase + SCount) {
            uint32_t SIndex = s - SBase;
            uint32_t L = LBase + SIndex / NCount;
            uint32_t V = VBase + (SIndex % NCount) / TCount;
            uint32_t T = TBase + SIndex % TCount;
            if (out_len + 2u + (T != TBase ? 1u : 0u) > cap) {
                robo_free(out);
                return ROBO_INVALID_ARGUMENT;
            }
            out[out_len++] = L;
            out[out_len++] = V;
            if (T != TBase) {
                out[out_len++] = T;
            }
        } else {
            if (out_len + 1u > cap) {
                robo_free(out);
                return ROBO_INVALID_ARGUMENT;
            }
            out[out_len++] = s;
        }
    }
    (void)r;
    robo_free(*cps);
    *cps = out;
    *inout_len = out_len;
    *inout_cap = cap;
    return ROBO_OK;
}

static robo_result_t apply_rules(const robo_translit_stage_t *stage,
                                 uint32_t **cps,
                                 size_t *inout_len,
                                 size_t *inout_cap,
                                 uint32_t *steps)
{
    uint32_t *out = NULL;
    size_t out_len = 0;
    size_t pos = 0;
    size_t len = *inout_len;
    size_t cap = *inout_cap;
    uint32_t rb = stage->rule_begin;
    uint32_t rc = stage->rule_count;

    out = (uint32_t *)robo_malloc(cap * sizeof(uint32_t));
    if (!out) {
        return ROBO_OUT_OF_MEMORY;
    }

    while (pos < len) {
        uint32_t ri;
        int matched = 0;
        if (*steps >= ROBO_I18N_TRANSLIT_STEP_MAX) {
            robo_free(out);
            return ROBO_INVALID_ARGUMENT;
        }
        (*steps)++;
        for (ri = 0; ri < rc; ri++) {
            const robo_translit_rule_t *rule = &robo_translit_rules[rb + ri];
            if (rule_matches_b(rule, *cps, len, pos)) {
                uint16_t k;
                if (out_len + rule->repl_len > cap) {
                    uint32_t *grown;
                    size_t ncap = cap * 2u;
                    if (ncap < out_len + rule->repl_len + 64u) {
                        ncap = out_len + rule->repl_len + 64u;
                    }
                    if (ncap > ROBO_I18N_TRANSLIT_OUTPUT_MAX) {
                        ncap = ROBO_I18N_TRANSLIT_OUTPUT_MAX;
                    }
                    if (out_len + rule->repl_len > ncap) {
                        robo_free(out);
                        return ROBO_INVALID_ARGUMENT;
                    }
                    grown = (uint32_t *)robo_malloc(ncap * sizeof(uint32_t));
                    if (!grown) {
                        robo_free(out);
                        return ROBO_OUT_OF_MEMORY;
                    }
                    memcpy(grown, out, out_len * sizeof(uint32_t));
                    robo_free(out);
                    out = grown;
                    cap = ncap;
                }
                for (k = 0; k < rule->repl_len; k++) {
                    out[out_len++] = robo_translit_cps[rule->repl_off + k];
                }
                pos += rule->key_len;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (out_len + 1u > cap) {
                uint32_t *grown;
                size_t ncap = cap * 2u + 64u;
                if (ncap > ROBO_I18N_TRANSLIT_OUTPUT_MAX) {
                    ncap = ROBO_I18N_TRANSLIT_OUTPUT_MAX;
                }
                if (out_len + 1u > ncap) {
                    robo_free(out);
                    return ROBO_INVALID_ARGUMENT;
                }
                grown = (uint32_t *)robo_malloc(ncap * sizeof(uint32_t));
                if (!grown) {
                    robo_free(out);
                    return ROBO_OUT_OF_MEMORY;
                }
                memcpy(grown, out, out_len * sizeof(uint32_t));
                robo_free(out);
                out = grown;
                cap = ncap;
            }
            out[out_len++] = (*cps)[pos++];
        }
    }
    robo_free(*cps);
    *cps = out;
    *inout_len = out_len;
    *inout_cap = cap;
    return ROBO_OK;
}

static robo_result_t run_pipeline(uint32_t id_index, uint32_t **cps, size_t *inout_len,
                                  size_t *inout_cap)
{
    const robo_translit_id_t *id = &robo_translit_ids[id_index];
    uint32_t si;
    uint32_t steps = 0;
    for (si = 0; si < id->stage_count; si++) {
        const robo_translit_stage_t *st =
            &robo_translit_stages[id->stage_begin + si];
        robo_result_t r = ROBO_OK;
        switch (st->kind) {
        case ROBO_TRANSLIT_STAGE_NFD:
            r = normalize_buf(cps, inout_len, inout_cap, ROBO_UNICODE_NORM_NFD);
            break;
        case ROBO_TRANSLIT_STAGE_NFC:
            r = normalize_buf(cps, inout_len, inout_cap, ROBO_UNICODE_NORM_NFC);
            break;
        case ROBO_TRANSLIT_STAGE_NFKD:
            r = normalize_buf(cps, inout_len, inout_cap, ROBO_UNICODE_NORM_NFKD);
            break;
        case ROBO_TRANSLIT_STAGE_NFKC:
            r = normalize_buf(cps, inout_len, inout_cap, ROBO_UNICODE_NORM_NFKC);
            break;
        case ROBO_TRANSLIT_STAGE_LOWER:
            r = lower_buf(cps, inout_len, inout_cap);
            break;
        case ROBO_TRANSLIT_STAGE_NULL:
            break;
        case ROBO_TRANSLIT_STAGE_HANGUL_DECOMP:
            r = hangul_decomp(cps, inout_len, inout_cap);
            break;
        case ROBO_TRANSLIT_STAGE_RULES:
            r = apply_rules(st, cps, inout_len, inout_cap, &steps);
            break;
        default:
            break;
        }
        if (r != ROBO_OK) {
            return r;
        }
    }
    return ROBO_OK;
}

static robo_result_t decode_all(const char *utf8,
                                size_t utf8_len,
                                uint32_t *cps,
                                size_t cap,
                                size_t *out_n)
{
    size_t off = 0;
    size_t n = 0;
    while (off < utf8_len) {
        robo_uchar32_t cp = 0;
        size_t next = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(utf8, utf8_len, off, ROBO_UNICODE_STRICT, &cp,
                                     &next);
        if (st != ROBO_UNICODE_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (n >= cap) {
            return ROBO_INVALID_ARGUMENT;
        }
        cps[n++] = cp;
        off = next;
    }
    *out_n = n;
    return ROBO_OK;
}

robo_result_t robo_i18n_transliterate(const robo_i18n_transliterator_t *tr,
                                      const char *utf8,
                                      size_t utf8_len,
                                      char *out_buf,
                                      size_t out_cap,
                                      size_t *out_len,
                                      robo_i18n_translit_map_entry_t *map_out,
                                      size_t map_cap,
                                      size_t *map_count)
{
    uint32_t *cps = NULL;
    size_t n = 0;
    size_t cp_cap;
    size_t written = 0;
    robo_result_t r;

    if (!tr || !tr->ready || (!utf8 && utf8_len) || !out_buf || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (utf8_len > ROBO_I18N_TRANSLIT_INPUT_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (map_count) {
        *map_count = 0;
    }

    cp_cap = utf8_len + 64u;
    if (cp_cap < 64u) {
        cp_cap = 64u;
    }
    if (cp_cap > ROBO_I18N_TRANSLIT_OUTPUT_MAX) {
        cp_cap = ROBO_I18N_TRANSLIT_OUTPUT_MAX;
    }
    cps = (uint32_t *)robo_malloc(cp_cap * sizeof(uint32_t));
    if (!cps) {
        return ROBO_OUT_OF_MEMORY;
    }

    r = decode_all(utf8, utf8_len, cps, cp_cap, &n);
    if (r != ROBO_OK) {
        robo_free(cps);
        return r;
    }

    r = run_pipeline(tr->id_index, &cps, &n, &cp_cap);
    if (r != ROBO_OK) {
        robo_free(cps);
        return r;
    }

    {
        robo_unicode_status_t st = robo_unicode_utf32_to_utf8(
            (const robo_uchar32_t *)cps, n, out_buf, out_cap, &written,
            ROBO_UNICODE_STRICT);
        if (st == ROBO_UNICODE_NO_SPACE) {
            robo_free(cps);
            return ROBO_INVALID_ARGUMENT;
        }
        if (st != ROBO_UNICODE_OK) {
            robo_free(cps);
            return ROBO_PARSE_ERROR;
        }
    }
    if (written > ROBO_I18N_TRANSLIT_OUTPUT_MAX) {
        robo_free(cps);
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = written;
    if (out_cap > written) {
        out_buf[written] = '\0';
    }

    if (map_out && map_cap > 0 && map_count) {
        map_out[0].src_start = 0;
        map_out[0].src_end = utf8_len;
        map_out[0].out_start = 0;
        map_out[0].out_end = written;
        *map_count = 1;
    }

    robo_free(cps);
    return ROBO_OK;
}

robo_result_t robo_i18n_transliterator_stream_reset(robo_i18n_transliterator_t *tr)
{
    if (!tr || !tr->ready) {
        return ROBO_INVALID_ARGUMENT;
    }
    tr->hold_len = 0;
    return ROBO_OK;
}

robo_result_t robo_i18n_transliterator_stream_feed(robo_i18n_transliterator_t *tr,
                                                   const char *utf8,
                                                   size_t utf8_len,
                                                   char *out_buf,
                                                   size_t out_cap,
                                                   size_t *out_len)
{
    uint32_t *cps = NULL;
    size_t n = 0;
    size_t cp_cap;
    size_t i;
    size_t commit_n;
    size_t written = 0;
    robo_result_t r;

    if (!tr || !tr->ready || !out_buf || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (utf8_len > ROBO_I18N_TRANSLIT_INPUT_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = 0;
    cp_cap = (size_t)tr->hold_len + utf8_len + 64u;
    if (cp_cap > ROBO_I18N_TRANSLIT_OUTPUT_MAX) {
        cp_cap = ROBO_I18N_TRANSLIT_OUTPUT_MAX;
    }
    cps = (uint32_t *)robo_malloc(cp_cap * sizeof(uint32_t));
    if (!cps) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0; i < tr->hold_len; i++) {
        cps[i] = tr->hold[i];
    }
    n = tr->hold_len;
    r = decode_all(utf8, utf8_len, cps + n, cp_cap - n, &i);
    if (r != ROBO_OK) {
        robo_free(cps);
        return r;
    }
    n += i;

    r = run_pipeline(tr->id_index, &cps, &n, &cp_cap);
    if (r != ROBO_OK) {
        robo_free(cps);
        return r;
    }

    if (n > ROBO_I18N_TRANSLIT_STREAM_LOOKAHEAD_MAX) {
        commit_n = n - ROBO_I18N_TRANSLIT_STREAM_LOOKAHEAD_MAX;
        tr->hold_len = (uint16_t)ROBO_I18N_TRANSLIT_STREAM_LOOKAHEAD_MAX;
        memcpy(tr->hold, cps + commit_n, tr->hold_len * sizeof(uint32_t));
    } else {
        commit_n = 0;
        tr->hold_len = (uint16_t)n;
        memcpy(tr->hold, cps, n * sizeof(uint32_t));
    }

    if (commit_n > 0) {
        robo_unicode_status_t st = robo_unicode_utf32_to_utf8(
            (const robo_uchar32_t *)cps, commit_n, out_buf, out_cap, &written,
            ROBO_UNICODE_STRICT);
        if (st != ROBO_UNICODE_OK) {
            robo_free(cps);
            return (st == ROBO_UNICODE_NO_SPACE) ? ROBO_INVALID_ARGUMENT
                                                 : ROBO_PARSE_ERROR;
        }
        *out_len = written;
        if (out_cap > written) {
            out_buf[written] = '\0';
        }
    }
    robo_free(cps);
    return ROBO_OK;
}

robo_result_t robo_i18n_transliterator_stream_finish(robo_i18n_transliterator_t *tr,
                                                     char *out_buf,
                                                     size_t out_cap,
                                                     size_t *out_len)
{
    size_t written = 0;
    if (!tr || !tr->ready || !out_buf || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = 0;
    if (tr->hold_len == 0) {
        if (out_cap > 0) {
            out_buf[0] = '\0';
        }
        return ROBO_OK;
    }
    {
        robo_unicode_status_t st = robo_unicode_utf32_to_utf8(
            (const robo_uchar32_t *)tr->hold, tr->hold_len, out_buf, out_cap,
            &written, ROBO_UNICODE_STRICT);
        tr->hold_len = 0;
        if (st != ROBO_UNICODE_OK) {
            return (st == ROBO_UNICODE_NO_SPACE) ? ROBO_INVALID_ARGUMENT
                                                 : ROBO_PARSE_ERROR;
        }
        *out_len = written;
        if (out_cap > written) {
            out_buf[written] = '\0';
        }
    }
    return ROBO_OK;
}
