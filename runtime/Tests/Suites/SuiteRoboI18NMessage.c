/*
 * RoboI18N message catalogs (Goal 4.11).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboMsgData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_msg_versions(void)
{
    robo_i18n_catalog_t *cat = NULL;
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.27.0");
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    ROBO_CHECK(cat != NULL);
    ROBO_CHECK_STR_EQ(robo_i18n_catalog_id(cat), "robo.demo");
    ROBO_CHECK_STR_EQ(robo_i18n_catalog_version(cat), "1.0.0");
    ROBO_CHECK_EQ_INT((int)robo_i18n_catalog_format_version(cat), 1);
    robo_i18n_catalog_close(cat);
    ROBO_CHECK(robo_msg_entry_count >= 100u);
}

static void test_msg_lookup_format(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t en;
    robo_i18n_locale_t fr;
    robo_i18n_msg_var_t vars[1];
    char buf[256];
    size_t n = 0;

    parse_tag("en", &en);
    parse_tag("fr", &fr);
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.login.title", 15, NULL, 0, &en, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Sign in");

    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.login.title", 15, NULL, 0, &fr, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Connexion");

    memset(&vars[0], 0, sizeof(vars[0]));
    strncpy(vars[0].name, "name", sizeof(vars[0].name) - 1);
    vars[0].value = "Ada";
    ROBO_CHECK_EQ_INT(robo_i18n_message_format_id(cat, "app.greeting", 12, NULL, 0, &en, NULL,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Hello, Ada!");

    ROBO_CHECK_EQ_INT(robo_i18n_message_format_id(cat, "app.greeting", 12, NULL, 0, &fr, NULL,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Ada") != NULL);
    ROBO_CHECK(strstr(buf, "Bonjour") != NULL);

    robo_i18n_catalog_close(cat);
}

static void test_msg_context(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t en;
    char buf[64];
    size_t n = 0;
    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.action.open", 15, "file", 4, &en, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Open");
    ROBO_CHECK(robo_i18n_message_exists(cat, "app.action.open", 15, "door", 4, &en, NULL));
    ROBO_CHECK(!robo_i18n_message_exists(cat, "app.action.open", 15, "window", 6, &en, NULL));
    robo_i18n_catalog_close(cat);
}

static void test_msg_plural(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t en;
    robo_i18n_decimal_t one;
    robo_i18n_decimal_t many;
    robo_i18n_msg_var_t vars[1];
    char buf[128];
    size_t n = 0;

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_decimal_from_i64(1, &one), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_decimal_from_i64(5, &many), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);

    memset(&vars[0], 0, sizeof(vars[0]));
    strncpy(vars[0].name, "count", sizeof(vars[0].name) - 1);
    vars[0].value = "1";
    ROBO_CHECK_EQ_INT(robo_i18n_message_format_id(cat, "app.items", 9, NULL, 0, &en, &one,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "1 item");

    vars[0].value = "5";
    ROBO_CHECK_EQ_INT(robo_i18n_message_format_id(cat, "app.items", 9, NULL, 0, &en, &many,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "5 items");
    robo_i18n_catalog_close(cat);
}

static void test_msg_fallback(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t fr_ca;
    robo_i18n_locale_t xx;
    robo_i18n_msg_options_t opt;
    char buf[128];
    size_t n = 0;

    parse_tag("fr-CA", &fr_ca);
    parse_tag("xx", &xx);
    robo_i18n_msg_options_init(&opt);
    strncpy(opt.fallback_locale, "en", sizeof(opt.fallback_locale) - 1);

    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);

    /* Regional missing → language fr */
    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.login.title", 15, NULL, 0, &fr_ca, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Connexion");

    /* Unknown language → configured fallback en */
    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.login.title", 15, NULL, 0, &xx, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Sign in");

    opt.missing = ROBO_I18N_MSG_MISS_NOT_FOUND;
    opt.fallback_locale[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.login.title", 15, NULL, 0, &xx, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      ROBO_NOT_FOUND);

    opt.missing = ROBO_I18N_MSG_MISS_RETURN_ID;
    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.no.such", 11, NULL, 0, &fr_ca, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "app.no.such");

    robo_i18n_catalog_close(cat);
}

