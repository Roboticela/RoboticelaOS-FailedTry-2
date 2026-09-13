/*
 * RoboI18N message-format validation & catalog compatibility (Goal 4.13).
 */

#include "RoboI18N/RoboI18NMessageValidate.h"

#include <string.h>

void robo_i18n_mf_limits_init(robo_i18n_mf_limits_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->template_max = ROBO_I18N_MF_TEMPLATE_MAX;
    out->nesting_max = ROBO_I18N_MF_NESTING_MAX;
    out->branches_max = ROBO_I18N_MF_BRANCHES_MAX;
    out->args_max = ROBO_I18N_MF_ARGS_MAX;
    out->output_max = ROBO_I18N_MF_OUTPUT_MAX;
    out->diagnostics_max = ROBO_I18N_MF_DIAGNOSTICS_MAX;
}

void robo_i18n_mf_diagnostic_reset(robo_i18n_mf_diagnostic_t *d)
{
    if (!d) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->result = ROBO_OK;
    d->code = ROBO_I18N_MF_DIAG_OK;
    d->byte_offset = (size_t)-1;
}

void robo_i18n_mf_contract_reset(robo_i18n_mf_contract_t *c)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

void robo_i18n_mf_catalog_report_reset(robo_i18n_mf_catalog_report_t *r)
{
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
}

uint32_t robo_i18n_mf_syntax_version(void)
{
    return ROBO_I18N_MF_SYNTAX_VERSION;
}

const char *robo_i18n_mf_diag_code_string(robo_i18n_mf_diag_code_t code)
{
    switch (code) {
    case ROBO_I18N_MF_DIAG_OK:
        return "ok";
    case ROBO_I18N_MF_DIAG_MALFORMED:
        return "malformed";
    case ROBO_I18N_MF_DIAG_INVALID_UTF8:
        return "invalid_utf8";
    case ROBO_I18N_MF_DIAG_UNKNOWN_TYPE:
        return "unknown_type";
    case ROBO_I18N_MF_DIAG_MISSING_OTHER:
        return "missing_other";
    case ROBO_I18N_MF_DIAG_DUPLICATE_BRANCH:
        return "duplicate_branch";
    case ROBO_I18N_MF_DIAG_TYPE_CONFLICT:
        return "type_conflict";
    case ROBO_I18N_MF_DIAG_NESTING:
        return "nesting";
    case ROBO_I18N_MF_DIAG_TOO_LARGE:
        return "too_large";
    case ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION:
        return "unsupported_version";
    case ROBO_I18N_MF_DIAG_UNSUPPORTED_OPTION:
        return "unsupported_option";
    case ROBO_I18N_MF_DIAG_MISSING_MESSAGE:
        return "missing_message";
    case ROBO_I18N_MF_DIAG_INCOMPATIBLE_ARG:
        return "incompatible_arg";
    case ROBO_I18N_MF_DIAG_INVALID_SELECTOR:
        return "invalid_selector";
    case ROBO_I18N_MF_DIAG_POUND_OUTSIDE_PLURAL:
        return "pound_outside_plural";
    default:
        return "unknown";
    }
}

robo_result_t robo_i18n_mf_validate_ex(const char *utf8,
                                       size_t len,
                                       const robo_i18n_mf_limits_t *limits,
                                       robo_i18n_mf_diagnostic_t *diag)
{
    robo_i18n_mf_message_t *msg = NULL;
    robo_result_t r = robo_i18n_mf_parse_ex(utf8, len, limits, diag, &msg);
    if (r == ROBO_OK) {
        robo_i18n_mf_contract_t c;
        r = robo_i18n_mf_analyze(msg, &c);
        if (r != ROBO_OK && diag) {
            diag->result = r;
            diag->code = ROBO_I18N_MF_DIAG_TYPE_CONFLICT;
            diag->severity = ROBO_I18N_MF_SEV_ERROR;
            if (!diag->explanation[0]) {
                strncpy(diag->explanation, "incompatible argument usages",
                        sizeof(diag->explanation) - 1u);
            }
        }
        robo_i18n_mf_destroy(msg);
    }
    return r;
}

