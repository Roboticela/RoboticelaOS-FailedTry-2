/*
 * RoboI18N timezone / tzdb tests (Goal 4.14).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NTimeZone.h"
#include "RoboI18N/RoboTzData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void TestTzVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nTzdbVersionString(), "2026c");
    RoboCheckStrEq(TzdbVersionString(), "2026c");
}

static void TestTzLookup(void)
{
    I18nTimezone tz;
    char canon[64];
    size_t n = 0;

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    RoboCheckEqInt((int)MakeI18nTimezoneKind(&tz), (int)RoboI18nTzIana);
    RoboCheckEqInt(I18nTimezoneCanonicalId(&tz, canon, sizeof(canon), &n), RoboOk);
    RoboCheckStrEq(canon, "Asia/Karachi");

    RoboCheckEqInt(I18nTimezoneFromId("Not/AZone", 9, &tz), RoboNotFound);
    RoboCheckEqInt(I18nTimezoneFromId("UTC", 3, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneFromId("Etc/UTC", 7, &tz), RoboOk);
}

static void TestTzAlias(void)
{
    I18nTimezone tz;
    char canon[64];
    size_t n = 0;
    /* US/Eastern is a Link alias in backward → America/New_York. */
    RoboCheckEqInt(I18nTimezoneFromId("US/Eastern", 10, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneCanonicalId(&tz, canon, sizeof(canon), &n), RoboOk);
    RoboCheckStrEq(canon, "America/New_York");
    RoboCheckStrEq(I18nTimezoneId(&tz), "US/Eastern");
}

static void TestTzHistorical(void)
{
    /* America/New_York 2006 spring (pre–Energy Policy Act permanent Mar/Nov rules). */
    I18nTimezone tz;
    I18nDatetime utc;
    I18nInstant inst;
    int32_t off;
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    RoboCheckEqInt(I18nDateInit(&utc.date, 2006, 4, 2), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 6, 30, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &inst, &off), RoboOk);
    RoboCheckEqInt(off, -5 * 3600); /* still EST just before 2006-04-02 07:00 UTC */
    RoboCheckEqInt(I18nDateInit(&utc.date, 2006, 4, 2), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 7, 30, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &inst, &off), RoboOk);
    RoboCheckEqInt(off, -4 * 3600);
}

static void TestTzKarachiOffset(void)
{
    I18nTimezone tz;
    I18nDatetime utc;
    I18nInstant inst;
    I18nDatetime local;
    I18nTzResolve res;
    int32_t off;
    int dst = 1;

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    RoboCheckEqInt(I18nDateInit(&utc.date, 2024, 6, 15), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 0, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &inst, &off), RoboOk);
    RoboCheckEqInt(off, 5 * 3600);
    RoboCheckEqInt(I18nTimezoneIsDstAt(&tz, &inst, &dst), RoboOk);
    RoboCheckEqInt(dst, 0);
    RoboCheckEqInt(I18nTimezoneToLocal(&tz, &inst, &local, &res), RoboOk);
    RoboCheckEqInt((int)local.time.hour, 5);
}

static void TestTzRoundtripUnique(void)
{
    static const char *zones[] = {
        "UTC", "Asia/Tokyo", "Asia/Kolkata", "Europe/Paris", "America/Sao_Paulo",
        "Australia/Sydney", "Africa/Cairo", "Asia/Shanghai", "Pacific/Auckland"
    };
    size_t zi;
    for (zi = 0; zi < sizeof(zones) / sizeof(zones[0]); zi++) {
        I18nTimezone tz;
        I18nDatetime utc;
        I18nInstant inst, back;
        I18nDatetime local;
        I18nTzResolve res;
        RoboCheckEqInt(I18nTimezoneFromId(zones[zi], strlen(zones[zi]), &tz), RoboOk);
        RoboCheckEqInt(I18nDateInit(&utc.date, 2023, 3, 10), RoboOk);
        RoboCheckEqInt(I18nTimeInit(&utc.time, 12, 0, 0, 0), RoboOk);
        RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);
        RoboCheckEqInt(I18nTimezoneToLocal(&tz, &inst, &local, &res), RoboOk);
        RoboCheckEqInt(I18nTimezoneToUtc(&tz, &local, &back, &res), RoboOk);
        /* Ambiguous folds may shift; for UNIQUE expect exact. */
        if (res == RoboI18nTzUnique) {
            RoboCheckEqInt(I18nInstantCompare(&inst, &back), 0);
        }
    }
}

