/*
 * RoboI18N message-format validation & catalog compatibility (Goal 4.13).
 */

#include "RoboI18N/RoboI18NMessageValidate.h"

#include <string.h>

void I18nMfLimitsInit(I18nMfLimits *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->templateMax = RoboI18nMfTemplateMax;
    out->nestingMax = RoboI18nMfNestingMax;
    out->branchesMax = RoboI18nMfBranchesMax;
    out->argsMax = RoboI18nMfArgsMax;
    out->outputMax = RoboI18nMfOutputMax;
    out->diagnosticsMax = RoboI18nMfDiagnosticsMax;
}

void I18nMfDiagnosticReset(I18nMfDiagnostic *d)
{
    if (!d) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->result = RoboOk;
    d->code = RoboI18nMfDiagOk;
    d->byteOffset = (size_t)-1;
}

void I18nMfContractReset(I18nMfContract *c)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

void I18nMfCatalogReportReset(I18nMfCatalogReport *r)
{
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
}

uint32_t I18nMfSyntaxVersion(void)
{
    return RoboI18nMfSyntaxVersion;
}

const char *I18nMfDiagCodeString(I18nMfDiagCode code)
{
    switch (code) {
    case RoboI18nMfDiagOk:
        return "ok";
    case RoboI18nMfDiagMalformed:
        return "malformed";
    case RoboI18nMfDiagInvalidUtf8:
        return "invalid_utf8";
    case RoboI18nMfDiagUnknownType:
        return "unknown_type";
    case RoboI18nMfDiagMissingOther:
        return "missing_other";
    case RoboI18nMfDiagDuplicateBranch:
        return "duplicate_branch";
    case RoboI18nMfDiagTypeConflict:
        return "type_conflict";
    case RoboI18nMfDiagNesting:
        return "nesting";
    case RoboI18nMfDiagTooLarge:
        return "too_large";
    case RoboI18nMfDiagUnsupportedVersion:
        return "unsupported_version";
    case RoboI18nMfDiagUnsupportedOption:
        return "unsupported_option";
    case RoboI18nMfDiagMissingMessage:
        return "missing_message";
    case RoboI18nMfDiagIncompatibleArg:
        return "incompatible_arg";
    case RoboI18nMfDiagInvalidSelector:
        return "invalid_selector";
    case RoboI18nMfDiagPoundOutsidePlural:
        return "pound_outside_plural";
    default:
        return "unknown";
    }
}

Result I18nMfValidateEx(const char *utf8,
                                       size_t len,
                                       const I18nMfLimits *limits,
                                       I18nMfDiagnostic *diag)
{
    I18nMfMessage *msg = NULL;
    Result r = I18nMfParseEx(utf8, len, limits, diag, &msg);
    if (r == RoboOk) {
        I18nMfContract c;
        r = I18nMfAnalyze(msg, &c);
        if (r != RoboOk && diag) {
            diag->result = r;
            diag->code = RoboI18nMfDiagTypeConflict;
            diag->severity = RoboI18nMfSevError;
            if (!diag->explanation[0]) {
                strncpy(diag->explanation, "incompatible argument usages",
                        sizeof(diag->explanation) - 1u);
            }
        }
        I18nMfDestroy(msg);
    }
    return r;
}

Result I18nMessageValidate(const char *utf8,
                                         size_t len,
                                         const I18nMfLimits *limits,
                                         I18nMfDiagnostic *diag)
{
    return I18nMfValidateEx(utf8, len, limits, diag);
}

int I18nMfUsageCompatible(uint32_t a, uint32_t b)
{
    uint32_t aNum = a & (RoboI18nMfUsageNumber | RoboI18nMfUsagePlural |
                          RoboI18nMfUsageOrdinal);
    uint32_t bNum = b & (RoboI18nMfUsageNumber | RoboI18nMfUsagePlural |
                          RoboI18nMfUsageOrdinal);
    uint32_t aStr = a & (RoboI18nMfUsageString | RoboI18nMfUsageSelect);
    uint32_t bStr = b & (RoboI18nMfUsageString | RoboI18nMfUsageSelect);
    uint32_t aDate = a & RoboI18nMfUsageDate;
    uint32_t bDate = b & RoboI18nMfUsageDate;
    uint32_t aTime = a & RoboI18nMfUsageTime;
    uint32_t bTime = b & RoboI18nMfUsageTime;

    /* Empty combines with anything. */
    if (a == 0 || b == 0) {
        return 1;
    }
    /* Numeric family is self-compatible. */
    if (aNum && bStr) {
        return 0;
    }
    if (bNum && aStr) {
        return 0;
    }
    if ((aNum || aStr) && (bDate || bTime) && !(aDate || aTime)) {
        /* a is only num/str, b has date/time — conflict if a lacks that bit */
    }
    if (aDate && bTime && !aTime && !bDate) {
        return 0; /* pure date vs pure time */
    }
    if (bDate && aTime && !bTime && !aDate) {
        return 0;
    }
    if ((aDate || aTime) && (bNum || bStr) && !(bDate || bTime)) {
        return 0;
    }
    if ((bDate || bTime) && (aNum || aStr) && !(aDate || aTime)) {
        return 0;
    }
    return 1;
}