robo_result_t robo_i18n_message_validate(const char *utf8,
                                         size_t len,
                                         const robo_i18n_mf_limits_t *limits,
                                         robo_i18n_mf_diagnostic_t *diag)
{
    return robo_i18n_mf_validate_ex(utf8, len, limits, diag);
}

int robo_i18n_mf_usage_compatible(uint32_t a, uint32_t b)
{
    uint32_t a_num = a & (ROBO_I18N_MF_USAGE_NUMBER | ROBO_I18N_MF_USAGE_PLURAL |
                          ROBO_I18N_MF_USAGE_ORDINAL);
    uint32_t b_num = b & (ROBO_I18N_MF_USAGE_NUMBER | ROBO_I18N_MF_USAGE_PLURAL |
                          ROBO_I18N_MF_USAGE_ORDINAL);
    uint32_t a_str = a & (ROBO_I18N_MF_USAGE_STRING | ROBO_I18N_MF_USAGE_SELECT);
    uint32_t b_str = b & (ROBO_I18N_MF_USAGE_STRING | ROBO_I18N_MF_USAGE_SELECT);
    uint32_t a_date = a & ROBO_I18N_MF_USAGE_DATE;
    uint32_t b_date = b & ROBO_I18N_MF_USAGE_DATE;
    uint32_t a_time = a & ROBO_I18N_MF_USAGE_TIME;
    uint32_t b_time = b & ROBO_I18N_MF_USAGE_TIME;

    /* Empty combines with anything. */
    if (a == 0 || b == 0) {
        return 1;
    }
    /* Numeric family is self-compatible. */
    if (a_num && b_str) {
        return 0;
    }
    if (b_num && a_str) {
        return 0;
    }
    if ((a_num || a_str) && (b_date || b_time) && !(a_date || a_time)) {
        /* a is only num/str, b has date/time — conflict if a lacks that bit */
    }
    if (a_date && b_time && !a_time && !b_date) {
        return 0; /* pure date vs pure time */
    }
    if (b_date && a_time && !b_time && !a_date) {
        return 0;
    }
    if ((a_date || a_time) && (b_num || b_str) && !(b_date || b_time)) {
        return 0;
    }
    if ((b_date || b_time) && (a_num || a_str) && !(a_date || a_time)) {
        return 0;
    }
    return 1;
}

robo_result_t robo_i18n_mf_contracts_compatible(const robo_i18n_mf_contract_t *a,
                                                const robo_i18n_mf_contract_t *b,
                                                robo_i18n_mf_diagnostic_t *diag)
{
    uint32_t i, j;
    if (!a || !b) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0; i < a->count; i++) {
        for (j = 0; j < b->count; j++) {
            if (strcmp(a->args[i].name, b->args[j].name) == 0) {
                if (!robo_i18n_mf_usage_compatible(a->args[i].usage, b->args[j].usage)) {
                    if (diag) {
                        robo_i18n_mf_diagnostic_reset(diag);
                        diag->result = ROBO_PARSE_ERROR;
                        diag->code = ROBO_I18N_MF_DIAG_INCOMPATIBLE_ARG;
                        diag->severity = ROBO_I18N_MF_SEV_ERROR;
                        strncpy(diag->argument_name, a->args[i].name,
                                sizeof(diag->argument_name) - 1u);
                        strncpy(diag->explanation,
                                "argument usage incompatible between contracts",
                                sizeof(diag->explanation) - 1u);
                    }
                    return ROBO_PARSE_ERROR;
                }
            }
        }
    }
    if (diag) {
        robo_i18n_mf_diagnostic_reset(diag);
        diag->result = ROBO_OK;
        diag->code = ROBO_I18N_MF_DIAG_OK;
    }
    return ROBO_OK;
}

static int report_push(robo_i18n_mf_catalog_report_t *report,
                       uint32_t diag_cap,
                       const robo_i18n_mf_diagnostic_t *d)
{
    if (!report || !d) {
        return 0;
    }
    if (d->severity == ROBO_I18N_MF_SEV_ERROR) {
        report->errors++;
    } else {
        report->warnings++;
    }
    if (report->diag_count >= diag_cap ||
        report->diag_count >= ROBO_I18N_MF_DIAGNOSTICS_MAX) {
        return 0;
    }
    report->diags[report->diag_count++] = *d;
    return 1;
}

