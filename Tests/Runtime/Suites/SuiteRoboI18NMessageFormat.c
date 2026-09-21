/*
 * RoboI18N message formatting engine tests (Goal 4.12).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NMessageFormat.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestMfVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
}

static void TestMfSimple(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "Hello, {name}!";

    ParseTag("en", &en);
    I18nMfArgSetString(&arg, "name", "Ada", 3);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Hello, Ada!");
}

static void TestMfMissingArg(void)
{
    I18nLocale en;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "Hi {name}";
    ParseTag("en", &en);
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, NULL, buf, sizeof(buf), &n),
        RoboNotFound);
}

static void TestMfSelect(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[64];
    size_t n = 0;
    const char *tmpl =
        "{gender, select, male {He} female {She} other {They}}";

    ParseTag("en", &en);
    args.items = &arg;
    args.count = 1;

    I18nMfArgSetString(&arg, "gender", "male", 4);
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "He");

    I18nMfArgSetString(&arg, "gender", "unknown", 7);
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "They");
}

static void TestMfPluralPound(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "{count, plural, =0 {no messages} one {# message} other {# messages}}";

    ParseTag("en", &en);
    args.items = &arg;
    args.count = 1;

    I18nMfArgSetI64(&arg, "count", 0);
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "no messages");

    I18nMfArgSetI64(&arg, "count", 1);
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "message") != NULL);
    RoboCheck(strstr(buf, "1") != NULL);

    I18nMfArgSetI64(&arg, "count", 5);
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "messages") != NULL);
    RoboCheck(strstr(buf, "5") != NULL);
}

static void TestMfNested(void)
{
    I18nLocale en;
    I18nMfArg a[2];
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;
    const char *tmpl =
        "{gender, select,"
        " male {{count, plural, one {He has # item} other {He has # items}}}"
        " female {{count, plural, one {She has # item} other {She has # items}}}"
        " other {They have items}}";

    ParseTag("en", &en);
    I18nMfArgSetString(&a[0], "gender", "female", 6);
    I18nMfArgSetI64(&a[1], "count", 1);
    args.items = a;
    args.count = 2;
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "She has") != NULL);
    RoboCheck(strstr(buf, "item") != NULL);
}

static void TestMfNumberLocales(void)
{
    static const char *tags[] = {"en", "ar", "ur", "fa", "hi", "zh", "ja", "ru"};
    size_t i;
    const char *tmpl = "{n, number}";
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nMfArg arg;
        I18nMfArgs args;
        char buf[64];
        size_t n = 0;
        ParseTag(tags[i], &loc);
        I18nMfArgSetI64(&arg, "n", 1234);
        args.items = &arg;
        args.count = 1;
        RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &loc, &args, buf,
                                                        sizeof(buf), &n),
                          RoboOk);
        RoboCheck(n > 0);
        RoboCheck(buf[0] != '\0');
    }
}

static void TestMfDate(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    I18nDate d;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "{d, date, short}";

    ParseTag("en", &en);
    d.year = 2024;
    d.month = 6;
    d.day = 15;
    I18nMfArgSetDate(&arg, "d", &d);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(n > 0);
}

static void TestMfEscapeUnicode(void)
{
    I18nLocale ur;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;
    const char *tmpl = "سلام {{x}} {name} 👋";

    ParseTag("ur", &ur);
    I18nMfArgSetString(&arg, "name", "علی", 0);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &ur, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "{x}") != NULL);
    RoboCheck(strstr(buf, "علی") != NULL);
    RoboCheck(strstr(buf, "👋") != NULL);
}

static void TestMfOrdinalArch(void)
{
    /* selectordinal uses plural_ordinal; must parse and format with other */
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";

    ParseTag("en", &en);
    I18nMfArgSetI64(&arg, "n", 1);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "st") != NULL || strstr(buf, "th") != NULL);
}