static void test_msg_intl_scripts(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "de", "fr", "es",
        "tr"
    };
    robo_i18n_catalog_t *cat = NULL;
    size_t i;
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        char buf[256];
        size_t n = 0;
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.settings.language", 21, NULL, 0,
                                                &loc, NULL, NULL, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(buf[0] != '\0');
        ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.welcome_emoji", 17, NULL, 0, &loc,
                                                NULL, NULL, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(strstr(buf, "👋") != NULL || n > 0);
        ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.combining", 13, NULL, 0, &loc, NULL,
                                                NULL, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(n >= 5);
    }
    robo_i18n_catalog_close(cat);
}

static void test_msg_placeholders_escape(void)
{
    robo_i18n_msg_var_t vars[1];
    char buf[64];
    size_t n = 0;
    memset(&vars[0], 0, sizeof(vars[0]));
    strncpy(vars[0].name, "x", sizeof(vars[0].name) - 1);
    vars[0].value = "Y";
    ROBO_CHECK_EQ_INT(
        robo_i18n_message_format("a {{b}} {x} c", 13, vars, 1, NULL, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "a {b} Y c");
}

static void test_msg_blob_load(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t en;
    char buf[64];
    size_t n = 0;
    uint8_t bad[8] = {0};

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(
        robo_i18n_catalog_load(robo_msg_demo_blob, robo_msg_demo_blob_size, &cat), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_i18n_catalog_id(cat), "robo.demo");
    ROBO_CHECK_EQ_INT(robo_i18n_message_get(cat, "app.login.title", 15, NULL, 0, &en, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Sign in");
    robo_i18n_catalog_close(cat);

    ROBO_CHECK_EQ_INT(robo_i18n_catalog_load(bad, sizeof(bad), &cat), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_load(NULL, 0, &cat), ROBO_INVALID_ARGUMENT);
}

static void test_msg_fuzz(void)
{
    robo_i18n_catalog_t *cat = NULL;
    robo_i18n_locale_t en;
    char buf[64];
    size_t n = 0;
    uint32_t seed = 0x411u;
    int i;
    uint8_t junk[64];

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_catalog_open_demo(&cat), ROBO_OK);
    for (i = 0; i < 40; i++) {
        robo_i18n_catalog_t *loaded = NULL;
        char id[16];
        size_t len = (seed % 8u) + 1u;
        size_t j;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            id[j] = (char)('a' + ((seed >> (j % 8)) % 26u));
            seed = seed * 1664525u + 1013904223u;
        }
        (void)robo_i18n_message_get(cat, id, len, NULL, 0, &en, NULL, NULL, buf, sizeof(buf),
                                    &n);
        junk[i % sizeof(junk)] = (uint8_t)(seed & 0xffu);
        if (robo_i18n_catalog_load(junk, sizeof(junk), &loaded) == ROBO_OK) {
            robo_i18n_catalog_close(loaded);
        }
    }
    ROBO_CHECK_EQ_INT(robo_i18n_message_format("{", 1, NULL, 0, NULL, buf, sizeof(buf), &n),
                      ROBO_PARSE_ERROR);
    robo_i18n_catalog_close(cat);
}

void robo_test_register_i18n_message(void)
{
    robo_test_register("i18n.msg.versions", test_msg_versions);
    robo_test_register("i18n.msg.lookup_format", test_msg_lookup_format);
    robo_test_register("i18n.msg.context", test_msg_context);
    robo_test_register("i18n.msg.plural", test_msg_plural);
    robo_test_register("i18n.msg.fallback", test_msg_fallback);
    robo_test_register("i18n.msg.intl_scripts", test_msg_intl_scripts);
    robo_test_register("i18n.msg.placeholders", test_msg_placeholders_escape);
    robo_test_register("i18n.msg.blob_load", test_msg_blob_load);
    robo_test_register("i18n.msg.fuzz", test_msg_fuzz);
}
