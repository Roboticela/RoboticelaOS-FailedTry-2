/*
 * RoboFontFallback — coverage-first resolver (Goal 5.15).
 */

#include "RoboFont/RoboFontFallback.h"

#include "RoboFont/RoboFontClassification.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define RESOLVER_COOKIE 0x5246424Bu /* 'RFBK' */

typedef struct {
    uint64_t registry_generation;
    robo_font_face_id_t primary_id;
    uint32_t cp_hash;
    uint32_t codepoint_count;
    uint32_t cps[ROBO_FONT_FALLBACK_MAX_CODEPOINTS];
    robo_font_weight_t weight;
    robo_font_stretch_t stretch;
    robo_font_style_t style;
    uint32_t lang_hash;
    robo_unicode_script_t script;
    uint8_t has_script;
    uint32_t flags;
    uint32_t policy_hash;
    /* value */
    uint8_t used_primary;
    uint8_t found;
    robo_font_fallback_stage_t stage;
    robo_font_face_id_t face_id;
    robo_font_fallback_score_t score;
    uint32_t primary_missing_count;
    uint64_t lru_tick;
} fallback_cache_entry_t;

struct robo_font_fallback_resolver {
    uint32_t cookie;
    robo_font_registry_t *registry; /* borrowed */
    robo_font_fallback_policy_t policy;
    /* owned copies of policy string lists (pointers into owned blob) */
    char **preferred_owned;
    uint32_t preferred_owned_count;
    char **lang_pref_owned;
    uint32_t lang_pref_owned_count;
    char *last_resort_owned;

    fallback_cache_entry_t *cache;
    uint32_t cache_cap;
    uint32_t cache_count;
    uint64_t lru_tick;
    robo_font_fallback_cache_stats_t stats;
};

/* ---- helpers ---- */

