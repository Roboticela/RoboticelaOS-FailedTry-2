/*
 * RoboI18N application message catalogs (Goal 4.11).
 */

#include "RoboI18N/RoboI18NMessage.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboMsgData.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define ROBO_MSG_MAGIC 0x47534D52u
#define ROBO_MSG_HEADER_SIZE 32u
#define ROBO_MSG_ENTRY_SIZE 40u

struct robo_i18n_catalog {
    int owned; /* 1 = free blob on close */
    uint32_t format_version;
    const char *catalog_id;
    const char *catalog_version;
    /* Demo tables */
    int is_demo;
    /* Loaded blob (owned when owned==1) */
    uint8_t *blob;
    size_t blob_len;
    const uint8_t *pool;
    uint32_t pool_size;
    uint32_t entry_count;
    /* For demo: use robo_msg_entries. For blob: offsets into pool. */
};

static robo_i18n_catalog_t g_demo_catalog;
static int g_demo_ready;

void robo_i18n_msg_options_init(robo_i18n_msg_options_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->missing = ROBO_I18N_MSG_MISS_NOT_FOUND;
    out->var_missing = ROBO_I18N_MSG_VAR_MISS_ERROR;
}

static const robo_i18n_msg_options_t *opts_or_default(
    const robo_i18n_msg_options_t *options,
    robo_i18n_msg_options_t *scratch)
{
    if (options) {
        return options;
    }
    robo_i18n_msg_options_init(scratch);
    return scratch;
}

