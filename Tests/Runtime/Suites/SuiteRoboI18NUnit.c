/*
 * RoboI18N unit formatting + relative ±2 suite (Goal 4.19).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboUnitData.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectUnit(const char *tag,
                        I18nUnitWidth width,
                        int64_t value,
                        I18nUnitId unit,
                        const char *want)
{
    I18nLocale loc;
    I18nUnitFormatter fmt;
    I18nUnitOptions opts;
    char buf[192];
    size_t len = 0;

    ParseTag(tag, &loc);
    I18nUnitOptionsInit(&opts);
    opts.width = width;
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nUnitFormatI64(&fmt, value, unit, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, want);
    I18nUnitFormatterDestroy(&fmt);
}

static void TestUnitVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nUnitCldrVersionString(), "46");
    RoboCheckStrEq(RoboUnitCldrVersion, "46");
    RoboCheckEqInt((int)RoboI18nUnitCount, (int)RoboUnitIdCount);
    RoboCheck(UnitRowsCount > 1000u);
}

static void TestUnitEnStyles(void)
{
    ExpectUnit("en", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter, "1 meter");
    ExpectUnit("en", RoboI18nUnitWidthLong, 2, RoboI18nUnitLengthMeter, "2 meters");
    ExpectUnit("en", RoboI18nUnitWidthShort, 1, RoboI18nUnitLengthMeter, "1 m");
    ExpectUnit("en", RoboI18nUnitWidthNarrow, 5, RoboI18nUnitLengthMeter, "5m");
    ExpectUnit("en", RoboI18nUnitWidthShort, 90, RoboI18nUnitSpeedKilometerPerHour,
                "90 km/h");
    ExpectUnit("en", RoboI18nUnitWidthLong, 7,
                RoboI18nUnitConsumptionLiterPer100Kilometer,
                "7 liters per 100 kilometers");
}

static void TestUnitLocales(void)
{
    /* French long meter uses NBSP (U+00A0) before unit. */
    ExpectUnit("fr", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter,
                "1\xC2\xA0m\xC3\xA8tre");
    ExpectUnit("de", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter, "1 Meter");
    ExpectUnit("es", RoboI18nUnitWidthLong, 2, RoboI18nUnitLengthMeter, "2 metros");
    /* Arabic: cardinal one drops the number placeholder. */
    ExpectUnit("ar", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter, "\xD9\x85\xD8\xAA\xD8\xB1");
    ExpectUnit("ru", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter,
                "1 \xD0\xBC\xD0\xB5\xD1\x82\xD1\x80");
    ExpectUnit("pl", RoboI18nUnitWidthLong, 2, RoboI18nUnitLengthMeter, "2 metry");
    ExpectUnit("ja", RoboI18nUnitWidthLong, 3, RoboI18nUnitLengthMeter,
                "3 \xE3\x83\xA1\xE3\x83\xBC\xE3\x83\x88\xE3\x83\xAB");
    ExpectUnit("zh", RoboI18nUnitWidthLong, 3, RoboI18nUnitLengthMeter,
                "3\xE7\xB1\xB3");
    ExpectUnit("ko", RoboI18nUnitWidthShort, 3, RoboI18nUnitLengthMeter, "3m");
    ExpectUnit("hi", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter,
                "1 \xE0\xA4\xAE\xE0\xA5\x80\xE0\xA4\x9F\xE0\xA4\xB0");
}

static void TestUnitCompoundPer(void)
{
    I18nLocale loc;
    I18nUnitFormatter fmt;
    I18nUnitOptions opts;
    char buf[192];
    size_t len = 0;

    ParseTag("en", &loc);
    I18nUnitOptionsInit(&opts);
    opts.width = RoboI18nUnitWidthShort;
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nUnitFormatPerI64(&fmt, 5, RoboI18nUnitDigitalMegabyte,
                                                    RoboI18nUnitDurationSecond, buf,
                                                    sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "5 MB/s");
    I18nUnitFormatterDestroy(&fmt);

    I18nUnitOptionsInit(&opts);
    opts.width = RoboI18nUnitWidthLong;
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nUnitFormatPerI64(&fmt, 5, RoboI18nUnitDigitalMegabyte,
                                                    RoboI18nUnitDurationSecond, buf,
                                                    sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "5 megabytes per second");
    I18nUnitFormatterDestroy(&fmt);

    ParseTag("fr", &loc);
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nUnitFormatPerI64(&fmt, 5, RoboI18nUnitDigitalMegabyte,
                                                    RoboI18nUnitDurationSecond, buf,
                                                    sizeof(buf), &len),
                      RoboOk);
    RoboCheck(len > 0);
    RoboCheck(strstr(buf, "par seconde") != NULL || strstr(buf, "/s") != NULL);
    I18nUnitFormatterDestroy(&fmt);
}

static void TestUnitConvert(void)
{
    int64_t out = 0;
    int approx = 0;
    I18nDecimal din;
    I18nDecimal dout;

    RoboCheckEqInt(I18nUnitConvertI64(RoboI18nUnitLengthKilometer,
                                                 RoboI18nUnitLengthMeter, 2, &out, &approx),
                      RoboOk);
    RoboCheckEqInt((int)out, 2000);
    RoboCheckEqInt(approx, 0);

    RoboCheckEqInt(I18nUnitConvertI64(RoboI18nUnitLengthMeter,
                                                 RoboI18nUnitLengthCentimeter, 3, &out,
                                                 &approx),
                      RoboOk);
    RoboCheckEqInt((int)out, 300);

    RoboCheckEqInt(I18nDecimalFromI64(100, &din), RoboOk);
    RoboCheckEqInt(I18nUnitConvert(RoboI18nUnitTemperatureCelsius,
                                             RoboI18nUnitTemperatureFahrenheit, &din, &dout,
                                             &approx),
                      RoboOk);
    RoboCheckEqInt(approx, 1);

    RoboCheck(I18nUnitConvertI64(RoboI18nUnitLengthMeter, RoboI18nUnitMassGram,
                                          1, &out, &approx) != RoboOk);
}