static uint32_t hash_bytes(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0u; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t hash_cstr(const char *s, size_t len)
{
    if (!s) {
        return 0u;
    }
    if (len == 0u) {
        len = strlen(s);
    }
    return hash_bytes(s, len);
}

static int utf8_casefold_eq(const char *a, size_t al, const char *b, size_t bl)
{
    int cmp = 1;
    if (!a || !b) {
        return 0;
    }
    if (al == 0u) {
        al = strlen(a);
    }
    if (bl == 0u) {
        bl = strlen(b);
    }
    if (robo_unicode_casefold_compare_utf8(a, al, b, bl, ROBO_UNICODE_REPLACE, &cmp) !=
        ROBO_OK) {
        return 0;
    }
    return cmp == 0;
}

static int family_eq(const char *a, const char *b)
{
    if (!a || !b || !a[0] || !b[0]) {
        return 0;
    }
    return utf8_casefold_eq(a, 0u, b, 0u);
}

static robo_result_t ensure_face_ready(robo_font_face_t *face)
{
    robo_result_t res;
    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_face_parse_classification(face);
    if (res != ROBO_OK && res != ROBO_NOT_FOUND && res != ROBO_NOT_SUPPORTED) {
        return res;
    }
    res = robo_font_face_parse_coverage(face);
    if (res == ROBO_NOT_FOUND || res == ROBO_NOT_SUPPORTED) {
        return ROBO_OK; /* empty coverage treated as uncovered */
    }
    return res;
}

static int face_covers_cp(robo_font_face_t *face, uint32_t cp)
{
    const robo_font_coverage_t *cov = robo_font_face_coverage(face);
    if (!cov) {
        return 0;
    }
    return robo_font_coverage_contains(cov, cp);
}

robo_result_t robo_font_fallback_face_covers(robo_font_face_t *face,
                                             const uint32_t *codepoints,
                                             uint32_t count,
                                             uint32_t flags,
                                             int *out_covers_all,
                                             int *out_covers_base,
                                             uint32_t *out_missing_count)
{
    uint32_t i;
    uint32_t missing = 0u;
    int all = 1;
    int base = 0;
    robo_result_t res;
    int require_all = (flags & ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL) != 0;

    if (!face || !codepoints || count == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Default safety: if neither flag bit is meaningful, require all. */
    if ((flags & ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL) == 0u &&
        (flags & ROBO_FONT_FALLBACK_FLAG_NO_CACHE) == 0u && flags == 0u) {
        require_all = 1;
    }

    res = ensure_face_ready(face);
    if (res != ROBO_OK) {
        return res;
    }

    for (i = 0u; i < count; i++) {
        if (!robo_font_cmap_is_unicode_scalar(codepoints[i])) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (face_covers_cp(face, codepoints[i])) {
            if (i == 0u) {
                base = 1;
            }
        } else {
            missing++;
            all = 0;
            if (i == 0u) {
                base = 0;
            }
        }
    }

    if (!require_all) {
        /* Sequence foundation / partial mode: base scalar is the gate. */
        all = base;
    }

    if (out_covers_all) {
        *out_covers_all = all;
    }
    if (out_covers_base) {
        *out_covers_base = base;
    }
    if (out_missing_count) {
        *out_missing_count = missing;
    }
    return ROBO_OK;
}

/* ---- init / validate ---- */

void robo_font_fallback_policy_init(robo_font_fallback_policy_t *policy)
{
    if (!policy) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->flags = ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL;
    policy->max_scan = ROBO_FONT_FALLBACK_MAX_SCAN;
    policy->cache_capacity = ROBO_FONT_FALLBACK_CACHE_DEFAULT;
}

void robo_font_fallback_request_init(robo_font_fallback_request_t *req)
{
    if (!req) {
        return;
    }
    memset(req, 0, sizeof(*req));
    req->weight = ROBO_FONT_WEIGHT_NORMAL;
    req->stretch = ROBO_FONT_STRETCH_NORMAL;
    req->style = ROBO_FONT_STYLE_NORMAL;
    req->flags = ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL;
}

robo_result_t robo_font_fallback_request_validate(
    const robo_font_fallback_request_t *req)
{
    uint32_t i;
    if (!req || !req->primary || !req->codepoints || req->codepoint_count == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->codepoint_count > ROBO_FONT_FALLBACK_MAX_CODEPOINTS) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < req->codepoint_count; i++) {
        if (!robo_font_cmap_is_unicode_scalar(req->codepoints[i])) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    if (!robo_font_weight_is_valid(req->weight) ||
        !robo_font_stretch_is_valid(req->stretch)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->preferred_family_count > ROBO_FONT_FALLBACK_MAX_PREFERRED) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (req->preferred_family_count != 0u && !req->preferred_families) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

void robo_font_fallback_result_init(robo_font_fallback_result_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cookie = ROBO_FONT_FALLBACK_RESULT_COOKIE;
    out->stage = ROBO_FONT_FALLBACK_STAGE_NONE;
}

void robo_font_fallback_result_clear(robo_font_fallback_result_t *res)
{
    if (!res) {
        return;
    }
    if (res->cookie == ROBO_FONT_FALLBACK_RESULT_COOKIE && res->face) {
        robo_font_face_release(res->face);
        res->face = NULL;
    }
    robo_font_fallback_result_init(res);
}

int robo_font_fallback_score_compare(const robo_font_fallback_score_t *a,
                                     const robo_font_fallback_score_t *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->eligible != b->eligible) {
        return a->eligible ? -1 : 1;
    }
    if (!a->eligible) {
        return 0;
    }
#define CMP_U32(field)                             \
    do {                                           \
        if (a->field < b->field) {                 \
            return -1;                             \
        }                                          \
        if (a->field > b->field) {                 \
            return 1;                              \
        }                                          \
    } while (0)
    CMP_U32(stage_rank);
    CMP_U32(preferred_family_rank);
    CMP_U32(language_rank);
    CMP_U32(script_rank);
    CMP_U32(stretch_dist);
    CMP_U32(style_dist);
    CMP_U32(weight_dist);
    if (a->face_id < b->face_id) {
        return -1;
    }
    if (a->face_id > b->face_id) {
        return 1;
    }
#undef CMP_U32
    return 0;
}

/* ---- policy ownership ---- */

static void free_owned_list(char **list, uint32_t n)
{
    uint32_t i;
    if (!list) {
        return;
    }
    for (i = 0u; i < n; i++) {
        robo_free(list[i]);
    }
    robo_free(list);
}

static robo_result_t dup_cstr_list(const char *const *src, uint32_t n, char ***out)
{
    char **dst;
    uint32_t i;
    *out = NULL;
    if (n == 0u) {
        return ROBO_OK;
    }
    if (!src) {
        return ROBO_INVALID_ARGUMENT;
    }
    dst = (char **)robo_calloc(n, sizeof(char *));
    if (!dst) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < n; i++) {
        size_t len;
        if (!src[i]) {
            free_owned_list(dst, i);
            return ROBO_INVALID_ARGUMENT;
        }
        len = strlen(src[i]);
        if (len > ROBO_FONT_FALLBACK_MAX_FAMILY_BYTES) {
            free_owned_list(dst, i);
            return ROBO_INVALID_ARGUMENT;
        }
        dst[i] = (char *)robo_malloc(len + 1u);
        if (!dst[i]) {
            free_owned_list(dst, i);
            return ROBO_OUT_OF_MEMORY;
        }
        memcpy(dst[i], src[i], len + 1u);
    }
    *out = dst;
    return ROBO_OK;
}

static uint32_t policy_hash(const robo_font_fallback_policy_t *p)
{
    uint32_t h = 0u;
    uint32_t i;
    if (!p) {
        return 0u;
    }
    h ^= p->flags;
    h ^= p->max_scan * 131u;
    h ^= (uint32_t)p->last_resort_face_id;
    for (i = 0u; i < p->preferred_family_count; i++) {
        h ^= hash_cstr(p->preferred_families[i], 0u) * (i + 3u);
    }
    for (i = 0u; i < p->language_preferred_family_count; i++) {
        h ^= hash_cstr(p->language_preferred_families[i], 0u) * (i + 7u);
    }
    if (p->last_resort_family) {
        h ^= hash_cstr(p->last_resort_family, p->last_resort_family_len);
    }
    return h;
}

static robo_result_t apply_policy(robo_font_fallback_resolver_t *r,
                                  const robo_font_fallback_policy_t *src)
{
    robo_font_fallback_policy_t tmp;
    char **pref = NULL;
    char **lang = NULL;
    char *last = NULL;
    robo_result_t res;

    if (!src) {
        robo_font_fallback_policy_init(&tmp);
        src = &tmp;
    }
    if (src->preferred_family_count > ROBO_FONT_FALLBACK_MAX_PREFERRED ||
        src->language_preferred_family_count > ROBO_FONT_FALLBACK_MAX_PREFERRED) {
        return ROBO_INVALID_ARGUMENT;
    }

    res = dup_cstr_list(src->preferred_families, src->preferred_family_count, &pref);
    if (res != ROBO_OK) {
        return res;
    }
    res = dup_cstr_list(src->language_preferred_families,
                        src->language_preferred_family_count, &lang);
    if (res != ROBO_OK) {
        free_owned_list(pref, src->preferred_family_count);
        return res;
    }
    if (src->last_resort_family) {
        size_t len = src->last_resort_family_len
                         ? src->last_resort_family_len
                         : strlen(src->last_resort_family);
        if (len > ROBO_FONT_FALLBACK_MAX_FAMILY_BYTES) {
            free_owned_list(pref, src->preferred_family_count);
            free_owned_list(lang, src->language_preferred_family_count);
            return ROBO_INVALID_ARGUMENT;
        }
        last = (char *)robo_malloc(len + 1u);
        if (!last) {
            free_owned_list(pref, src->preferred_family_count);
            free_owned_list(lang, src->language_preferred_family_count);
            return ROBO_OUT_OF_MEMORY;
        }
        memcpy(last, src->last_resort_family, len);
        last[len] = '\0';
    }

    free_owned_list(r->preferred_owned, r->preferred_owned_count);
    free_owned_list(r->lang_pref_owned, r->lang_pref_owned_count);
    robo_free(r->last_resort_owned);

    r->preferred_owned = pref;
    r->preferred_owned_count = src->preferred_family_count;
    r->lang_pref_owned = lang;
    r->lang_pref_owned_count = src->language_preferred_family_count;
    r->last_resort_owned = last;

    r->policy = *src;
    r->policy.preferred_families =
        (const char *const *)r->preferred_owned;
    r->policy.language_preferred_families =
        (const char *const *)r->lang_pref_owned;
    r->policy.last_resort_family = r->last_resort_owned;
    r->policy.last_resort_family_len = 0u;
    if ((r->policy.flags & ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL) == 0u &&
        (src->flags & ROBO_FONT_FALLBACK_FLAG_NO_CACHE) == 0u &&
        src->flags == 0u) {
        r->policy.flags |= ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL;
    }
    if (r->policy.max_scan == 0u || r->policy.max_scan > ROBO_FONT_FALLBACK_MAX_SCAN) {
        r->policy.max_scan = ROBO_FONT_FALLBACK_MAX_SCAN;
    }
    return ROBO_OK;
}

robo_result_t robo_font_fallback_resolver_create(
    robo_font_registry_t *registry,
    const robo_font_fallback_policy_t *default_policy,
    robo_font_fallback_resolver_t **out)
{
    robo_font_fallback_resolver_t *r;
    robo_font_fallback_policy_t pol;
    uint32_t cap;
    robo_result_t res;

    if (!out || !registry) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!default_policy) {
        robo_font_fallback_policy_init(&pol);
        default_policy = &pol;
    }
    cap = default_policy->cache_capacity
              ? default_policy->cache_capacity
              : ROBO_FONT_FALLBACK_CACHE_DEFAULT;
    if (cap > ROBO_FONT_FALLBACK_CACHE_HARD) {
        cap = ROBO_FONT_FALLBACK_CACHE_HARD;
    }

    r = (robo_font_fallback_resolver_t *)robo_calloc(1u, sizeof(*r));
    if (!r) {
        return ROBO_OUT_OF_MEMORY;
    }
    r->cookie = RESOLVER_COOKIE;
    r->registry = registry;
    r->cache_cap = cap;
    if (cap > 0u) {
        r->cache = (fallback_cache_entry_t *)robo_calloc(cap, sizeof(*r->cache));
        if (!r->cache) {
            robo_free(r);
            return ROBO_OUT_OF_MEMORY;
        }
    }
    res = apply_policy(r, default_policy);
    if (res != ROBO_OK) {
        robo_free(r->cache);
        robo_free(r);
        return res;
    }
    *out = r;
    return ROBO_OK;
}

