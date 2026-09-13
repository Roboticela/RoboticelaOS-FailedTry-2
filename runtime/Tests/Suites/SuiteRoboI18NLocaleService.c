/*
 * RoboI18N runtime locale service (Goal 4.16).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NCollation.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboI18NMessage.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboHcData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_ls_version(void)
{
    robo_i18n_data_versions_t v;
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    robo_i18n_data_versions(&v);
    ROBO_CHECK_STR_EQ(v.api_version, "0.25.0");
    ROBO_CHECK_STR_EQ(v.generator_version, "1.22.0");
    ROBO_CHECK_STR_EQ(v.cldr_version, "46");
    ROBO_CHECK_STR_EQ(v.unicode_version, "16.0.0");
    ROBO_CHECK_STR_EQ(v.tzdb_version, "2026c");
    ROBO_CHECK(robo_hc_region_count > 100u);
}

static void test_ls_unicode_keywords(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_unicode_keywords_t kw;
    char buf[32];

    parse_tag("ur-PK-u-ca-islamic-nu-arabext-hc-h12-co-standard", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_unicode_keywords(&loc, &kw), ROBO_OK);
    ROBO_CHECK_STR_EQ(kw.calendar, "islamic");
    ROBO_CHECK_STR_EQ(kw.numbering_system, "arabext");
    ROBO_CHECK_STR_EQ(kw.hour_cycle, "h12");
    ROBO_CHECK_STR_EQ(kw.collation, "standard");

    ROBO_CHECK_EQ_INT(robo_i18n_locale_get_keyword(&loc, "nu", buf, sizeof(buf)), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "arabext");
    ROBO_CHECK_EQ_INT(robo_i18n_locale_get_keyword(&loc, "xx", buf, sizeof(buf)),
                      ROBO_NOT_FOUND);

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_set_keyword(&loc, "nu", "arab"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_unicode_keywords(&loc, &kw), ROBO_OK);
    ROBO_CHECK_STR_EQ(kw.numbering_system, "arab");
}

static void test_ls_resolve_precedence(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_overrides_t ov;
    robo_i18n_locale_settings_t st;
    char ns[16];
    char cal[32];
    char hc[8];

    parse_tag("ar-EG-u-nu-latn-ca-gregory-hc-h23", &loc);
    robo_i18n_locale_overrides_init(&ov);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve(&loc, &ov, &st), ROBO_OK);
    ROBO_CHECK_STR_EQ(st.numbering_system, "latn");
    ROBO_CHECK_EQ_INT(st.numbering_src, ROBO_I18N_SRC_EXTENSION);
    ROBO_CHECK_STR_EQ(st.calendar, "gregory");
    ROBO_CHECK_EQ_INT(st.calendar_src, ROBO_I18N_SRC_EXTENSION);
    ROBO_CHECK_STR_EQ(st.hour_cycle, "h23");
    ROBO_CHECK_EQ_INT(st.hour_cycle_src, ROBO_I18N_SRC_EXTENSION);

    ov.numbering_system = "arab";
    ov.calendar = "islamic";
    ov.hour_cycle = "h12";
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve(&loc, &ov, &st), ROBO_OK);
    ROBO_CHECK_STR_EQ(st.numbering_system, "arab");
    ROBO_CHECK_EQ_INT(st.numbering_src, ROBO_I18N_SRC_OVERRIDE);
    ROBO_CHECK_STR_EQ(st.calendar, "islamic");
    ROBO_CHECK_STR_EQ(st.hour_cycle, "h12");

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_numbering(&loc, NULL, ns, sizeof(ns)), ROBO_OK);
    ROBO_CHECK_STR_EQ(ns, "latn");
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_calendar(&loc, NULL, cal, sizeof(cal)), ROBO_OK);
    ROBO_CHECK_STR_EQ(cal, "gregorian");
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_hour_cycle(&loc, NULL, hc, sizeof(hc)), ROBO_OK);
    ROBO_CHECK_STR_EQ(hc, "h12"); /* US prefers 12-hour */

    parse_tag("de-DE", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_hour_cycle(&loc, NULL, hc, sizeof(hc)), ROBO_OK);
    ROBO_CHECK_STR_EQ(hc, "h23");
}

