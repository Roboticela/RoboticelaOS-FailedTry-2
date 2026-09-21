/*
 * RoboI18N message catalogs (Goal 4.11).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboMsgData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestMsgVersions(void)
{
    I18nCatalog *cat = NULL;
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    RoboCheck(cat != NULL);
    RoboCheckStrEq(I18nCatalogId(cat), "robo.demo");
    RoboCheckStrEq(I18nCatalogVersion(cat), "1.0.0");
    RoboCheckEqInt((int)I18nCatalogFormatVersion(cat), 1);
    I18nCatalogClose(cat);
    RoboCheck(MsgEntryCount >= 100u);
}

static void TestMsgLookupFormat(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale en;
    I18nLocale fr;
    I18nMsgVar vars[1];
    char buf[256];
    size_t n = 0;

    ParseTag("en", &en);
    ParseTag("fr", &fr);
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);

    RoboCheckEqInt(I18nMessageGet(cat, "app.login.title", 15, NULL, 0, &en, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Sign in");

    RoboCheckEqInt(I18nMessageGet(cat, "app.login.title", 15, NULL, 0, &fr, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Connexion");

    memset(&vars[0], 0, sizeof(vars[0]));
    strncpy(vars[0].name, "name", sizeof(vars[0].name) - 1);
    vars[0].value = "Ada";
    RoboCheckEqInt(I18nMessageFormatId(cat, "app.greeting", 12, NULL, 0, &en, NULL,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Hello, Ada!");

    RoboCheckEqInt(I18nMessageFormatId(cat, "app.greeting", 12, NULL, 0, &fr, NULL,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "Ada") != NULL);
    RoboCheck(strstr(buf, "Bonjour") != NULL);

    I18nCatalogClose(cat);
}

static void TestMsgContext(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale en;
    char buf[64];
    size_t n = 0;
    ParseTag("en", &en);
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    RoboCheckEqInt(I18nMessageGet(cat, "app.action.open", 15, "file", 4, &en, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Open");
    RoboCheck(I18nMessageExists(cat, "app.action.open", 15, "door", 4, &en, NULL));
    RoboCheck(!I18nMessageExists(cat, "app.action.open", 15, "window", 6, &en, NULL));
    I18nCatalogClose(cat);
}

static void TestMsgPlural(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale en;
    I18nDecimal one;
    I18nDecimal many;
    I18nMsgVar vars[1];
    char buf[128];
    size_t n = 0;

    ParseTag("en", &en);
    RoboCheckEqInt(I18nDecimalFromI64(1, &one), RoboOk);
    RoboCheckEqInt(I18nDecimalFromI64(5, &many), RoboOk);
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);

    memset(&vars[0], 0, sizeof(vars[0]));
    strncpy(vars[0].name, "count", sizeof(vars[0].name) - 1);
    vars[0].value = "1";
    RoboCheckEqInt(I18nMessageFormatId(cat, "app.items", 9, NULL, 0, &en, &one,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "1 item");

    vars[0].value = "5";
    RoboCheckEqInt(I18nMessageFormatId(cat, "app.items", 9, NULL, 0, &en, &many,
                                                  vars, 1, NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "5 items");
    I18nCatalogClose(cat);
}

static void TestMsgFallback(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale frCa;
    I18nLocale xx;
    I18nMsgOptions opt;
    char buf[128];
    size_t n = 0;

    ParseTag("fr-CA", &frCa);
    ParseTag("xx", &xx);
    I18nMsgOptionsInit(&opt);
    strncpy(opt.fallbackLocale, "en", sizeof(opt.fallbackLocale) - 1);

    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);

    /* Regional missing → language fr */
    RoboCheckEqInt(I18nMessageGet(cat, "app.login.title", 15, NULL, 0, &frCa, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Connexion");

    /* Unknown language → configured fallback en */
    RoboCheckEqInt(I18nMessageGet(cat, "app.login.title", 15, NULL, 0, &xx, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Sign in");

    opt.missing = RoboI18nMsgMissNotFound;
    opt.fallbackLocale[0] = '\0';
    RoboCheckEqInt(I18nMessageGet(cat, "app.login.title", 15, NULL, 0, &xx, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      RoboNotFound);

    opt.missing = RoboI18nMsgMissReturnId;
    RoboCheckEqInt(I18nMessageGet(cat, "app.no.such", 11, NULL, 0, &frCa, NULL,
                                            &opt, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "app.no.such");

    I18nCatalogClose(cat);
}

static void TestMsgIntlScripts(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "de", "fr", "es",
        "tr"
    };
    I18nCatalog *cat = NULL;
    size_t i;
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        char buf[256];
        size_t n = 0;
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nMessageGet(cat, "app.settings.language", 21, NULL, 0,
                                                &loc, NULL, NULL, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(buf[0] != '\0');
        RoboCheckEqInt(I18nMessageGet(cat, "app.welcome_emoji", 17, NULL, 0, &loc,
                                                NULL, NULL, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(strstr(buf, "👋") != NULL || n > 0);
        RoboCheckEqInt(I18nMessageGet(cat, "app.combining", 13, NULL, 0, &loc, NULL,
                                                NULL, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(n >= 5);
    }
    I18nCatalogClose(cat);
}

static void TestMsgPlaceholdersEscape(void)
{
    I18nMsgVar vars[1];
    char buf[64];
    size_t n = 0;
    memset(&vars[0], 0, sizeof(vars[0]));
    strncpy(vars[0].name, "x", sizeof(vars[0].name) - 1);
    vars[0].value = "Y";
    RoboCheckEqInt(
        I18nMessageFormat("a {{b}} {x} c", 13, vars, 1, NULL, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "a {b} Y c");
}

static void TestMsgBlobLoad(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale en;
    char buf[64];
    size_t n = 0;
    uint8_t bad[8] = {0};

    ParseTag("en", &en);
    RoboCheckEqInt(
        I18nCatalogLoad(MsgDemoBlob, MsgDemoBlobSize, &cat), RoboOk);
    RoboCheckStrEq(I18nCatalogId(cat), "robo.demo");
    RoboCheckEqInt(I18nMessageGet(cat, "app.login.title", 15, NULL, 0, &en, NULL,
                                            NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Sign in");
    I18nCatalogClose(cat);

    RoboCheckEqInt(I18nCatalogLoad(bad, sizeof(bad), &cat), RoboParseError);
    RoboCheckEqInt(I18nCatalogLoad(NULL, 0, &cat), RoboInvalidArgument);
}

static void TestMsgFuzz(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale en;
    char buf[64];
    size_t n = 0;
    uint32_t seed = 0x411u;
    int i;
    uint8_t junk[64];

    ParseTag("en", &en);
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    for (i = 0; i < 40; i++) {
        I18nCatalog *loaded = NULL;
        char id[16];
        size_t len = (seed % 8u) + 1u;
        size_t j;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            id[j] = (char)('a' + ((seed >> (j % 8)) % 26u));
            seed = seed * 1664525u + 1013904223u;
        }
        (void)I18nMessageGet(cat, id, len, NULL, 0, &en, NULL, NULL, buf, sizeof(buf),
                                    &n);
        junk[i % sizeof(junk)] = (uint8_t)(seed & 0xffu);
        if (I18nCatalogLoad(junk, sizeof(junk), &loaded) == RoboOk) {
            I18nCatalogClose(loaded);
        }
    }
    RoboCheckEqInt(I18nMessageFormat("{", 1, NULL, 0, NULL, buf, sizeof(buf), &n),
                      RoboParseError);
    I18nCatalogClose(cat);
}

void TestRegisterI18nMessage(void)
{
    TestRegister("i18n.msg.versions", TestMsgVersions);
    TestRegister("i18n.msg.lookup_format", TestMsgLookupFormat);
    TestRegister("i18n.msg.context", TestMsgContext);
    TestRegister("i18n.msg.plural", TestMsgPlural);
    TestRegister("i18n.msg.fallback", TestMsgFallback);
    TestRegister("i18n.msg.intl_scripts", TestMsgIntlScripts);
    TestRegister("i18n.msg.placeholders", TestMsgPlaceholdersEscape);
    TestRegister("i18n.msg.blob_load", TestMsgBlobLoad);
    TestRegister("i18n.msg.fuzz", TestMsgFuzz);
}