static void stamp_entry(robo_i18n_mf_diagnostic_t *d,
                        const char *locale,
                        const char *id)
{
    if (!d) {
        return;
    }
    if (locale) {
        strncpy(d->locale_tag, locale, sizeof(d->locale_tag) - 1u);
    }
    if (id) {
        strncpy(d->message_id, id, sizeof(d->message_id) - 1u);
    }
}

robo_result_t robo_i18n_mf_catalog_validate(const robo_i18n_catalog_t *catalog,
                                            const robo_i18n_mf_limits_t *limits,
                                            robo_i18n_mf_catalog_report_t *report)
{
    uint32_t n;
    uint32_t i;
    uint32_t diag_cap;
    robo_i18n_mf_limits_t lim;

    if (!catalog || !report) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_mf_catalog_report_reset(report);
    robo_i18n_mf_limits_init(&lim);
    if (limits) {
        lim = *limits;
        if (!lim.template_max) {
            lim.template_max = ROBO_I18N_MF_TEMPLATE_MAX;
        }
        if (!lim.diagnostics_max) {
            lim.diagnostics_max = ROBO_I18N_MF_DIAGNOSTICS_MAX;
        }
    }
    diag_cap = lim.diagnostics_max;
    if (diag_cap > ROBO_I18N_MF_DIAGNOSTICS_MAX) {
        diag_cap = ROBO_I18N_MF_DIAGNOSTICS_MAX;
    }

    n = robo_i18n_catalog_entry_count(catalog);
    for (i = 0; i < n; i++) {
        const char *loc = NULL;
        const char *id = NULL;
        const char *ctx = NULL;
        int is_plural = 0;
        int slot;

        if (robo_i18n_catalog_entry_at(catalog, i, &loc, &id, &ctx, &is_plural) != ROBO_OK) {
            continue;
        }
        if (is_plural) {
            for (slot = 0; slot <= (int)ROBO_I18N_PLURAL_OTHER; slot++) {
                char tmpl[ROBO_I18N_MF_TEMPLATE_MAX + 1u];
                size_t tlen = 0;
                robo_i18n_mf_diagnostic_t d;
                robo_result_t r;

                if (robo_i18n_catalog_entry_text(catalog, i, slot, tmpl, sizeof(tmpl), &tlen) !=
                    ROBO_OK) {
                    continue;
                }
                report->checked++;
                r = robo_i18n_mf_validate_ex(tmpl, tlen, &lim, &d);
                if (r != ROBO_OK) {
                    stamp_entry(&d, loc, id);
                    report_push(report, diag_cap, &d);
                }
            }
        } else {
            char tmpl[ROBO_I18N_MF_TEMPLATE_MAX + 1u];
            size_t tlen = 0;
            robo_i18n_mf_diagnostic_t d;
            robo_result_t r;

            if (robo_i18n_catalog_entry_text(catalog, i, -1, tmpl, sizeof(tmpl), &tlen) !=
                ROBO_OK) {
                continue;
            }
            report->checked++;
            r = robo_i18n_mf_validate_ex(tmpl, tlen, &lim, &d);
            if (r != ROBO_OK) {
                stamp_entry(&d, loc, id);
                report_push(report, diag_cap, &d);
            }
        }
    }
    return report->errors ? ROBO_PARSE_ERROR : ROBO_OK;
}

static int locale_eq(const char *a, const char *b, size_t blen)
{
    size_t alen;
    if (!a || !b) {
        return 0;
    }
    alen = strlen(a);
    return alen == blen && memcmp(a, b, blen) == 0;
}