static void test_ls_timezone_not_locale(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_settings_t st;
    char tz[80];

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve(&loc, NULL, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.timezone_src, ROBO_I18N_SRC_UNSET);
    ROBO_CHECK(st.timezone_id[0] == '\0');
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_timezone(&loc, NULL, tz, sizeof(tz)), ROBO_OK);
    ROBO_CHECK_STR_EQ(tz, "UTC"); /* runtime fallback only */

    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_resolve_timezone(&loc, "Asia/Karachi", tz, sizeof(tz)), ROBO_OK);
    ROBO_CHECK_STR_EQ(tz, "Asia/Karachi");
}

static void test_ls_fallback_chain(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t chain[ROBO_I18N_FALLBACK_MAX];
    size_t n = 0;
    char tag[64];
    size_t tlen = 0;

    parse_tag("zh-Hans-CN", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_fallback_chain(&loc, chain, ROBO_I18N_FALLBACK_MAX, &n),
                      ROBO_OK);
    ROBO_CHECK(n >= 2u);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_format(&chain[0], tag, sizeof(tag), &tlen), ROBO_OK);
    ROBO_CHECK(strstr(tag, "zh") != NULL);
    /* Chain is finite */
    ROBO_CHECK(n <= ROBO_I18N_FALLBACK_MAX);
}

static void test_ls_negotiate(void)
{
    robo_i18n_locale_list_t *req = NULL;
    robo_i18n_locale_t available[4];
    robo_i18n_locale_t best;
    robo_i18n_match_t match = ROBO_I18N_MATCH_NONE;

    parse_tag("en", &available[0]);
    parse_tag("ur", &available[1]);
    parse_tag("ar", &available[2]);
    parse_tag("de", &available[3]);

    ROBO_CHECK_EQ_INT(robo_i18n_locale_list_parse("ur-PK, en;q=0.8", 15, &req), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_negotiate(req, available, 4, &best, &match), ROBO_OK);
    ROBO_CHECK_STR_EQ(best.language, "ur");
    ROBO_CHECK(match >= ROBO_I18N_MATCH_LANGUAGE);
    robo_i18n_locale_list_destroy(req);
}

static void test_ls_application_locale(void)
{
    robo_i18n_locale_t ur;
    robo_i18n_locale_t got;
    parse_tag("ur-PK", &ur);
    robo_i18n_clear_application_locale();
    ROBO_CHECK_EQ_INT(robo_i18n_get_application_locale(&got), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_i18n_set_application_locale(&ur), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_get_application_locale(&got), ROBO_OK);
    ROBO_CHECK(robo_i18n_locale_equal(&ur, &got));
    ROBO_CHECK_EQ_INT(robo_i18n_get_effective_locale(&got), ROBO_OK);
    ROBO_CHECK(robo_i18n_locale_equal(&ur, &got));
    robo_i18n_clear_application_locale();
}

static void test_ls_capabilities(void)
{
    robo_i18n_locale_t en;
    parse_tag("en", &en);
    ROBO_CHECK(robo_i18n_locale_supports_calendar(&en, "gregorian"));
    ROBO_CHECK(robo_i18n_locale_supports_calendar(&en, "gregory"));
    ROBO_CHECK(robo_i18n_locale_supports_numbering(&en, "latn"));
    ROBO_CHECK(robo_i18n_locale_supports_collation_type(&en, "standard"));
    ROBO_CHECK(robo_i18n_timezone_id_is_known("Asia/Karachi", 12));
    ROBO_CHECK(!robo_i18n_timezone_id_is_known("Not/AZone", 9));
}

