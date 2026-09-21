/*
 * RoboI18N search & matching (Goal 4.24).
 *
 * Windowed search over UTF-8 byte spans at code-point (or grapheme) boundaries
 * preserves mapping from transformed equality back to the original haystack.
 */

#include "RoboI18N/RoboI18NSearch.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboUnicode/RoboUcdData.h"

#include <string.h>

void I18nSearchOptionsInit(I18nSearchOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mode = RoboI18nSearchExact;
    opts->collationStrength = RoboI18nCollationTertiary;
    opts->ignoreDiacritics = 0;
    opts->compatibility = 0;
    opts->graphemeSafe = 1;
}

const char *I18nSearchModeString(I18nSearchMode mode)
{
    switch (mode) {
    case RoboI18nSearchExact:
        return "exact";
    case RoboI18nSearchCodepoint:
        return "codepoint";
    case RoboI18nSearchCanonical:
        return "canonical";
    case RoboI18nSearchCasefold:
        return "casefold";
    case RoboI18nSearchCanonicalCasefold:
        return "canonical_casefold";
    case RoboI18nSearchCollation:
        return "collation";
    default:
        return "unknown";
    }
}

static Result EnsureCollator(I18nSearcher *searcher)
{
    I18nCollatorOptions copt;
    I18nLocale und;
    const I18nLocale *loc;
    Result r;

    if (searcher->collator) {
        I18nCollatorOptionsInit(&copt);
        copt.strength = searcher->options.collationStrength;
        return I18nCollatorSetOptions(searcher->collator, &copt);
    }
    if (searcher->hasLocale) {
        loc = &searcher->locale;
    } else {
        r = I18nLocaleParseCanonical("und", 3, &und);
        if (r != RoboOk) {
            return r;
        }
        loc = &und;
    }
    I18nCollatorOptionsInit(&copt);
    copt.strength = searcher->options.collationStrength;
    return I18nCollatorCreate(loc, NULL, &copt, &searcher->collator);
}

Result I18nSearcherInit(I18nSearcher *searcher,
                                     const I18nLocale *locale,
                                     const I18nSearchOptions *opts)
{
    Result r;

    if (!searcher) {
        return RoboInvalidArgument;
    }
    memset(searcher, 0, sizeof(*searcher));
    if (opts) {
        searcher->options = *opts;
    } else {
        I18nSearchOptionsInit(&searcher->options);
    }
    if (locale) {
        searcher->locale = *locale;
        searcher->hasLocale = 1;
    }
    if (searcher->options.mode == RoboI18nSearchCollation) {
        r = EnsureCollator(searcher);
        if (r != RoboOk) {
            memset(searcher, 0, sizeof(*searcher));
            return r;
        }
    }
    searcher->ready = 1;
    return RoboOk;
}

void I18nSearcherDestroy(I18nSearcher *searcher)
{
    if (!searcher) {
        return;
    }
    if (searcher->collator) {
        I18nCollatorDestroy(searcher->collator);
        searcher->collator = NULL;
    }
    memset(searcher, 0, sizeof(*searcher));
}

Result I18nSearcherSetOptions(I18nSearcher *searcher,
                                            const I18nSearchOptions *opts)
{
    if (!searcher || !searcher->ready || !opts) {
        return RoboInvalidArgument;
    }
    searcher->options = *opts;
    if (searcher->options.mode == RoboI18nSearchCollation) {
        return EnsureCollator(searcher);
    }
    if (searcher->collator) {
        I18nCollatorDestroy(searcher->collator);
        searcher->collator = NULL;
    }
    return RoboOk;
}

static int Utf8Ok(const char *s, size_t len)
{
    size_t off = 0;
    if (len == 0) {
        return 1;
    }
    if (!s) {
        return 0;
    }
    while (off < len) {
        size_t next = off;
        if (UnicodeUtf8Decode(s, len, off, RoboUnicodeStrict, NULL, &next) !=
            RoboUnicodeOk) {
            return 0;
        }
        off = next;
    }
    return 1;
}

static Result CheckInput(const char *s, size_t len)
{
    if (len > RoboI18nSearchInputMax) {
        return RoboInvalidArgument;
    }
    if (len > 0 && !s) {
        return RoboInvalidArgument;
    }
    if (!Utf8Ok(s, len)) {
        return RoboParseError;
    }
    return RoboOk;
}

