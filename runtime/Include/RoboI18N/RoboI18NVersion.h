/* RoboI18N version pin (Goals 4.1–4.4). Synced with Tools/i18n/robo_i18n_version.py. */
#ifndef ROBO_I18N_VERSION_H
#define ROBO_I18N_VERSION_H

#define ROBO_I18N_VERSION_MAJOR 0
#define ROBO_I18N_VERSION_MINOR 4
#define ROBO_I18N_VERSION_PATCH 0
#define ROBO_I18N_VERSION_STRING "0.4.0"

/* IANA Language Subtag Registry File-Date used for Preferred-Value tables. */
#define ROBO_IANA_REGISTRY_FILE_DATE "2026-05-05"

/*
 * CLDR is a separate version stream from Unicode UCD and from IANA.
 * Unicode association below is the version published inside CLDR 46
 * supplemental data — keep in sync with Tools/ucd pin when upgrading.
 */
#define ROBO_CLDR_VERSION_STRING "46"
#define ROBO_CLDR_UNICODE_VERSION_STRING "16.0.0"

#endif /* ROBO_I18N_VERSION_H */
