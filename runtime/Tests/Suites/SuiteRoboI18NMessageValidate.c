/*
 * RoboI18N message-format validation / compatibility tests (Goal 4.13).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NMessageValidate.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>
static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_mfv_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.14.0");
    ROBO_CHECK_EQ_INT((int)robo_i18n_mf_syntax_version(), 1);
}

static void test_mfv_validate_ok(void)
{
    robo_i18n_mf_diagnostic_t d;
    ROBO_CHECK_EQ_INT(robo_i18n_message_validate("Hello, {name}!", 14, NULL, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_OK);
}

static void test_mfv_validate_diag(void)
{
    robo_i18n_mf_diagnostic_t d;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex("{n, unknown}", 12, NULL, &d), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_UNKNOWN_TYPE);
    ROBO_CHECK(d.byte_offset != (size_t)-1);
    ROBO_CHECK(d.line >= 1u);
    ROBO_CHECK(d.explanation[0] != '\0');
}

static void test_mfv_duplicate_branch(void)
{
    robo_i18n_mf_diagnostic_t d;
    const char *tmpl = "{g, select, male {a} male {b} other {c}}";
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex(tmpl, strlen(tmpl), NULL, &d), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_DUPLICATE_BRANCH);
}

static void test_mfv_version_directive(void)
{
    robo_i18n_mf_diagnostic_t d;
    const char *ok = "#!robo-mf/1\nHello";
    const char *bad = "#!robo-mf/99\nHello";
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex(ok, strlen(ok), NULL, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex(bad, strlen(bad), NULL, &d), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION);
}

static void test_mfv_analyze(void)
{
    robo_i18n_mf_contract_t c;
    const char *tmpl = "{user} has {count, plural, one {# item} other {# items}}";
    ROBO_CHECK_EQ_INT(robo_i18n_mf_analyze_template(tmpl, strlen(tmpl), NULL, NULL, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)c.count, 2);
    ROBO_CHECK_STR_EQ(c.args[0].name, "count");
    ROBO_CHECK(c.args[0].usage & ROBO_I18N_MF_USAGE_PLURAL);
    ROBO_CHECK_STR_EQ(c.args[1].name, "user");
    ROBO_CHECK(c.args[1].usage & ROBO_I18N_MF_USAGE_STRING);
}

static void test_mfv_type_conflict(void)
{
    robo_i18n_mf_diagnostic_t d;
    const char *tmpl = "{x, number} {x, date}";
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex(tmpl, strlen(tmpl), NULL, &d), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_TYPE_CONFLICT);
}

static void test_mfv_contracts(void)
{
    robo_i18n_mf_contract_t a;
    robo_i18n_mf_contract_t b;
    robo_i18n_mf_diagnostic_t d;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_analyze_template("{n, number}", 11, NULL, NULL, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_analyze_template("{n, plural, other {#}}", 22, NULL, NULL, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_contracts_compatible(&a, &b, &d), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_mf_analyze_template("{n, date}", 9, NULL, NULL, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_contracts_compatible(&a, &b, &d), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_INCOMPATIBLE_ARG);
}

static void test_mfv_limits(void)
{
    robo_i18n_mf_limits_t lim;
    robo_i18n_mf_diagnostic_t d;
    robo_i18n_mf_limits_init(&lim);
    lim.template_max = 4u;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex("Hello", 5, &lim, &d), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)d.code, (int)ROBO_I18N_MF_DIAG_TOO_LARGE);
}

static void test_mfv_catalog(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_mf_catalog_report_t report;
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    ROBO_CHECK(robo_i18n_catalog_entry_count(cat) > 0u);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_catalog_validate(cat, NULL, &report), ROBO_OK);
    ROBO_CHECK(report.checked > 0u);
    ROBO_CHECK_EQ_INT((int)report.errors, 0);
    robo_i18n_catalog_close(cat);
}

static void test_mfv_fallback(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_mf_catalog_report_t report;
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_catalog_validate_fallback(cat, "en", 2, "ur", 2, NULL, &report),
                      ROBO_OK);
    /* missing messages are warnings, not errors */
    ROBO_CHECK_EQ_INT((int)report.errors, 0);
    robo_i18n_catalog_close(cat);
}

static void test_mfv_deterministic(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    robo_i18n_mf_message_t *msg = NULL;
    char a[64];
    char b[64];
    size_t na = 0;
    size_t nb = 0;
    const char *tmpl = "Hi {name}";
    int i;

    parse_tag("en", &en);
    robo_i18n_mf_arg_set_string(&arg, "name", "Ada", 3);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_parse(tmpl, strlen(tmpl), &msg), ROBO_OK);
    for (i = 0; i < 5; i++) {
        ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, a, sizeof(a), &na), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, b, sizeof(b), &nb), ROBO_OK);
        ROBO_CHECK_STR_EQ(a, b);
        ROBO_CHECK_STR_EQ(a, "Hi Ada");
    }
    robo_i18n_mf_destroy(msg);
}