/* ---- Transform helpers (whole-string) ---- */

static Result DecodeCps(const char *s,
                                size_t len,
                                Uchar32 **outCps,
                                size_t *outN)
{
    size_t off = 0;
    size_t n = 0;
    size_t cap;
    Uchar32 *cps;

    *outCps = NULL;
    *outN = 0;
    while (off < len) {
        Uchar32 cp = 0;
        size_t next = off;
        if (UnicodeUtf8Decode(s, len, off, RoboUnicodeStrict, &cp, &next) !=
            RoboUnicodeOk) {
            return RoboParseError;
        }
        n++;
        off = next;
    }
    if (n == 0) {
        return RoboOk;
    }
    cap = n * sizeof(Uchar32);
    cps = (Uchar32 *)Malloc(cap);
    if (!cps) {
        return RoboOutOfMemory;
    }
    off = 0;
    n = 0;
    while (off < len) {
        Uchar32 cp = 0;
        size_t next = off;
        (void)UnicodeUtf8Decode(s, len, off, RoboUnicodeStrict, &cp, &next);
        cps[n++] = cp;
        off = next;
    }
    *outCps = cps;
    *outN = n;
    return RoboOk;
}

static Result StripMn(Uchar32 *cps, size_t n, size_t *outN)
{
    size_t w = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        if (UnicodeCategory(cps[i]) == (UnicodeGeneralCategory)RoboUGcMn) {
            continue;
        }
        cps[w++] = cps[i];
    }
    *outN = w;
    return RoboOk;
}

/**
 * Build a comparable scalar sequence for @p s under matcher options.
 * Caller frees *outCps when non-NULL.
 */
static Result TransformCps(const I18nSearcher *searcher,
                                   const char *s,
                                   size_t len,
                                   Uchar32 **outCps,
                                   size_t *outN)
{
    Uchar32 *cps = NULL;
    size_t n = 0;
    Result r;
    UnicodeNormForm nform;

    *outCps = NULL;
    *outN = 0;

    if (searcher->options.mode == RoboI18nSearchExact) {
        /* Represent as raw bytes in a synthetic way — callers use memcmp path. */
        return RoboOk;
    }

    r = DecodeCps(s, len, &cps, &n);
    if (r != RoboOk) {
        return r;
    }

    nform = searcher->options.compatibility ? RoboUnicodeNormNfkd : RoboUnicodeNormNfd;

    if (searcher->options.mode == RoboI18nSearchCanonical ||
        searcher->options.mode == RoboI18nSearchCanonicalCasefold ||
        searcher->options.ignoreDiacritics) {
        Uchar32 *norm = NULL;
        size_t nn = 0;
        r = UnicodeNormalize(cps, n, nform, &norm, &nn);
        RoboFree(cps);
        cps = norm;
        n = nn;
        if (r != RoboOk) {
            return r;
        }
    }

    if (searcher->options.ignoreDiacritics &&
        searcher->options.mode != RoboI18nSearchCollation) {
        r = StripMn(cps, n, &n);
        if (r != RoboOk) {
            RoboFree(cps);
            return r;
        }
    }

    if (searcher->options.mode == RoboI18nSearchCasefold ||
        searcher->options.mode == RoboI18nSearchCanonicalCasefold) {
        Uchar32 *folded = NULL;
        size_t fn = 0;
        r = UnicodeCaseMapCps(cps, n, RoboUnicodeCaseFold, &folded, &fn);
        RoboFree(cps);
        if (r != RoboOk) {
            return r;
        }
        cps = folded;
        n = fn;
    }

    *outCps = cps;
    *outN = n;
    return RoboOk;
}

static int CpsCmp(const Uchar32 *a, size_t an, const Uchar32 *b, size_t bn)
{
    size_t i;
    size_t n = an < bn ? an : bn;
    for (i = 0; i < n; i++) {
        if (a[i] < b[i]) {
            return -1;
        }
        if (a[i] > b[i]) {
            return 1;
        }
    }
    if (an < bn) {
        return -1;
    }
    if (an > bn) {
        return 1;
    }
    return 0;
}

