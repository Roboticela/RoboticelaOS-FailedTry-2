/*
 * RoboI18N date/time parsing (Goal 4.23).
 *
 * Formatting (Goal 4.6) and parsing are separate APIs.
 * Calendar conversion and timezone resolution are distinct steps after parse.
 *
 * Concepts (do not conflate):
 *   calendar date / wall time / local date-time / instant /
 *   UTC offset / named timezone / calendar system
 *
 * Missing timezone: returns a local date-time. Never invents UTC or the
 * host machine zone. Instant conversion requires an explicit offset/zone in
 * the input, or a resolve timezone supplied on the parser plus
 * options.resolveInstant.
 *
 * Modes:
 *   STRICT     — exact pattern literals, field ranges, weekday consistency,
 *                reject ambiguous/nonexistent local times when resolve policy
 *                is REJECT (default).
 *   PERMISSIVE — flexible whitespace around literals; numeric width may vary;
 *                weekday mismatch ignored; still deterministic.
 *
 * Disambiguation (local → instant): REJECT / EARLIER / LATER / COMPATIBLE.
 * COMPATIBLE maps to EARLIER for ambiguous folds; nonexistent gaps still fail
 * (same as the timezone engine — no silent gap-fill).
 *
 * Error positions are UTF-8 byte offsets into the input span.
 *
 * Thread safety: parsers are not synchronized — one owner thread, or lock.
 * No global mutable parse state. Pattern tokens are cached inside the parser.
 */

#ifndef ROBO_I18N_DATETIME_PARSE_H
#define ROBO_I18N_DATETIME_PARSE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NParse.h"
#include "RoboI18N/RoboI18NTimeZone.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard limit on input span length (bytes). */
#define RoboI18nDtparseInputMax 4096u

typedef enum I18nDtparseErr {
    RoboI18nDtparseErrNone = 0,
    RoboI18nDtparseErrEmpty = 1,
    RoboI18nDtparseErrInvalidUtf8 = 2,
    RoboI18nDtparseErrUnexpectedChar = 3,
    RoboI18nDtparseErrTrailing = 4,
    RoboI18nDtparseErrInvalidDate = 5,
    RoboI18nDtparseErrInvalidTime = 6,
    RoboI18nDtparseErrInvalidField = 7,
    RoboI18nDtparseErrInvalidMonthName = 8,
    RoboI18nDtparseErrInvalidWeekdayName = 9,
    RoboI18nDtparseErrInvalidEra = 10,
    RoboI18nDtparseErrInvalidDayPeriod = 11,
    RoboI18nDtparseErrInvalidTimezone = 12,
    RoboI18nDtparseErrAmbiguousTimezone = 13,
    RoboI18nDtparseErrAmbiguousLocal = 14,
    RoboI18nDtparseErrNonexistentLocal = 15,
    RoboI18nDtparseErrMissingTimezone = 16,
    RoboI18nDtparseErrOverflow = 17,
    RoboI18nDtparseErrUnsupportedCalendar = 18,
    RoboI18nDtparseErrUnsupportedLocale = 19,
    RoboI18nDtparseErrInputTooLong = 20,
    RoboI18nDtparseErrPattern = 21
} I18nDtparseErr;

/**
 * Local→instant policy for the parser (extends timezone disambiguation).
 * COMPATIBLE → EARLIER on ambiguous; nonexistent still fails.
 */
typedef enum I18nDtparseDisambig {
    RoboI18nDtparseDisambigReject = 0,
    RoboI18nDtparseDisambigEarlier = 1,
    RoboI18nDtparseDisambigLater = 2,
    RoboI18nDtparseDisambigCompatible = 3
} I18nDtparseDisambig;

typedef struct I18nDtparseOptions {
    I18nParseMode mode;
    I18nCalFormatStyle dateStyle; /* style init: DATE / DATETIME */
    I18nCalFormatStyle timeStyle; /* style init: TIME / DATETIME */
    int allowLeadingWs;  /* default 1 */
    int allowTrailingWs; /* default 1 */
    int requireFull;      /* default 1 */
    int resolveInstant;   /* default 0: do not invent instants */
    I18nDtparseDisambig disambiguation; /* default REJECT */
    char numberingSystem[16];
    char hourCycle[8]; /* empty = locale service */
} I18nDtparseOptions;