void robo_font_fallback_resolver_destroy(robo_font_fallback_resolver_t *r)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    free_owned_list(r->preferred_owned, r->preferred_owned_count);
    free_owned_list(r->lang_pref_owned, r->lang_pref_owned_count);
    robo_free(r->last_resort_owned);
    robo_free(r->cache);
    memset(r, 0, sizeof(*r));
    robo_free(r);
}

robo_result_t robo_font_fallback_resolver_set_policy(
    robo_font_fallback_resolver_t *r,
    const robo_font_fallback_policy_t *policy)
{
    robo_result_t res;
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = apply_policy(r, policy);
    if (res == ROBO_OK) {
        robo_font_fallback_resolver_cache_clear(r);
    }
    return res;
}

const robo_font_fallback_policy_t *robo_font_fallback_resolver_policy(
    const robo_font_fallback_resolver_t *r)
{
    return (r && r->cookie == RESOLVER_COOKIE) ? &r->policy : NULL;
}

void robo_font_fallback_resolver_cache_clear(robo_font_fallback_resolver_t *r)
{
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    if (r->cache && r->cache_cap) {
        memset(r->cache, 0, (size_t)r->cache_cap * sizeof(*r->cache));
    }
    r->cache_count = 0u;
    r->stats.invalidations++;
}