Result I18nSearchCompare(const I18nSearcher *searcher,
                                      const char *a,
                                      size_t aLen,
                                      const char *b,
                                      size_t bLen,
                                      int *outCmp)
{
    Result r;
    if (!searcher || !searcher->ready || !outCmp) {
        return RoboInvalidArgument;
    }
    r = CheckInput(a, aLen);
    if (r != RoboOk) {
        return r;
    }
    r = CheckInput(b, bLen);
    if (r != RoboOk) {
        return r;
    }

    if (searcher->options.mode == RoboI18nSearchExact) {
        size_t n = aLen < bLen ? aLen : bLen;
        int c = 0;
        if (n > 0) {
            c = memcmp(a, b, n);
        }
        if (c == 0) {
            c = (aLen > bLen) - (aLen < bLen);
        }
        *outCmp = c;
        return RoboOk;
    }

    if (searcher->options.mode == RoboI18nSearchCollation) {
        if (!searcher->collator) {
            return RoboInvalidArgument;
        }
        return I18nCollatorCompare(searcher->collator, a, aLen, b, bLen, outCmp);
    }

    {
        Uchar32 *ac = NULL;
        Uchar32 *bc = NULL;
        size_t an = 0;
        size_t bn = 0;
        r = TransformCps(searcher, a, aLen, &ac, &an);
        if (r != RoboOk) {
            return r;
        }
        r = TransformCps(searcher, b, bLen, &bc, &bn);
        if (r != RoboOk) {
            RoboFree(ac);
            return r;
        }
        if (searcher->options.mode == RoboI18nSearchCodepoint &&
            !searcher->options.ignoreDiacritics) {
            /* TransformCps for CODEPOINT without diacritics is decode only */
        }
        *outCmp = CpsCmp(ac, an, bc, bn);
        RoboFree(ac);
        RoboFree(bc);
        return RoboOk;
    }
}

Result I18nSearchEqual(const I18nSearcher *searcher,
                                    const char *a,
                                    size_t aLen,
                                    const char *b,
                                    size_t bLen,
                                    int *outEqual)
{
    int cmp = 1;
    Result r = I18nSearchCompare(searcher, a, aLen, b, bLen, &cmp);
    if (r != RoboOk) {
        return r;
    }
    *outEqual = (cmp == 0) ? 1 : 0;
    return RoboOk;
}

/* ---- Boundary enumeration ---- */

#define RoboI18nSearchBoundMax 8192u

static Result CollectCpBounds(const char *s,
                                       size_t len,
                                       size_t *bounds,
                                       size_t boundCap,
                                       size_t *outCount)
{
    size_t off = 0;
    size_t n = 0;
    if (boundCap < 1) {
        return RoboError;
    }
    bounds[n++] = 0;
    while (off < len) {
        size_t next = off;
        if (UnicodeUtf8Decode(s, len, off, RoboUnicodeStrict, NULL, &next) !=
            RoboUnicodeOk) {
            return RoboParseError;
        }
        if (n >= boundCap) {
            return RoboError;
        }
        bounds[n++] = next;
        off = next;
    }
    *outCount = n;
    return RoboOk;
}

static Result CollectGraphemeBounds(const char *s,
                                             size_t len,
                                             size_t *bounds,
                                             size_t boundCap,
                                             size_t *outCount)
{
    UnicodeGraphemeIter it;
    Result r;
    size_t n = 0;
    size_t cs = 0;
    size_t ce = 0;

    if (boundCap < 1) {
        return RoboError;
    }
    bounds[n++] = 0;
    r = UnicodeGraphemeIterInit(&it, s, len, RoboUnicodeStrict);
    if (r != RoboOk) {
        return r;
    }
    for (;;) {
        r = UnicodeGraphemeIterNext(&it, &cs, &ce);
        if (r != RoboOk) {
            return r;
        }
        if (cs == ce && cs == len) {
            break;
        }
        if (n >= boundCap) {
            return RoboError;
        }
        if (bounds[n - 1] != ce) {
            bounds[n++] = ce;
        }
    }
    if (n == 0 || bounds[n - 1] != len) {
        if (n >= boundCap) {
            return RoboError;
        }
        bounds[n++] = len;
    }
    *outCount = n;
    return RoboOk;
}

