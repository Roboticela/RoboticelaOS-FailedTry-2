/*
 * RoboI18N search & matching (Goal 4.24).
 *
 * Windowed search over UTF-8 byte spans at code-point (or grapheme) boundaries
 * preserves mapping from transformed equality back to the original haystack.
 */

#include "RoboI18N/RoboI18NSearch.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboUnicode/RoboUcdData.h"

#include <string.h>

void robo_i18n_search_options_init(robo_i18n_search_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mode = ROBO_I18N_SEARCH_EXACT;
    opts->collation_strength = ROBO_I18N_COLLATION_TERTIARY;
    opts->ignore_diacritics = 0;
    opts->compatibility = 0;
    opts->grapheme_safe = 1;
}

const char *robo_i18n_search_mode_string(robo_i18n_search_mode_t mode)
{
    switch (mode) {
    case ROBO_I18N_SEARCH_EXACT:
        return "exact";
    case ROBO_I18N_SEARCH_CODEPOINT:
        return "codepoint";
    case ROBO_I18N_SEARCH_CANONICAL:
        return "canonical";
    case ROBO_I18N_SEARCH_CASEFOLD:
        return "casefold";
    case ROBO_I18N_SEARCH_CANONICAL_CASEFOLD:
        return "canonical_casefold";
    case ROBO_I18N_SEARCH_COLLATION:
        return "collation";
    default:
        return "unknown";
    }
}

static robo_result_t ensure_collator(robo_i18n_searcher_t *searcher)
{
    robo_i18n_collator_options_t copt;
    robo_i18n_locale_t und;
    const robo_i18n_locale_t *loc;
    robo_result_t r;

    if (searcher->collator) {
        robo_i18n_collator_options_init(&copt);
        copt.strength = searcher->options.collation_strength;
        return robo_i18n_collator_set_options(searcher->collator, &copt);
    }
    if (searcher->has_locale) {
        loc = &searcher->locale;
    } else {
        r = robo_i18n_locale_parse_canonical("und", 3, &und);
        if (r != ROBO_OK) {
            return r;
        }
        loc = &und;
    }
    robo_i18n_collator_options_init(&copt);
    copt.strength = searcher->options.collation_strength;
    return robo_i18n_collator_create(loc, NULL, &copt, &searcher->collator);
}

robo_result_t robo_i18n_searcher_init(robo_i18n_searcher_t *searcher,
                                     const robo_i18n_locale_t *locale,
                                     const robo_i18n_search_options_t *opts)
{
    robo_result_t r;

    if (!searcher) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(searcher, 0, sizeof(*searcher));
    if (opts) {
        searcher->options = *opts;
    } else {
        robo_i18n_search_options_init(&searcher->options);
    }
    if (locale) {
        searcher->locale = *locale;
        searcher->has_locale = 1;
    }
    if (searcher->options.mode == ROBO_I18N_SEARCH_COLLATION) {
        r = ensure_collator(searcher);
        if (r != ROBO_OK) {
            memset(searcher, 0, sizeof(*searcher));
            return r;
        }
    }
    searcher->ready = 1;
    return ROBO_OK;
}

void robo_i18n_searcher_destroy(robo_i18n_searcher_t *searcher)
{
    if (!searcher) {
        return;
    }
    if (searcher->collator) {
        robo_i18n_collator_destroy(searcher->collator);
        searcher->collator = NULL;
    }
    memset(searcher, 0, sizeof(*searcher));
}

robo_result_t robo_i18n_searcher_set_options(robo_i18n_searcher_t *searcher,
                                            const robo_i18n_search_options_t *opts)
{
    if (!searcher || !searcher->ready || !opts) {
        return ROBO_INVALID_ARGUMENT;
    }
    searcher->options = *opts;
    if (searcher->options.mode == ROBO_I18N_SEARCH_COLLATION) {
        return ensure_collator(searcher);
    }
    if (searcher->collator) {
        robo_i18n_collator_destroy(searcher->collator);
        searcher->collator = NULL;
    }
    return ROBO_OK;
}

static int utf8_ok(const char *s, size_t len)
{
    size_t off = 0;
    if (len == 0) {
        return 1;
    }
    if (!s) {
        return 0;
    }
    while (off < len) {
        size_t next = off;
        if (robo_unicode_utf8_decode(s, len, off, ROBO_UNICODE_STRICT, NULL, &next) !=
            ROBO_UNICODE_OK) {
            return 0;
        }
        off = next;
    }
    return 1;
}