void robo_font_fallback_resolver_cache_stats(const robo_font_fallback_resolver_t *r,
                                             robo_font_fallback_cache_stats_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!r || r->cookie != RESOLVER_COOKIE) {
        return;
    }
    *out = r->stats;
    out->entries = r->cache_count;
}

/* ---- scoring / resolve ---- */

static const char *reg_family(const robo_font_registration_t *reg)
{
    return reg && reg->family[0] ? reg->family : "";
}

static uint32_t preferred_rank(const char *family,
                               const char *const *list_a,
                               uint32_t na,
                               const char *const *list_b,
                               uint32_t nb)
{
    uint32_t i;
    uint32_t none = 0xFFFFFFFFu;
    if (!family || !family[0]) {
        return none;
    }
    for (i = 0u; i < na; i++) {
        if (family_eq(family, list_a[i])) {
            return i;
        }
    }
    for (i = 0u; i < nb; i++) {
        if (family_eq(family, list_b[i])) {
            return na + i;
        }
    }
    return none;
}

static uint32_t language_rank(const char *family,
                              const char *language,
                              const robo_font_fallback_policy_t *pol)
{
    uint32_t i;
    if (!language || !language[0] || !pol) {
        return 0u; /* neutral */
    }
    if (pol->language_preferred_family_count == 0u) {
        return 1u; /* language asked but no configured hints */
    }
    for (i = 0u; i < pol->language_preferred_family_count; i++) {
        if (family_eq(family, pol->language_preferred_families[i])) {
            return 0u; /* best */
        }
    }
    return 2u;
}

