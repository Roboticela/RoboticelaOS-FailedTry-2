/* RoboI18N version pin (Goals 4.1–4.27). Synced with Tools/i18n/robo_i18n_version.py.
 * Phase 4 public API is frozen when ROBO_I18N_API_FROZEN is 1.
 */
#ifndef ROBO_I18N_VERSION_H
#define ROBO_I18N_VERSION_H

#define ROBO_I18N_VERSION_MAJOR 0
#define ROBO_I18N_VERSION_MINOR 27
#define ROBO_I18N_VERSION_PATCH 0
#define ROBO_I18N_VERSION_STRING "0.27.0"

/** 1 = Phase 4 public API frozen (Goal 4.27). Breaking changes require a new major/minor. */
#define ROBO_I18N_API_FROZEN 1

#define ROBO_IANA_REGISTRY_FILE_DATE "2026-05-05"
#define ROBO_CLDR_VERSION_STRING "46"
#define ROBO_CLDR_UNICODE_VERSION_STRING "16.0.0"

/** RoboMsg catalog binary format version (Goal 4.11). */
#define ROBO_MSG_FORMAT_VERSION 1u

/** IANA tzdb release pin (Goal 4.14). */
#define ROBO_TZDB_VERSION_STRING_PIN "2026c"

/** UCA / DUCET pin (must match Unicode 16.0.0). */
#define ROBO_UCA_VERSION_STRING_PIN "16.0.0"

/**
 * ISO 4217 currency identifiers arrive via CLDR codeMappings (Goal 4.21/4.26).
 * Not a separate ISO dump version.
 */
#define ROBO_ISO4217_SOURCE_STRING "CLDR-46-codeMappings"

/** Data generator stream version (Tools/i18n). */
#define ROBO_I18N_GENERATOR_VERSION_STRING "1.25.0"

#endif /* ROBO_I18N_VERSION_H */