Result I18nMfContractsCompatible(const I18nMfContract *a,
                                                const I18nMfContract *b,
                                                I18nMfDiagnostic *diag)
{
    uint32_t i, j;
    if (!a || !b) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < a->count; i++) {
        for (j = 0; j < b->count; j++) {
            if (strcmp(a->args[i].name, b->args[j].name) == 0) {
                if (!I18nMfUsageCompatible(a->args[i].usage, b->args[j].usage)) {
                    if (diag) {
                        I18nMfDiagnosticReset(diag);
                        diag->result = RoboParseError;
                        diag->code = RoboI18nMfDiagIncompatibleArg;
                        diag->severity = RoboI18nMfSevError;
                        strncpy(diag->argumentName, a->args[i].name,
                                sizeof(diag->argumentName) - 1u);
                        strncpy(diag->explanation,
                                "argument usage incompatible between contracts",
                                sizeof(diag->explanation) - 1u);
                    }
                    return RoboParseError;
                }
            }
        }
    }
    if (diag) {
        I18nMfDiagnosticReset(diag);
        diag->result = RoboOk;
        diag->code = RoboI18nMfDiagOk;
    }
    return RoboOk;
}

static int ReportPush(I18nMfCatalogReport *report,
                       uint32_t diagCap,
                       const I18nMfDiagnostic *d)
{
    if (!report || !d) {
        return 0;
    }
    if (d->severity == RoboI18nMfSevError) {
        report->errors++;
    } else {
        report->warnings++;
    }
    if (report->diagCount >= diagCap ||
        report->diagCount >= RoboI18nMfDiagnosticsMax) {
        return 0;
    }
    report->diags[report->diagCount++] = *d;
    return 1;
}

static void StampEntry(I18nMfDiagnostic *d,
                        const char *locale,
                        const char *id)
{
    if (!d) {
        return;
    }
    if (locale) {
        strncpy(d->localeTag, locale, sizeof(d->localeTag) - 1u);
    }
    if (id) {
        strncpy(d->messageId, id, sizeof(d->messageId) - 1u);
    }
}

Result I18nMfCatalogValidate(const I18nCatalog *catalog,
                                            const I18nMfLimits *limits,
                                            I18nMfCatalogReport *report)
{
    uint32_t n;
    uint32_t i;
    uint32_t diagCap;
    I18nMfLimits lim;

    if (!catalog || !report) {
        return RoboInvalidArgument;
    }
    I18nMfCatalogReportReset(report);
    I18nMfLimitsInit(&lim);
    if (limits) {
        lim = *limits;
        if (!lim.templateMax) {
            lim.templateMax = RoboI18nMfTemplateMax;
        }
        if (!lim.diagnosticsMax) {
            lim.diagnosticsMax = RoboI18nMfDiagnosticsMax;
        }
    }
    diagCap = lim.diagnosticsMax;
    if (diagCap > RoboI18nMfDiagnosticsMax) {
        diagCap = RoboI18nMfDiagnosticsMax;
    }

    n = I18nCatalogEntryCount(catalog);
    for (i = 0; i < n; i++) {
        const char *loc = NULL;
        const char *id = NULL;
        const char *ctx = NULL;
        int isPlural = 0;
        int slot;

        if (I18nCatalogEntryAt(catalog, i, &loc, &id, &ctx, &isPlural) != RoboOk) {
            continue;
        }
        if (isPlural) {
            for (slot = 0; slot <= (int)RoboI18nPluralOther; slot++) {
                char tmpl[RoboI18nMfTemplateMax + 1u];
                size_t tlen = 0;
                I18nMfDiagnostic d;
                Result r;

                if (I18nCatalogEntryText(catalog, i, slot, tmpl, sizeof(tmpl), &tlen) !=
                    RoboOk) {
                    continue;
                }
                report->checked++;
                r = I18nMfValidateEx(tmpl, tlen, &lim, &d);
                if (r != RoboOk) {
                    StampEntry(&d, loc, id);
                    ReportPush(report, diagCap, &d);
                }
            }
        } else {
            char tmpl[RoboI18nMfTemplateMax + 1u];
            size_t tlen = 0;
            I18nMfDiagnostic d;
            Result r;

            if (I18nCatalogEntryText(catalog, i, -1, tmpl, sizeof(tmpl), &tlen) !=
                RoboOk) {
                continue;
            }
            report->checked++;
            r = I18nMfValidateEx(tmpl, tlen, &lim, &d);
            if (r != RoboOk) {
                StampEntry(&d, loc, id);
                ReportPush(report, diagCap, &d);
            }
        }
    }
    return report->errors ? RoboParseError : RoboOk;
}

