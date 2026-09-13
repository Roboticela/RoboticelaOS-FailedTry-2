/*
 * RoboI18N collation — UCA 16.0.0 + CLDR 46 tailorings (Goal 4.9).
 *
 * Algorithm: UTS #10 (Unicode Collation Algorithm).
 *   S1 NFD → S2 CE formation → S3 sort key / level compare → S4 result
 *
 * Invalid UTF-8: STRICT → ROBO_PARSE_ERROR.
 */

#include "RoboI18N/RoboI18NCollation.h"

#include "RoboI18N/RoboCollData.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboUcaData.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboNormData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

struct robo_i18n_collator {
    robo_i18n_locale_t locale;
    robo_i18n_collator_options_t options;
    const robo_coll_remap_t *remaps;
    uint16_t remap_count;
    char type_name[32];
};

enum {
    CE_CAP_INIT = 256,
    KEY_LEVEL_SEP = 0x00,
    KEY_PRIMARY_SHIFT = 0 /* store primary as 2 big-endian bytes */
};

const char *robo_i18n_collation_uca_version_string(void)
{
    return robo_uca_version_string();
}

const char *robo_i18n_collation_cldr_version_string(void)
{
    return robo_coll_cldr_version_string();
}

void robo_i18n_collator_options_init(robo_i18n_collator_options_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->strength = ROBO_I18N_COLLATION_TERTIARY;
    out->alternate = ROBO_I18N_COLLATION_NON_IGNORABLE;
    out->numeric = 0;
    out->case_first = 0;
}

static int find_locale_row(const char *lang, const robo_coll_locale_t **out)
{
    uint32_t i;
    for (i = 0; i < robo_coll_locale_count; i++) {
        const char *id = robo_coll_string_pool + robo_coll_locales[i].id_offset;
        if (strcmp(id, lang) == 0) {
            *out = &robo_coll_locales[i];
            return 1;
        }
    }
    return 0;
}

static void bind_tailoring(robo_i18n_collator_t *c, const char *type_name)
{
    const robo_coll_locale_t *row = NULL;
    const char *lang = c->locale.language[0] ? c->locale.language : "root";
    uint16_t i;
    uint16_t want_tid = 0;
    int have_type = 0;

    c->remaps = NULL;
    c->remap_count = 0;
    strncpy(c->type_name, type_name && type_name[0] ? type_name : "standard",
            sizeof(c->type_name) - 1);
    c->type_name[sizeof(c->type_name) - 1] = '\0';

    if (!find_locale_row(lang, &row)) {
        find_locale_row("root", &row);
    }
    if (!row) {
        return;
    }

    for (i = 0; i < row->type_count; i++) {
        const robo_coll_type_t *t = &robo_coll_types[row->type_begin + i];
        const char *n = robo_coll_string_pool + t->name_offset;
        if (strcmp(n, c->type_name) == 0) {
            want_tid = t->type_id;
            have_type = 1;
            if (t->remap_count > 0 && t->remap_offset + t->remap_count <= robo_coll_remap_count) {
                c->remaps = &robo_coll_remaps[t->remap_offset];
                c->remap_count = t->remap_count;
            }
            break;
        }
    }
    if (!have_type) {
        /* fall back to locale default type name if present */
        for (i = 0; i < row->type_count; i++) {
            const robo_coll_type_t *t = &robo_coll_types[row->type_begin + i];
            if (t->type_id == row->default_type_id && t->remap_count > 0 &&
                t->remap_offset + t->remap_count <= robo_coll_remap_count) {
                c->remaps = &robo_coll_remaps[t->remap_offset];
                c->remap_count = t->remap_count;
                break;
            }
        }
        (void)want_tid;
    }
}

