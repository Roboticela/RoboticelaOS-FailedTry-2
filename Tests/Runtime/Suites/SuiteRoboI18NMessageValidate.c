/*
 * RoboI18N message-format validation / compatibility tests (Goal 4.13).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NMessageValidate.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>
static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestMfvVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckEqInt((int)I18nMfSyntaxVersion(), 1);
}

static void TestMfvValidateOk(void)
{
    I18nMfDiagnostic d;
    RoboCheckEqInt(I18nMessageValidate("Hello, {name}!", 14, NULL, &d), RoboOk);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagOk);
}

static void TestMfvValidateDiag(void)
{
    I18nMfDiagnostic d;
    RoboCheckEqInt(I18nMfValidateEx("{n, unknown}", 12, NULL, &d), RoboParseError);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagUnknownType);
    RoboCheck(d.byteOffset != (size_t)-1);
    RoboCheck(d.line >= 1u);
    RoboCheck(d.explanation[0] != '\0');
}

static void TestMfvDuplicateBranch(void)
{
    I18nMfDiagnostic d;
    const char *tmpl = "{g, select, male {a} male {b} other {c}}";
    RoboCheckEqInt(I18nMfValidateEx(tmpl, strlen(tmpl), NULL, &d), RoboParseError);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagDuplicateBranch);
}

static void TestMfvVersionDirective(void)
{
    I18nMfDiagnostic d;
    const char *ok = "#!robo-mf/1\nHello";
    const char *bad = "#!robo-mf/99\nHello";
    RoboCheckEqInt(I18nMfValidateEx(ok, strlen(ok), NULL, &d), RoboOk);
    RoboCheckEqInt(I18nMfValidateEx(bad, strlen(bad), NULL, &d), RoboNotSupported);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagUnsupportedVersion);
}

static void TestMfvAnalyze(void)
{
    I18nMfContract c;
    const char *tmpl = "{user} has {count, plural, one {# item} other {# items}}";
    RoboCheckEqInt(I18nMfAnalyzeTemplate(tmpl, strlen(tmpl), NULL, NULL, &c), RoboOk);
    RoboCheckEqInt((int)c.count, 2);
    RoboCheckStrEq(c.args[0].name, "count");
    RoboCheck(c.args[0].usage & RoboI18nMfUsagePlural);
    RoboCheckStrEq(c.args[1].name, "user");
    RoboCheck(c.args[1].usage & RoboI18nMfUsageString);
}

static void TestMfvTypeConflict(void)
{
    I18nMfDiagnostic d;
    const char *tmpl = "{x, number} {x, date}";
    RoboCheckEqInt(I18nMfValidateEx(tmpl, strlen(tmpl), NULL, &d), RoboParseError);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagTypeConflict);
}

static void TestMfvContracts(void)
{
    I18nMfContract a;
    I18nMfContract b;
    I18nMfDiagnostic d;
    RoboCheckEqInt(I18nMfAnalyzeTemplate("{n, number}", 11, NULL, NULL, &a), RoboOk);
    RoboCheckEqInt(
        I18nMfAnalyzeTemplate("{n, plural, other {#}}", 22, NULL, NULL, &b), RoboOk);
    RoboCheckEqInt(I18nMfContractsCompatible(&a, &b, &d), RoboOk);

    RoboCheckEqInt(I18nMfAnalyzeTemplate("{n, date}", 9, NULL, NULL, &b), RoboOk);
    RoboCheckEqInt(I18nMfContractsCompatible(&a, &b, &d), RoboParseError);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagIncompatibleArg);
}

static void TestMfvLimits(void)
{
    I18nMfLimits lim;
    I18nMfDiagnostic d;
    I18nMfLimitsInit(&lim);
    lim.templateMax = 4u;
    RoboCheckEqInt(I18nMfValidateEx("Hello", 5, &lim, &d), RoboParseError);
    RoboCheckEqInt((int)d.code, (int)RoboI18nMfDiagTooLarge);
}

static void TestMfvCatalog(void)
{
    I18nCatalog *cat = NULL;
    I18nMfCatalogReport report;
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    RoboCheck(I18nCatalogEntryCount(cat) > 0u);
    RoboCheckEqInt(I18nMfCatalogValidate(cat, NULL, &report), RoboOk);
    RoboCheck(report.checked > 0u);
    RoboCheckEqInt((int)report.errors, 0);
    I18nCatalogClose(cat);
}

static void TestMfvFallback(void)
{
    I18nCatalog *cat = NULL;
    I18nMfCatalogReport report;
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    RoboCheckEqInt(I18nMfCatalogValidateFallback(cat, "en", 2, "ur", 2, NULL, &report),
                      RoboOk);
    /* missing messages are warnings, not errors */
    RoboCheckEqInt((int)report.errors, 0);
    I18nCatalogClose(cat);
}