static void test_ls_maximize_matrix(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "ta", "th", "zh", "ja", "ko", "ru",
        "fr", "de", "es", "tr", "en-Latn", "zh-Hans", "sr-Cyrl", "ar-Arab-SA",
        "hi-Deva-IN", "th-Thai-TH"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_locale_t maxed;
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_locale_maximize(&loc, &maxed), ROBO_OK);
        ROBO_CHECK(maxed.language[0] != '\0');
    }
}

static void test_ls_cross_subsystem_numbering(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t nfmt;
    robo_i18n_datetime_formatter_t dfmt;
    robo_i18n_locale_settings_t st;
    char ns[16];

    parse_tag("ar-EG-u-nu-arab", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve(&loc, NULL, &st), ROBO_OK);
    ROBO_CHECK_STR_EQ(st.numbering_system, "arab");

    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&nfmt, &loc, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(nfmt.numbering_system, "arab");

    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&dfmt, &loc, NULL, NULL, "yMd", NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(dfmt.number.numbering_system, "arab");

    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_numbering(&loc, NULL, ns, sizeof(ns)), ROBO_OK);
    ROBO_CHECK_STR_EQ(ns, nfmt.numbering_system);
    ROBO_CHECK_STR_EQ(ns, dfmt.number.numbering_system);

    robo_i18n_number_formatter_destroy(&nfmt);
    robo_i18n_datetime_formatter_destroy(&dfmt);
}

static void test_ls_collation_extension(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_collator_t *c = NULL;
    char type[16];

    /* BCP47 u-type max length is 8; CLDR short form is phonebk. */
    parse_tag("de-u-co-phonebk", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve_collation(&loc, NULL, type, sizeof(type)),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(type, "phonebk");
    ROBO_CHECK_EQ_INT(robo_i18n_collator_create(&loc, NULL, NULL, &c), ROBO_OK);
    ROBO_CHECK(c != NULL);
    robo_i18n_collator_destroy(c);
}

static void test_ls_robustness(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_settings_t st;
    robo_i18n_unicode_keywords_t kw;
    char big[512];
    size_t i;

    ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve(NULL, NULL, &st), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_unicode_keywords(NULL, &kw), ROBO_INVALID_ARGUMENT);

    memset(big, 'a', sizeof(big) - 1u);
    big[sizeof(big) - 1u] = '\0';
    ROBO_CHECK(robo_i18n_locale_parse_canonical(big, strlen(big), &loc) != ROBO_OK);

    parse_tag("en", &loc);
    for (i = 0; i < 64; i++) {
        robo_i18n_locale_t chain[ROBO_I18N_FALLBACK_MAX];
        size_t n = 0;
        ROBO_CHECK_EQ_INT(
            robo_i18n_locale_fallback_chain(&loc, chain, ROBO_I18N_FALLBACK_MAX, &n),
            ROBO_OK);
        ROBO_CHECK(n <= ROBO_I18N_FALLBACK_MAX);
    }
}

void robo_test_register_i18n_locale_service(void)
{
    robo_test_register("i18n.ls.version", test_ls_version);
    robo_test_register("i18n.ls.unicode_keywords", test_ls_unicode_keywords);
    robo_test_register("i18n.ls.resolve_precedence", test_ls_resolve_precedence);
    robo_test_register("i18n.ls.timezone_not_locale", test_ls_timezone_not_locale);
    robo_test_register("i18n.ls.fallback_chain", test_ls_fallback_chain);
    robo_test_register("i18n.ls.negotiate", test_ls_negotiate);
    robo_test_register("i18n.ls.application_locale", test_ls_application_locale);
    robo_test_register("i18n.ls.capabilities", test_ls_capabilities);
    robo_test_register("i18n.ls.maximize_matrix", test_ls_maximize_matrix);
    robo_test_register("i18n.ls.cross_subsystem_numbering", test_ls_cross_subsystem_numbering);
    robo_test_register("i18n.ls.collation_extension", test_ls_collation_extension);
    robo_test_register("i18n.ls.robustness", test_ls_robustness);
}