static robo_unicode_script_t derive_script(uint32_t cp)
{
    robo_unicode_script_t sc = robo_unicode_script((robo_uchar32_t)cp);
    if (sc == ROBO_U_SCRIPT_COMMON || sc == ROBO_U_SCRIPT_INHERITED ||
        sc == ROBO_U_SCRIPT_UNKNOWN) {
        /* Prefer a concrete SCX entry when present. */
        robo_unicode_script_t scx[8];
        size_t n = robo_unicode_script_extensions((robo_uchar32_t)cp, scx, 8u);
        size_t i;
        for (i = 0u; i < n && i < 8u; i++) {
            if (scx[i] != ROBO_U_SCRIPT_COMMON && scx[i] != ROBO_U_SCRIPT_INHERITED &&
                scx[i] != ROBO_U_SCRIPT_UNKNOWN) {
                return scx[i];
            }
        }
        return ROBO_U_SCRIPT_UNKNOWN;
    }
    return sc;
}

static robo_result_t score_candidate(robo_font_fallback_resolver_t *r,
                                     const robo_font_fallback_request_t *req,
                                     const robo_font_fallback_policy_t *pol,
                                     robo_font_face_t *face,
                                     const robo_font_registration_t *reg,
                                     robo_font_fallback_stage_t stage,
                                     robo_unicode_script_t want_script,
                                     robo_font_fallback_score_t *out)
{
    int covers_all = 0;
    int covers_base = 0;
    uint32_t missing = 0u;
    uint32_t flags;
    const robo_font_classification_t *cls;
    robo_font_style_desc_t style;
    robo_result_t res;
    const char *family;

    (void)r;
    memset(out, 0, sizeof(*out));
    out->stage = stage;
    out->stage_rank = (uint32_t)stage;
    out->face_id = robo_font_face_id(face);
    out->preferred_family_rank = 0xFFFFFFFFu;

    flags = pol->flags | req->flags;
    if ((flags & ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL) == 0u) {
        flags |= ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL;
    }

    res = robo_font_fallback_face_covers(face, req->codepoints, req->codepoint_count,
                                         flags, &covers_all, &covers_base, &missing);
    if (res != ROBO_OK) {
        return res;
    }
    out->covers_all = (uint8_t)covers_all;
    out->covers_base = (uint8_t)covers_base;
    if (!covers_all) {
        out->eligible = 0;
        return ROBO_OK;
    }
    out->eligible = 1;

    family = reg_family(reg);
    out->preferred_family_rank =
        preferred_rank(family, req->preferred_families, req->preferred_family_count,
                       pol->preferred_families, pol->preferred_family_count);
    out->language_rank = language_rank(family, req->language, pol);

    {
        const robo_font_coverage_t *cov = robo_font_face_coverage(face);
        if (want_script != ROBO_U_SCRIPT_UNKNOWN && cov &&
            robo_font_coverage_has_script(cov, want_script)) {
            out->script_rank = 0u;
        } else if (want_script == ROBO_U_SCRIPT_UNKNOWN) {
            out->script_rank = 0u;
        } else {
            out->script_rank = 1u;
        }
    }

    res = robo_font_face_style(face, &style);
    if (res != ROBO_OK) {
        style.weight = ROBO_FONT_WEIGHT_NORMAL;
        style.stretch = ROBO_FONT_STRETCH_NORMAL;
        style.style = ROBO_FONT_STYLE_NORMAL;
    }
    cls = robo_font_face_classification(face);
    out->wws_flag = cls ? cls->wws_flag : 0u;
    out->stretch_dist = robo_font_select_stretch_dist(req->stretch, style.stretch);
    out->weight_dist = robo_font_select_weight_dist(req->weight, style.weight);
    out->style_dist = robo_font_select_style_dist(
        req->style, style.style, req->has_oblique_angle, req->oblique_angle_millidegrees,
        cls ? cls->has_italic_angle : 0u,
        cls ? cls->italic_angle_millidegrees : 0);
    return ROBO_OK;
}

static int cache_key_eq(const fallback_cache_entry_t *e,
                        uint64_t gen,
                        const robo_font_fallback_request_t *req,
                        uint32_t cp_hash,
                        uint32_t lang_hash,
                        uint32_t pol_hash,
                        robo_unicode_script_t script)
{
    uint32_t i;
    if (e->registry_generation != gen || e->primary_id != robo_font_face_id(req->primary) ||
        e->cp_hash != cp_hash || e->codepoint_count != req->codepoint_count ||
        e->weight != req->weight || e->stretch != req->stretch || e->style != req->style ||
        e->lang_hash != lang_hash || e->script != script || e->has_script != req->has_script ||
        e->flags != (req->flags | 0u) || e->policy_hash != pol_hash) {
        return 0;
    }
    for (i = 0u; i < req->codepoint_count; i++) {
        if (e->cps[i] != req->codepoints[i]) {
            return 0;
        }
    }
    return 1;
}