static void TestMfvDeterministic(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    I18nMfMessage *msg = NULL;
    char a[64];
    char b[64];
    size_t na = 0;
    size_t nb = 0;
    const char *tmpl = "Hi {name}";
    int i;

    ParseTag("en", &en);
    I18nMfArgSetString(&arg, "name", "Ada", 3);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfParse(tmpl, strlen(tmpl), &msg), RoboOk);
    for (i = 0; i < 5; i++) {
        RoboCheckEqInt(I18nMfFormat(msg, &en, &args, a, sizeof(a), &na), RoboOk);
        RoboCheckEqInt(I18nMfFormat(msg, &en, &args, b, sizeof(b), &nb), RoboOk);
        RoboCheckStrEq(a, b);
        RoboCheckStrEq(a, "Hi Ada");
    }
    I18nMfDestroy(msg);
}

static int ApplyArgs(I18nMfArgs *args,
                      I18nMfArg *storage,
                      size_t storageCap,
                      const char *spec)
{
    /* spec: name=s:text | name=i:123  comma-separated */
    size_t count = 0;
    const char *p = spec;
    if (!spec || !spec[0]) {
        args->items = NULL;
        args->count = 0;
        return 1;
    }
    while (*p && count < storageCap) {
        char name[64];
        size_t ni = 0;
        while (*p && *p != '=' && ni + 1 < sizeof(name)) {
            name[ni++] = *p++;
        }
        name[ni] = '\0';
        if (*p != '=') {
            return 0;
        }
        p++;
        if (*p == 's' && p[1] == ':') {
            p += 2;
            {
                const char *start = p;
                while (*p && *p != ',') {
                    p++;
                }
                I18nMfArgSetString(&storage[count], name, start, (size_t)(p - start));
            }
        } else if (*p == 'i' && p[1] == ':') {
            long v;
            p += 2;
            v = 0;
            {
                int neg = 0;
                if (*p == '-') {
                    neg = 1;
                    p++;
                }
                while (*p >= '0' && *p <= '9') {
                    v = v * 10 + (*p - '0');
                    p++;
                }
                if (neg) {
                    v = -v;
                }
            }
            I18nMfArgSetI64(&storage[count], name, (int64_t)v);
        } else {
            return 0;
        }
        count++;
        if (*p == ',') {
            p++;
        }
    }
    args->items = storage;
    args->count = count;
    return 1;
}