static int LocaleEq(const char *a, const char *b, size_t blen)
{
    size_t alen;
    if (!a || !b) {
        return 0;
    }
    alen = strlen(a);
    return alen == blen && memcmp(a, b, blen) == 0;
}

Result I18nMfCatalogValidateFallback(
    const I18nCatalog *catalog,
    const char *referenceLocale,
    size_t referenceLocaleLen,
    const char *locale,
    size_t localeLen,
    const I18nMfLimits *limits,
    I18nMfCatalogReport *report)
{
    uint32_t n;
    uint32_t i;
    uint32_t diagCap;
    I18nMfLimits lim;

    if (!catalog || !referenceLocale || !locale || !report) {
        return RoboInvalidArgument;
    }
    I18nMfCatalogReportReset(report);
    I18nMfLimitsInit(&lim);
    if (limits) {
        lim = *limits;
        if (!lim.diagnosticsMax) {
            lim.diagnosticsMax = RoboI18nMfDiagnosticsMax;
        }
    }
    diagCap = lim.diagnosticsMax;
    if (diagCap > RoboI18nMfDiagnosticsMax) {
        diagCap = RoboI18nMfDiagnosticsMax;
    }

    n = I18nCatalogEntryCount(catalog);
    /* For each reference-locale message, find matching locale entry. */
    for (i = 0; i < n; i++) {
        const char *rloc = NULL;
        const char *rid = NULL;
        const char *rctx = NULL;
        int rplural = 0;
        uint32_t j;
        int found = 0;
        I18nMfContract refC;
        I18nMfContract locC;
        char refTmpl[RoboI18nMfTemplateMax + 1u];
        size_t refLen = 0;

        if (I18nCatalogEntryAt(catalog, i, &rloc, &rid, &rctx, &rplural) != RoboOk) {
            continue;
        }
        if (!LocaleEq(rloc, referenceLocale, referenceLocaleLen)) {
            continue;
        }

        /* Build reference contract from plain text or "other" plural. */
        if (rplural) {
            if (I18nCatalogEntryText(catalog, i, (int)RoboI18nPluralOther, refTmpl,
                                             sizeof(refTmpl), &refLen) != RoboOk) {
                continue;
            }
        } else {
            if (I18nCatalogEntryText(catalog, i, -1, refTmpl, sizeof(refTmpl),
                                             &refLen) != RoboOk) {
                continue;
            }
        }
        report->checked++;
        if (I18nMfAnalyzeTemplate(refTmpl, refLen, &lim, NULL, &refC) != RoboOk) {
            continue;
        }

        for (j = 0; j < n; j++) {
            const char *lloc = NULL;
            const char *lid = NULL;
            const char *lctx = NULL;
            int lplural = 0;
            char locTmpl[RoboI18nMfTemplateMax + 1u];
            size_t locLen = 0;
            I18nMfDiagnostic d;

            if (I18nCatalogEntryAt(catalog, j, &lloc, &lid, &lctx, &lplural) != RoboOk) {
                continue;
            }
            if (!LocaleEq(lloc, locale, localeLen)) {
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
                if (I18nCatalogEntryText(catalog, j, (int)RoboI18nPluralOther,
                                                 locTmpl, sizeof(locTmpl), &locLen) !=
                    RoboOk) {
                    break;
                }
            } else {
                if (I18nCatalogEntryText(catalog, j, -1, locTmpl, sizeof(locTmpl),
                                                 &locLen) != RoboOk) {
                    break;
                }
            }
            if (I18nMfAnalyzeTemplate(locTmpl, locLen, &lim, &d, &locC) != RoboOk) {
                StampEntry(&d, lloc, lid);
                ReportPush(report, diagCap, &d);
                break;
            }
            if (I18nMfContractsCompatible(&refC, &locC, &d) != RoboOk) {
                StampEntry(&d, lloc, lid);
                ReportPush(report, diagCap, &d);
            }
            break;
        }
        if (!found) {
            I18nMfDiagnostic d;
            I18nMfDiagnosticReset(&d);
            d.result = RoboOk; /* warning */
            d.code = RoboI18nMfDiagMissingMessage;
            d.severity = RoboI18nMfSevWarning;
            StampEntry(&d, locale, rid);
            strncpy(d.explanation, "message missing in locale relative to reference",
                    sizeof(d.explanation) - 1u);
            ReportPush(report, diagCap, &d);
        }
    }
    return report->errors ? RoboParseError : RoboOk;
}
