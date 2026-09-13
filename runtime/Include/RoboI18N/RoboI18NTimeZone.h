/*
 * RoboI18N time-zone abstraction (Goals 4.5 + 4.14).
 *
 * Distinguishes:
 *   UTC / FIXED — constant offset
 *   IANA — named zone with tzdb transitions
 *   SYSTEM — unresolved until OS integration
 *
 * Leap seconds: not modeled (POSIX 86400-second days).
 * Language: C.
 *
 * Ownership: value types; tzdb tables are immutable process data.
 * Thread safety: pure functions over immutable tables; re-entrant.
 */

#ifndef ROBO_I18N_TIMEZONE_H
#define ROBO_I18N_TIMEZONE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NDateTime.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_I18N_TZ_ID_MAX 64u
#define ROBO_I18N_TZ_ABBR_MAX 16u

typedef enum robo_i18n_timezone_kind {
    ROBO_I18N_TZ_UTC = 0,
    ROBO_I18N_TZ_FIXED = 1,
    ROBO_I18N_TZ_IANA = 2,
    ROBO_I18N_TZ_SYSTEM = 3
} robo_i18n_timezone_kind_t;

/**
 * Local-time classification relative to zone transitions.
 * Fixed/UTC offsets are always UNIQUE.
 */
typedef enum robo_i18n_tz_resolve {
    ROBO_I18N_TZ_UNIQUE = 0,
    ROBO_I18N_TZ_AMBIGUOUS = 1,   /* fold / repeated local interval */
    ROBO_I18N_TZ_NONEXISTENT = 2  /* gap / skipped local interval */
} robo_i18n_tz_resolve_t;

/**
 * Policy when local → instant is ambiguous or nonexistent.
 * EARLIER/LATER apply to AMBIGUOUS only; NONEXISTENT always fails
 * with ROBO_INVALID_ARGUMENT (resolve set to NONEXISTENT).
 * REJECT fails unless UNIQUE.
 */
typedef enum robo_i18n_tz_disambiguation {
    ROBO_I18N_TZ_DISAMBIG_EARLIER = 0,
    ROBO_I18N_TZ_DISAMBIG_LATER = 1,
    ROBO_I18N_TZ_DISAMBIG_REJECT = 2
} robo_i18n_tz_disambiguation_t;

typedef struct robo_i18n_timezone {
    robo_i18n_timezone_kind_t kind;
    /** Fixed offset east of UTC in seconds; valid for UTC/FIXED. */
    int32_t offset_seconds;
    /** Requested id (IANA id, fixed label, or UTC); copied. */
    char id[ROBO_I18N_TZ_ID_MAX + 1u];
} robo_i18n_timezone_t;

typedef struct robo_i18n_tz_transition {
    robo_i18n_instant_t at;
    int32_t offset_before;
    int32_t offset_after;
    int is_dst_before;
    int is_dst_after;
    char abbr_before[ROBO_I18N_TZ_ABBR_MAX];
    char abbr_after[ROBO_I18N_TZ_ABBR_MAX];
} robo_i18n_tz_transition_t;

/** IANA tzdb release string (e.g. "2026c"). */
const char *robo_i18n_tzdb_version_string(void);

robo_result_t robo_i18n_timezone_utc(robo_i18n_timezone_t *out);

/** Fixed offset in seconds east of UTC (−18h..+18h). */
robo_result_t robo_i18n_timezone_fixed_seconds(int32_t offset_seconds,
                                               robo_i18n_timezone_t *out);

/**
 * Parse id:
 *   "UTC" / "Z" / "Etc/UTC" → UTC
 *   "+05:00" / "UTC+05:30" → FIXED
 *   "Asia/Karachi" → IANA (must exist in tzdb; aliases resolve)
 *   "local" / "system" → SYSTEM (unresolved)
 * Unknown IANA id → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_timezone_from_id(const char *id,
                                         size_t len,
                                         robo_i18n_timezone_t *out);

robo_i18n_timezone_kind_t robo_i18n_timezone_kind(const robo_i18n_timezone_t *tz);

/** Requested identifier (may be an alias). */
const char *robo_i18n_timezone_id(const robo_i18n_timezone_t *tz);

/**
 * Canonical IANA id for @p tz (alias → target). FIXED/UTC return their id.
 * SYSTEM → ROBO_NOT_SUPPORTED. Unknown → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_timezone_canonical_id(const robo_i18n_timezone_t *tz,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len);

robo_result_t robo_i18n_timezone_offset_at(const robo_i18n_timezone_t *tz,
                                           const robo_i18n_instant_t *instant,
                                           int32_t *out_offset_seconds);

/** 1 if DST in effect at @p instant (IANA); 0 for UTC/FIXED. */
robo_result_t robo_i18n_timezone_is_dst_at(const robo_i18n_timezone_t *tz,
                                           const robo_i18n_instant_t *instant,
                                           int *out_is_dst);

robo_result_t robo_i18n_timezone_abbreviation_at(const robo_i18n_timezone_t *tz,
                                                 const robo_i18n_instant_t *instant,
                                                 char *out,
                                                 size_t out_cap,
                                                 size_t *out_len);

robo_result_t robo_i18n_timezone_to_local(const robo_i18n_timezone_t *tz,
                                          const robo_i18n_instant_t *instant,
                                          robo_i18n_datetime_t *out_local,
                                          robo_i18n_tz_resolve_t *out_resolve);

/**
 * Local → instant using EARLIER disambiguation (historical default).
 * Sets @p out_resolve. NONEXISTENT → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_i18n_timezone_to_utc(const robo_i18n_timezone_t *tz,
                                        const robo_i18n_datetime_t *local,
                                        robo_i18n_instant_t *out_instant,
                                        robo_i18n_tz_resolve_t *out_resolve);

robo_result_t robo_i18n_timezone_to_utc_ex(const robo_i18n_timezone_t *tz,
                                           const robo_i18n_datetime_t *local,
                                           robo_i18n_tz_disambiguation_t policy,
                                           robo_i18n_instant_t *out_instant,
                                           robo_i18n_tz_resolve_t *out_resolve);

/**
 * Next transition at or after @p instant (exclusive of a transition exactly
 * at @p instant for "next"; use >= for inclusive — documented as strictly after).
 * No further transition → ROBO_NOT_FOUND.
 */
robo_result_t robo_i18n_timezone_next_transition(const robo_i18n_timezone_t *tz,
                                                 const robo_i18n_instant_t *instant,
                                                 robo_i18n_tz_transition_t *out);

/** Previous transition strictly before @p instant. */
robo_result_t robo_i18n_timezone_prev_transition(const robo_i18n_timezone_t *tz,
                                                 const robo_i18n_instant_t *instant,
                                                 robo_i18n_tz_transition_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_TIMEZONE_H */