robo_result_t robo_i18n_mf_catalog_validate_fallback(
    const robo_i18n_catalog_t *catalog,
    const char *reference_locale,
    size_t reference_locale_len,
    const char *locale,
    size_t locale_len,
    const robo_i18n_mf_limits_t *limits,
    robo_i18n_mf_catalog_report_t *report)
{
    uint32_t n;
    uint32_t i;
    uint32_t diag_cap;
    robo_i18n_mf_limits_t lim;

    if (!catalog || !reference_locale || !locale || !report) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_mf_catalog_report_reset(report);
    robo_i18n_mf_limits_init(&lim);
    if (limits) {
        lim = *limits;
        if (!lim.diagnostics_max) {
            lim.diagnostics_max = ROBO_I18N_MF_DIAGNOSTICS_MAX;
        }
    }
    diag_cap = lim.diagnostics_max;
    if (diag_cap > ROBO_I18N_MF_DIAGNOSTICS_MAX) {
        diag_cap = ROBO_I18N_MF_DIAGNOSTICS_MAX;
    }

    n = robo_i18n_catalog_entry_count(catalog);
    /* For each reference-locale message, find matching locale entry. */
    for (i = 0; i < n; i++) {
        const char *rloc = NULL;
        const char *rid = NULL;
        const char *rctx = NULL;
        int rplural = 0;
        uint32_t j;
        int found = 0;
        robo_i18n_mf_contract_t ref_c;
        robo_i18n_mf_contract_t loc_c;
        char ref_tmpl[ROBO_I18N_MF_TEMPLATE_MAX + 1u];
        size_t ref_len = 0;

        if (robo_i18n_catalog_entry_at(catalog, i, &rloc, &rid, &rctx, &rplural) != ROBO_OK) {
            continue;
        }
        if (!locale_eq(rloc, reference_locale, reference_locale_len)) {
            continue;
        }

        /* Build reference contract from plain text or "other" plural. */
        if (rplural) {
            if (robo_i18n_catalog_entry_text(catalog, i, (int)ROBO_I18N_PLURAL_OTHER, ref_tmpl,
                                             sizeof(ref_tmpl), &ref_len) != ROBO_OK) {
                continue;
            }
        } else {
            if (robo_i18n_catalog_entry_text(catalog, i, -1, ref_tmpl, sizeof(ref_tmpl),
                                             &ref_len) != ROBO_OK) {
                continue;
            }
        }
        report->checked++;
        if (robo_i18n_mf_analyze_template(ref_tmpl, ref_len, &lim, NULL, &ref_c) != ROBO_OK) {
            continue;
        }

        for (j = 0; j < n; j++) {
            const char *lloc = NULL;
            const char *lid = NULL;
            const char *lctx = NULL;
            int lplural = 0;
            char loc_tmpl[ROBO_I18N_MF_TEMPLATE_MAX + 1u];
            size_t loc_len = 0;
            robo_i18n_mf_diagnostic_t d;

            if (robo_i18n_catalog_entry_at(catalog, j, &lloc, &lid, &lctx, &lplural) != ROBO_OK) {
                continue;
            }
            if (!locale_eq(lloc, locale, locale_len)) {
                continue;
            }
            if (strcmp(lid, rid) != 0) {
                continue;
            }
            if (strcmp(lctx ? lctx : "", rctx ? rctx : "") != 0) {
                continue;
            }
            found = 1;
            if (lplural) {
                if (robo_i18n_catalog_entry_text(catalog, j, (int)ROBO_I18N_PLURAL_OTHER,
                                                 loc_tmpl, sizeof(loc_tmpl), &loc_len) !=
                    ROBO_OK) {
                    break;
                }
            } else {
                if (robo_i18n_catalog_entry_text(catalog, j, -1, loc_tmpl, sizeof(loc_tmpl),
                                                 &loc_len) != ROBO_OK) {
                    break;
                }
            }
            if (robo_i18n_mf_analyze_template(loc_tmpl, loc_len, &lim, &d, &loc_c) != ROBO_OK) {
                stamp_entry(&d, lloc, lid);
                report_push(report, diag_cap, &d);
                break;
            }
            if (robo_i18n_mf_contracts_compatible(&ref_c, &loc_c, &d) != ROBO_OK) {
                stamp_entry(&d, lloc, lid);
                report_push(report, diag_cap, &d);
            }
            break;
        }
        if (!found) {
            robo_i18n_mf_diagnostic_t d;
            robo_i18n_mf_diagnostic_reset(&d);
            d.result = ROBO_OK; /* warning */
            d.code = ROBO_I18N_MF_DIAG_MISSING_MESSAGE;
            d.severity = ROBO_I18N_MF_SEV_WARNING;
            stamp_entry(&d, locale, rid);
            strncpy(d.explanation, "message missing in locale relative to reference",
                    sizeof(d.explanation) - 1u);
            report_push(report, diag_cap, &d);
        }
    }
    return report->errors ? ROBO_PARSE_ERROR : ROBO_OK;
}
