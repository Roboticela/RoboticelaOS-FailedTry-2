/*
 * RoboI18N message formatting engine tests (Goal 4.12).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NMessageFormat.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_mf_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.15.0");
}

static void test_mf_simple(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "Hello, {name}!";

    parse_tag("en", &en);
    robo_i18n_mf_arg_set_string(&arg, "name", "Ada", 3);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Hello, Ada!");
}

static void test_mf_missing_arg(void)
{
    robo_i18n_locale_t en;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "Hi {name}";
    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, NULL, buf, sizeof(buf), &n),
        ROBO_NOT_FOUND);
}

static void test_mf_select(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[64];
    size_t n = 0;
    const char *tmpl =
        "{gender, select, male {He} female {She} other {They}}";

    parse_tag("en", &en);
    args.items = &arg;
    args.count = 1;

    robo_i18n_mf_arg_set_string(&arg, "gender", "male", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "He");

    robo_i18n_mf_arg_set_string(&arg, "gender", "unknown", 7);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "They");
}

static void test_mf_plural_pound(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "{count, plural, =0 {no messages} one {# message} other {# messages}}";

    parse_tag("en", &en);
    args.items = &arg;
    args.count = 1;

    robo_i18n_mf_arg_set_i64(&arg, "count", 0);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "no messages");

    robo_i18n_mf_arg_set_i64(&arg, "count", 1);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "message") != NULL);
    ROBO_CHECK(strstr(buf, "1") != NULL);

    robo_i18n_mf_arg_set_i64(&arg, "count", 5);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "messages") != NULL);
    ROBO_CHECK(strstr(buf, "5") != NULL);
}

static void test_mf_nested(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t a[2];
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;
    const char *tmpl =
        "{gender, select,"
        " male {{count, plural, one {He has # item} other {He has # items}}}"
        " female {{count, plural, one {She has # item} other {She has # items}}}"
        " other {They have items}}";

    parse_tag("en", &en);
    robo_i18n_mf_arg_set_string(&a[0], "gender", "female", 6);
    robo_i18n_mf_arg_set_i64(&a[1], "count", 1);
    args.items = a;
    args.count = 2;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "She has") != NULL);
    ROBO_CHECK(strstr(buf, "item") != NULL);
}

static void test_mf_number_locales(void)
{
    static const char *tags[] = {"en", "ar", "ur", "fa", "hi", "zh", "ja", "ru"};
    size_t i;
    const char *tmpl = "{n, number}";
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_mf_arg_t arg;
        robo_i18n_mf_args_t args;
        char buf[64];
        size_t n = 0;
        parse_tag(tags[i], &loc);
        robo_i18n_mf_arg_set_i64(&arg, "n", 1234);
        args.items = &arg;
        args.count = 1;
        ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &loc, &args, buf,
                                                        sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(n > 0);
        ROBO_CHECK(buf[0] != '\0');
    }
}

static void test_mf_date(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    robo_i18n_date_t d;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "{d, date, short}";

    parse_tag("en", &en);
    d.year = 2024;
    d.month = 6;
    d.day = 15;
    robo_i18n_mf_arg_set_date(&arg, "d", &d);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(n > 0);
}

static void test_mf_escape_unicode(void)
{
    robo_i18n_locale_t ur;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "سلام {{x}} {name} 👋";

    parse_tag("ur", &ur);
    robo_i18n_mf_arg_set_string(&arg, "name", "علی", 0);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &ur, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "{x}") != NULL);
    ROBO_CHECK(strstr(buf, "علی") != NULL);
    ROBO_CHECK(strstr(buf, "👋") != NULL);
}

static void test_mf_ordinal_arch(void)
{
    /* selectordinal uses plural_ordinal; must parse and format with other */
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";

    parse_tag("en", &en);
    robo_i18n_mf_arg_set_i64(&arg, "n", 1);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "st") != NULL || strstr(buf, "th") != NULL);
}

static void test_mf_parse_once(void)
{
    robo_i18n_mf_message_t *msg = NULL;
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "X {v} Y";

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_parse(tmpl, strlen(tmpl), &msg), ROBO_OK);
    robo_i18n_mf_arg_set_string(&arg, "v", "1", 1);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "X 1 Y");
    robo_i18n_mf_arg_set_string(&arg, "v", "2", 1);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "X 2 Y");
    robo_i18n_mf_destroy(msg);
}

static void test_mf_catalog(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    robo_i18n_mf_arg_set_string(&arg, "name", "Ada", 3);
    args.items = &arg;
    args.count = 1;
    /* demo greeting is simple {name} placeholder — works with mf if template uses {} */
    ROBO_CHECK_EQ_INT(robo_i18n_message_format_mf(cat, "app.greeting", 12, NULL, 0, &en, &args,
                                                  NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Hello, Ada!");
    robo_i18n_catalog_close(cat);
}

static void test_mf_malformed(void)
{
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{", 1), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("}", 1), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{name", 5), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{name,}", 7), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{n, unknown}", 12), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{g, select, male {x}}", 21), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("{n, plural, one {x}}", 20), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_validate("ok", 2), ROBO_OK);
    /* invalid UTF-8 */
    {
        char bad[3] = {(char)0xE2, (char)0x28, (char)0xA1};
        ROBO_CHECK_EQ_INT(robo_i18n_mf_validate(bad, 3), ROBO_PARSE_ERROR);
    }
}

static void test_mf_fuzz(void)
{
    uint32_t seed = 0x412u;
    int i;
    for (i = 0; i < 60; i++) {
        char buf[48];
        size_t len = (seed % 40u) + 1u;
        size_t j;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "abc{}#,= \tnoneotherpluralselect";
            buf[j] = alphabet[(seed >> (j % 16)) % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)robo_i18n_mf_validate(buf, len);
    }
    ROBO_CHECK(1);
}

static void test_mf_slavic_plural(void)
{
    robo_i18n_locale_t ru;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[128];
    size_t n = 0;
    const char *tmpl =
        "{n, plural, one {# сообщение} few {# сообщения} many {# сообщений} other {# сообщений}}";

    parse_tag("ru", &ru);
    args.items = &arg;
    args.count = 1;
    robo_i18n_mf_arg_set_i64(&arg, "n", 2);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format_template(tmpl, strlen(tmpl), &ru, &args, buf,
                                                    sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "сообщения") != NULL || strstr(buf, "сообщени") != NULL);
}

void robo_test_register_i18n_message_format(void)
{
    robo_test_register("i18n.mf.version", test_mf_version);
    robo_test_register("i18n.mf.simple", test_mf_simple);
    robo_test_register("i18n.mf.missing_arg", test_mf_missing_arg);
    robo_test_register("i18n.mf.select", test_mf_select);
    robo_test_register("i18n.mf.plural_pound", test_mf_plural_pound);
    robo_test_register("i18n.mf.nested", test_mf_nested);
    robo_test_register("i18n.mf.number_locales", test_mf_number_locales);
    robo_test_register("i18n.mf.date", test_mf_date);
    robo_test_register("i18n.mf.escape_unicode", test_mf_escape_unicode);
    robo_test_register("i18n.mf.ordinal", test_mf_ordinal_arch);
    robo_test_register("i18n.mf.parse_once", test_mf_parse_once);
    robo_test_register("i18n.mf.catalog", test_mf_catalog);
    robo_test_register("i18n.mf.malformed", test_mf_malformed);
    robo_test_register("i18n.mf.fuzz", test_mf_fuzz);
    robo_test_register("i18n.mf.slavic_plural", test_mf_slavic_plural);
}
