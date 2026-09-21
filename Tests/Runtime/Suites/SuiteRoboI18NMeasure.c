/*
 * RoboI18N measurement preferences suite (Goal 4.20).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboMeasurePrefData.h"
#include "RoboTest.h"

#include <math.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectSystem(const char *tag, I18nMeasurement want)
{
    I18nLocale loc;
    I18nMeasurePrefs prefs;
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheckEqInt((int)I18nMeasurePrefsSystem(&prefs), (int)want);
}

static void ExpectUnit(const char *tag,
                        I18nMeasureCategory cat,
                        I18nMeasureUsage usage,
                        I18nUnitId want)
{
    I18nLocale loc;
    I18nMeasurePrefs prefs;
    I18nUnitId got = RoboI18nUnitCount;
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheckEqInt(I18nMeasurePreferredUnitUsage(&prefs, cat, usage, &got), RoboOk);
    RoboCheckEqInt((int)got, (int)want);
}

static void TestMeasVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nMeasureCldrVersionString(), "46");
    RoboCheckStrEq(RoboMprefCldrVersion, "46");
    RoboCheck(MprefUnitsCount > 20u);
}

static void TestMeasSystemsMatrix(void)
{
    ExpectSystem("en-US", RoboI18nMeasureUs);
    ExpectSystem("en-GB", RoboI18nMeasureUk);
    ExpectSystem("en-CA", RoboI18nMeasureMetric);
    ExpectSystem("en-AU", RoboI18nMeasureMetric);
    ExpectSystem("fr-FR", RoboI18nMeasureMetric);
    ExpectSystem("de-DE", RoboI18nMeasureMetric);
    ExpectSystem("es-ES", RoboI18nMeasureMetric);
    ExpectSystem("ru-RU", RoboI18nMeasureMetric);
    ExpectSystem("ar-SA", RoboI18nMeasureMetric);
    ExpectSystem("fa-IR", RoboI18nMeasureMetric);
    ExpectSystem("ur-PK", RoboI18nMeasureMetric);
    ExpectSystem("hi-IN", RoboI18nMeasureMetric);
    ExpectSystem("bn-BD", RoboI18nMeasureMetric);
    ExpectSystem("zh-CN", RoboI18nMeasureMetric);
    ExpectSystem("zh-TW", RoboI18nMeasureMetric);
    ExpectSystem("ja-JP", RoboI18nMeasureMetric);
    ExpectSystem("ko-KR", RoboI18nMeasureMetric);
}

static void TestMeasLanguageVsRegion(void)
{
    /* Same language, different regions. */
    ExpectUnit("en-US", RoboI18nMeasureCatTemperature, RoboI18nMeasureUsageDefault,
                RoboI18nUnitTemperatureFahrenheit);
    ExpectUnit("en-GB", RoboI18nMeasureCatTemperature, RoboI18nMeasureUsageDefault,
                RoboI18nUnitTemperatureCelsius);
    ExpectUnit("en-CA", RoboI18nMeasureCatTemperature, RoboI18nMeasureUsageDefault,
                RoboI18nUnitTemperatureCelsius);
    ExpectUnit("en-AU", RoboI18nMeasureCatTemperature, RoboI18nMeasureUsageDefault,
                RoboI18nUnitTemperatureCelsius);

    ExpectUnit("en-US", RoboI18nMeasureCatLength, RoboI18nMeasureUsageDefault,
                RoboI18nUnitLengthMile);
    ExpectUnit("en-GB", RoboI18nMeasureCatLength, RoboI18nMeasureUsageDefault,
                RoboI18nUnitLengthMile);
    ExpectUnit("en-CA", RoboI18nMeasureCatLength, RoboI18nMeasureUsageDefault,
                RoboI18nUnitLengthKilometer);
    ExpectUnit("fr-FR", RoboI18nMeasureCatLength, RoboI18nMeasureUsageDefault,
                RoboI18nUnitLengthKilometer);

    ExpectUnit("en-US", RoboI18nMeasureCatVolume, RoboI18nMeasureUsageDefault,
                RoboI18nUnitVolumeGallon);
    ExpectUnit("fr-FR", RoboI18nMeasureCatVolume, RoboI18nMeasureUsageDefault,
                RoboI18nUnitVolumeLiter);

    ExpectUnit("en-US", RoboI18nMeasureCatSpeed, RoboI18nMeasureUsageDefault,
                RoboI18nUnitSpeedMilePerHour);
    ExpectUnit("de-DE", RoboI18nMeasureCatSpeed, RoboI18nMeasureUsageDefault,
                RoboI18nUnitSpeedKilometerPerHour);

    ExpectUnit("en-US", RoboI18nMeasureCatMass, RoboI18nMeasureUsageDefault,
                RoboI18nUnitMassPound);
    ExpectUnit("ja-JP", RoboI18nMeasureCatMass, RoboI18nMeasureUsageDefault,
                RoboI18nUnitMassKilogram);
}