static void TestMfvGolden(void)
{
    /* Embedded golden cases (also mirrored in Tests/Data/robo_mf_golden.txt). */
    static const struct {
        const char *locale;
        const char *tmpl;
        const char *argspec;
        const char *expect;
    } cases[] = {
        {"en", "Hello, {name}!", "name=s:Ada", "Hello, Ada!"},
        {"en", "{count, number}", "count=i:1234", "1,234"},
        {"en", "{g, select, male {He} female {She} other {They}}", "g=s:male", "He"},
        {"en", "{n, plural, =0 {none} one {# item} other {# items}}", "n=i:1", "1 item"},
        {"en", "{n, plural, =0 {none} one {# item} other {# items}}", "n=i:0", "none"},
        {"ur", "سلام {name}", "name=s:علی", "سلام علی"},
        {"ar", "{n, number}", "n=i:5", NULL}, /* non-NULL output only */
        {"ja", "{n, number}", "n=i:42", "42"},
        {"ru",
         "{n, plural, one {# сообщение} few {# сообщения} many {# сообщений} other {# сообщений}}",
         "n=i:2", NULL},
        {"zh", "你好，{name}", "name=s:李", "你好，李"},
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        I18nLocale loc;
        I18nMfArg storage[4];
        I18nMfArgs args;
        char buf[256];
        size_t n = 0;
        ParseTag(cases[i].locale, &loc);
        RoboCheck(ApplyArgs(&args, storage, 4, cases[i].argspec));
        RoboCheckEqInt(I18nMfFormatTemplate(cases[i].tmpl, strlen(cases[i].tmpl), &loc,
                                                        &args, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(n > 0);
        if (cases[i].expect) {
            RoboCheckStrEq(buf, cases[i].expect);
        }
    }
}

static void TestMfvNegative(void)
{
    RoboCheckEqInt(I18nMfValidate("{", 1), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{g, select, male {x}}", 21), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{n, plural, one {x} one {y} other {z}}", 37),
                      RoboParseError);
    {
        char bad[3] = {(char)0xE2, (char)0x28, (char)0xA1};
        RoboCheckEqInt(I18nMfValidate(bad, 3), RoboParseError);
    }
    {
        I18nMfLimits lim;
        char deep[256];
        size_t i;
        size_t n = 0;
        I18nMfLimitsInit(&lim);
        lim.nestingMax = 2u;
        /* nest select depth > 2 */
        n = 0;
        for (i = 0; i < 4; i++) {
            memcpy(deep + n, "{a, select, other {", 19);
            n += 19;
        }
        memcpy(deep + n, "x", 1);
        n += 1;
        for (i = 0; i < 4; i++) {
            deep[n++] = '}';
            deep[n++] = '}';
        }
        deep[n] = '\0';
        RoboCheckEqInt(I18nMfValidateEx(deep, n, &lim, NULL), RoboParseError);
    }
}

static void TestMfvFuzz(void)
{
    uint32_t seed = 0x413u;
    int i;
    for (i = 0; i < 80; i++) {
        char buf[64];
        size_t len = (seed % 50u) + 1u;
        size_t j;
        I18nMfDiagnostic d;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "abc{}#,= \tnoneotherpluralselectordinal#!robo-mf/19";
            buf[j] = alphabet[(seed >> (j % 16)) % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)I18nMfValidateEx(buf, len, NULL, &d);
        {
            I18nMfContract c;
            (void)I18nMfAnalyzeTemplate(buf, len, NULL, &d, &c);
        }
    }
    RoboCheck(1);
}

static void TestMfvUnicodeGolden(void)
{
    I18nLocale he;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "שלום {name} 👋";
    ParseTag("he", &he);
    I18nMfArgSetString(&arg, "name", "עולם", 0);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &he, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "שלום") != NULL);
    RoboCheck(strstr(buf, "עולם") != NULL);
    RoboCheck(strstr(buf, "👋") != NULL);
}

void TestRegisterI18nMessageValidate(void)
{
    TestRegister("i18n.mfv.version", TestMfvVersion);
    TestRegister("i18n.mfv.validate_ok", TestMfvValidateOk);
    TestRegister("i18n.mfv.validate_diag", TestMfvValidateDiag);
    TestRegister("i18n.mfv.duplicate_branch", TestMfvDuplicateBranch);
    TestRegister("i18n.mfv.version_directive", TestMfvVersionDirective);
    TestRegister("i18n.mfv.analyze", TestMfvAnalyze);
    TestRegister("i18n.mfv.type_conflict", TestMfvTypeConflict);
    TestRegister("i18n.mfv.contracts", TestMfvContracts);
    TestRegister("i18n.mfv.limits", TestMfvLimits);
    TestRegister("i18n.mfv.catalog", TestMfvCatalog);
    TestRegister("i18n.mfv.fallback", TestMfvFallback);
    TestRegister("i18n.mfv.deterministic", TestMfvDeterministic);
    TestRegister("i18n.mfv.golden", TestMfvGolden);
    TestRegister("i18n.mfv.negative", TestMfvNegative);
    TestRegister("i18n.mfv.fuzz", TestMfvFuzz);
    TestRegister("i18n.mfv.unicode", TestMfvUnicodeGolden);
}