static robo_result_t check_input(const char *s, size_t len)
{
    if (len > ROBO_I18N_SEARCH_INPUT_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len > 0 && !s) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!utf8_ok(s, len)) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

/* ---- Transform helpers (whole-string) ---- */

static robo_result_t decode_cps(const char *s,
                                size_t len,
                                robo_uchar32_t **out_cps,
                                size_t *out_n)
{
    size_t off = 0;
    size_t n = 0;
    size_t cap;
    robo_uchar32_t *cps;

    *out_cps = NULL;
    *out_n = 0;
    while (off < len) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        if (robo_unicode_utf8_decode(s, len, off, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            return ROBO_PARSE_ERROR;
        }
        n++;
        off = next;
    }
    if (n == 0) {
        return ROBO_OK;
    }
    cap = n * sizeof(robo_uchar32_t);
    cps = (robo_uchar32_t *)robo_malloc(cap);
    if (!cps) {
        return ROBO_OUT_OF_MEMORY;
    }
    off = 0;
    n = 0;
    while (off < len) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        (void)robo_unicode_utf8_decode(s, len, off, ROBO_UNICODE_STRICT, &cp, &next);
        cps[n++] = cp;
        off = next;
    }
    *out_cps = cps;
    *out_n = n;
    return ROBO_OK;
}

static robo_result_t strip_mn(robo_uchar32_t *cps, size_t n, size_t *out_n)
{
    size_t w = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        if (robo_unicode_category(cps[i]) == (robo_unicode_general_category_t)ROBO_U_GC_MN) {
            continue;
        }
        cps[w++] = cps[i];
    }
    *out_n = w;
    return ROBO_OK;
}

/**
 * Build a comparable scalar sequence for @p s under matcher options.
 * Caller frees *out_cps when non-NULL.
 */
static robo_result_t transform_cps(const robo_i18n_searcher_t *searcher,
                                   const char *s,
                                   size_t len,
                                   robo_uchar32_t **out_cps,
                                   size_t *out_n)
{
    robo_uchar32_t *cps = NULL;
    size_t n = 0;
    robo_result_t r;
    robo_unicode_norm_form_t nform;

    *out_cps = NULL;
    *out_n = 0;

    if (searcher->options.mode == ROBO_I18N_SEARCH_EXACT) {
        /* Represent as raw bytes in a synthetic way — callers use memcmp path. */
        return ROBO_OK;
    }

    r = decode_cps(s, len, &cps, &n);
    if (r != ROBO_OK) {
        return r;
    }

    nform = searcher->options.compatibility ? ROBO_UNICODE_NORM_NFKD : ROBO_UNICODE_NORM_NFD;

    if (searcher->options.mode == ROBO_I18N_SEARCH_CANONICAL ||
        searcher->options.mode == ROBO_I18N_SEARCH_CANONICAL_CASEFOLD ||
        searcher->options.ignore_diacritics) {
        robo_uchar32_t *norm = NULL;
        size_t nn = 0;
        r = robo_unicode_normalize(cps, n, nform, &norm, &nn);
        robo_free(cps);
        cps = norm;
        n = nn;
        if (r != ROBO_OK) {
            return r;
        }
    }

    if (searcher->options.ignore_diacritics &&
        searcher->options.mode != ROBO_I18N_SEARCH_COLLATION) {
        r = strip_mn(cps, n, &n);
        if (r != ROBO_OK) {
            robo_free(cps);
            return r;
        }
    }

    if (searcher->options.mode == ROBO_I18N_SEARCH_CASEFOLD ||
        searcher->options.mode == ROBO_I18N_SEARCH_CANONICAL_CASEFOLD) {
        robo_uchar32_t *folded = NULL;
        size_t fn = 0;
        r = robo_unicode_case_map_cps(cps, n, ROBO_UNICODE_CASE_FOLD, &folded, &fn);
        robo_free(cps);
        if (r != ROBO_OK) {
            return r;
        }
        cps = folded;
        n = fn;
    }

    *out_cps = cps;
    *out_n = n;
    return ROBO_OK;
}