static void TestMeasOverridesAndExplicit(void)
{
    I18nLocale loc;
    I18nMeasurePrefs prefs;
    I18nUnitId got = RoboI18nUnitCount;
    I18nUnitId explicitU = RoboI18nUnitTemperatureKelvin;
    I18nMeasureSource src = RoboI18nMeasureSrcSystemDefault;

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);

    RoboCheckEqInt(I18nMeasurePrefsSetUnit(&prefs, RoboI18nMeasureCatTemperature,
                                                       RoboI18nUnitTemperatureCelsius),
                      RoboOk);
    RoboCheckEqInt(I18nMeasureResolveUnit(&prefs, RoboI18nMeasureCatTemperature,
                                                     RoboI18nMeasureUsageDefault, NULL, &got,
                                                     &src),
                      RoboOk);
    RoboCheckEqInt((int)got, (int)RoboI18nUnitTemperatureCelsius);
    RoboCheckEqInt((int)src, (int)RoboI18nMeasureSrcCategoryOverride);

    RoboCheckEqInt(I18nMeasureResolveUnit(&prefs, RoboI18nMeasureCatTemperature,
                                                     RoboI18nMeasureUsageDefault, &explicitU,
                                                     &got, &src),
                      RoboOk);
    RoboCheckEqInt((int)got, (int)RoboI18nUnitTemperatureKelvin);
    RoboCheckEqInt((int)src, (int)RoboI18nMeasureSrcExplicit);

    RoboCheckEqInt(I18nMeasurePrefsClearUnit(&prefs, RoboI18nMeasureCatTemperature),
                      RoboOk);
    RoboCheckEqInt(I18nMeasurePrefsSetSystem(&prefs, RoboI18nMeasureMetric),
                      RoboOk);
    RoboCheckEqInt((int)I18nMeasurePrefsSystem(&prefs), (int)RoboI18nMeasureMetric);

    /* Wrong quantity override rejected. */
    RoboCheck(I18nMeasurePrefsSetUnit(&prefs, RoboI18nMeasureCatTemperature,
                                                RoboI18nUnitLengthMeter) != RoboOk);
}