static void TestTzNySpringForward(void)
{
    /* America/New_York 2024-03-10: 02:00 → 03:00 EDT (gap). */
    I18nTimezone tz;
    I18nDatetime local;
    I18nInstant inst;
    I18nTzResolve res;

    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    RoboCheckEqInt(I18nDateInit(&local.date, 2024, 3, 10), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&local.time, 2, 30, 0, 0), RoboOk);
    RoboCheckEqInt(I18nTimezoneToUtcEx(&tz, &local, RoboI18nTzDisambigReject, &inst,
                                                    &res),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)res, (int)RoboI18nTzNonexistent);
}

static void TestTzNyFallBack(void)
{
    /* America/New_York 2024-11-03: 01:30 occurs twice. */
    I18nTimezone tz;
    I18nDatetime local;
    I18nInstant earlier, later;
    I18nTzResolve res;

    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    RoboCheckEqInt(I18nDateInit(&local.date, 2024, 11, 3), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&local.time, 1, 30, 0, 0), RoboOk);
    RoboCheckEqInt(I18nTimezoneToUtcEx(&tz, &local, RoboI18nTzDisambigEarlier,
                                                    &earlier, &res),
                      RoboOk);
    RoboCheckEqInt((int)res, (int)RoboI18nTzAmbiguous);
    RoboCheckEqInt(I18nTimezoneToUtcEx(&tz, &local, RoboI18nTzDisambigLater, &later,
                                                    &res),
                      RoboOk);
    RoboCheckEqInt((int)res, (int)RoboI18nTzAmbiguous);
    RoboCheck(later.unixMs > earlier.unixMs);
    RoboCheckEqInt((int)((later.unixMs - earlier.unixMs) / 1000), 3600);
}

static void TestTzTransitions(void)
{
    I18nTimezone tz;
    I18nDatetime utc;
    I18nInstant inst;
    I18nTzTransition tr;
    int32_t offBefore;
    int32_t offAfter;

    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    /* Just before 2024 spring forward (~2024-03-10 07:00 UTC). */
    RoboCheckEqInt(I18nDateInit(&utc.date, 2024, 3, 10), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&utc.time, 6, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nInstantFromUtc(&utc, &inst), RoboOk);
    RoboCheckEqInt(I18nTimezoneNextTransition(&tz, &inst, &tr), RoboOk);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &inst, &offBefore), RoboOk);
    {
        I18nInstant after = tr.at;
        after.unixMs += 1000;
        RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &after, &offAfter), RoboOk);
    }
    RoboCheck(tr.offsetAfter != tr.offsetBefore || tr.isDstAfter != tr.isDstBefore);
    RoboCheckEqInt(offBefore, tr.offsetBefore);
}

static void TestTzFixedAndUtc(void)
{
    I18nTimezone tz;
    I18nInstant inst = {0};
    int32_t off;
    RoboCheckEqInt(I18nTimezoneFixedSeconds(5 * 3600 + 30 * 60, &tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &inst, &off), RoboOk);
    RoboCheckEqInt(off, 5 * 3600 + 30 * 60);
    RoboCheckEqInt(I18nTimezoneUtc(&tz), RoboOk);
    RoboCheckEqInt(I18nTimezoneOffsetAt(&tz, &inst, &off), RoboOk);
    RoboCheckEqInt(off, 0);
}

static void TestTzFuzz(void)
{
    uint32_t seed = 0x414u;
    int i;
    for (i = 0; i < 80; i++) {
        char id[32];
        size_t len = (seed % 20u) + 1u;
        size_t j;
        I18nTimezone tz;
        I18nInstant inst;
        int32_t off;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/_-+";
            id[j] = alphabet[seed % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)I18nTimezoneFromId(id, len, &tz);
        inst.unixMs = (int64_t)(int32_t)seed * 1000;
        if (tz.kind == RoboI18nTzIana || tz.kind == RoboI18nTzUtc ||
            tz.kind == RoboI18nTzFixed) {
            (void)I18nTimezoneOffsetAt(&tz, &inst, &off);
        }
    }
    RoboCheck(1);
}

void TestRegisterI18nTimezone(void)
{
    TestRegister("i18n.tz.version", TestTzVersion);
    TestRegister("i18n.tz.lookup", TestTzLookup);
    TestRegister("i18n.tz.alias", TestTzAlias);
    TestRegister("i18n.tz.historical", TestTzHistorical);
    TestRegister("i18n.tz.karachi", TestTzKarachiOffset);
    TestRegister("i18n.tz.roundtrip", TestTzRoundtripUnique);
    TestRegister("i18n.tz.spring_forward", TestTzNySpringForward);
    TestRegister("i18n.tz.fall_back", TestTzNyFallBack);
    TestRegister("i18n.tz.transitions", TestTzTransitions);
    TestRegister("i18n.tz.fixed", TestTzFixedAndUtc);
    TestRegister("i18n.tz.fuzz", TestTzFuzz);
}
