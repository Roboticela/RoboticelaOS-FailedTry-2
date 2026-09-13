/*
 * RoboI18N — internationalization foundation (Goal 4.1).
 *
 * Locale identification (BCP 47 / RFC 5646). Not language detection.
 * Not number/date formatting, plural rules, collation, or messages yet.
 *
 * Unicode Script != locale language != region. See i18n/README.md.
 */

#ifndef ROBO_I18N_H
#define ROBO_I18N_H

#include "RoboI18N/RoboI18NLocale.h"
#include "RoboI18N/RoboI18NVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_i18n_version_string(void);
const char *robo_i18n_iana_file_date(void);
const char *robo_i18n_cldr_version_string(void);

/**
 * Initialize process-wide default locale to "und" (undefined).
 * Safe to call multiple times. Does not read OS settings in Goal 4.1.
 */
robo_result_t robo_i18n_init(void);

/** No-op for now; reserved for future teardown. */
void robo_i18n_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_H */