/**
 * Structured parse outcome. Fields are only meaningful when the corresponding
 * have_* flag is set. gregorianDate is filled when calendar conversion succeeds.
 * Instant is filled only when resolve succeeds (haveInstant).
 */
typedef struct I18nDtparseResult {
    uint8_t haveDate;
    uint8_t haveTime;
    uint8_t haveOffset;
    uint8_t haveTimezone;
    uint8_t zoneExplicit; /* 1 if offset/zone came from the input */
    uint8_t haveInstant;
    uint8_t haveEra;
    uint8_t haveDayPeriod;
    uint8_t dayPeriodIsPm; /* meaningful if haveDayPeriod */

    I18nCalendarFields fields;
    I18nDate gregorianDate;
    I18nTime time;
    I18nDatetime local;
    int32_t offsetSeconds;
    char timezoneId[RoboI18nTzIdMax + 1u];
    I18nInstant instant;
    I18nTzResolve tzResolve;

    size_t consumed;
    size_t errorOffset;
    I18nDtparseErr error;
} I18nDtparseResult;

/**
 * Stack parser. Embeds a style/pattern formatter for compiled tokens and
 * numbering-system digits. Ownership: value type; destroy clears ready.
 */
typedef struct I18nDatetimeParser {
    I18nDatetimeFormatter fmt;
    I18nDtparseOptions options;
    I18nDtfmtKind kind;
    const char *digitUtf8[10];
    uint8_t digitLen[10];
    uint8_t hasResolveTz;
    uint8_t ready;
} I18nDatetimeParser;

void I18nDtparseOptionsInit(I18nDtparseOptions *opts);
void I18nDtparseResultClear(I18nDtparseResult *out);

/**
 * Style-based parser. @p calendar NULL → gregorian. @p resolveTz NULL →
 * no default zone for instant resolution (input zone/offset still honored).
 */
Result I18nDatetimeParserInit(I18nDatetimeParser *parser,
                                             const I18nLocale *locale,
                                             const I18nCalendar *calendar,
                                             const I18nTimezone *resolveTz,
                                             I18nDtfmtKind kind,
                                             const I18nDtparseOptions *opts);

Result I18nDatetimeParserInitPattern(
    I18nDatetimeParser *parser,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *resolveTz,
    const char *pattern,
    const I18nDtparseOptions *opts);

Result I18nDatetimeParserInitSkeleton(
    I18nDatetimeParser *parser,
    const I18nLocale *locale,
    const I18nCalendar *calendar,
    const I18nTimezone *resolveTz,
    const char *skeleton,
    const I18nDtparseOptions *opts);

void I18nDatetimeParserDestroy(I18nDatetimeParser *parser);

/**
 * Parse UTF-8 @p text[0..len) against the parser's pattern.
 * Returns RoboOk on success; RoboParseError on grammar/content failure
 * (details in @p out); RoboInvalidArgument for NULL/oversized input;
 * RoboNotSupported for calendars without conversion when date fields present.
 */
Result I18nParseDate(const I18nDatetimeParser *parser,
                                   const char *text,
                                   size_t len,
                                   I18nDtparseResult *out);

Result I18nParseTime(const I18nDatetimeParser *parser,
                                   const char *text,
                                   size_t len,
                                   I18nDtparseResult *out);

Result I18nParseLocalDatetime(const I18nDatetimeParser *parser,
                                             const char *text,
                                             size_t len,
                                             I18nDtparseResult *out);

/**
 * Same as local datetime parse, but requires a timezone or offset in the
 * input (or resolveInstant + resolveTz) so an instant can be produced when
 * options.resolveInstant is set. Without resolveInstant, still requires
 * zone/offset markers in the matched pattern payload.
 */
Result I18nParseZonedDatetime(const I18nDatetimeParser *parser,
                                             const char *text,
                                             size_t len,
                                             I18nDtparseResult *out);

const char *I18nDtparseErrString(I18nDtparseErr err);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_DATETIME_PARSE_H */