static void TestMeasUnicodeMs(void)
{
    I18nLocale loc;
    I18nMeasurePrefs prefs;
    I18nUnitId got = RoboI18nUnitCount;

    ParseTag("en-US-u-ms-metric", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheckEqInt((int)I18nMeasurePrefsSystem(&prefs), (int)RoboI18nMeasureMetric);
    /* Preference lists are still region-US; length stays mile from CLDR prefs. */
    RoboCheckEqInt(I18nMeasurePreferredUnit(&prefs, RoboI18nMeasureCatLength, &got),
                      RoboOk);
    RoboCheckEqInt((int)got, (int)RoboI18nUnitLengthMile);

    ParseTag("fr-FR-u-ms-ussystem", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheckEqInt((int)I18nMeasurePrefsSystem(&prefs), (int)RoboI18nMeasureUs);
}

static void TestMeasConvertFormat(void)
{
    I18nLocale loc;
    I18nMeasurePrefs prefs;
    I18nUnitFormatter fmt;
    I18nUnitOptions opts;
    char buf[128];
    size_t len = 0;
    I18nDecimal din;
    I18nDecimal dout;
    int approx = 0;
    double f;

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    I18nUnitOptionsInit(&opts);
    opts.width = RoboI18nUnitWidthShort;
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, &opts), RoboOk);

    /* 0 °C → Fahrenheit preference */
    RoboCheckEqInt(I18nMeasureFormatConvertedI64(
                          &prefs, &fmt, RoboI18nMeasureCatTemperature,
                          RoboI18nMeasureUsageDefault, 0, RoboI18nUnitTemperatureCelsius,
                          NULL, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "32\xC2\xB0""F");

    /* Explicit Kelvin must not be overridden */
    {
        I18nUnitId kelvin = RoboI18nUnitTemperatureKelvin;
        RoboCheckEqInt(I18nMeasureFormatConvertedI64(
                              &prefs, &fmt, RoboI18nMeasureCatTemperature,
                              RoboI18nMeasureUsageDefault, 0, RoboI18nUnitTemperatureCelsius,
                              &kelvin, buf, sizeof(buf), &len),
                          RoboOk);
        RoboCheck(strstr(buf, "K") != NULL);
    }

    I18nUnitFormatterDestroy(&fmt);

    /* Conversion math checks */
    RoboCheckEqInt(I18nDecimalFromI64(100, &din), RoboOk);
    RoboCheckEqInt(I18nUnitConvert(RoboI18nUnitTemperatureCelsius,
                                             RoboI18nUnitTemperatureFahrenheit, &din, &dout,
                                             &approx),
                      RoboOk);
    f = 0.0;
    {
        size_t i;
        for (i = 0; i < dout.digitCount; i++) {
            f = f * 10.0 + (double)(dout.digits[i] - '0');
        }
        for (i = 0; i < dout.scale; i++) {
            f /= 10.0;
        }
        if (dout.negative) {
            f = -f;
        }
    }
    RoboCheck(fabs(f - 212.0) < 0.05);

    {
        int64_t out = 0;
        RoboCheckEqInt(I18nUnitConvertI64(RoboI18nUnitLengthKilometer,
                                                     RoboI18nUnitLengthMeter, 5, &out, &approx),
                          RoboOk);
        RoboCheckEqInt((int)out, 5000);
        RoboCheckEqInt(approx, 0);
    }

    ParseTag("en-GB", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nMeasureFormatConvertedI64(
                          &prefs, &fmt, RoboI18nMeasureCatLength,
                          RoboI18nMeasureUsageDefault, 1609, RoboI18nUnitLengthMeter, NULL,
                          buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheck(len > 0);
    I18nUnitFormatterDestroy(&fmt);
}

static void TestMeasIntlSmoke(void)
{
    static const char *tags[] = {"en-US", "en-GB", "en-CA", "en-AU", "fr-FR", "de-DE",
                                 "es-ES", "ru-RU", "ar-SA", "fa-IR", "ur-PK",  "hi-IN",
                                 "bn-BD", "zh-CN", "zh-TW", "ja-JP", "ko-KR"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nMeasurePrefs prefs;
        I18nUnitId u = RoboI18nUnitCount;
        int c;
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
        for (c = 0; c < (int)RoboI18nMeasureCatCount; c++) {
            RoboCheckEqInt(I18nMeasurePreferredUnit(
                                  &prefs, (I18nMeasureCategory)c, &u),
                              RoboOk);
            RoboCheck((unsigned)u < (unsigned)RoboI18nUnitCount);
        }
    }
}

static void TestMeasFuzz(void)
{
    I18nLocale loc;
    I18nMeasurePrefs prefs;
    I18nUnitId u = RoboI18nUnitCount;
    char buf[64];
    size_t len = 0;

    RoboCheck(I18nMeasurePrefsInit(NULL, NULL) != RoboOk);
    ParseTag("en", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheck(I18nMeasurePreferredUnit(&prefs, (I18nMeasureCategory)99, &u) !=
               RoboOk);
    RoboCheck(I18nMeasurePrefsSetSystem(&prefs, (I18nMeasurement)9) != RoboOk);
    RoboCheck(I18nLocaleParseCanonical("!!!", 3, &loc) != RoboOk);

    ParseTag("xx-ZZ", &loc);
    RoboCheckEqInt(I18nMeasurePrefsInit(&prefs, &loc), RoboOk);
    RoboCheckEqInt(I18nMeasurePreferredUnit(&prefs, RoboI18nMeasureCatLength, &u),
                      RoboOk);

    {
        I18nUnitFormatter fmt;
        RoboCheckEqInt(I18nUnitFormatterInit(&fmt, &prefs.locale, NULL), RoboOk);
        RoboCheckEqInt(I18nMeasureFormatConvertedI64(
                              &prefs, &fmt, RoboI18nMeasureCatMass,
                              RoboI18nMeasureUsageDefault, 0, RoboI18nUnitMassKilogram,
                              NULL, buf, sizeof(buf), &len),
                          RoboOk);
        I18nUnitFormatterDestroy(&fmt);
    }

    RoboCheckStrEq(I18nMeasureCategoryName(RoboI18nMeasureCatSpeed), "speed");
    RoboCheckStrEq(I18nMeasureUsageName(RoboI18nMeasureUsageFluid), "fluid");
}

void TestRegisterI18nMeasure(void)
{
    TestRegister("i18n.measure.versions", TestMeasVersions);
    TestRegister("i18n.measure.systems_matrix", TestMeasSystemsMatrix);
    TestRegister("i18n.measure.lang_vs_region", TestMeasLanguageVsRegion);
    TestRegister("i18n.measure.overrides", TestMeasOverridesAndExplicit);
    TestRegister("i18n.measure.unicode_ms", TestMeasUnicodeMs);
    TestRegister("i18n.measure.convert_format", TestMeasConvertFormat);
    TestRegister("i18n.measure.intl_smoke", TestMeasIntlSmoke);
    TestRegister("i18n.measure.fuzz", TestMeasFuzz);
}