static int cps_cmp(const robo_uchar32_t *a, size_t an, const robo_uchar32_t *b, size_t bn)
{
    size_t i;
    size_t n = an < bn ? an : bn;
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

robo_result_t robo_i18n_search_compare(const robo_i18n_searcher_t *searcher,
                                      const char *a,
                                      size_t a_len,
                                      const char *b,
                                      size_t b_len,
                                      int *out_cmp)
{
    robo_result_t r;
    if (!searcher || !searcher->ready || !out_cmp) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = check_input(a, a_len);
    if (r != ROBO_OK) {
        return r;
    }
    r = check_input(b, b_len);
    if (r != ROBO_OK) {
        return r;
    }

    if (searcher->options.mode == ROBO_I18N_SEARCH_EXACT) {
        size_t n = a_len < b_len ? a_len : b_len;
        int c = 0;
        if (n > 0) {
            c = memcmp(a, b, n);
        }
        if (c == 0) {
            c = (a_len > b_len) - (a_len < b_len);
        }
        *out_cmp = c;
        return ROBO_OK;
    }

    if (searcher->options.mode == ROBO_I18N_SEARCH_COLLATION) {
        if (!searcher->collator) {
            return ROBO_INVALID_ARGUMENT;
        }
        return robo_i18n_collator_compare(searcher->collator, a, a_len, b, b_len, out_cmp);
    }

    {
        robo_uchar32_t *ac = NULL;
        robo_uchar32_t *bc = NULL;
        size_t an = 0;
        size_t bn = 0;
        r = transform_cps(searcher, a, a_len, &ac, &an);
        if (r != ROBO_OK) {
            return r;
        }
        r = transform_cps(searcher, b, b_len, &bc, &bn);
        if (r != ROBO_OK) {
            robo_free(ac);
            return r;
        }
        if (searcher->options.mode == ROBO_I18N_SEARCH_CODEPOINT &&
            !searcher->options.ignore_diacritics) {
            /* transform_cps for CODEPOINT without diacritics is decode only */
        }
        *out_cmp = cps_cmp(ac, an, bc, bn);
        robo_free(ac);
        robo_free(bc);
        return ROBO_OK;
    }
}

robo_result_t robo_i18n_search_equal(const robo_i18n_searcher_t *searcher,
                                    const char *a,
                                    size_t a_len,
                                    const char *b,
                                    size_t b_len,
                                    int *out_equal)
{
    int cmp = 1;
    robo_result_t r = robo_i18n_search_compare(searcher, a, a_len, b, b_len, &cmp);
    if (r != ROBO_OK) {
        return r;
    }
    *out_equal = (cmp == 0) ? 1 : 0;
    return ROBO_OK;
}

/* ---- Boundary enumeration ---- */

#define ROBO_I18N_SEARCH_BOUND_MAX 8192u

static robo_result_t collect_cp_bounds(const char *s,
                                       size_t len,
                                       size_t *bounds,
                                       size_t bound_cap,
                                       size_t *out_count)
{
    size_t off = 0;
    size_t n = 0;
    if (bound_cap < 1) {
        return ROBO_ERROR;
    }
    bounds[n++] = 0;
    while (off < len) {
        size_t next = off;
        if (robo_unicode_utf8_decode(s, len, off, ROBO_UNICODE_STRICT, NULL, &next) !=
            ROBO_UNICODE_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (n >= bound_cap) {
            return ROBO_ERROR;
        }
        bounds[n++] = next;
        off = next;
    }
    *out_count = n;
    return ROBO_OK;
}

static robo_result_t collect_grapheme_bounds(const char *s,
                                             size_t len,
                                             size_t *bounds,
                                             size_t bound_cap,
                                             size_t *out_count)
{
    robo_unicode_grapheme_iter_t it;
    robo_result_t r;
    size_t n = 0;
    size_t cs = 0;
    size_t ce = 0;

    if (bound_cap < 1) {
        return ROBO_ERROR;
    }
    bounds[n++] = 0;
    r = robo_unicode_grapheme_iter_init(&it, s, len, ROBO_UNICODE_STRICT);
    if (r != ROBO_OK) {
        return r;
    }
    for (;;) {
        r = robo_unicode_grapheme_iter_next(&it, &cs, &ce);
        if (r != ROBO_OK) {
            return r;
        }
        if (cs == ce && cs == len) {
            break;
        }
        if (n >= bound_cap) {
            return ROBO_ERROR;
        }
        if (bounds[n - 1] != ce) {
            bounds[n++] = ce;
        }
    }
    if (n == 0 || bounds[n - 1] != len) {
        if (n >= bound_cap) {
            return ROBO_ERROR;
        }
        bounds[n++] = len;
    }
    *out_count = n;
    return ROBO_OK;
}

static robo_result_t expand_grapheme(const char *hay,
                                     size_t hay_len,
                                     size_t start,
                                     size_t end,
                                     size_t *gs,
                                     size_t *ge)
{
    size_t cs = start;
    size_t ce = start;

    *gs = start;
    *ge = end;
    if (hay_len == 0) {
        return ROBO_OK;
    }
    if (start < hay_len) {
        if (robo_unicode_grapheme_next(hay, hay_len, start, ROBO_UNICODE_STRICT, &cs, &ce) ==
            ROBO_OK) {
            /* If start is mid-cluster, walk back. */
            if (cs > start) {
                size_t ps = 0;
                size_t pe = 0;
                if (robo_unicode_grapheme_prev(hay, hay_len, start, ROBO_UNICODE_STRICT, &ps,
                                               &pe) == ROBO_OK &&
                    ps < pe) {
                    *gs = ps;
                }
            } else {
                *gs = cs;
            }
        }
    }
    if (end > 0 && end <= hay_len) {
        size_t pos = end - 1u;
        size_t ps = 0;
        size_t pe = 0;
        if (robo_unicode_grapheme_prev(hay, hay_len, end, ROBO_UNICODE_STRICT, &ps, &pe) ==
                ROBO_OK &&
            ps < pe) {
            *ge = pe;
        } else if (robo_unicode_grapheme_next(hay, hay_len, pos, ROBO_UNICODE_STRICT, &ps, &pe) ==
                       ROBO_OK &&
                   ps < pe) {
            *ge = pe;
        }
    }
    if (*gs > start) {
        *gs = start;
    }
    if (*ge < end) {
        *ge = end;
    }
    (void)ce;
    return ROBO_OK;
}

static void fill_match(const robo_i18n_searcher_t *searcher,
                       const char *hay,
                       size_t hay_len,
                       size_t start,
                       size_t end,
                       robo_i18n_search_hit_t *out)
{
    out->start = start;
    out->end = end;
    out->grapheme_start = start;
    out->grapheme_end = end;
    if (searcher->options.grapheme_safe) {
        (void)expand_grapheme(hay, hay_len, start, end, &out->grapheme_start, &out->grapheme_end);
    }
}

static robo_result_t window_equal(const robo_i18n_searcher_t *searcher,
                                  const char *hay,
                                  size_t hay_len,
                                  size_t start,
                                  size_t end,
                                  const char *needle,
                                  size_t needle_len,
                                  int *out_eq)
{
    (void)hay_len;
    return robo_i18n_search_equal(searcher, hay + start, end - start, needle, needle_len, out_eq);
}

/**
 * Find first match at/after @p from_byte using windowed boundary search.
 */
static robo_result_t find_from(const robo_i18n_searcher_t *searcher,
                               const char *hay,
                               size_t hay_len,
                               const char *needle,
                               size_t needle_len,
                               size_t from_byte,
                               int *out_found,
                               robo_i18n_search_hit_t *out_match)
{
    size_t bounds[ROBO_I18N_SEARCH_BOUND_MAX];
    size_t bn = 0;
    size_t i;
    size_t j;
    robo_result_t r;
    int use_grapheme_windows = searcher->options.grapheme_safe;

    *out_found = 0;
    if (out_match) {
        memset(out_match, 0, sizeof(*out_match));
    }

    if (needle_len == 0) {
        size_t at = from_byte;
        if (at > hay_len) {
            at = hay_len;
        }
        *out_found = 1;
        if (out_match) {
            fill_match(searcher, hay, hay_len, at, at, out_match);
        }
        return ROBO_OK;
    }

    if (searcher->options.mode == ROBO_I18N_SEARCH_EXACT && !searcher->options.ignore_diacritics) {
        /* Fast path: byte search; validate needle already checked. */
        size_t pos = from_byte;
        while (pos + needle_len <= hay_len) {
            if (memcmp(hay + pos, needle, needle_len) == 0) {
                *out_found = 1;
                if (out_match) {
                    fill_match(searcher, hay, hay_len, pos, pos + needle_len, out_match);
                }
                return ROBO_OK;
            }
            {
                size_t next = pos;
                if (robo_unicode_utf8_decode(hay, hay_len, pos, ROBO_UNICODE_STRICT, NULL, &next) !=
                    ROBO_UNICODE_OK) {
                    return ROBO_PARSE_ERROR;
                }
                pos = next;
            }
        }
        return ROBO_OK;
    }

    if (use_grapheme_windows) {
        r = collect_grapheme_bounds(hay, hay_len, bounds, ROBO_I18N_SEARCH_BOUND_MAX, &bn);
    } else {
        r = collect_cp_bounds(hay, hay_len, bounds, ROBO_I18N_SEARCH_BOUND_MAX, &bn);
    }
    if (r != ROBO_OK) {
        return r;
    }

    for (i = 0; i + 1 < bn; i++) {
        size_t start = bounds[i];
        if (start < from_byte) {
            continue;
        }
        for (j = i + 1; j < bn; j++) {
            size_t end = bounds[j];
            int eq = 0;
            /* Length heuristic for exact-ish modes */
            if (searcher->options.mode == ROBO_I18N_SEARCH_CODEPOINT &&
                !searcher->options.ignore_diacritics && end - start > needle_len * 4u + 16u &&
                end - start > needle_len + 64u) {
                /* still try — casefold/norm can shrink */
            }
            r = window_equal(searcher, hay, hay_len, start, end, needle, needle_len, &eq);
            if (r != ROBO_OK) {
                return r;
            }
            if (eq) {
                *out_found = 1;
                if (out_match) {
                    fill_match(searcher, hay, hay_len, start, end, out_match);
                }
                return ROBO_OK;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_search_find(const robo_i18n_searcher_t *searcher,
                                   const char *haystack,
                                   size_t hay_len,
                                   const char *needle,
                                   size_t needle_len,
                                   int *out_found,
                                   robo_i18n_search_hit_t *out_match)
{
    robo_result_t r;
    if (!searcher || !searcher->ready || !out_found) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = check_input(haystack, hay_len);
    if (r != ROBO_OK) {
        return r;
    }
    r = check_input(needle, needle_len);
    if (r != ROBO_OK) {
        return r;
    }
    return find_from(searcher, haystack, hay_len, needle, needle_len, 0, out_found, out_match);
}

robo_result_t robo_i18n_search_contains(const robo_i18n_searcher_t *searcher,
                                       const char *haystack,
                                       size_t hay_len,
                                       const char *needle,
                                       size_t needle_len,
                                       int *out_found)
{
    return robo_i18n_search_find(searcher, haystack, hay_len, needle, needle_len, out_found, NULL);
}

robo_result_t robo_i18n_search_starts_with(const robo_i18n_searcher_t *searcher,
                                          const char *haystack,
                                          size_t hay_len,
                                          const char *needle,
                                          size_t needle_len,
                                          int *out_found)
{
    size_t bounds[ROBO_I18N_SEARCH_BOUND_MAX];
    size_t bn = 0;
    size_t j;
    robo_result_t r;

    if (!searcher || !searcher->ready || !out_found) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_found = 0;
    r = check_input(haystack, hay_len);
    if (r != ROBO_OK) {
        return r;
    }
    r = check_input(needle, needle_len);
    if (r != ROBO_OK) {
        return r;
    }
    if (needle_len == 0) {
        *out_found = 1;
        return ROBO_OK;
    }
    if (searcher->options.grapheme_safe) {
        r = collect_grapheme_bounds(haystack, hay_len, bounds, ROBO_I18N_SEARCH_BOUND_MAX, &bn);
    } else {
        r = collect_cp_bounds(haystack, hay_len, bounds, ROBO_I18N_SEARCH_BOUND_MAX, &bn);
    }
    if (r != ROBO_OK) {
        return r;
    }
    for (j = 1; j < bn; j++) {
        int eq = 0;
        r = window_equal(searcher, haystack, hay_len, 0, bounds[j], needle, needle_len, &eq);
        if (r != ROBO_OK) {
            return r;
        }
        if (eq) {
            *out_found = 1;
            return ROBO_OK;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_search_ends_with(const robo_i18n_searcher_t *searcher,
                                        const char *haystack,
                                        size_t hay_len,
                                        const char *needle,
                                        size_t needle_len,
                                        int *out_found)
{
    size_t bounds[ROBO_I18N_SEARCH_BOUND_MAX];
    size_t bn = 0;
    size_t i;
    robo_result_t r;

    if (!searcher || !searcher->ready || !out_found) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_found = 0;
    r = check_input(haystack, hay_len);
    if (r != ROBO_OK) {
        return r;
    }
    r = check_input(needle, needle_len);
    if (r != ROBO_OK) {
        return r;
    }
    if (needle_len == 0) {
        *out_found = 1;
        return ROBO_OK;
    }
    if (searcher->options.grapheme_safe) {
        r = collect_grapheme_bounds(haystack, hay_len, bounds, ROBO_I18N_SEARCH_BOUND_MAX, &bn);
    } else {
        r = collect_cp_bounds(haystack, hay_len, bounds, ROBO_I18N_SEARCH_BOUND_MAX, &bn);
    }
    if (r != ROBO_OK) {
        return r;
    }
    for (i = 0; i + 1 < bn; i++) {
        int eq = 0;
        r = window_equal(searcher, haystack, hay_len, bounds[i], hay_len, needle, needle_len, &eq);
        if (r != ROBO_OK) {
            return r;
        }
        if (eq) {
            *out_found = 1;
            return ROBO_OK;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_search_iter_init(robo_i18n_search_iter_t *it,
                                        const robo_i18n_searcher_t *searcher,
                                        const char *haystack,
                                        size_t hay_len,
                                        const char *needle,
                                        size_t needle_len)
{
    robo_result_t r;
    if (!it || !searcher || !searcher->ready) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = check_input(haystack, hay_len);
    if (r != ROBO_OK) {
        return r;
    }
    r = check_input(needle, needle_len);
    if (r != ROBO_OK) {
        return r;
    }
    memset(it, 0, sizeof(*it));
    it->searcher = searcher;
    it->haystack = haystack;
    it->hay_len = hay_len;
    it->needle = needle;
    it->needle_len = needle_len;
    it->next_start = 0;
    it->active = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_search_iter_next(robo_i18n_search_iter_t *it,
                                        int *out_found,
                                        robo_i18n_search_hit_t *out_match)
{
    robo_result_t r;
    robo_i18n_search_hit_t m;
    int found = 0;

    if (!it || !it->active || !out_found) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_found = 0;
    if (it->next_start > it->hay_len) {
        return ROBO_OK;
    }
    r = find_from(it->searcher, it->haystack, it->hay_len, it->needle, it->needle_len,
                  it->next_start, &found, &m);
    if (r != ROBO_OK) {
        return r;
    }
    if (!found) {
        it->next_start = it->hay_len + 1u;
        return ROBO_OK;
    }
    *out_found = 1;
    if (out_match) {
        *out_match = m;
    }
    if (it->needle_len == 0) {
        /* Advance one grapheme to avoid infinite empty matches. */
        size_t cs = 0;
        size_t ce = 0;
        if (m.end >= it->hay_len) {
            it->next_start = it->hay_len + 1u;
        } else if (robo_unicode_grapheme_next(it->haystack, it->hay_len, m.end, ROBO_UNICODE_STRICT,
                                              &cs, &ce) == ROBO_OK &&
                   ce > m.end) {
            it->next_start = ce;
        } else {
            size_t next = m.end;
            if (robo_unicode_utf8_decode(it->haystack, it->hay_len, m.end, ROBO_UNICODE_STRICT, NULL,
                                         &next) == ROBO_UNICODE_OK) {
                it->next_start = next;
            } else {
                it->next_start = it->hay_len + 1u;
            }
        }
    } else {
        it->next_start = m.end;
        if (it->next_start == m.start) {
            it->next_start++;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_search_find_all(const robo_i18n_searcher_t *searcher,
                                       const char *haystack,
                                       size_t hay_len,
                                       const char *needle,
                                       size_t needle_len,
                                       robo_i18n_search_hit_t *out_matches,
                                       size_t out_cap,
                                       size_t *out_count)
{
    robo_i18n_search_iter_t it;
    robo_result_t r;
    size_t n = 0;
    size_t cap;

    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0;
    if (out_cap > 0 && !out_matches) {
        return ROBO_INVALID_ARGUMENT;
    }
    cap = out_cap;
    if (cap > ROBO_I18N_SEARCH_FIND_ALL_MAX) {
        cap = ROBO_I18N_SEARCH_FIND_ALL_MAX;
    }
    r = robo_i18n_search_iter_init(&it, searcher, haystack, hay_len, needle, needle_len);
    if (r != ROBO_OK) {
        return r;
    }
    while (n < cap) {
        int found = 0;
        robo_i18n_search_hit_t m;
        r = robo_i18n_search_iter_next(&it, &found, &m);
        if (r != ROBO_OK) {
            return r;
        }
        if (!found) {
            break;
        }
        out_matches[n++] = m;
    }
    *out_count = n;
    return ROBO_OK;
}