robo_result_t robo_i18n_collator_create(const robo_i18n_locale_t *locale,
                                        const char *collation_type,
                                        const robo_i18n_collator_options_t *options,
                                        robo_i18n_collator_t **out)
{
    robo_i18n_collator_t *c;
    char resolved_type[32];
    const char *type_use;
    if (!locale || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    c = (robo_i18n_collator_t *)robo_calloc(1, sizeof(*c));
    if (!c) {
        return ROBO_OUT_OF_MEMORY;
    }
    c->locale = *locale;
    if (options) {
        c->options = *options;
    } else {
        robo_i18n_collator_options_init(&c->options);
    }
    type_use = collation_type;
    if (!type_use || !type_use[0]) {
        if (robo_i18n_locale_resolve_collation(locale, NULL, resolved_type,
                                               sizeof(resolved_type)) == ROBO_OK) {
            type_use = resolved_type;
        } else {
            type_use = "standard";
        }
    }
    bind_tailoring(c, type_use);
    *out = c;
    return ROBO_OK;
}

void robo_i18n_collator_destroy(robo_i18n_collator_t *coll)
{
    robo_free(coll);
}

robo_result_t robo_i18n_collator_set_options(robo_i18n_collator_t *coll,
                                             const robo_i18n_collator_options_t *options)
{
    if (!coll || !options) {
        return ROBO_INVALID_ARGUMENT;
    }
    coll->options = *options;
    return ROBO_OK;
}

robo_result_t robo_i18n_collator_get_options(const robo_i18n_collator_t *coll,
                                             robo_i18n_collator_options_t *out)
{
    if (!coll || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = coll->options;
    return ROBO_OK;
}

static int map_lookup(uint32_t cp, uint32_t *ce_off, uint16_t *ce_cnt)
{
    uint32_t lo = 0;
    uint32_t hi = robo_uca_map_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t v = robo_uca_map[mid].cp;
        if (v < cp) {
            lo = mid + 1u;
        } else if (v > cp) {
            hi = mid;
        } else {
            *ce_off = robo_uca_map[mid].ce_offset;
            *ce_cnt = robo_uca_map[mid].ce_count;
            return 1;
        }
    }
    return 0;
}

static int remap_lookup(const robo_i18n_collator_t *coll,
                        uint32_t cp,
                        uint16_t *p,
                        uint16_t *s,
                        uint16_t *t)
{
    uint16_t i;
    if (!coll || !coll->remaps) {
        return 0;
    }
    for (i = 0; i < coll->remap_count; i++) {
        if (coll->remaps[i].cp == cp) {
            *p = coll->remaps[i].primary;
            *s = coll->remaps[i].secondary;
            *t = coll->remaps[i].tertiary;
            return 1;
        }
    }
    return 0;
}

static int find_contraction(const uint32_t *cps,
                            size_t n,
                            size_t i,
                            uint32_t *ce_off,
                            uint16_t *ce_cnt,
                            size_t *consumed)
{
    uint32_t first;
    uint32_t lo;
    uint32_t hi;
    size_t best_len = 0;
    uint32_t best_off = 0;
    uint16_t best_cnt = 0;

    if (i >= n) {
        return 0;
    }
    first = cps[i];
    /* contractions sorted by first_cp, then longer first */
    lo = 0;
    hi = robo_uca_contraction_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (robo_uca_contractions[mid].first_cp < first) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    while (lo < robo_uca_contraction_count &&
           robo_uca_contractions[lo].first_cp == first) {
        const robo_uca_contraction_t *c = &robo_uca_contractions[lo];
        size_t len = c->seq_len;
        if (i + len <= n) {
            size_t k;
            int ok = 1;
            for (k = 0; k < len; k++) {
                if (cps[i + k] != robo_uca_contraction_seq[c->seq_offset + k]) {
                    ok = 0;
                    break;
                }
            }
            if (ok && len > best_len) {
                best_len = len;
                best_off = c->ce_offset;
                best_cnt = c->ce_count;
            }
        }
        lo++;
    }
    if (best_len == 0) {
        return 0;
    }
    *ce_off = best_off;
    *ce_cnt = best_cnt;
    *consumed = best_len;
    return 1;
}

static void implicit_ces(uint32_t cp, uint64_t *out_ce, uint16_t *out_n)
{
    uint32_t i;
    uint16_t base = 0xFBC0;
    uint32_t aaaa;
    uint32_t bbbb;

    for (i = 0; i < robo_uca_implicit_range_count; i++) {
        if (cp >= robo_uca_implicit_ranges[i].start &&
            cp <= robo_uca_implicit_ranges[i].end) {
            base = robo_uca_implicit_ranges[i].base;
            break;
        }
    }
    /* UTS #10 §7.1.3 implicit weight formula (core Unified Ideograph path). */
    if ((cp >= 0x4E00u && cp <= 0x9FFFu) || (cp >= 0xF900u && cp <= 0xFAFFu)) {
        aaaa = 0xFB40u + (cp >> 15);
        bbbb = (cp & 0x7FFFu) | 0x8000u;
    } else if ((cp >= 0x3400u && cp <= 0x4DBFu) || (cp >= 0x20000u && cp <= 0x2A6DFu) ||
               (cp >= 0x2A700u && cp <= 0x2B73Fu) || (cp >= 0x2B740u && cp <= 0x2B81Fu) ||
               (cp >= 0x2B820u && cp <= 0x2CEAFu) || (cp >= 0x2CEB0u && cp <= 0x2EBEFu) ||
               (cp >= 0x30000u && cp <= 0x3134Fu)) {
        aaaa = 0xFB80u + (cp >> 15);
        bbbb = (cp & 0x7FFFu) | 0x8000u;
    } else {
        aaaa = (uint32_t)base + (cp >> 15);
        bbbb = (cp & 0x7FFFu) | 0x8000u;
    }
    out_ce[0] = ((uint64_t)(aaaa & 0xFFFFu) << 32) | (0x0020ULL << 16) | 0x0002ULL;
    out_ce[1] = ((uint64_t)(bbbb & 0xFFFFu) << 32);
    *out_n = 2;
}

static uint16_t apply_case_first(const robo_i18n_collator_t *coll, uint16_t tertiary)
{
    /* Tertiary case bits in DUCET: uppercase often 0x0008 vs lowercase 0x0002.
     * case_first upperFirst: invert high/low for case-sensitive tertiaries. */
    if (!coll || coll->options.case_first == 0) {
        return tertiary;
    }
    if (coll->options.case_first == 1) { /* upper first */
        if (tertiary == 0x0008) {
            return 0x0002;
        }
        if (tertiary == 0x0002) {
            return 0x0008;
        }
    } else if (coll->options.case_first == 2) { /* lower first — DUCET defaultish */
        return tertiary;
    }
    return tertiary;
}

typedef struct {
    uint64_t *ces;
    size_t len;
    size_t cap;
} ce_buf_t;

static robo_result_t ce_buf_push(ce_buf_t *b, uint64_t ce)
{
    if (b->len >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2u : (size_t)CE_CAP_INIT;
        uint64_t *n = (uint64_t *)robo_malloc(ncap * sizeof(uint64_t));
        if (!n) {
            return ROBO_OUT_OF_MEMORY;
        }
        if (b->ces) {
            memcpy(n, b->ces, b->len * sizeof(uint64_t));
            robo_free(b->ces);
        }
        b->ces = n;
        b->cap = ncap;
    }
    b->ces[b->len++] = ce;
    return ROBO_OK;
}

static int digit_value(uint32_t cp, int *out_v)
{
    if (cp >= '0' && cp <= '9') {
        *out_v = (int)(cp - '0');
        return 1;
    }
    if (robo_unicode_category((robo_uchar32_t)cp) == ROBO_U_GC_ND) {
        /* Common decimal digits: use offset within 10-block when possible. */
        uint32_t base = cp - (cp % 10u);
        if (cp >= base && cp < base + 10u) {
            *out_v = (int)(cp - base);
            return 1;
        }
    }
    return 0;
}

static robo_result_t form_ces(const robo_i18n_collator_t *coll,
                              const uint32_t *cps,
                              size_t n,
                              ce_buf_t *out)
{
    size_t i = 0;
    memset(out, 0, sizeof(*out));
    while (i < n) {
        uint32_t ce_off = 0;
        uint16_t ce_cnt = 0;
        size_t consumed = 1;
        uint16_t rp, rs, rt;
        uint16_t k;

        if (coll->options.numeric) {
            int dv;
            if (digit_value(cps[i], &dv)) {
                /* Collect run into a synthetic primary weight encoding. */
                uint64_t value = 0;
                size_t j = i;
                int overflow = 0;
                while (j < n && digit_value(cps[j], &dv)) {
                    if (value > (UINT64_MAX - (uint64_t)dv) / 10u) {
                        overflow = 1;
                        break;
                    }
                    value = value * 10u + (uint64_t)dv;
                    j++;
                }
                if (!overflow && j > i) {
                    uint8_t bytes[8];
                    int bi;
                    for (bi = 7; bi >= 0; bi--) {
                        bytes[bi] = (uint8_t)(value & 0xFFu);
                        value >>= 8;
                    }
                    for (bi = 0; bi < 8; bi++) {
                        uint64_t ce =
                            ((uint64_t)(0xFF00u + bytes[bi]) << 32) | (0x0020ULL << 16) | 2ULL;
                        if (ce_buf_push(out, ce) != ROBO_OK) {
                            return ROBO_OUT_OF_MEMORY;
                        }
                    }
                    i = j;
                    continue;
                }
            }
        }

        /* Tailoring remaps are stored on NFC code points; after NFD, try compose. */
        {
            uint32_t composed = cps[i];
            size_t j = i + 1;
            while (j < n) {
                uint32_t next = robo_norm_compose_pair(composed, cps[j]);
                if (next == 0) {
                    break;
                }
                composed = next;
                j++;
            }
            if (remap_lookup(coll, composed, &rp, &rs, &rt) ||
                (composed == cps[i] && remap_lookup(coll, cps[i], &rp, &rs, &rt))) {
                uint64_t ce = ((uint64_t)rp << 32) | ((uint64_t)rs << 16) | (uint64_t)rt;
                if (ce_buf_push(out, ce) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
                i = (composed != cps[i]) ? j : i + 1;
                continue;
            }
        }

        if (find_contraction(cps, n, i, &ce_off, &ce_cnt, &consumed)) {
            for (k = 0; k < ce_cnt; k++) {
                if (ce_buf_push(out, robo_uca_ce_pool[ce_off + k]) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
            i += consumed;
            continue;
        }

        if (map_lookup(cps[i], &ce_off, &ce_cnt)) {
            for (k = 0; k < ce_cnt; k++) {
                if (ce_buf_push(out, robo_uca_ce_pool[ce_off + k]) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
        } else {
            uint64_t impl[2];
            uint16_t nn = 0;
            implicit_ces(cps[i], impl, &nn);
            for (k = 0; k < nn; k++) {
                if (ce_buf_push(out, impl[k]) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
        }
        i++;
    }
    return ROBO_OK;
}

static robo_result_t decode_nfd(const char *text,
                                size_t len,
                                uint32_t **out_cps,
                                size_t *out_n)
{
    robo_uchar32_t *tmp = NULL;
    size_t tn = 0;
    size_t off = 0;
    size_t cap = 0;
    robo_result_t r;

    *out_cps = NULL;
    *out_n = 0;
    if (len == 0) {
        return ROBO_OK;
    }
    while (off < len) {
        robo_uchar32_t cp;
        size_t next = 0;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(text, len, off, ROBO_UNICODE_STRICT, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            robo_free(tmp);
            return ROBO_PARSE_ERROR;
        }
        if (tn >= cap) {
            size_t ncap = cap ? cap * 2u : 64u;
            robo_uchar32_t *nbuf =
                (robo_uchar32_t *)robo_malloc(ncap * sizeof(robo_uchar32_t));
            if (!nbuf) {
                robo_free(tmp);
                return ROBO_OUT_OF_MEMORY;
            }
            if (tmp) {
                memcpy(nbuf, tmp, tn * sizeof(robo_uchar32_t));
                robo_free(tmp);
            }
            tmp = nbuf;
            cap = ncap;
        }
        tmp[tn++] = cp;
        off = next;
    }

    {
        robo_uchar32_t *nfd = NULL;
        size_t nfd_n = 0;
        r = robo_unicode_normalize_nfd(tmp, tn, &nfd, &nfd_n);
        robo_free(tmp);
        if (r != ROBO_OK) {
            return r;
        }
        *out_cps = (uint32_t *)nfd;
        *out_n = nfd_n;
    }
    return ROBO_OK;
}

typedef struct {
    uint16_t *w;
    size_t len;
    size_t cap;
} wbuf_t;

static robo_result_t wbuf_push(wbuf_t *b, uint16_t v)
{
    if (b->len >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2u : 128u;
        uint16_t *n = (uint16_t *)robo_malloc(ncap * sizeof(uint16_t));
        if (!n) {
            return ROBO_OUT_OF_MEMORY;
        }
        if (b->w) {
            memcpy(n, b->w, b->len * sizeof(uint16_t));
            robo_free(b->w);
        }
        b->w = n;
        b->cap = ncap;
    }
    b->w[b->len++] = v;
    return ROBO_OK;
}

static robo_result_t build_level_weights(const robo_i18n_collator_t *coll,
                                         const ce_buf_t *ces,
                                         int level,
                                         wbuf_t *out)
{
    size_t i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < ces->len; i++) {
        uint64_t ce = ces->ces[i];
        int variable = ROBO_UCA_CE_IS_VARIABLE(ce);
        uint16_t p = ROBO_UCA_CE_PRIMARY(ce);
        uint16_t s = ROBO_UCA_CE_SECONDARY(ce);
        uint16_t t = apply_case_first(coll, ROBO_UCA_CE_TERTIARY(ce));

        if (coll->options.alternate == ROBO_I18N_COLLATION_SHIFTED && variable) {
            if (level == 0 || level == 1 || level == 2) {
                continue; /* ignorable at L1–L3 */
            }
            if (level == 3) {
                if (wbuf_push(out, p ? p : 0xFFFF) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
            continue;
        }

        if (level == 0) {
            if (p != 0) {
                if (wbuf_push(out, p) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
        } else if (level == 1) {
            if (s != 0) {
                if (wbuf_push(out, s) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
        } else if (level == 2) {
            if (t != 0) {
                if (wbuf_push(out, t) != ROBO_OK) {
                    return ROBO_OUT_OF_MEMORY;
                }
            }
        } else if (level == 3) {
            /* Quaternary: non-ignorable variables already handled; others 0xFFFF */
            if (wbuf_push(out, 0xFFFFu) != ROBO_OK) {
                return ROBO_OUT_OF_MEMORY;
            }
        }
    }
    return ROBO_OK;
}

static int cmp_wbuf(const wbuf_t *a, const wbuf_t *b)
{
    size_t n = a->len < b->len ? a->len : b->len;
    size_t i;
    for (i = 0; i < n; i++) {
        if (a->w[i] < b->w[i]) {
            return -1;
        }
        if (a->w[i] > b->w[i]) {
            return 1;
        }
    }
    if (a->len < b->len) {
        return -1;
    }
    if (a->len > b->len) {
        return 1;
    }
    return 0;
}

static int max_level(robo_i18n_collation_strength_t s)
{
    switch (s) {
    case ROBO_I18N_COLLATION_PRIMARY:
        return 0;
    case ROBO_I18N_COLLATION_SECONDARY:
        return 1;
    case ROBO_I18N_COLLATION_TERTIARY:
        return 2;
    case ROBO_I18N_COLLATION_QUATERNARY:
        return 3;
    case ROBO_I18N_COLLATION_IDENTICAL:
        return 4;
    default:
        return 2;
    }
}

static int cmp_codepoints(const uint32_t *a, size_t an, const uint32_t *b, size_t bn)
{
    size_t n = an < bn ? an : bn;
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] < b[i]) {
            return -1;
        }
        if (a[i] > b[i]) {
            return 1;
        }
    }
    if (an < bn) {
        return -1;
    }
    if (an > bn) {
        return 1;
    }
    return 0;
}

robo_result_t robo_i18n_collator_compare(const robo_i18n_collator_t *coll,
                                         const char *a,
                                         size_t a_len,
                                         const char *b,
                                         size_t b_len,
                                         int *out_cmp)
{
    uint32_t *ac = NULL;
    uint32_t *bc = NULL;
    size_t an = 0;
    size_t bn = 0;
    ce_buf_t ace;
    ce_buf_t bce;
    robo_result_t r;
    int level;
    int max_lv;

    if (!coll || !out_cmp || (!a && a_len) || (!b && b_len)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_cmp = 0;
    memset(&ace, 0, sizeof(ace));
    memset(&bce, 0, sizeof(bce));

    r = decode_nfd(a, a_len, &ac, &an);
    if (r != ROBO_OK) {
        return r;
    }
    r = decode_nfd(b, b_len, &bc, &bn);
    if (r != ROBO_OK) {
        robo_free(ac);
        return r;
    }

    r = form_ces(coll, ac, an, &ace);
    if (r != ROBO_OK) {
        goto done;
    }
    r = form_ces(coll, bc, bn, &bce);
    if (r != ROBO_OK) {
        goto done;
    }

    max_lv = max_level(coll->options.strength);
    for (level = 0; level <= max_lv && level <= 3; level++) {
        wbuf_t aw;
        wbuf_t bw;
        int c;
        r = build_level_weights(coll, &ace, level, &aw);
        if (r != ROBO_OK) {
            robo_free(aw.w);
            goto done;
        }
        r = build_level_weights(coll, &bce, level, &bw);
        if (r != ROBO_OK) {
            robo_free(aw.w);
            robo_free(bw.w);
            goto done;
        }
        c = cmp_wbuf(&aw, &bw);
        robo_free(aw.w);
        robo_free(bw.w);
        if (c != 0) {
            *out_cmp = c;
            r = ROBO_OK;
            goto done;
        }
    }
    if (max_lv >= 4) {
        *out_cmp = cmp_codepoints(ac, an, bc, bn);
    } else {
        *out_cmp = 0;
    }
    r = ROBO_OK;

done:
    robo_free(ac);
    robo_free(bc);
    robo_free(ace.ces);
    robo_free(bce.ces);
    return r;
}

robo_result_t robo_i18n_collator_equal(const robo_i18n_collator_t *coll,
                                       const char *a,
                                       size_t a_len,
                                       const char *b,
                                       size_t b_len,
                                       int *out_equal)
{
    int cmp = 0;
    robo_result_t r = robo_i18n_collator_compare(coll, a, a_len, b, b_len, &cmp);
    if (r != ROBO_OK) {
        return r;
    }
    if (!out_equal) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_equal = (cmp == 0);
    return ROBO_OK;
}

robo_result_t robo_i18n_collator_get_sort_key(const robo_i18n_collator_t *coll,
                                              const char *text,
                                              size_t text_len,
                                              uint8_t *out_key,
                                              size_t out_cap,
                                              size_t *out_len)
{
    uint32_t *cps = NULL;
    size_t n = 0;
    ce_buf_t ces;
    robo_result_t r;
    int level;
    int max_lv;
    size_t need = 0;
    size_t pos = 0;
    wbuf_t levels[5];
    int li;

    if (!coll || !out_len || (!text && text_len)) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&ces, 0, sizeof(ces));
    memset(levels, 0, sizeof(levels));

    r = decode_nfd(text, text_len, &cps, &n);
    if (r != ROBO_OK) {
        return r;
    }
    r = form_ces(coll, cps, n, &ces);
    if (r != ROBO_OK) {
        robo_free(cps);
        return r;
    }

    max_lv = max_level(coll->options.strength);
    for (level = 0; level <= max_lv && level <= 3; level++) {
        r = build_level_weights(coll, &ces, level, &levels[level]);
        if (r != ROBO_OK) {
            goto cleanup;
        }
        need += levels[level].len * 2u + 1u; /* weights + separator */
    }
    if (max_lv >= 4) {
        need += n * 4u + 1u;
    }

    *out_len = need;
    if (!out_key) {
        r = ROBO_OK;
        goto cleanup;
    }
    if (out_cap < need) {
        r = ROBO_ERROR;
        goto cleanup;
    }

    for (level = 0; level <= max_lv && level <= 3; level++) {
        size_t i;
        for (i = 0; i < levels[level].len; i++) {
            uint16_t w = levels[level].w[i];
            out_key[pos++] = (uint8_t)((w >> 8) & 0xFFu);
            out_key[pos++] = (uint8_t)(w & 0xFFu);
        }
        out_key[pos++] = KEY_LEVEL_SEP;
    }
    if (max_lv >= 4) {
        size_t i;
        for (i = 0; i < n; i++) {
            uint32_t cp = cps[i];
            out_key[pos++] = (uint8_t)((cp >> 24) & 0xFFu);
            out_key[pos++] = (uint8_t)((cp >> 16) & 0xFFu);
            out_key[pos++] = (uint8_t)((cp >> 8) & 0xFFu);
            out_key[pos++] = (uint8_t)(cp & 0xFFu);
        }
        out_key[pos++] = KEY_LEVEL_SEP;
    }
    *out_len = pos;
    r = ROBO_OK;

cleanup:
    for (li = 0; li < 5; li++) {
        robo_free(levels[li].w);
    }
    robo_free(cps);
    robo_free(ces.ces);
    return r;
}

robo_result_t robo_i18n_compare_utf8_binary(const char *a,
                                            size_t a_len,
                                            const char *b,
                                            size_t b_len,
                                            int *out_cmp)
{
    size_t n;
    int c;
    if (!out_cmp || (!a && a_len) || (!b && b_len)) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = a_len < b_len ? a_len : b_len;
    c = 0;
    if (n) {
        c = memcmp(a, b, n);
    }
    if (c == 0) {
        if (a_len < b_len) {
            c = -1;
        } else if (a_len > b_len) {
            c = 1;
        }
    } else {
        c = c < 0 ? -1 : 1;
    }
    *out_cmp = c;
    return ROBO_OK;
}

robo_result_t robo_i18n_compare_utf8_codepoints(const char *a,
                                                size_t a_len,
                                                const char *b,
                                                size_t b_len,
                                                int *out_cmp)
{
    size_t ao = 0;
    size_t bo = 0;
    if (!out_cmp || (!a && a_len) || (!b && b_len)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_cmp = 0;
    while (ao < a_len && bo < b_len) {
        robo_uchar32_t ca, cb;
        size_t an = 0, bn = 0;
        if (robo_unicode_utf8_decode(a, a_len, ao, ROBO_UNICODE_STRICT, &ca, &an) !=
                ROBO_UNICODE_OK ||
            robo_unicode_utf8_decode(b, b_len, bo, ROBO_UNICODE_STRICT, &cb, &bn) !=
                ROBO_UNICODE_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (ca < cb) {
            *out_cmp = -1;
            return ROBO_OK;
        }
        if (ca > cb) {
            *out_cmp = 1;
            return ROBO_OK;
        }
        ao = an;
        bo = bn;
    }
    if (ao < a_len || bo < b_len) {
        /* ensure remaining is well-formed */
        size_t o = ao < a_len ? ao : bo;
        const char *s = ao < a_len ? a : b;
        size_t len = ao < a_len ? a_len : b_len;
        while (o < len) {
            robo_uchar32_t cp;
            size_t nx = 0;
            if (robo_unicode_utf8_decode(s, len, o, ROBO_UNICODE_STRICT, &cp, &nx) !=
                ROBO_UNICODE_OK) {
                return ROBO_PARSE_ERROR;
            }
            o = nx;
        }
        *out_cmp = (ao < a_len) ? 1 : -1;
    }
    return ROBO_OK;
}