static Result ExpandGrapheme(const char *hay,
                                     size_t hayLen,
                                     size_t start,
                                     size_t end,
                                     size_t *gs,
                                     size_t *ge)
{
    size_t cs = start;
    size_t ce = start;

    *gs = start;
    *ge = end;
    if (hayLen == 0) {
        return RoboOk;
    }
    if (start < hayLen) {
        if (UnicodeGraphemeNext(hay, hayLen, start, RoboUnicodeStrict, &cs, &ce) ==
            RoboOk) {
            /* If start is mid-cluster, walk back. */
            if (cs > start) {
                size_t ps = 0;
                size_t pe = 0;
                if (UnicodeGraphemePrev(hay, hayLen, start, RoboUnicodeStrict, &ps,
                                               &pe) == RoboOk &&
                    ps < pe) {
                    *gs = ps;
                }
            } else {
                *gs = cs;
            }
        }
    }
    if (end > 0 && end <= hayLen) {
        size_t pos = end - 1u;
        size_t ps = 0;
        size_t pe = 0;
        if (UnicodeGraphemePrev(hay, hayLen, end, RoboUnicodeStrict, &ps, &pe) ==
                RoboOk &&
            ps < pe) {
            *ge = pe;
        } else if (UnicodeGraphemeNext(hay, hayLen, pos, RoboUnicodeStrict, &ps, &pe) ==
                       RoboOk &&
                   ps < pe) {
            *ge = pe;
        }
    }
    if (*gs > start) {
        *gs = start;
    }
    if (*ge < end) {
        *ge = end;
    }
    (void)ce;
    return RoboOk;
}

static void FillMatch(const I18nSearcher *searcher,
                       const char *hay,
                       size_t hayLen,
                       size_t start,
                       size_t end,
                       I18nSearchHit *out)
{
    out->start = start;
    out->end = end;
    out->graphemeStart = start;
    out->graphemeEnd = end;
    if (searcher->options.graphemeSafe) {
        (void)ExpandGrapheme(hay, hayLen, start, end, &out->graphemeStart, &out->graphemeEnd);
    }
}

static Result WindowEqual(const I18nSearcher *searcher,
                                  const char *hay,
                                  size_t hayLen,
                                  size_t start,
                                  size_t end,
                                  const char *needle,
                                  size_t needleLen,
                                  int *outEq)
{
    (void)hayLen;
    return I18nSearchEqual(searcher, hay + start, end - start, needle, needleLen, outEq);
}

/**
 * Find first match at/after @p fromByte using windowed boundary search.
 */
static Result FindFrom(const I18nSearcher *searcher,
                               const char *hay,
                               size_t hayLen,
                               const char *needle,
                               size_t needleLen,
                               size_t fromByte,
                               int *outFound,
                               I18nSearchHit *outMatch)
{
    size_t bounds[RoboI18nSearchBoundMax];
    size_t bn = 0;
    size_t i;
    size_t j;
    Result r;
    int useGraphemeWindows = searcher->options.graphemeSafe;

    *outFound = 0;
    if (outMatch) {
        memset(outMatch, 0, sizeof(*outMatch));
    }

    if (needleLen == 0) {
        size_t at = fromByte;
        if (at > hayLen) {
            at = hayLen;
        }
        *outFound = 1;
        if (outMatch) {
            FillMatch(searcher, hay, hayLen, at, at, outMatch);
        }
        return RoboOk;
    }

    if (searcher->options.mode == RoboI18nSearchExact && !searcher->options.ignoreDiacritics) {
        /* Fast path: byte search; validate needle already checked. */
        size_t pos = fromByte;
        while (pos + needleLen <= hayLen) {
            if (memcmp(hay + pos, needle, needleLen) == 0) {
                *outFound = 1;
                if (outMatch) {
                    FillMatch(searcher, hay, hayLen, pos, pos + needleLen, outMatch);
                }
                return RoboOk;
            }
            {
                size_t next = pos;
                if (UnicodeUtf8Decode(hay, hayLen, pos, RoboUnicodeStrict, NULL, &next) !=
                    RoboUnicodeOk) {
                    return RoboParseError;
                }
                pos = next;
            }
        }
        return RoboOk;
    }

    if (useGraphemeWindows) {
        r = CollectGraphemeBounds(hay, hayLen, bounds, RoboI18nSearchBoundMax, &bn);
    } else {
        r = CollectCpBounds(hay, hayLen, bounds, RoboI18nSearchBoundMax, &bn);
    }
    if (r != RoboOk) {
        return r;
    }

    for (i = 0; i + 1 < bn; i++) {
        size_t start = bounds[i];
        if (start < fromByte) {
            continue;
        }
        for (j = i + 1; j < bn; j++) {
            size_t end = bounds[j];
            int eq = 0;
            /* Length heuristic for exact-ish modes */
            if (searcher->options.mode == RoboI18nSearchCodepoint &&
                !searcher->options.ignoreDiacritics && end - start > needleLen * 4u + 16u &&
                end - start > needleLen + 64u) {
                /* still try — casefold/norm can shrink */
            }
            r = WindowEqual(searcher, hay, hayLen, start, end, needle, needleLen, &eq);
            if (r != RoboOk) {
                return r;
            }
            if (eq) {
                *outFound = 1;
                if (outMatch) {
                    FillMatch(searcher, hay, hayLen, start, end, outMatch);
                }
                return RoboOk;
            }
        }
    }
    return RoboOk;
}