static void TestUnitIdsAndFallback(void)
{
    I18nUnitId id = RoboI18nUnitCount;
    I18nLocale loc;
    RoboCheckEqInt(I18nUnitIdFromCldrKey("length-meter", &id), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboI18nUnitLengthMeter);
    RoboCheckStrEq(I18nUnitCldrKey(RoboI18nUnitLengthMeter), "length-meter");
    RoboCheck(I18nUnitIdFromCldrKey("not-a-unit", &id) != RoboOk);

    ParseTag("xx-YY", &loc);
    RoboCheck(I18nUnitHasPatterns(&loc, RoboI18nUnitLengthMeter,
                                           RoboI18nUnitWidthLong));
    ExpectUnit("xx-YY", RoboI18nUnitWidthLong, 1, RoboI18nUnitLengthMeter, "1 meter");
}

static void TestUnitIntlSmoke(void)
{
    static const char *tags[] = {"en", "fr", "de", "es", "ru", "pl", "ar", "fa", "ur",
                                 "he", "hi", "bn", "th", "zh", "ja", "ko", "it"};
    I18nLocale loc;
    I18nUnitFormatter fmt;
    char buf[160];
    size_t len = 0;
    size_t i;

    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, NULL), RoboOk);
        RoboCheckEqInt(I18nUnitFormatI64(&fmt, 0, RoboI18nUnitLengthMeter, buf,
                                                    sizeof(buf), &len),
                          RoboOk);
        RoboCheck(len > 0);
        RoboCheckEqInt(I18nUnitFormatI64(&fmt, -3, RoboI18nUnitMassKilogram, buf,
                                                    sizeof(buf), &len),
                          RoboOk);
        RoboCheck(len > 0);
        RoboCheckEqInt(
            I18nUnitFormatI64(&fmt, 1000000, RoboI18nUnitDigitalMegabyte, buf,
                                      sizeof(buf), &len),
            RoboOk);
        I18nUnitFormatterDestroy(&fmt);
    }
}

static void TestUnitFuzz(void)
{
    I18nLocale loc;
    I18nUnitFormatter fmt;
    char buf[128];
    size_t len = 0;
    int u;
    int64_t v;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, NULL), RoboOk);
    for (u = 0; u < (int)RoboI18nUnitCount; u++) {
        for (v = -5; v <= 5; v++) {
            RoboCheckEqInt(I18nUnitFormatI64(&fmt, v, (I18nUnitId)u, buf,
                                                        sizeof(buf), &len),
                              RoboOk);
        }
    }
    RoboCheck(I18nUnitFormatI64(&fmt, 1, (I18nUnitId)999, buf, sizeof(buf),
                                         &len) != RoboOk);
    RoboCheck(I18nUnitFormatPerI64(&fmt, 1, RoboI18nUnitLengthMeter,
                                             (I18nUnitId)999, buf, sizeof(buf),
                                             &len) != RoboOk);
    I18nUnitFormatterDestroy(&fmt);
}

static void TestRelSpecialPm2(void)
{
    I18nLocale loc;
    I18nRelativeFormatter fmt;
    I18nRelativeOptions opts;
    char buf[96];
    size_t len = 0;

    /* English has no ±2 keywords → numeric past/future. */
    ParseTag("en", &loc);
    I18nRelativeOptionsInit(&opts);
    opts.numeric = RoboI18nRelNumericAuto;
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, -2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "2 days ago");
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, 2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "in 2 days");
    I18nRelativeFormatterDestroy(&fmt);

    ParseTag("es", &loc);
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, -2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "anteayer");
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, 2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "pasado ma\xC3\xB1""ana");
    I18nRelativeFormatterDestroy(&fmt);

    ParseTag("de", &loc);
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, -2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "vorgestern");
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, 2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "\xC3\xBC""bermorgen");
    I18nRelativeFormatterDestroy(&fmt);

    ParseTag("fr", &loc);
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, -2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "avant-hier");
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, 2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "apr\xC3\xA8""s-demain");
    I18nRelativeFormatterDestroy(&fmt);

    ParseTag("pl", &loc);
    RoboCheckEqInt(I18nRelativeFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, -2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "przedwczoraj");
    RoboCheckEqInt(
        I18nRelativeTimeFormatUnit(&fmt, 2, RoboI18nRelDay, buf, sizeof(buf), &len),
        RoboOk);
    RoboCheckStrEq(buf, "pojutrze");
    I18nRelativeFormatterDestroy(&fmt);
}

void TestRegisterI18nUnit(void)
{
    TestRegister("i18n.unit.versions", TestUnitVersions);
    TestRegister("i18n.unit.en_styles", TestUnitEnStyles);
    TestRegister("i18n.unit.locales", TestUnitLocales);
    TestRegister("i18n.unit.compound_per", TestUnitCompoundPer);
    TestRegister("i18n.unit.convert", TestUnitConvert);
    TestRegister("i18n.unit.ids_fallback", TestUnitIdsAndFallback);
    TestRegister("i18n.unit.intl_smoke", TestUnitIntlSmoke);
    TestRegister("i18n.unit.fuzz", TestUnitFuzz);
    TestRegister("i18n.unit.rel_special_pm2", TestRelSpecialPm2);
}
