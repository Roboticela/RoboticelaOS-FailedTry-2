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
 * Reversibility: not inherent; see I18nTranslitIdInfo.reversible
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
#define RoboI18nTranslitInputMax 65536u

/** Hard limit on output UTF-8 byte length. */
#define RoboI18nTranslitOutputMax 262144u

/** Max code points retained across streaming chunk boundaries. */
#define RoboI18nTranslitStreamLookaheadMax 16u

/** Max rule application steps per transliterate call. */
#define RoboI18nTranslitStepMax 1000000u

typedef struct I18nTranslitIdInfo {
    const char *id;              /* canonical id, e.g. "Cyrillic-Latin" */
    uint8_t direction;           /* 1 = forward table, 2 = reverse table */
    uint8_t reversible;          /* 1 if CLDR source was bidirectional */
    UnicodeScript sourceScript;
    UnicodeScript targetScript;
} I18nTranslitIdInfo;

/**
 * Optional source→output range map entry (UTF-8 byte offsets).
 * Many-to-one / one-to-many mappings may leave gaps; exact reverse
 * reconstruction is not guaranteed.
 */
typedef struct I18nTranslitMapEntry {
    size_t srcStart;
    size_t srcEnd;
    size_t outStart;
    size_t outEnd;
} I18nTranslitMapEntry;

typedef struct I18nTransliterator {
    const I18nTranslitIdInfo *info; /* points into static metadata */
    uint32_t idIndex;
    uint8_t ready;
    /* Streaming residual (code points) */
    uint32_t hold[RoboI18nTranslitStreamLookaheadMax];
    uint16_t holdLen;
} I18nTransliterator;

/** Number of compiled public rule-set IDs. */
uint32_t I18nTranslitIdCount(void);

/**
 * Lookup by canonical id or alias (case-sensitive CLDR id / BCP47-ish alias).
 * @return RoboOk + fills @p outIndex; RoboNotFound if unknown.
 */
Result I18nTranslitFindId(const char *id, uint32_t *outIndex);

Result GetI18nTranslitIdInfo(uint32_t index,
                                         I18nTranslitIdInfo *out);

/**
 * Create transliterator for a compiled rule-set index.
 * @p locale is reserved for future language-specific selection; may be NULL.
 * Missing source/target is not inferred from text — caller picks the id.
 */
Result I18nTransliteratorCreate(uint32_t idIndex,
                                              const I18nLocale *locale,
                                              I18nTransliterator *out);

Result I18nTransliteratorCreateId(const char *id,
                                                 const I18nLocale *locale,
                                                 I18nTransliterator *out);

void I18nTransliteratorDestroy(I18nTransliterator *tr);

/**
 * Transliterate entire UTF-8 input into @p outBuf.
 * @p outLen receives bytes written (not including a forced NUL; a NUL is
 * written when outCap > written).
 * Optional @p mapOut / @p mapCap collect coarse segment mappings.
 */
Result I18nTransliterate(const I18nTransliterator *tr,
                                      const char *utf8,
                                      size_t utf8Len,
                                      char *outBuf,
                                      size_t outCap,
                                      size_t *outLen,
                                      I18nTranslitMapEntry *mapOut,
                                      size_t mapCap,
                                      size_t *mapCount);

/**
 * Streaming: feed a UTF-8 chunk. May retain a bounded suffix for context.
 * Call finish to flush residual. Not every rule set is chunk-independent —
 * context-sensitive rules require the hold buffer.
 */
Result I18nTransliteratorStreamReset(I18nTransliterator *tr);

Result I18nTransliteratorStreamFeed(I18nTransliterator *tr,
                                                   const char *utf8,
                                                   size_t utf8Len,
                                                   char *outBuf,
                                                   size_t outCap,
                                                   size_t *outLen);

Result I18nTransliteratorStreamFinish(I18nTransliterator *tr,
                                                     char *outBuf,
                                                     size_t outCap,
                                                     size_t *outLen);

/** CLDR version embedded in generated translit tables. */
const char *I18nTranslitCldrVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_TRANSLIT_H */