Result I18nSearchFind(const I18nSearcher *searcher,
                                   const char *haystack,
                                   size_t hayLen,
                                   const char *needle,
                                   size_t needleLen,
                                   int *outFound,
                                   I18nSearchHit *outMatch)
{
    Result r;
    if (!searcher || !searcher->ready || !outFound) {
        return RoboInvalidArgument;
    }
    r = CheckInput(haystack, hayLen);
    if (r != RoboOk) {
        return r;
    }
    r = CheckInput(needle, needleLen);
    if (r != RoboOk) {
        return r;
    }
    return FindFrom(searcher, haystack, hayLen, needle, needleLen, 0, outFound, outMatch);
}

Result I18nSearchContains(const I18nSearcher *searcher,
                                       const char *haystack,
                                       size_t hayLen,
                                       const char *needle,
                                       size_t needleLen,
                                       int *outFound)
{
    return I18nSearchFind(searcher, haystack, hayLen, needle, needleLen, outFound, NULL);
}

Result I18nSearchStartsWith(const I18nSearcher *searcher,
                                          const char *haystack,
                                          size_t hayLen,
                                          const char *needle,
                                          size_t needleLen,
                                          int *outFound)
{
    size_t bounds[RoboI18nSearchBoundMax];
    size_t bn = 0;
    size_t j;
    Result r;

    if (!searcher || !searcher->ready || !outFound) {
        return RoboInvalidArgument;
    }
    *outFound = 0;
    r = CheckInput(haystack, hayLen);
    if (r != RoboOk) {
        return r;
    }
    r = CheckInput(needle, needleLen);
    if (r != RoboOk) {
        return r;
    }
    if (needleLen == 0) {
        *outFound = 1;
        return RoboOk;
    }
    if (searcher->options.graphemeSafe) {
        r = CollectGraphemeBounds(haystack, hayLen, bounds, RoboI18nSearchBoundMax, &bn);
    } else {
        r = CollectCpBounds(haystack, hayLen, bounds, RoboI18nSearchBoundMax, &bn);
    }
    if (r != RoboOk) {
        return r;
    }
    for (j = 1; j < bn; j++) {
        int eq = 0;
        r = WindowEqual(searcher, haystack, hayLen, 0, bounds[j], needle, needleLen, &eq);
        if (r != RoboOk) {
            return r;
        }
        if (eq) {
            *outFound = 1;
            return RoboOk;
        }
    }
    return RoboOk;
}

Result I18nSearchEndsWith(const I18nSearcher *searcher,
                                        const char *haystack,
                                        size_t hayLen,
                                        const char *needle,
                                        size_t needleLen,
                                        int *outFound)
{
    size_t bounds[RoboI18nSearchBoundMax];
    size_t bn = 0;
    size_t i;
    Result r;

    if (!searcher || !searcher->ready || !outFound) {
        return RoboInvalidArgument;
    }
    *outFound = 0;
    r = CheckInput(haystack, hayLen);
    if (r != RoboOk) {
        return r;
    }
    r = CheckInput(needle, needleLen);
    if (r != RoboOk) {
        return r;
    }
    if (needleLen == 0) {
        *outFound = 1;
        return RoboOk;
    }
    if (searcher->options.graphemeSafe) {
        r = CollectGraphemeBounds(haystack, hayLen, bounds, RoboI18nSearchBoundMax, &bn);
    } else {
        r = CollectCpBounds(haystack, hayLen, bounds, RoboI18nSearchBoundMax, &bn);
    }
    if (r != RoboOk) {
        return r;
    }
    for (i = 0; i + 1 < bn; i++) {
        int eq = 0;
        r = WindowEqual(searcher, haystack, hayLen, bounds[i], hayLen, needle, needleLen, &eq);
        if (r != RoboOk) {
            return r;
        }
        if (eq) {
            *outFound = 1;
            return RoboOk;
        }
    }
    return RoboOk;
}

