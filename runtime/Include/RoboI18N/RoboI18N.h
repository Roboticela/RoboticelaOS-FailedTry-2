/*
 * RoboI18N — internationalization foundation (Goals 4.1–4.27).
 *
 * Phase 4 public API is FROZEN (ROBO_I18N_API_FROZEN == 1). Applications must
 * include this umbrella or the individual RoboI18N*.h headers listed below.
 * Generated table headers (Robo*Data.h) are NOT part of the frozen public API.
 *
 * Locale identification (BCP 47 / RFC 5646), CLDR locale-data metadata,
 * runtime locale service (extensions, resolve, fallback, negotiation),
 * Accept-Language / language-range negotiation,
 * cardinal and ordinal plural rules,
 * number formatting, plural-rule categories, date/time/calendar/timezone
 * (IANA tzdb + CLDR timezone display), localized date/time formatting,
 * interval / relative-time formatting, measurement unit formatting,
 * measurement systems / unit preferences, currency metadata / formatting,
 * number/currency parsing, date/time parsing,
 * locale-aware search / string matching,
 * transliteration (CLDR transforms),
 * calendar arithmetic / date-time utilities,
 * locale-aware collation (UCA + CLDR tailorings), locale display names,
 * application message catalogs (RoboMsg), RoboMsgFmt message formatting, and
 * message-format validation/compatibility. Not language detection.
 * Not a full ICU MessageFormat clone.
 *
 * Unicode Script != locale language != region. See i18n/README.md,
 * i18n/ApiFreeze.md, i18n/Utf8Contract.md, i18n/Architecture.md.
 */

#ifndef ROBO_I18N_H
#define ROBO_I18N_H

#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboI18NLocaleNegotiate.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboI18NDateTime.h"
#include "RoboI18N/RoboI18NTimeZone.h"
#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NDateTimeInterval.h"
#include "RoboI18N/RoboI18NRelativeTime.h"
#include "RoboI18N/RoboI18NUnit.h"
#include "RoboI18N/RoboI18NMeasure.h"
#include "RoboI18N/RoboI18NCurrency.h"
#include "RoboI18N/RoboI18NParse.h"
#include "RoboI18N/RoboI18NDateTimeParse.h"
#include "RoboI18N/RoboI18NSearch.h"
#include "RoboI18N/RoboI18NTranslit.h"
#include "RoboI18N/RoboI18NDateTimeArith.h"
#include "RoboI18N/RoboI18NCollation.h"
#include "RoboI18N/RoboI18NDisplayNames.h"
#include "RoboI18N/RoboI18NMessage.h"
#include "RoboI18N/RoboI18NMessageFormat.h"
#include "RoboI18N/RoboI18NMessageValidate.h"
#include "RoboI18N/RoboI18NVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_i18n_version_string(void);
const char *robo_i18n_iana_file_date(void);
const char *robo_i18n_cldr_version_string(void);

/**
 * Initialize process-wide default locale to "und" (undefined).
 * Safe to call multiple times. Does not read OS settings in Phase 4.
 * Process-wide default locale: caller synchronizes concurrent set/get.
 */
robo_result_t robo_i18n_init(void);

/** No-op for now; reserved for future teardown. */
void robo_i18n_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_H */