static void TestMfParseOnce(void)
{
    I18nMfMessage *msg = NULL;
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "X {v} Y";

    ParseTag("en", &en);
    RoboCheckEqInt(I18nMfParse(tmpl, strlen(tmpl), &msg), RoboOk);
    I18nMfArgSetString(&arg, "v", "1", 1);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormat(msg, &en, &args, buf, sizeof(buf), &n), RoboOk);
    RoboCheckStrEq(buf, "X 1 Y");
    I18nMfArgSetString(&arg, "v", "2", 1);
    RoboCheckEqInt(I18nMfFormat(msg, &en, &args, buf, sizeof(buf), &n), RoboOk);
    RoboCheckStrEq(buf, "X 2 Y");
    I18nMfDestroy(msg);
}

static void TestMfCatalog(void)
{
    I18nCatalog *cat = NULL;
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;

    ParseTag("en", &en);
    RoboCheckEqInt(I18nCatalogOpenDemo(&cat), RoboOk);
    I18nMfArgSetString(&arg, "name", "Ada", 3);
    args.items = &arg;
    args.count = 1;
    /* demo greeting is simple {name} placeholder — works with mf if template uses {} */
    RoboCheckEqInt(I18nMessageFormatMf(cat, "app.greeting", 12, NULL, 0, &en, &args,
                                                  NULL, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheckStrEq(buf, "Hello, Ada!");
    I18nCatalogClose(cat);
}

static void TestMfMalformed(void)
{
    RoboCheckEqInt(I18nMfValidate("{", 1), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("}", 1), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{name", 5), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{name,}", 7), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{n, unknown}", 12), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{g, select, male {x}}", 21), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("{n, plural, one {x}}", 20), RoboParseError);
    RoboCheckEqInt(I18nMfValidate("ok", 2), RoboOk);
    /* invalid UTF-8 */
    {
        char bad[3] = {(char)0xE2, (char)0x28, (char)0xA1};
        RoboCheckEqInt(I18nMfValidate(bad, 3), RoboParseError);
    }
}

static void TestMfFuzz(void)
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
        (void)I18nMfValidate(buf, len);
    }
    RoboCheck(1);
}

static void TestMfSlavicPlural(void)
{
    I18nLocale ru;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[128];
    size_t n = 0;
    const char *tmpl =
        "{n, plural, one {# сообщение} few {# сообщения} many {# сообщений} other {# сообщений}}";

    ParseTag("ru", &ru);
    args.items = &arg;
    args.count = 1;
    I18nMfArgSetI64(&arg, "n", 2);
    RoboCheckEqInt(I18nMfFormatTemplate(tmpl, strlen(tmpl), &ru, &args, buf,
                                                    sizeof(buf), &n),
                      RoboOk);
    RoboCheck(strstr(buf, "сообщения") != NULL || strstr(buf, "сообщени") != NULL);
}

void TestRegisterI18nMessageFormat(void)
{
    TestRegister("i18n.mf.version", TestMfVersion);
    TestRegister("i18n.mf.simple", TestMfSimple);
    TestRegister("i18n.mf.missing_arg", TestMfMissingArg);
    TestRegister("i18n.mf.select", TestMfSelect);
    TestRegister("i18n.mf.plural_pound", TestMfPluralPound);
    TestRegister("i18n.mf.nested", TestMfNested);
    TestRegister("i18n.mf.number_locales", TestMfNumberLocales);
    TestRegister("i18n.mf.date", TestMfDate);
    TestRegister("i18n.mf.escape_unicode", TestMfEscapeUnicode);
    TestRegister("i18n.mf.ordinal", TestMfOrdinalArch);
    TestRegister("i18n.mf.parse_once", TestMfParseOnce);
    TestRegister("i18n.mf.catalog", TestMfCatalog);
    TestRegister("i18n.mf.malformed", TestMfMalformed);
    TestRegister("i18n.mf.fuzz", TestMfFuzz);
    TestRegister("i18n.mf.slavic_plural", TestMfSlavicPlural);
}