Result I18nSearchIterInit(I18nSearchIter *it,
                                        const I18nSearcher *searcher,
                                        const char *haystack,
                                        size_t hayLen,
                                        const char *needle,
                                        size_t needleLen)
{
    Result r;
    if (!it || !searcher || !searcher->ready) {
        return RoboInvalidArgument;
    }
    r = CheckInput(haystack, hayLen);
    if (r != RoboOk) {
        return r;
    }
    r = CheckInput(needle, needleLen);
    if (r != RoboOk) {
        return r;
    }
    memset(it, 0, sizeof(*it));
    it->searcher = searcher;
    it->haystack = haystack;
    it->hayLen = hayLen;
    it->needle = needle;
    it->needleLen = needleLen;
    it->nextStart = 0;
    it->active = 1;
    return RoboOk;
}

Result I18nSearchIterNext(I18nSearchIter *it,
                                        int *outFound,
                                        I18nSearchHit *outMatch)
{
    Result r;
    I18nSearchHit m;
    int found = 0;

    if (!it || !it->active || !outFound) {
        return RoboInvalidArgument;
    }
    *outFound = 0;
    if (it->nextStart > it->hayLen) {
        return RoboOk;
    }
    r = FindFrom(it->searcher, it->haystack, it->hayLen, it->needle, it->needleLen,
                  it->nextStart, &found, &m);
    if (r != RoboOk) {
        return r;
    }
    if (!found) {
        it->nextStart = it->hayLen + 1u;
        return RoboOk;
    }
    *outFound = 1;
    if (outMatch) {
        *outMatch = m;
    }
    if (it->needleLen == 0) {
        /* Advance one grapheme to avoid infinite empty matches. */
        size_t cs = 0;
        size_t ce = 0;
        if (m.end >= it->hayLen) {
            it->nextStart = it->hayLen + 1u;
        } else if (UnicodeGraphemeNext(it->haystack, it->hayLen, m.end, RoboUnicodeStrict,
                                              &cs, &ce) == RoboOk &&
                   ce > m.end) {
            it->nextStart = ce;
        } else {
            size_t next = m.end;
            if (UnicodeUtf8Decode(it->haystack, it->hayLen, m.end, RoboUnicodeStrict, NULL,
                                         &next) == RoboUnicodeOk) {
                it->nextStart = next;
            } else {
                it->nextStart = it->hayLen + 1u;
            }
        }
    } else {
        it->nextStart = m.end;
        if (it->nextStart == m.start) {
            it->nextStart++;
        }
    }
    return RoboOk;
}

Result I18nSearchFindAll(const I18nSearcher *searcher,
                                       const char *haystack,
                                       size_t hayLen,
                                       const char *needle,
                                       size_t needleLen,
                                       I18nSearchHit *outMatches,
                                       size_t outCap,
                                       size_t *outCount)
{
    I18nSearchIter it;
    Result r;
    size_t n = 0;
    size_t cap;

    if (!outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0;
    if (outCap > 0 && !outMatches) {
        return RoboInvalidArgument;
    }
    cap = outCap;
    if (cap > RoboI18nSearchFindAllMax) {
        cap = RoboI18nSearchFindAllMax;
    }
    r = I18nSearchIterInit(&it, searcher, haystack, hayLen, needle, needleLen);
    if (r != RoboOk) {
        return r;
    }
    while (n < cap) {
        int found = 0;
        I18nSearchHit m;
        r = I18nSearchIterNext(&it, &found, &m);
        if (r != RoboOk) {
            return r;
        }
        if (!found) {
            break;
        }
        outMatches[n++] = m;
    }
    *outCount = n;
    return RoboOk;
}