static robo_result_t write_out(const char *s,
                               size_t n,
                               char *out,
                               size_t out_cap,
                               size_t *out_len)
{
    if (!out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_len = n;
    if (!out) {
        return ROBO_OK;
    }
    if (out_cap < n + 1u) {
        *out_len = n + 1u;
        return ROBO_ERROR;
    }
    if (n) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return ROBO_OK;
}

static int utf8_ok(const uint8_t *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if (c < 0x80u) {
            i++;
        } else if ((c >> 5) == 0x6u && i + 1 < n && (p[i + 1] & 0xC0u) == 0x80u) {
            i += 2;
        } else if ((c >> 4) == 0xEu && i + 2 < n && (p[i + 1] & 0xC0u) == 0x80u &&
                   (p[i + 2] & 0xC0u) == 0x80u) {
            i += 3;
        } else if ((c >> 3) == 0x1Eu && i + 3 < n && (p[i + 1] & 0xC0u) == 0x80u &&
                   (p[i + 2] & 0xC0u) == 0x80u && (p[i + 3] & 0xC0u) == 0x80u) {
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static const char *blob_str(const robo_i18n_catalog_t *cat, int32_t off)
{
    size_t o;
    if (!cat || !cat->pool || off < 0) {
        return NULL;
    }
    o = (size_t)(uint32_t)off;
    if (o >= cat->pool_size) {
        return NULL;
    }
    return (const char *)(cat->pool + o);
}

static int cmp_bytes(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    int c = n ? memcmp(a, b, n) : 0;
    if (c) {
        return c;
    }
    if (alen < blen) {
        return -1;
    }
    if (alen > blen) {
        return 1;
    }
    return 0;
}

static int entry_cmp_key(const char *loc,
                         const char *id,
                         const char *ctx,
                         const char *qloc,
                         size_t qloc_len,
                         const char *qid,
                         size_t qid_len,
                         const char *qctx,
                         size_t qctx_len)
{
    int c;
    if (!loc) {
        loc = "";
    }
    if (!id) {
        id = "";
    }
    if (!ctx) {
        ctx = "";
    }
    c = cmp_bytes(loc, strlen(loc), qloc, qloc_len);
    if (c) {
        return c;
    }
    c = cmp_bytes(id, strlen(id), qid, qid_len);
    if (c) {
        return c;
    }
    return cmp_bytes(ctx, strlen(ctx), qctx, qctx_len);
}

static int find_entry_demo(const char *loc,
                           size_t loc_len,
                           const char *id,
                           size_t id_len,
                           const char *ctx,
                           size_t ctx_len,
                           uint32_t *out_idx)
{
    uint32_t lo = 0;
    uint32_t hi = robo_msg_entry_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const robo_msg_entry_t *e = &robo_msg_entries[mid];
        const char *el = robo_msg_string(e->locale);
        const char *ei = robo_msg_string(e->id);
        const char *ec = e->context >= 0 ? robo_msg_string(e->context) : "";
        int cmp = entry_cmp_key(el, ei, ec, loc, loc_len, id, id_len, ctx, ctx_len);
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < robo_msg_entry_count) {
        const robo_msg_entry_t *e = &robo_msg_entries[lo];
        const char *el = robo_msg_string(e->locale);
        const char *ei = robo_msg_string(e->id);
        const char *ec = e->context >= 0 ? robo_msg_string(e->context) : "";
        if (entry_cmp_key(el, ei, ec, loc, loc_len, id, id_len, ctx, ctx_len) == 0) {
            *out_idx = lo;
            return 1;
        }
    }
    return 0;
}

static int find_entry_blob(const robo_i18n_catalog_t *cat,
                           const char *loc,
                           size_t loc_len,
                           const char *id,
                           size_t id_len,
                           const char *ctx,
                           size_t ctx_len,
                           uint32_t *out_idx)
{
    const uint8_t *base;
    uint32_t lo = 0;
    uint32_t hi;
    if (!cat || !cat->blob || cat->entry_count == 0) {
        return 0;
    }
    base = cat->blob + ROBO_MSG_HEADER_SIZE;
    hi = cat->entry_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const int32_t *raw = (const int32_t *)(base + mid * ROBO_MSG_ENTRY_SIZE);
        const char *el = blob_str(cat, raw[0]);
        const char *ei = blob_str(cat, raw[1]);
        const char *ec = raw[2] < 0 ? "" : blob_str(cat, raw[2]);
        int cmp = entry_cmp_key(el, ei, ec, loc, loc_len, id, id_len, ctx, ctx_len);
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < cat->entry_count) {
        const int32_t *raw = (const int32_t *)(base + lo * ROBO_MSG_ENTRY_SIZE);
        const char *el = blob_str(cat, raw[0]);
        const char *ei = blob_str(cat, raw[1]);
        const char *ec = raw[2] < 0 ? "" : blob_str(cat, raw[2]);
        if (entry_cmp_key(el, ei, ec, loc, loc_len, id, id_len, ctx, ctx_len) == 0) {
            *out_idx = lo;
            return 1;
        }
    }
    return 0;
}

static const char *select_text_demo(uint32_t idx,
                                    const robo_i18n_locale_t *locale,
                                    const robo_i18n_decimal_t *plural_value)
{
    const robo_msg_entry_t *e = &robo_msg_entries[idx];
    if (e->text >= 0) {
        return robo_msg_string(e->text);
    }
    if (plural_value) {
        robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
        if (robo_i18n_plural_cardinal(locale, plural_value, &cat) == ROBO_OK) {
            if (cat <= ROBO_I18N_PLURAL_OTHER && e->plural[cat] >= 0) {
                return robo_msg_string(e->plural[cat]);
            }
        }
    }
    if (e->plural[ROBO_I18N_PLURAL_OTHER] >= 0) {
        return robo_msg_string(e->plural[ROBO_I18N_PLURAL_OTHER]);
    }
    return NULL;
}

static const char *select_text_blob(const robo_i18n_catalog_t *cat,
                                    uint32_t idx,
                                    const robo_i18n_locale_t *locale,
                                    const robo_i18n_decimal_t *plural_value)
{
    const uint8_t *base = cat->blob + ROBO_MSG_HEADER_SIZE;
    const int32_t *raw = (const int32_t *)(base + idx * ROBO_MSG_ENTRY_SIZE);
    if (raw[3] >= 0) {
        return blob_str(cat, raw[3]);
    }
    if (plural_value) {
        robo_i18n_plural_category_t pc = ROBO_I18N_PLURAL_OTHER;
        if (robo_i18n_plural_cardinal(locale, plural_value, &pc) == ROBO_OK) {
            if (pc <= ROBO_I18N_PLURAL_OTHER && raw[4 + (int)pc] >= 0) {
                return blob_str(cat, raw[4 + (int)pc]);
            }
        }
    }
    if (raw[4 + ROBO_I18N_PLURAL_OTHER] >= 0) {
        return blob_str(cat, raw[4 + ROBO_I18N_PLURAL_OTHER]);
    }
    return NULL;
}

static int next_parent(robo_i18n_locale_t *cur)
{
    robo_i18n_locale_t parent;
    /* Prefer CLDR parentLocales / data inheritance over truncation parent. */
    if (robo_i18n_locale_data_parent(cur, &parent) != ROBO_OK) {
        return 0;
    }
    if (robo_i18n_locale_equal(cur, &parent)) {
        return 0;
    }
    *cur = parent;
    return 1;
}

static const char *resolve_message(const robo_i18n_catalog_t *catalog,
                                   const char *message_id,
                                   size_t id_len,
                                   const char *context,
                                   size_t context_len,
                                   const robo_i18n_locale_t *locale,
                                   const robo_i18n_decimal_t *plural_value,
                                   const robo_i18n_msg_options_t *opt,
                                   robo_i18n_locale_t *used_locale)
{
    robo_i18n_locale_t cur;
    char tag[ROBO_I18N_TAG_MAX];
    int guard = 0;
    uint32_t idx = 0;
    const char *ctx = context ? context : "";
    size_t ctx_len = context ? context_len : 0;

    if (!locale) {
        return NULL;
    }
    cur = *locale;
    for (;;) {
        size_t tlen = 0;
        (void)robo_i18n_locale_format(&cur, tag, sizeof(tag), &tlen);
        if (catalog->is_demo) {
            if (find_entry_demo(tag, tlen, message_id, id_len, ctx, ctx_len, &idx)) {
                if (used_locale) {
                    *used_locale = cur;
                }
                return select_text_demo(idx, &cur, plural_value);
            }
        } else {
            if (find_entry_blob(catalog, tag, tlen, message_id, id_len, ctx, ctx_len,
                                &idx)) {
                if (used_locale) {
                    *used_locale = cur;
                }
                return select_text_blob(catalog, idx, &cur, plural_value);
            }
        }
        if (!next_parent(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }

    if (opt->fallback_locale[0]) {
        robo_i18n_locale_t fb;
        if (robo_i18n_locale_parse_canonical(opt->fallback_locale,
                                             strlen(opt->fallback_locale),
                                             &fb) == ROBO_OK) {
            cur = fb;
            guard = 0;
            for (;;) {
                size_t tlen = 0;
                (void)robo_i18n_locale_format(&cur, tag, sizeof(tag), &tlen);
                if (catalog->is_demo) {
                    if (find_entry_demo(tag, tlen, message_id, id_len, ctx, ctx_len,
                                        &idx)) {
                        if (used_locale) {
                            *used_locale = cur;
                        }
                        return select_text_demo(idx, &cur, plural_value);
                    }
                } else {
                    if (find_entry_blob(catalog, tag, tlen, message_id, id_len, ctx,
                                        ctx_len, &idx)) {
                        if (used_locale) {
                            *used_locale = cur;
                        }
                        return select_text_blob(catalog, idx, &cur, plural_value);
                    }
                }
                if (!next_parent(&cur)) {
                    break;
                }
                if (++guard > 16) {
                    break;
                }
            }
        }
    }
    return NULL;
}

robo_result_t robo_i18n_catalog_open_demo(robo_i18n_catalog_t **out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_demo_ready) {
        memset(&g_demo_catalog, 0, sizeof(g_demo_catalog));
        g_demo_catalog.is_demo = 1;
        g_demo_catalog.owned = 0;
        g_demo_catalog.format_version = ROBO_MSG_DEMO_FORMAT_VERSION;
        g_demo_catalog.catalog_id = robo_msg_catalog_id();
        g_demo_catalog.catalog_version = robo_msg_catalog_version();
        g_demo_catalog.entry_count = robo_msg_entry_count;
        g_demo_ready = 1;
    }
    *out = &g_demo_catalog;
    return ROBO_OK;
}

robo_result_t robo_i18n_catalog_load(const uint8_t *blob,
                                     size_t blob_len,
                                     robo_i18n_catalog_t **out)
{
    uint32_t magic;
    uint32_t format;
    uint32_t catalog_id_off;
    uint32_t catalog_ver_off;
    uint32_t gen_off;
    uint32_t locale_count;
    uint32_t entry_count;
    uint32_t pool_size;
    size_t entries_bytes;
    size_t need;
    const uint8_t *pool;
    uint32_t i;
    robo_i18n_catalog_t *cat;
    uint8_t *copy;

    if (!blob || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (blob_len < ROBO_MSG_HEADER_SIZE) {
        return ROBO_PARSE_ERROR;
    }
    memcpy(&magic, blob + 0, 4);
    memcpy(&format, blob + 4, 4);
    memcpy(&catalog_id_off, blob + 8, 4);
    memcpy(&catalog_ver_off, blob + 12, 4);
    memcpy(&gen_off, blob + 16, 4);
    memcpy(&locale_count, blob + 20, 4);
    memcpy(&entry_count, blob + 24, 4);
    memcpy(&pool_size, blob + 28, 4);
    (void)locale_count;
    (void)gen_off;
    if (magic != ROBO_MSG_MAGIC || format != ROBO_MSG_FORMAT_VERSION) {
        return ROBO_PARSE_ERROR;
    }
    if (entry_count > 1000000u || pool_size > 64u * 1024u * 1024u) {
        return ROBO_PARSE_ERROR;
    }
    entries_bytes = (size_t)entry_count * ROBO_MSG_ENTRY_SIZE;
    need = ROBO_MSG_HEADER_SIZE + entries_bytes + (size_t)pool_size;
    if (need != blob_len || need < ROBO_MSG_HEADER_SIZE) {
        return ROBO_PARSE_ERROR;
    }
    pool = blob + ROBO_MSG_HEADER_SIZE + entries_bytes;
    if (catalog_id_off >= pool_size || catalog_ver_off >= pool_size) {
        return ROBO_PARSE_ERROR;
    }
    if (!utf8_ok(pool, pool_size)) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0; i < entry_count; i++) {
        const int32_t *raw =
            (const int32_t *)(blob + ROBO_MSG_HEADER_SIZE + i * ROBO_MSG_ENTRY_SIZE);
        int k;
        for (k = 0; k < 10; k++) {
            int32_t off = raw[k];
            if (off < 0) {
                continue;
            }
            if ((uint32_t)off >= pool_size) {
                return ROBO_PARSE_ERROR;
            }
        }
    }

    copy = (uint8_t *)robo_malloc(blob_len);
    if (!copy) {
        return ROBO_OUT_OF_MEMORY;
    }
    memcpy(copy, blob, blob_len);
    cat = (robo_i18n_catalog_t *)robo_calloc(1, sizeof(*cat));
    if (!cat) {
        robo_free(copy);
        return ROBO_OUT_OF_MEMORY;
    }
    cat->owned = 1;
    cat->is_demo = 0;
    cat->format_version = format;
    cat->blob = copy;
    cat->blob_len = blob_len;
    cat->pool = copy + ROBO_MSG_HEADER_SIZE + entries_bytes;
    cat->pool_size = pool_size;
    cat->entry_count = entry_count;
    cat->catalog_id = (const char *)(cat->pool + catalog_id_off);
    cat->catalog_version = (const char *)(cat->pool + catalog_ver_off);
    *out = cat;
    return ROBO_OK;
}

void robo_i18n_catalog_close(robo_i18n_catalog_t *catalog)
{
    if (!catalog) {
        return;
    }
    if (catalog->owned) {
        robo_free(catalog->blob);
        catalog->blob = NULL;
        robo_free(catalog);
    }
}

const char *robo_i18n_catalog_id(const robo_i18n_catalog_t *catalog)
{
    return catalog ? catalog->catalog_id : NULL;
}

const char *robo_i18n_catalog_version(const robo_i18n_catalog_t *catalog)
{
    return catalog ? catalog->catalog_version : NULL;
}

uint32_t robo_i18n_catalog_format_version(const robo_i18n_catalog_t *catalog)
{
    return catalog ? catalog->format_version : 0u;
}

int robo_i18n_message_exists(const robo_i18n_catalog_t *catalog,
                             const char *message_id,
                             size_t id_len,
                             const char *context,
                             size_t context_len,
                             const robo_i18n_locale_t *locale,
                             const robo_i18n_msg_options_t *options)
{
    robo_i18n_msg_options_t scratch;
    const robo_i18n_msg_options_t *opt;
    const char *t;
    if (!catalog || !message_id || !locale) {
        return 0;
    }
    opt = opts_or_default(options, &scratch);
    t = resolve_message(catalog, message_id, id_len, context, context_len, locale, NULL,
                        opt, NULL);
    return t != NULL;
}

robo_result_t robo_i18n_message_get(const robo_i18n_catalog_t *catalog,
                                    const char *message_id,
                                    size_t id_len,
                                    const char *context,
                                    size_t context_len,
                                    const robo_i18n_locale_t *locale,
                                    const robo_i18n_decimal_t *plural_value,
                                    const robo_i18n_msg_options_t *options,
                                    char *out,
                                    size_t out_cap,
                                    size_t *out_len)
{
    robo_i18n_msg_options_t scratch;
    const robo_i18n_msg_options_t *opt;
    const char *t;
    robo_i18n_locale_t used;

    if (!catalog || !message_id || !locale || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (id_len == 0 || id_len > ROBO_I18N_MSG_ID_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    t = resolve_message(catalog, message_id, id_len, context, context_len, locale,
                        plural_value, opt, &used);
    if (t) {
        return write_out(t, strlen(t), out, out_cap, out_len);
    }
    if (opt->missing == ROBO_I18N_MSG_MISS_RETURN_ID) {
        return write_out(message_id, id_len, out, out_cap, out_len);
    }
    if (out_len) {
        *out_len = 0;
    }
    if (out && out_cap > 0) {
        out[0] = '\0';
    }
    return ROBO_NOT_FOUND;
}

static const char *find_var(const robo_i18n_msg_var_t *vars,
                            size_t var_count,
                            const char *name,
                            size_t name_len,
                            size_t *out_vlen)
{
    size_t i;
    for (i = 0; i < var_count; i++) {
        size_t n = strlen(vars[i].name);
        if (n == name_len && memcmp(vars[i].name, name, name_len) == 0) {
            if (!vars[i].value) {
                *out_vlen = 0;
                return "";
            }
            *out_vlen = vars[i].value_len ? vars[i].value_len : strlen(vars[i].value);
            return vars[i].value;
        }
    }
    return NULL;
}

robo_result_t robo_i18n_message_format(const char *template_utf8,
                                       size_t template_len,
                                       const robo_i18n_msg_var_t *vars,
                                       size_t var_count,
                                       const robo_i18n_msg_options_t *options,
                                       char *out,
                                       size_t out_cap,
                                       size_t *out_len)
{
    robo_i18n_msg_options_t scratch;
    const robo_i18n_msg_options_t *opt;
    size_t i = 0;
    size_t oi = 0;
    char tmp[2048];

    if (!template_utf8 || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (var_count > ROBO_I18N_MSG_VARS_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);

    while (i < template_len) {
        char c = template_utf8[i];
        if (c == '{' && i + 1 < template_len && template_utf8[i + 1] == '{') {
            if (oi + 1 > sizeof(tmp)) {
                return ROBO_ERROR;
            }
            tmp[oi++] = '{';
            i += 2;
            continue;
        }
        if (c == '}' && i + 1 < template_len && template_utf8[i + 1] == '}') {
            if (oi + 1 > sizeof(tmp)) {
                return ROBO_ERROR;
            }
            tmp[oi++] = '}';
            i += 2;
            continue;
        }
        if (c == '{') {
            size_t j = i + 1;
            size_t vlen = 0;
            const char *val;
            while (j < template_len && template_utf8[j] != '}') {
                j++;
            }
            if (j >= template_len) {
                return ROBO_PARSE_ERROR;
            }
            val = find_var(vars, var_count, template_utf8 + i + 1, j - (i + 1), &vlen);
            if (!val) {
                if (opt->var_missing == ROBO_I18N_MSG_VAR_MISS_LITERAL) {
                    size_t chunk = j - i + 1u;
                    if (oi + chunk > sizeof(tmp)) {
                        return ROBO_ERROR;
                    }
                    memcpy(tmp + oi, template_utf8 + i, chunk);
                    oi += chunk;
                    i = j + 1;
                    continue;
                }
                return ROBO_NOT_FOUND;
            }
            if (oi + vlen > sizeof(tmp)) {
                return ROBO_ERROR;
            }
            if (vlen) {
                memcpy(tmp + oi, val, vlen);
            }
            oi += vlen;
            i = j + 1;
            continue;
        }
        if (c == '}') {
            return ROBO_PARSE_ERROR;
        }
        if (oi + 1 > sizeof(tmp)) {
            return ROBO_ERROR;
        }
        tmp[oi++] = c;
        i++;
    }
    return write_out(tmp, oi, out, out_cap, out_len);
}

robo_result_t robo_i18n_message_format_id(const robo_i18n_catalog_t *catalog,
                                          const char *message_id,
                                          size_t id_len,
                                          const char *context,
                                          size_t context_len,
                                          const robo_i18n_locale_t *locale,
                                          const robo_i18n_decimal_t *plural_value,
                                          const robo_i18n_msg_var_t *vars,
                                          size_t var_count,
                                          const robo_i18n_msg_options_t *options,
                                          char *out,
                                          size_t out_cap,
                                          size_t *out_len)
{
    char tmpl[1024];
    size_t n = 0;
    robo_result_t r;

    r = robo_i18n_message_get(catalog, message_id, id_len, context, context_len, locale,
                              plural_value, options, tmpl, sizeof(tmpl), &n);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_message_format(tmpl, n, vars, var_count, options, out, out_cap,
                                    out_len);
}

uint32_t robo_i18n_catalog_entry_count(const robo_i18n_catalog_t *catalog)
{
    return catalog ? catalog->entry_count : 0u;
}

robo_result_t robo_i18n_catalog_entry_at(const robo_i18n_catalog_t *catalog,
                                         uint32_t index,
                                         const char **out_locale,
                                         const char **out_id,
                                         const char **out_context,
                                         int *out_is_plural)
{
    if (!catalog || index >= catalog->entry_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (catalog->is_demo) {
        const robo_msg_entry_t *e = &robo_msg_entries[index];
        if (out_locale) {
            *out_locale = robo_msg_string(e->locale);
        }
        if (out_id) {
            *out_id = robo_msg_string(e->id);
        }
        if (out_context) {
            *out_context = (e->context >= 0) ? robo_msg_string(e->context) : "";
        }
        if (out_is_plural) {
            *out_is_plural = (e->text < 0) ? 1 : 0;
        }
        return ROBO_OK;
    }
    {
        const uint8_t *base = catalog->blob + ROBO_MSG_HEADER_SIZE;
        const int32_t *raw = (const int32_t *)(base + index * ROBO_MSG_ENTRY_SIZE);
        if (out_locale) {
            *out_locale = blob_str(catalog, raw[0]);
        }
        if (out_id) {
            *out_id = blob_str(catalog, raw[1]);
        }
        if (out_context) {
            *out_context = (raw[2] >= 0) ? blob_str(catalog, raw[2]) : "";
        }
        if (out_is_plural) {
            *out_is_plural = (raw[3] < 0) ? 1 : 0;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_catalog_entry_text(const robo_i18n_catalog_t *catalog,
                                           uint32_t index,
                                           int plural_slot,
                                           char *out,
                                           size_t out_cap,
                                           size_t *out_len)
{
    const char *t = NULL;

    if (!catalog || index >= catalog->entry_count || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (catalog->is_demo) {
        const robo_msg_entry_t *e = &robo_msg_entries[index];
        if (plural_slot < 0) {
            if (e->text < 0) {
                return ROBO_NOT_FOUND;
            }
            t = robo_msg_string(e->text);
        } else if (plural_slot <= (int)ROBO_I18N_PLURAL_OTHER) {
            if (e->plural[plural_slot] < 0) {
                return ROBO_NOT_FOUND;
            }
            t = robo_msg_string(e->plural[plural_slot]);
        } else {
            return ROBO_INVALID_ARGUMENT;
        }
    } else {
        const uint8_t *base = catalog->blob + ROBO_MSG_HEADER_SIZE;
        const int32_t *raw = (const int32_t *)(base + index * ROBO_MSG_ENTRY_SIZE);
        if (plural_slot < 0) {
            if (raw[3] < 0) {
                return ROBO_NOT_FOUND;
            }
            t = blob_str(catalog, raw[3]);
        } else if (plural_slot <= (int)ROBO_I18N_PLURAL_OTHER) {
            if (raw[4 + plural_slot] < 0) {
                return ROBO_NOT_FOUND;
            }
            t = blob_str(catalog, raw[4 + plural_slot]);
        } else {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    if (!t) {
        return ROBO_NOT_FOUND;
    }
    return write_out(t, strlen(t), out, out_cap, out_len);
}