static fallback_cache_entry_t *cache_lookup(robo_font_fallback_resolver_t *r,
                                            uint64_t gen,
                                            const robo_font_fallback_request_t *req,
                                            uint32_t cp_hash,
                                            uint32_t lang_hash,
                                            uint32_t pol_hash,
                                            robo_unicode_script_t script)
{
    uint32_t i;
    if (!r->cache || r->cache_count == 0u) {
        return NULL;
    }
    for (i = 0u; i < r->cache_cap; i++) {
        if (r->cache[i].codepoint_count == 0u && r->cache[i].primary_id == 0u) {
            continue;
        }
        if (cache_key_eq(&r->cache[i], gen, req, cp_hash, lang_hash, pol_hash, script)) {
            r->cache[i].lru_tick = ++r->lru_tick;
            return &r->cache[i];
        }
    }
    return NULL;
}

static void cache_store(robo_font_fallback_resolver_t *r,
                        uint64_t gen,
                        const robo_font_fallback_request_t *req,
                        uint32_t cp_hash,
                        uint32_t lang_hash,
                        uint32_t pol_hash,
                        robo_unicode_script_t script,
                        const robo_font_fallback_result_t *res)
{
    fallback_cache_entry_t *slot = NULL;
    uint32_t i;
    uint64_t oldest = ~(uint64_t)0;
    uint32_t oldest_i = 0u;

    if (!r->cache || r->cache_cap == 0u) {
        return;
    }
    for (i = 0u; i < r->cache_cap; i++) {
        if (r->cache[i].primary_id == 0u && r->cache[i].codepoint_count == 0u) {
            slot = &r->cache[i];
            break;
        }
        if (r->cache[i].lru_tick < oldest) {
            oldest = r->cache[i].lru_tick;
            oldest_i = i;
        }
    }
    if (!slot) {
        slot = &r->cache[oldest_i];
    } else if (r->cache_count < r->cache_cap) {
        r->cache_count++;
    }

    memset(slot, 0, sizeof(*slot));
    slot->registry_generation = gen;
    slot->primary_id = robo_font_face_id(req->primary);
    slot->cp_hash = cp_hash;
    slot->codepoint_count = req->codepoint_count;
    memcpy(slot->cps, req->codepoints, (size_t)req->codepoint_count * sizeof(uint32_t));
    slot->weight = req->weight;
    slot->stretch = req->stretch;
    slot->style = req->style;
    slot->lang_hash = lang_hash;
    slot->script = script;
    slot->has_script = req->has_script;
    slot->flags = req->flags;
    slot->policy_hash = pol_hash;
    slot->used_primary = res->used_primary;
    slot->found = res->found;
    slot->stage = res->stage;
    slot->face_id = res->face_id;
    slot->score = res->score;
    slot->primary_missing_count = res->primary_missing_count;
    slot->lru_tick = ++r->lru_tick;
    r->stats.inserts++;
}

static int is_last_resort_candidate(const robo_font_fallback_policy_t *pol,
                                    const robo_font_registration_t *reg,
                                    robo_font_face_t *face)
{
    if (!pol) {
        return 0;
    }
    if (pol->last_resort_face_id != 0u &&
        robo_font_face_id(face) == pol->last_resort_face_id) {
        return 1;
    }
    if (pol->last_resort_family && pol->last_resort_family[0] &&
        family_eq(reg_family(reg), pol->last_resort_family)) {
        return 1;
    }
    return 0;
}

static int is_preferred_candidate(const robo_font_fallback_request_t *req,
                                  const robo_font_fallback_policy_t *pol,
                                  const robo_font_registration_t *reg)
{
    return preferred_rank(reg_family(reg), req->preferred_families,
                          req->preferred_family_count, pol->preferred_families,
                          pol->preferred_family_count) != 0xFFFFFFFFu;
}

