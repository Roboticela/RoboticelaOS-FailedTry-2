/*
 * RoboI18N transliteration foundation (Goal 4.25).
 *
 * Distinct from (do not conflate):
 *   translation              — meaning across languages
 *   transcription / phonetics — pronunciation guides
 *   Unicode normalization    — NFC/NFD/NFKC/NFKD (used as pipeline stages)
 *   case folding             — CaseFolding C+F
 *   locale-aware search      — Goal 4.24 (must NOT auto-enable translit)
 *   collation                — UCA sorting / equality (Goal 4.9)
 *   language detection       — out of scope
 *
 * Encoding: UTF-8 in / UTF-8 out (STRICT decode). Logical order only.
 * Normalization: only where the compiled CLDR pipeline requests a stage.
 * Reversibility: not inherent; see robo_i18n_translit_id_info_t.reversible
 * and information-loss notes in i18n/Transliteration.md.
 *
 * Ownership: transliterator is caller-owned; destroy with *_destroy.
 * Thread safety: not synchronized — one owner thread, or external lock.
 * Generated rule tables are immutable process data.
 */

#ifndef ROBO_I18N_TRANSLIT_H
#define ROBO_I18N_TRANSLIT_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NLocale.h"
#include "RoboUnicode/RoboUcdData.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard limit on input UTF-8 byte length. */
#define ROBO_I18N_TRANSLIT_INPUT_MAX 65536u

/** Hard limit on output UTF-8 byte length. */
#define ROBO_I18N_TRANSLIT_OUTPUT_MAX 262144u

/** Max code points retained across streaming chunk boundaries. */
#define ROBO_I18N_TRANSLIT_STREAM_LOOKAHEAD_MAX 16u

/** Max rule application steps per transliterate call. */
#define ROBO_I18N_TRANSLIT_STEP_MAX 1000000u

typedef struct robo_i18n_translit_id_info {
    const char *id;              /* canonical id, e.g. "Cyrillic-Latin" */
    uint8_t direction;           /* 1 = forward table, 2 = reverse table */
    uint8_t reversible;          /* 1 if CLDR source was bidirectional */
    robo_unicode_script_t source_script;
    robo_unicode_script_t target_script;
} robo_i18n_translit_id_info_t;

/**
 * Optional source→output range map entry (UTF-8 byte offsets).
 * Many-to-one / one-to-many mappings may leave gaps; exact reverse
 * reconstruction is not guaranteed.
 */
typedef struct robo_i18n_translit_map_entry {
    size_t src_start;
    size_t src_end;
    size_t out_start;
    size_t out_end;
} robo_i18n_translit_map_entry_t;

typedef struct robo_i18n_transliterator {
    const robo_i18n_translit_id_info_t *info; /* points into static metadata */
    uint32_t id_index;
    uint8_t ready;
    /* Streaming residual (code points) */
    uint32_t hold[ROBO_I18N_TRANSLIT_STREAM_LOOKAHEAD_MAX];
    uint16_t hold_len;
} robo_i18n_transliterator_t;

/** Number of compiled public rule-set IDs. */
uint32_t robo_i18n_translit_id_count(void);

/**
 * Lookup by canonical id or alias (case-sensitive CLDR id / BCP47-ish alias).
 * @return ROBO_OK + fills @p out_index; ROBO_NOT_FOUND if unknown.
 */
robo_result_t robo_i18n_translit_find_id(const char *id, uint32_t *out_index);

robo_result_t robo_i18n_translit_id_info(uint32_t index,
                                         robo_i18n_translit_id_info_t *out);

/**
 * Create transliterator for a compiled rule-set index.
 * @p locale is reserved for future language-specific selection; may be NULL.
 * Missing source/target is not inferred from text — caller picks the id.
 */
robo_result_t robo_i18n_transliterator_create(uint32_t id_index,
                                              const robo_i18n_locale_t *locale,
                                              robo_i18n_transliterator_t *out);

robo_result_t robo_i18n_transliterator_create_id(const char *id,
                                                 const robo_i18n_locale_t *locale,
                                                 robo_i18n_transliterator_t *out);

void robo_i18n_transliterator_destroy(robo_i18n_transliterator_t *tr);

/**
 * Transliterate entire UTF-8 input into @p out_buf.
 * @p out_len receives bytes written (not including a forced NUL; a NUL is
 * written when out_cap > written).
 * Optional @p map_out / @p map_cap collect coarse segment mappings.
 */
robo_result_t robo_i18n_transliterate(const robo_i18n_transliterator_t *tr,
                                      const char *utf8,
                                      size_t utf8_len,
                                      char *out_buf,
                                      size_t out_cap,
                                      size_t *out_len,
                                      robo_i18n_translit_map_entry_t *map_out,
                                      size_t map_cap,
                                      size_t *map_count);

/**
 * Streaming: feed a UTF-8 chunk. May retain a bounded suffix for context.
 * Call finish to flush residual. Not every rule set is chunk-independent —
 * context-sensitive rules require the hold buffer.
 */
robo_result_t robo_i18n_transliterator_stream_reset(robo_i18n_transliterator_t *tr);

robo_result_t robo_i18n_transliterator_stream_feed(robo_i18n_transliterator_t *tr,
                                                   const char *utf8,
                                                   size_t utf8_len,
                                                   char *out_buf,
                                                   size_t out_cap,
                                                   size_t *out_len);

robo_result_t robo_i18n_transliterator_stream_finish(robo_i18n_transliterator_t *tr,
                                                     char *out_buf,
                                                     size_t out_cap,
                                                     size_t *out_len);

/** CLDR version embedded in generated translit tables. */
const char *robo_i18n_translit_cldr_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_TRANSLIT_H */
