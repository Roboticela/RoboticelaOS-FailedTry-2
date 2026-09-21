/* RoboI18N version pin (Goals 4.1–4.27). Synced with Tools/i18n/robo_i18n_version.py.
 * Phase 4 public API is frozen when RoboI18nApiFrozen is 1.
 */
#ifndef ROBO_I18N_VERSION_H
#define ROBO_I18N_VERSION_H

#define RoboI18nVersionMajor 0
#define RoboI18nVersionMinor 27
#define RoboI18nVersionPatch 0
#define RoboI18nVersionString "0.27.0"

/** 1 = Phase 4 public API frozen (Goal 4.27). Breaking changes require a new major/minor. */
#define RoboI18nApiFrozen 1

#define RoboIanaRegistryFileDate "2026-05-05"
#define RoboCldrVersionString "46"
#define RoboCldrUnicodeVersionString "16.0.0"

/** RoboMsg catalog binary format version (Goal 4.11). */
#define RoboMsgFormatVersion 1u

/** IANA tzdb release pin (Goal 4.14). */
#define RoboTzdbVersionStringPin "2026c"

/** UCA / DUCET pin (must match Unicode 16.0.0). */
#define RoboUcaVersionStringPin "16.0.0"

/**
 * ISO 4217 currency identifiers arrive via CLDR codeMappings (Goal 4.21/4.26).
 * Not a separate ISO dump version.
 */
#define RoboIso4217SourceString "CLDR-46-codeMappings"

/** Data generator stream version (Tools/i18n). */
#define RoboI18nGeneratorVersionString "1.25.0"

#endif /* ROBO_I18N_VERSION_H */