static int apply_args(robo_i18n_mf_args_t *args,
                      robo_i18n_mf_arg_t *storage,
                      size_t storage_cap,
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
    while (*p && count < storage_cap) {
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
                robo_i18n_mf_arg_set_string(&storage[count], name, start, (size_t)(p - start));
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
            robo_i18n_mf_arg_set_i64(&storage[count], name, (int64_t)v);
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

static void test_mfv_golden(void)
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
        robo_i18n_locale_t loc;
        robo_i18n_mf_arg_t storage[4];
        robo_i18n_mf_args_t args;
        char buf[256];
        size_t n = 0;
        parse_tag(cases[i].locale, &loc);
        ROBO_CHECK(apply_args(&args, storage, 4, cases[i].argspec));
        ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(cases[i].tmpl, strlen(cases[i].tmpl), &loc,
                                                        &args, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(n > 0);
        if (cases[i].expect) {
            ROBO_CHECK_STR_EQ(buf, cases[i].expect);
        }
    }
}

static void test_mfv_negative(void)
{
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{", 1), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{g, select, male {x}}", 21), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{n, plural, one {x} one {y} other {z}}", 37),
                      ROBO_PARSE_ERROR);
    {
        char bad[3] = {(char)0xE2, (char)0x28, (char)0xA1};
        ROBO_CHECK_EQ_INT(robo_i18n_mf_validate(bad, 3), ROBO_PARSE_ERROR);
    }
    {
        robo_i18n_mf_limits_t lim;
        char deep[256];
        size_t i;
        size_t n = 0;
        robo_i18n_mf_limits_init(&lim);
        lim.nesting_max = 2u;
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
        ROBO_CHECK_EQ_INT(robo_i18n_mf_validate_ex(deep, n, &lim, NULL), ROBO_PARSE_ERROR);
    }
}

static void test_mfv_fuzz(void)
{
    uint32_t seed = 0x413u;
    int i;
    for (i = 0; i < 80; i++) {
        char buf[64];
        size_t len = (seed % 50u) + 1u;
        size_t j;
        robo_i18n_mf_diagnostic_t d;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "abc{}#,= \tnoneotherpluralselectordinal#!robo-mf/19";
            buf[j] = alphabet[(seed >> (j % 16)) % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)robo_i18n_mf_validate_ex(buf, len, NULL, &d);
        {
            robo_i18n_mf_contract_t c;
            (void)robo_i18n_mf_analyze_template(buf, len, NULL, &d, &c);
        }
    }
    ROBO_CHECK(1);
}

static void test_mfv_unicode_golden(void)
{
    robo_i18n_locale_t he;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "שלום {name} 👋";
    parse_tag("he", &he);
    robo_i18n_mf_arg_set_string(&arg, "name", "עולם", 0);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &he, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "שלום") != NULL);
    ROBO_CHECK(strstr(buf, "עולם") != NULL);
    ROBO_CHECK(strstr(buf, "👋") != NULL);
}

void robo_test_register_i18n_message_validate(void)
{
    robo_test_register("i18n.mfv.version", test_mfv_version);
    robo_test_register("i18n.mfv.validate_ok", test_mfv_validate_ok);
    robo_test_register("i18n.mfv.validate_diag", test_mfv_validate_diag);
    robo_test_register("i18n.mfv.duplicate_branch", test_mfv_duplicate_branch);
    robo_test_register("i18n.mfv.version_directive", test_mfv_version_directive);
    robo_test_register("i18n.mfv.analyze", test_mfv_analyze);
    robo_test_register("i18n.mfv.type_conflict", test_mfv_type_conflict);
    robo_test_register("i18n.mfv.contracts", test_mfv_contracts);
    robo_test_register("i18n.mfv.limits", test_mfv_limits);
    robo_test_register("i18n.mfv.catalog", test_mfv_catalog);
    robo_test_register("i18n.mfv.fallback", test_mfv_fallback);
    robo_test_register("i18n.mfv.deterministic", test_mfv_deterministic);
    robo_test_register("i18n.mfv.golden", test_mfv_golden);
    robo_test_register("i18n.mfv.negative", test_mfv_negative);
    robo_test_register("i18n.mfv.fuzz", test_mfv_fuzz);
    robo_test_register("i18n.mfv.unicode", test_mfv_unicode_golden);
}