robo_result_t robo_font_fallback_resolve(robo_font_fallback_resolver_t *r,
                                         const robo_font_fallback_request_t *req,
                                         robo_font_fallback_result_t *out)
{
    const robo_font_fallback_policy_t *pol;
    uint64_t gen;
    uint32_t cp_hash;
    uint32_t lang_hash;
    uint32_t pol_hash;
    robo_unicode_script_t want_script;
    int covers_all = 0;
    int covers_base = 0;
    uint32_t missing = 0u;
    uint32_t flags;
    fallback_cache_entry_t *hit;
    robo_font_fallback_score_t best_score;
    robo_font_face_t *best_face = NULL;
    robo_font_fallback_stage_t best_stage = ROBO_FONT_FALLBACK_STAGE_NONE;
    uint32_t i;
    uint32_t scanned = 0u;
    uint32_t max_scan;
    robo_result_t res;
    int use_cache;

    if (!r || r->cookie != RESOLVER_COOKIE || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_fallback_request_validate(req);
    if (res != ROBO_OK) {
        return res;
    }

    if (out->cookie == ROBO_FONT_FALLBACK_RESULT_COOKIE) {
        robo_font_fallback_result_clear(out);
    } else {
        robo_font_fallback_result_init(out);
    }

    pol = req->policy ? req->policy : &r->policy;
    flags = pol->flags | req->flags;
    if ((flags & ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL) == 0u) {
        flags |= ROBO_FONT_FALLBACK_FLAG_REQUIRE_ALL;
    }
    use_cache = (flags & ROBO_FONT_FALLBACK_FLAG_NO_CACHE) == 0 && r->cache_cap > 0u;
    max_scan = pol->max_scan ? pol->max_scan : ROBO_FONT_FALLBACK_MAX_SCAN;
    if (max_scan > ROBO_FONT_FALLBACK_MAX_SCAN) {
        max_scan = ROBO_FONT_FALLBACK_MAX_SCAN;
    }

    gen = robo_font_registry_generation(r->registry);
    cp_hash = hash_bytes(req->codepoints, (size_t)req->codepoint_count * sizeof(uint32_t));
    lang_hash = hash_cstr(req->language, req->language_len);
    pol_hash = policy_hash(pol);
    want_script = req->has_script ? req->script : derive_script(req->codepoints[0]);

    if (use_cache) {
        hit = cache_lookup(r, gen, req, cp_hash, lang_hash, pol_hash, want_script);
        if (hit) {
            const robo_font_registration_t *reg = NULL;
            r->stats.hits++;
            out->from_cache = 1;
            out->used_primary = hit->used_primary;
            out->found = hit->found;
            out->stage = hit->stage;
            out->face_id = hit->face_id;
            out->score = hit->score;
            out->primary_missing_count = hit->primary_missing_count;
            out->registry_generation = gen;
            if (hit->found && hit->face_id != 0u) {
                if (robo_font_registry_find_by_face_id(r->registry, hit->face_id, &reg) ==
                        ROBO_OK &&
                    reg && reg->face) {
                    robo_font_face_retain(reg->face);
                    out->face = reg->face;
                    return ROBO_OK;
                }
                /* Stale face — fall through to recompute. */
                robo_font_fallback_resolver_cache_clear(r);
            } else {
                return ROBO_OK;
            }
        } else {
            r->stats.misses++;
        }
    }

    /* Fast path: primary coverage. */
    res = robo_font_fallback_face_covers(req->primary, req->codepoints,
                                         req->codepoint_count, flags, &covers_all,
                                         &covers_base, &missing);
    if (res != ROBO_OK) {
        return res;
    }
    out->primary_missing_count = missing;
    if (covers_all) {
        out->used_primary = 1;
        out->found = 1;
        out->stage = ROBO_FONT_FALLBACK_STAGE_PRIMARY;
        out->face_id = robo_font_face_id(req->primary);
        robo_font_face_retain(req->primary);
        out->face = req->primary;
        out->score.eligible = 1;
        out->score.covers_all = 1;
        out->score.covers_base = 1;
        out->score.stage = ROBO_FONT_FALLBACK_STAGE_PRIMARY;
        out->score.stage_rank = 0u;
        out->score.face_id = out->face_id;
        out->registry_generation = gen;
        if (use_cache) {
            cache_store(r, gen, req, cp_hash, lang_hash, pol_hash, want_script, out);
        }
        return ROBO_OK;
    }

    memset(&best_score, 0, sizeof(best_score));
    best_score.eligible = 0;

    {
        uint32_t n = robo_font_registry_registration_count(r->registry);
        for (i = 0u; i < n && scanned < max_scan; i++) {
            const robo_font_registration_t *reg = NULL;
            robo_font_face_t *face;
            robo_font_fallback_score_t score;
            robo_font_fallback_stage_t stage;

            if (robo_font_registry_registration_at(r->registry, i, &reg) != ROBO_OK ||
                !reg || !reg->face) {
                continue;
            }
            face = reg->face;
            if (robo_font_face_id(face) == robo_font_face_id(req->primary)) {
                continue;
            }
            scanned++;

            if (is_last_resort_candidate(pol, reg, face)) {
                stage = ROBO_FONT_FALLBACK_STAGE_LAST_RESORT;
            } else if (is_preferred_candidate(req, pol, reg)) {
                stage = ROBO_FONT_FALLBACK_STAGE_PREFERRED;
            } else {
                /* Script vs general decided after coverage/script check. */
                stage = ROBO_FONT_FALLBACK_STAGE_GENERAL;
            }

            res = score_candidate(r, req, pol, face, reg, stage, want_script, &score);
            if (res != ROBO_OK) {
                continue; /* skip broken candidates */
            }
            if (!score.eligible) {
                continue;
            }

            if (stage == ROBO_FONT_FALLBACK_STAGE_GENERAL &&
                want_script != ROBO_U_SCRIPT_UNKNOWN && score.script_rank == 0u) {
                score.stage = ROBO_FONT_FALLBACK_STAGE_SCRIPT;
                score.stage_rank = (uint32_t)ROBO_FONT_FALLBACK_STAGE_SCRIPT;
            }

            if (!best_score.eligible ||
                robo_font_fallback_score_compare(&score, &best_score) < 0) {
                best_score = score;
                best_face = face;
                best_stage = score.stage;
            }
        }
    }

    if (best_face && best_score.eligible) {
        out->found = 1;
        out->used_primary = 0;
        out->stage = best_stage;
        out->face_id = robo_font_face_id(best_face);
        out->score = best_score;
        robo_font_face_retain(best_face);
        out->face = best_face;
    } else {
        out->found = 0;
        out->used_primary = 0;
        out->stage = ROBO_FONT_FALLBACK_STAGE_NONE;
    }
    out->registry_generation = gen;
    if (use_cache) {
        cache_store(r, gen, req, cp_hash, lang_hash, pol_hash, want_script, out);
    }
    return ROBO_OK;
}

robo_result_t robo_font_fallback_resolve_codepoint(
    robo_font_fallback_resolver_t *r,
    robo_font_face_t *primary,
    uint32_t code_point,
    const robo_font_fallback_request_t *style_hints,
    robo_font_fallback_result_t *out)
{
    robo_font_fallback_request_t req;
    uint32_t cp = code_point;

    robo_font_fallback_request_init(&req);
    if (style_hints) {
        req = *style_hints;
    }
    req.primary = primary;
    req.codepoints = &cp;
    req.codepoint_count = 1u;
    return robo_font_fallback_resolve(r, &req, out);
}

robo_result_t robo_font_fallback_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_registry_t *reg = NULL;
    robo_font_fallback_resolver_t *resolver = NULL;
    robo_font_fallback_policy_t pol;
    robo_font_fallback_request_t req;
    robo_font_fallback_result_t res;
    uint32_t cps[4];
    robo_result_t rr;

    if (!data && size > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    rr = robo_font_registry_create(&reg);
    if (rr != ROBO_OK) {
        return rr == ROBO_OUT_OF_MEMORY ? rr : ROBO_OK;
    }
    robo_font_fallback_policy_init(&pol);
    pol.cache_capacity = 8u;
    pol.max_scan = 16u;
    if (size > 0u && data) {
        pol.flags = data[0] & 0x3u;
    }
    rr = robo_font_fallback_resolver_create(reg, &pol, &resolver);
    if (rr != ROBO_OK) {
        robo_font_registry_destroy(reg);
        return rr == ROBO_OUT_OF_MEMORY ? rr : ROBO_OK;
    }
    robo_font_fallback_request_init(&req);
    cps[0] = size > 1u ? (uint32_t)data[1] : 0x41u;
    cps[1] = size > 2u ? (uint32_t)data[2] : 0x42u;
    if (cps[0] > 0x10FFFFu) {
        cps[0] = 0x41u;
    }
    if (cps[0] >= 0xD800u && cps[0] <= 0xDFFFu) {
        cps[0] = 0x41u;
    }
    req.codepoints = cps;
    req.codepoint_count = 1u;
    /* No primary → validate fails safely. */
    (void)robo_font_fallback_resolve(resolver, &req, &res);
    robo_font_fallback_result_clear(&res);
    robo_font_fallback_resolver_cache_clear(resolver);
    robo_font_fallback_resolver_destroy(resolver);
    robo_font_registry_destroy(reg);
    return ROBO_OK;
}
