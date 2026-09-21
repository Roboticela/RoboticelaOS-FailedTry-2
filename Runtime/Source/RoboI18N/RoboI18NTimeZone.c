/*
 * Time-zone abstraction + IANA tzdb (Goals 4.5 + 4.14).
 */

#include "RoboI18N/RoboI18NTimeZone.h"

#include "RoboI18N/RoboTzData.h"

#include <limits.h>
#include <string.h>

const char *I18nTzdbVersionString(void)
{
    return TzdbVersionString();
}

Result I18nTimezoneUtc(I18nTimezone *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RoboI18nTzUtc;
    out->offsetSeconds = 0;
    memcpy(out->id, "UTC", 4);
    return RoboOk;
}

Result I18nTimezoneFixedSeconds(int32_t offsetSeconds,
                                               I18nTimezone *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (offsetSeconds < -18 * 3600 || offsetSeconds > 18 * 3600) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RoboI18nTzFixed;
    out->offsetSeconds = offsetSeconds;
    {
        int32_t a = offsetSeconds < 0 ? -offsetSeconds : offsetSeconds;
        int h = a / 3600;
        int m = (a % 3600) / 60;
        char buf[16];
        int n = 0;
        buf[n++] = offsetSeconds < 0 ? '-' : '+';
        buf[n++] = (char)('0' + h / 10);
        buf[n++] = (char)('0' + h % 10);
        buf[n++] = ':';
        buf[n++] = (char)('0' + m / 10);
        buf[n++] = (char)('0' + m % 10);
        buf[n] = '\0';
        memcpy(out->id, buf, (size_t)n + 1u);
    }
    return RoboOk;
}

static int ParseOffset(const char *s, size_t len, int32_t *outSec)
{
    size_t i = 0;
    int sign = 1;
    int h = 0;
    int m = 0;
    int sec = 0;
    if (i < len && (s[i] == '+' || s[i] == '-')) {
        sign = s[i] == '-' ? -1 : 1;
        i++;
    } else {
        return 0;
    }
    if (i + 2 > len || s[i] < '0' || s[i] > '9' || s[i + 1] < '0' || s[i + 1] > '9') {
        return 0;
    }
    h = (s[i] - '0') * 10 + (s[i + 1] - '0');
    i += 2;
    if (i < len && s[i] == ':') {
        i++;
        if (i + 2 > len) {
            return 0;
        }
        m = (s[i] - '0') * 10 + (s[i + 1] - '0');
        i += 2;
        if (i < len && s[i] == ':') {
            i++;
            if (i + 2 > len) {
                return 0;
            }
            sec = (s[i] - '0') * 10 + (s[i + 1] - '0');
            i += 2;
        }
    } else if (i + 2 <= len && s[i] >= '0' && s[i] <= '9') {
        m = (s[i] - '0') * 10 + (s[i + 1] - '0');
        i += 2;
    }
    if (i != len || h > 18 || m > 59 || sec > 59) {
        return 0;
    }
    *outSec = sign * (h * 3600 + m * 60 + sec);
    return 1;
}

static int EqCi(const char *a, size_t alen, const char *b)
{
    size_t i;
    size_t blen = strlen(b);
    if (alen != blen) {
        return 0;
    }
    for (i = 0; i < alen; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

static int CmpId(const char *a, size_t alen, const char *b)
{
    size_t blen = strlen(b);
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) {
        return c;
    }
    if (alen < blen) {
        return -1;
    }
    if (alen > blen) {
        return 1;
    }
    return 0;
}

static const TzZone *FindZoneById(const char *id, size_t len)
{
    uint32_t lo = 0;
    uint32_t hi = RoboTzZoneCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const char *zid = TzString(TzZones[mid].id);
        int c = CmpId(id, len, zid);
        if (c == 0) {
            return &TzZones[mid];
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return NULL;
}

static const char *ResolveAlias(const char *id, size_t len)
{
    uint32_t lo = 0;
    uint32_t hi = RoboTzAliasCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const char *aid = TzString(TzAliases[mid].aliasId);
        int c = CmpId(id, len, aid);
        if (c == 0) {
            return TzString(TzAliases[mid].canonicalId);
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return NULL;
}

static const TzZone *LookupIana(const char *id, size_t len)
{
    const TzZone *z = FindZoneById(id, len);
    const char *canon;
    if (z) {
        return z;
    }
    canon = ResolveAlias(id, len);
    if (!canon) {
        return NULL;
    }
    return FindZoneById(canon, strlen(canon));
}

Result I18nTimezoneFromId(const char *id,
                                         size_t len,
                                         I18nTimezone *out)
{
    int32_t off = 0;
    size_t i = 0;
    const TzZone *z;
    if (!id || !out || len == 0) {
        return RoboInvalidArgument;
    }
    while (i < len && (id[i] == ' ' || id[i] == '\t')) {
        i++;
    }
    id += i;
    len -= i;
    if (EqCi(id, len, "UTC") || EqCi(id, len, "Z") || EqCi(id, len, "Etc/UTC") ||
        EqCi(id, len, "Etc/GMT")) {
        return I18nTimezoneUtc(out);
    }
    if (EqCi(id, len, "local") || EqCi(id, len, "system")) {
        memset(out, 0, sizeof(*out));
        out->kind = RoboI18nTzSystem;
        memcpy(out->id, "local", 6);
        return RoboOk;
    }
    if (len >= 3 && (id[0] == 'U' || id[0] == 'u') && (id[1] == 'T' || id[1] == 't') &&
        (id[2] == 'C' || id[2] == 'c') && (len == 3 || id[3] == '+' || id[3] == '-')) {
        if (len == 3) {
            return I18nTimezoneUtc(out);
        }
        if (ParseOffset(id + 3, len - 3, &off)) {
            return I18nTimezoneFixedSeconds(off, out);
        }
        return RoboParseError;
    }
    if (id[0] == '+' || id[0] == '-') {
        if (ParseOffset(id, len, &off)) {
            return I18nTimezoneFixedSeconds(off, out);
        }
        return RoboParseError;
    }
    if (len > RoboI18nTzIdMax) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '/' || c == '-' || c == '+')) {
            return RoboParseError;
        }
    }
    z = LookupIana(id, len);
    if (!z) {
        return RoboNotFound;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RoboI18nTzIana;
    memcpy(out->id, id, len);
    out->id[len] = '\0';
    return RoboOk;
}

I18nTimezoneKind MakeI18nTimezoneKind(const I18nTimezone *tz)
{
    return tz ? tz->kind : RoboI18nTzUtc;
}

const char *I18nTimezoneId(const I18nTimezone *tz)
{
    return tz ? tz->id : "";
}

Result I18nTimezoneCanonicalId(const I18nTimezone *tz,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen)
{
    const char *canon;
    size_t n;
    if (!tz || !outLen) {
        return RoboInvalidArgument;
    }
    if (tz->kind == RoboI18nTzSystem) {
        return RoboNotSupported;
    }
    if (tz->kind == RoboI18nTzUtc || tz->kind == RoboI18nTzFixed) {
        canon = tz->id;
    } else {
        const TzZone *z = LookupIana(tz->id, strlen(tz->id));
        if (!z) {
            return RoboNotFound;
        }
        canon = TzString(z->id);
    }
    n = strlen(canon);
    *outLen = n;
    if (!out) {
        return RoboOk;
    }
    if (outCap < n + 1u) {
        *outLen = n + 1u;
        return RoboError;
    }
    memcpy(out, canon, n + 1u);
    return RoboOk;
}

typedef struct tzAt {
    int32_t offset;
    int isDst;
    const char *abbr;
} TzAt;

static void ZoneAtSec(const TzZone *z, int64_t unixSec, TzAt *out)
{
    uint32_t lo = z->transStart;
    uint32_t hi = z->transStart + z->transCount;
    uint32_t best = (uint32_t)-1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (TzTransTable[mid].unixSec <= unixSec) {
            best = mid;
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (best == (uint32_t)-1) {
        out->offset = z->initialOffset;
        out->isDst = z->initialDst;
        out->abbr = TzString(z->initialAbbr);
        return;
    }
    out->offset = TzTransTable[best].offset;
    out->isDst = TzTransTable[best].isDst;
    out->abbr = TzString(TzTransTable[best].abbr);
}

static Result IanaAt(const I18nTimezone *tz,
                             const I18nInstant *instant,
                             TzAt *out)
{
    const TzZone *z;
    int64_t sec;
    if (!tz || !instant || !out) {
        return RoboInvalidArgument;
    }
    z = LookupIana(tz->id, strlen(tz->id));
    if (!z) {
        return RoboNotFound;
    }
    /* Floor-divide toward -inf for negative instants. */
    if (instant->unixMs >= 0) {
        sec = instant->unixMs / 1000;
    } else {
        sec = -(((-instant->unixMs) + 999) / 1000);
    }
    ZoneAtSec(z, sec, out);
    return RoboOk;
}

Result I18nTimezoneOffsetAt(const I18nTimezone *tz,
                                           const I18nInstant *instant,
                                           int32_t *outOffsetSeconds)
{
    TzAt at;
    Result r;
    if (!tz || !outOffsetSeconds) {
        return RoboInvalidArgument;
    }
    if (tz->kind == RoboI18nTzUtc || tz->kind == RoboI18nTzFixed) {
        *outOffsetSeconds = tz->offsetSeconds;
        return RoboOk;
    }
    if (tz->kind == RoboI18nTzSystem) {
        return RoboNotSupported;
    }
    r = IanaAt(tz, instant, &at);
    if (r != RoboOk) {
        return r;
    }
    *outOffsetSeconds = at.offset;
    return RoboOk;
}

Result I18nTimezoneIsDstAt(const I18nTimezone *tz,
                                           const I18nInstant *instant,
                                           int *outIsDst)
{
    TzAt at;
    Result r;
    if (!tz || !outIsDst) {
        return RoboInvalidArgument;
    }
    if (tz->kind == RoboI18nTzUtc || tz->kind == RoboI18nTzFixed) {
        *outIsDst = 0;
        return RoboOk;
    }
    if (tz->kind == RoboI18nTzSystem) {
        return RoboNotSupported;
    }
    r = IanaAt(tz, instant, &at);
    if (r != RoboOk) {
        return r;
    }
    *outIsDst = at.isDst ? 1 : 0;
    return RoboOk;
}

Result I18nTimezoneAbbreviationAt(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 char *out,
                                                 size_t outCap,
                                                 size_t *outLen)
{
    TzAt at;
    Result r;
    const char *abbr;
    size_t n;
    if (!tz || !outLen) {
        return RoboInvalidArgument;
    }
    if (tz->kind == RoboI18nTzUtc) {
        abbr = "UTC";
    } else if (tz->kind == RoboI18nTzFixed) {
        abbr = tz->id;
    } else if (tz->kind == RoboI18nTzSystem) {
        return RoboNotSupported;
    } else {
        r = IanaAt(tz, instant, &at);
        if (r != RoboOk) {
            return r;
        }
        abbr = at.abbr ? at.abbr : "";
    }
    n = strlen(abbr);
    *outLen = n;
    if (!out) {
        return RoboOk;
    }
    if (outCap < n + 1u) {
        *outLen = n + 1u;
        return RoboError;
    }
    memcpy(out, abbr, n + 1u);
    return RoboOk;
}

Result I18nTimezoneToLocal(const I18nTimezone *tz,
                                          const I18nInstant *instant,
                                          I18nDatetime *outLocal,
                                          I18nTzResolve *outResolve)
{
    int32_t off;
    I18nInstant shifted;
    Result r;
    if (!tz || !instant || !outLocal) {
        return RoboInvalidArgument;
    }
    r = I18nTimezoneOffsetAt(tz, instant, &off);
    if (r != RoboOk) {
        return r;
    }
    if ((off > 0 && instant->unixMs > INT64_MAX - (int64_t)off * 1000) ||
        (off < 0 && instant->unixMs < INT64_MIN - (int64_t)off * 1000)) {
        return RoboError;
    }
    shifted.unixMs = instant->unixMs + (int64_t)off * 1000;
    r = I18nInstantToUtc(&shifted, outLocal);
    if (r != RoboOk) {
        return r;
    }
    if (outResolve) {
        *outResolve = RoboI18nTzUnique;
    }
    return RoboOk;
}

static int DatetimeEqual(const I18nDatetime *a, const I18nDatetime *b)
{
    return a->date.year == b->date.year && a->date.month == b->date.month &&
           a->date.day == b->date.day && a->time.hour == b->time.hour &&
           a->time.minute == b->time.minute && a->time.second == b->time.second;
}

Result I18nTimezoneToUtcEx(const I18nTimezone *tz,
                                           const I18nDatetime *local,
                                           I18nTzDisambiguation policy,
                                           I18nInstant *outInstant,
                                           I18nTzResolve *outResolve)
{
    I18nInstant asUtc;
    Result r;
    int32_t off;
    if (!tz || !local || !outInstant) {
        return RoboInvalidArgument;
    }
    if (tz->kind == RoboI18nTzSystem) {
        return RoboNotSupported;
    }
    if (tz->kind == RoboI18nTzUtc || tz->kind == RoboI18nTzFixed) {
        off = tz->offsetSeconds;
        r = I18nInstantFromUtc(local, &asUtc);
        if (r != RoboOk) {
            return r;
        }
        if ((off > 0 && asUtc.unixMs < INT64_MIN + (int64_t)off * 1000) ||
            (off < 0 && asUtc.unixMs > INT64_MAX + (int64_t)off * 1000)) {
            return RoboError;
        }
        outInstant->unixMs = asUtc.unixMs - (int64_t)off * 1000;
        if (outResolve) {
            *outResolve = RoboI18nTzUnique;
        }
        return RoboOk;
    }
    {
        const TzZone *z = LookupIana(tz->id, strlen(tz->id));
        I18nInstant candidates[4];
        int nCand = 0;
        int64_t probeSec;
        uint32_t i;
        int64_t offsetsTry[8];
        int nOff = 0;

        if (!z) {
            return RoboNotFound;
        }
        r = I18nInstantFromUtc(local, &asUtc);
        if (r != RoboOk) {
            return r;
        }
        probeSec = asUtc.unixMs / 1000;

        /* Collect distinct offsets from nearby transitions + initial. */
        offsetsTry[nOff++] = z->initialOffset;
        for (i = 0; i < z->transCount; i++) {
            const TzTrans *tr = &TzTransTable[z->transStart + i];
            int64_t d = tr->unixSec - probeSec;
            if (d < -4 * 86400LL || d > 4 * 86400LL) {
                continue;
            }
            {
                int j;
                int seen = 0;
                for (j = 0; j < nOff; j++) {
                    if (offsetsTry[j] == tr->offset) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen && nOff < 8) {
                    offsetsTry[nOff++] = tr->offset;
                }
            }
        }
        /* Also offset immediately before/after probe via zone_at. */
        {
            TzAt at;
            int j;
            int seen = 0;
            ZoneAtSec(z, probeSec, &at);
            for (j = 0; j < nOff; j++) {
                if (offsetsTry[j] == at.offset) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && nOff < 8) {
                offsetsTry[nOff++] = at.offset;
            }
            ZoneAtSec(z, probeSec - 86400, &at);
            seen = 0;
            for (j = 0; j < nOff; j++) {
                if (offsetsTry[j] == at.offset) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && nOff < 8) {
                offsetsTry[nOff++] = at.offset;
            }
            ZoneAtSec(z, probeSec + 86400, &at);
            seen = 0;
            for (j = 0; j < nOff; j++) {
                if (offsetsTry[j] == at.offset) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && nOff < 8) {
                offsetsTry[nOff++] = at.offset;
            }
        }

        for (i = 0; i < (uint32_t)nOff; i++) {
            int32_t o = (int32_t)offsetsTry[i];
            I18nInstant cand;
            I18nDatetime back;
            I18nTzResolve ignore;
            TzAt at;
            cand.unixMs = asUtc.unixMs - (int64_t)o * 1000;
            ZoneAtSec(z, cand.unixMs >= 0 ? cand.unixMs / 1000
                                             : -(((-cand.unixMs) + 999) / 1000),
                        &at);
            if (at.offset != o) {
                continue;
            }
            if (I18nTimezoneToLocal(tz, &cand, &back, &ignore) != RoboOk) {
                continue;
            }
            /* Compare wall fields ignoring nano. */
            back.time.nano = local->time.nano;
            if (!DatetimeEqual(&back, local)) {
                continue;
            }
            if (nCand < 4) {
                candidates[nCand++] = cand;
            }
        }

        if (nCand == 0) {
            if (outResolve) {
                *outResolve = RoboI18nTzNonexistent;
            }
            return RoboInvalidArgument;
        }
        if (nCand == 1) {
            *outInstant = candidates[0];
            if (outResolve) {
                *outResolve = RoboI18nTzUnique;
            }
            return RoboOk;
        }
        /* Ambiguous: sort by unixMs. */
        if (candidates[0].unixMs > candidates[1].unixMs) {
            I18nInstant tmp = candidates[0];
            candidates[0] = candidates[1];
            candidates[1] = tmp;
        }
        if (outResolve) {
            *outResolve = RoboI18nTzAmbiguous;
        }
        if (policy == RoboI18nTzDisambigReject) {
            return RoboInvalidArgument;
        }
        if (policy == RoboI18nTzDisambigLater) {
            *outInstant = candidates[1];
        } else {
            *outInstant = candidates[0]; /* EARLIER */
        }
        return RoboOk;
    }
}

Result I18nTimezoneToUtc(const I18nTimezone *tz,
                                        const I18nDatetime *local,
                                        I18nInstant *outInstant,
                                        I18nTzResolve *outResolve)
{
    return I18nTimezoneToUtcEx(tz, local, RoboI18nTzDisambigEarlier, outInstant,
                                        outResolve);
}

static void FillTransition(const TzZone *z,
                            uint32_t idx,
                            I18nTzTransition *out)
{
    TzAt before;
    const TzTrans *tr = &TzTransTable[idx];
    int64_t beforeSec = tr->unixSec - 1;
    memset(out, 0, sizeof(*out));
    out->at.unixMs = tr->unixSec * 1000;
    ZoneAtSec(z, beforeSec, &before);
    out->offsetBefore = before.offset;
    out->isDstBefore = before.isDst;
    strncpy(out->abbrBefore, before.abbr ? before.abbr : "", RoboI18nTzAbbrMax - 1u);
    out->offsetAfter = tr->offset;
    out->isDstAfter = tr->isDst;
    strncpy(out->abbrAfter, TzString(tr->abbr), RoboI18nTzAbbrMax - 1u);
}

Result I18nTimezoneNextTransition(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 I18nTzTransition *out)
{
    const TzZone *z;
    int64_t sec;
    uint32_t lo;
    uint32_t hi;
    if (!tz || !instant || !out) {
        return RoboInvalidArgument;
    }
    if (tz->kind != RoboI18nTzIana) {
        return RoboNotFound;
    }
    z = LookupIana(tz->id, strlen(tz->id));
    if (!z || z->transCount == 0) {
        return RoboNotFound;
    }
    sec = instant->unixMs >= 0 ? instant->unixMs / 1000
                                : -(((-instant->unixMs) + 999) / 1000);
    lo = z->transStart;
    hi = z->transStart + z->transCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (TzTransTable[mid].unixSec <= sec) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= z->transStart + z->transCount) {
        return RoboNotFound;
    }
    FillTransition(z, lo, out);
    return RoboOk;
}

Result I18nTimezonePrevTransition(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 I18nTzTransition *out)
{
    const TzZone *z;
    int64_t sec;
    uint32_t lo;
    uint32_t hi;
    uint32_t best;
    if (!tz || !instant || !out) {
        return RoboInvalidArgument;
    }
    if (tz->kind != RoboI18nTzIana) {
        return RoboNotFound;
    }
    z = LookupIana(tz->id, strlen(tz->id));
    if (!z || z->transCount == 0) {
        return RoboNotFound;
    }
    sec = instant->unixMs >= 0 ? instant->unixMs / 1000
                                : -(((-instant->unixMs) + 999) / 1000);
    lo = z->transStart;
    hi = z->transStart + z->transCount;
    best = (uint32_t)-1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (TzTransTable[mid].unixSec < sec) {
            best = mid;
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (best == (uint32_t)-1) {
        return RoboNotFound;
    }
    FillTransition(z, best, out);
    return RoboOk;
}
