/*
 * RoboHTML Phase 8.13 — anchors and navigation.
 *
 * Three layers, each with one job:
 *
 *   attributes   read `href` / `target` / `rel` / `download` / `title` off an
 *                element. No resolution, no navigation.
 *   target       HtmlNavigationTarget — one resolved reference, plus
 *                the scheme policy that decides whether it may travel.
 *                Parsing is pure; nothing here performs I/O.
 *   activation   build a request, then either answer it here (a fragment,
 *                which needs only a tree walk) or hand it to the caller's
 *                seam. The outcome vocabulary deliberately has no
 *                `NAVIGATED`: there is no way to spell "success" for
 *                navigation that did not happen.
 *
 * The resolution algorithm is small on purpose. It handles the forms an
 * application route actually uses — root-relative, relative, `./`, `../`,
 * fragment-only, absolute URI — and refuses the rest rather than guessing.
 * What it does not do (percent-encoding, IDNA, backslash-as-slash,
 * protocol-relative references) is listed in the header, because a URL
 * library is a later phase's business and half of one here would be a promise
 * this code cannot keep.
 */

#include "RoboHTML/RoboHTMLAnchor.h"

#include "RoboBase/RoboError.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Character helpers                                                   */
/* ------------------------------------------------------------------ */

static int AsciiIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

static char AsciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int AsciiIsAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int AsciiIsDigit(char c)
{
    return c >= '0' && c <= '9';
}

/* Scheme-name characters after the first: ALPHA / DIGIT / "+" / "-" / ".". */
static int AsciiIsSchemeChar(char c)
{
    return AsciiIsAlpha(c) || AsciiIsDigit(c) || c == '+' || c == '-' ||
           c == '.';
}

/*
 * A C0 control byte, or DEL.
 *
 * Every one of them is either unrepresentable in a reference or something a
 * caller will misread: a NUL inside `href` silently truncates at the first
 * C-string boundary, and a stray ESC or BEL has no business in a path. Tab,
 * LF, CR, FF, VT and space are trimmed at the ends but refused *inside*,
 * which is the strict reading and the one that stops "  /a  " and "/a"
 * from resolving differently for two callers.
 */
static int IsRefusedByte(char c)
{
    unsigned char u = (unsigned char)c;

    return u < 0x20u || u == 0x7Fu;
}

static void CopySpan(char *dst, const char *src, size_t length)
{
    if (length > 0u) {
        memcpy(dst, src, length);
    }
}

/* ------------------------------------------------------------------ */
/* Kind / context / reason vocabularies                                */
/* ------------------------------------------------------------------ */

int HtmlTargetKindIsKnown(HtmlTargetKind kind)
{
    return kind >= RoboHtmlTargetNone && kind <= RoboHtmlTargetInvalid;
}

const char *HtmlTargetKindName(HtmlTargetKind kind)
{
    switch (kind) {
    case RoboHtmlTargetNone:
        return "none";
    case RoboHtmlTargetRoute:
        return "route";
    case RoboHtmlTargetFragment:
        return "fragment";
    case RoboHtmlTargetExternal:
        return "external";
    case RoboHtmlTargetInvalid:
        return "invalid";
    default:
        return "unknown";
    }
}

int HtmlLinkTargetContextIsKnown(HtmlLinkTargetContext c)
{
    return c >= RoboHtmlLinkTargetContextCurrent &&
           c <= RoboHtmlLinkTargetContextUnknown;
}

const char *HtmlLinkTargetContextName(
    HtmlLinkTargetContext context)
{
    switch (context) {
    case RoboHtmlLinkTargetContextCurrent:
        return "current";
    case RoboHtmlLinkTargetContextNamed:
        return "named";
    case RoboHtmlLinkTargetContextNew:
        return "new";
    case RoboHtmlLinkTargetContextParent:
        return "parent";
    case RoboHtmlLinkTargetContextTop:
        return "top";
    case RoboHtmlLinkTargetContextUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

/** Case-insensitive comparison of @p aLen bytes against a NUL-terminated @p b. */
static int SpanEqAsciiCi(const char *a, size_t aLen, const char *b)
{
    size_t i;

    if (!a || !b) {
        return 0;
    }
    for (i = 0u; i < aLen; i++) {
        if (b[i] == '\0' || AsciiLower(a[i]) != AsciiLower(b[i])) {
            return 0;
        }
    }
    return b[aLen] == '\0';
}

HtmlLinkTargetContext HtmlLinkTargetContextFor(
    const char *target, size_t length)
{
    if (!target || length == 0u) {
        return RoboHtmlLinkTargetContextCurrent;
    }
    if (SpanEqAsciiCi(target, length, "_self")) {
        return RoboHtmlLinkTargetContextCurrent;
    }
    if (SpanEqAsciiCi(target, length, "_blank")) {
        return RoboHtmlLinkTargetContextNew;
    }
    if (SpanEqAsciiCi(target, length, "_parent")) {
        return RoboHtmlLinkTargetContextParent;
    }
    if (SpanEqAsciiCi(target, length, "_top")) {
        return RoboHtmlLinkTargetContextTop;
    }
    /*
     * Any other `_`-prefixed name is reserved by the spec for a keyword this
     * runtime does not define. Reporting it as NAMED would claim the document
     * named a context when it actually asked for one we cannot honour, and
     * that distinction is the only thing this classification is for.
     */
    if (target[0] == '_') {
        return RoboHtmlLinkTargetContextUnknown;
    }
    return RoboHtmlLinkTargetContextNamed;
}

int HtmlLinkRelContainsToken(const char *rel, size_t relLength,
                                      const char *token)
{
    size_t tokenLength;
    size_t i;

    if (!rel || !token) {
        return 0;
    }
    tokenLength = strlen(token);
    if (tokenLength == 0u) {
        return 0;
    }
    i = 0u;
    while (i < relLength) {
        size_t start;

        while (i < relLength && AsciiIsSpace(rel[i])) {
            i++;
        }
        start = i;
        while (i < relLength && !AsciiIsSpace(rel[i])) {
            i++;
        }
        if (i > start && SpanEqAsciiCi(rel + start, i - start, token)) {
            return 1;
        }
    }
    return 0;
}

int HtmlNavigationReasonIsKnown(HtmlNavigationReason reason)
{
    return reason >= RoboHtmlNavigationReasonNone &&
           reason <= RoboHtmlNavigationReasonProgrammatic;
}

const char *HtmlNavigationReasonName(HtmlNavigationReason r)
{
    switch (r) {
    case RoboHtmlNavigationReasonNone:
        return "none";
    case RoboHtmlNavigationReasonActivation:
        return "activation";
    case RoboHtmlNavigationReasonProgrammatic:
        return "programmatic";
    default:
        return "unknown";
    }
}

int HtmlNavigationOutcomeKindIsKnown(
    HtmlNavigationOutcomeKind kind)
{
    return kind >= RoboHtmlNavigationNone &&
           kind <= RoboHtmlNavigationRejected;
}

const char *HtmlNavigationOutcomeKindName(
    HtmlNavigationOutcomeKind kind)
{
    switch (kind) {
    case RoboHtmlNavigationNone:
        return "none";
    case RoboHtmlNavigationNoTarget:
        return "no-target";
    case RoboHtmlNavigationRoute:
        return "route";
    case RoboHtmlNavigationFragment:
        return "fragment";
    case RoboHtmlNavigationFragmentMissing:
        return "fragment-missing";
    case RoboHtmlNavigationExternal:
        return "external";
    case RoboHtmlNavigationUnsupported:
        return "unsupported";
    case RoboHtmlNavigationDownloadPending:
        return "download-pending";
    case RoboHtmlNavigationRejected:
        return "rejected";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Scheme policy                                                       */
/* ------------------------------------------------------------------ */

typedef struct schemePolicy {
    const char *name;
    int known;
    int permitted;
} SchemePolicy;

/*
 * The whole scheme table, and the reason it is a table rather than a switch:
 * `known` and `permitted` are two columns, and the interesting rows are the
 * ones where they disagree. `file` and `data` are understood and refused;
 * `javascript` is understood and refused *hard*, because an anchor that could
 * reach script execution is the one thing a link activation must never do.
 */
static const SchemePolicy kSchemes[] = {
    {"http", 1, 1},
    {"https", 1, 1},
    {"mailto", 1, 1},
    {"tel", 1, 1},
    {"file", 1, 0},
    {"data", 1, 0},
    {"javascript", 1, 0},
    {"vbscript", 1, 0},
    {"about", 1, 0},
    {"blob", 1, 0}
};

#define K_SCHEME_COUNT (sizeof(kSchemes) / sizeof(kSchemes[0]))

static const SchemePolicy *SchemeLookup(const char *scheme, size_t length)
{
    size_t i;
    size_t j;

    if (!scheme || length == 0u) {
        return NULL;
    }
    for (i = 0u; i < K_SCHEME_COUNT; i++) {
        const char *name = kSchemes[i].name;

        for (j = 0u; j < length; j++) {
            if (name[j] == '\0' || AsciiLower(scheme[j]) != name[j]) {
                break;
            }
        }
        if (j == length && name[j] == '\0') {
            return &kSchemes[i];
        }
    }
    return NULL;
}

int HtmlNavigationSchemeIsKnown(const char *scheme, size_t length)
{
    return SchemeLookup(scheme, length) != NULL;
}

int HtmlNavigationSchemeIsPermitted(const char *scheme, size_t length)
{
    const SchemePolicy *policy = SchemeLookup(scheme, length);

    return policy != NULL && policy->permitted;
}

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

/*
 * One target is two allocations: the struct, and one text buffer holding
 * every string NUL-terminated and back to back. Two rather than one because
 * the alternative — a trailing flexible array with byte offsets hand-rolled
 * into it — buys nothing but the chance to compute an offset wrong, and this
 * code has to be readable to be reviewable.
 *
 * An offset of 0 means "absent" and every real offset is +1, so a target with
 * no strings at all (kind NONE) needs no buffer and no length juggling.
 */
struct HtmlNavigationTarget {
    HtmlTargetKind kind;
    size_t referenceOff;
    size_t referenceLen;
    size_t resolvedOff;
    size_t resolvedLen;
    size_t fragmentOff;
    size_t fragmentLen;
    size_t schemeOff;
    size_t schemeLen;
    size_t capacity;
    size_t used;
    char *text;
};

static const char *TextField(const HtmlNavigationTarget *t,
                              size_t off)
{
    if (!t || off == 0u || !t->text) {
        return NULL;
    }
    return t->text + (off - 1u);
}

/**
 * Create a target with room for @p textBytes of string data.
 *
 * @p textBytes counts what will be written, not the terminators; one extra
 * byte covers each writer's terminator. 0 gives a target whose every string
 * is absent, which is exactly what kind NONE needs.
 */
static HtmlNavigationTarget *TargetNew(
    HtmlTargetKind kind, size_t textBytes)
{
    HtmlNavigationTarget *t;

    if (textBytes > RoboHtmlTargetMaxReference * 4u) {
        return NULL;
    }
    t = (HtmlNavigationTarget *)RoboCalloc(
        1u, sizeof(HtmlNavigationTarget));
    if (!t) {
        return NULL;
    }
    t->kind = kind;
    if (textBytes > 0u) {
        t->text = (char *)RoboCalloc(1u, textBytes + 1u);
        if (!t->text) {
            RoboFree(t);
            return NULL;
        }
        t->capacity = textBytes + 1u;
    }
    return t;
}

/**
 * Write @p length bytes into the target's text buffer.
 *
 * Each string gets its own terminator and the next string starts *after* it,
 * so every field is a self-contained C string as well as a counted run. That
 * costs a byte per field and is not an optimisation anyone would choose — but
 * a caller that reads `resolved` with strlen must not be handed the bytes of
 * the string written next to it, and back-to-back packing made exactly that
 * happen: `resolved` and `reference` were written adjacently, so the
 * resolution appeared to continue into the reference.
 *
 * @return the offset+1 to record, or 0 when the buffer is full — which the
 *         callers treat as "this string is absent", because a target that
 *         silently truncated a path would be worse than one that reports it
 *         could not hold it.
 */
static size_t TargetTextPut(HtmlNavigationTarget *t,
                              const char *bytes, size_t length)
{
    size_t off;

    if (!t || !t->text) {
        return 0u;
    }
    if (t->used + length + 1u > t->capacity) {
        return 0u;
    }
    off = t->used + 1u;
    CopySpan(t->text + t->used, bytes, length);
    t->used += length;
    t->text[t->used] = '\0';
    /* Step past the terminator so the next field cannot overwrite it. */
    t->used += 1u;
    return off;
}

void HtmlNavigationTargetDestroy(HtmlNavigationTarget *target)
{
    if (!target) {
        return;
    }
    RoboFree(target->text);
    RoboFree(target);
}

HtmlTargetKind HtmlNavigationTargetKind(
    const HtmlNavigationTarget *target)
{
    return target ? target->kind : RoboHtmlTargetNone;
}

const char *HtmlNavigationTargetReference(
    const HtmlNavigationTarget *target, size_t *outLength)
{
    if (outLength) {
        *outLength = target ? target->referenceLen : 0u;
    }
    return TextField(target, target ? target->referenceOff : 0u);
}

const char *HtmlNavigationTargetResolved(
    const HtmlNavigationTarget *target, size_t *outLength)
{
    if (outLength) {
        *outLength = target ? target->resolvedLen : 0u;
    }
    return TextField(target, target ? target->resolvedOff : 0u);
}

const char *HtmlNavigationTargetFragment(
    const HtmlNavigationTarget *target, size_t *outLength)
{
    if (outLength) {
        *outLength = target ? target->fragmentLen : 0u;
    }
    return TextField(target, target ? target->fragmentOff : 0u);
}

const char *HtmlNavigationTargetScheme(
    const HtmlNavigationTarget *target, size_t *outLength)
{
    if (outLength) {
        *outLength = target ? target->schemeLen : 0u;
    }
    return TextField(target, target ? target->schemeOff : 0u);
}

int HtmlNavigationTargetIsInternal(
    const HtmlNavigationTarget *target)
{
    if (!target) {
        return 0;
    }
    return target->kind == RoboHtmlTargetRoute ||
           target->kind == RoboHtmlTargetFragment;
}

/* ------------------------------------------------------------------ */
/* Path resolution                                                     */
/* ------------------------------------------------------------------ */

/**
 * Append one path segment, inserting the separator.
 *
 * The body never carries a leading slash: the root is tracked by a flag and
 * re-attached at the end. That is what makes `..` a pure "truncate at the
 * previous separator" with no special case for the root itself.
 *
 * Overflow is reported by setting @p len past @p cap, which every caller
 * checks once at the end rather than per segment.
 */
static void BodyPush(char *body, size_t *len, size_t cap, const char *seg,
                      size_t segLen)
{
    if (*len > cap) {
        return;
    }
    if (segLen == 0u) {
        return;
    }
    if (*len + segLen + 1u > cap) {
        *len = cap + 1u;
        return;
    }
    if (*len > 0u) {
        body[(*len)++] = '/';
    }
    CopySpan(body + *len, seg, segLen);
    *len += segLen;
}

/* Only ever called with a length that is within the buffer: BodyFeed stops
 * as soon as BodyPush has raised the overflow sentinel. */
static void BodyPop(char *body, size_t *len)
{
    size_t i = *len;

    while (i > 0u && body[i - 1u] != '/') {
        i--;
    }
    if (i > 0u) {
        i--; /* drop the separator too */
    }
    *len = i;
}

/** Feed slash-separated path bytes into the body buffer. */
static void BodyFeed(char *body, size_t *len, size_t cap, const char *path,
                      size_t pathLen)
{
    size_t i = 0u;

    while (i < pathLen && *len <= cap) {
        size_t start;

        while (i < pathLen && path[i] == '/') {
            i++;
        }
        start = i;
        while (i < pathLen && path[i] != '/') {
            i++;
        }
        if (i == start) {
            continue;
        }
        if (i - start == 1u && path[start] == '.') {
            continue;
        }
        if (i - start == 2u && path[start] == '.' && path[start + 1u] == '.') {
            BodyPop(body, len);
            continue;
        }
        BodyPush(body, len, cap, path + start, i - start);
    }
}

/** Whether the reference's path asked to end in a directory separator. */
static int PathEndsWithSeparator(const char *path, size_t pathLen)
{
    size_t i = pathLen;

    if (i == 0u) {
        return 0;
    }
    if (path[i - 1u] == '/') {
        return 1;
    }
    /* `.` and `..` are the other two spellings of "…/". */
    if (path[i - 1u] == '.') {
        if (i == 1u || path[i - 2u] == '/') {
            return 1;
        }
        if (i >= 2u && path[i - 2u] == '.' &&
            (i == 2u || path[i - 3u] == '/')) {
            return 1;
        }
    }
    return 0;
}

/**
 * Resolve @p path against @p base into @p out.
 *
 * Only the base's *directory* matters, and only its path part: a base that
 * carries a fragment or query contributes the bytes before them. A base with
 * no separator in the path (a bare file name, or empty) has no directory, so
 * a relative reference resolves against nothing and `..` clamps at the root
 * rather than failing — a nameless in-memory document is normal, not an
 * error.
 *
 * The result is written whole — leading separator included when the outcome
 * is root-served — so the caller never has to reassemble it. Returns 0 when
 * it would not fit @p cap.
 */
static int ResolvePath(const char *base, size_t baseLen, const char *path,
                        size_t pathLen, char *out, size_t cap,
                        size_t *outLen)
{
    size_t len = 0u;
    int rooted = 0;

    if (pathLen > 0u && path[0] == '/') {
        /* Root-relative: the base contributes nothing. */
        rooted = 1;
        BodyFeed(out, &len, cap, path, pathLen);
    } else {
        if (base && baseLen > 0u) {
            size_t basePathLen = baseLen;
            size_t i;

            for (i = 0u; i < basePathLen; i++) {
                if (base[i] == '#' || base[i] == '?') {
                    basePathLen = i;
                    break;
                }
            }
            if (basePathLen > 0u && base[0] == '/') {
                rooted = 1;
            }
            /* Keep only the directory: drop everything after the last
             * separator, and the separator with it. */
            while (basePathLen > 0u && base[basePathLen - 1u] != '/') {
                basePathLen--;
            }
            BodyFeed(out, &len, cap, base, basePathLen);
        }
        BodyFeed(out, &len, cap, path, pathLen);
    }
    if (len > cap) {
        return 0;
    }
    /*
     * A trailing separator is part of the reference's meaning — `/a/` and
     * `/a` are different targets — so it survives resolution.
     */
    if (PathEndsWithSeparator(path, pathLen)) {
        if (len + 1u > cap) {
            return 0;
        }
        out[len++] = '/';
    }
    /* The body is built without a leading separator so that `..` is a plain
     * "truncate at the previous separator", and the root is re-attached here,
     * once, now that no more popping can happen. */
    if (rooted) {
        if (len + 1u > cap) {
            return 0;
        }
        memmove(out + 1, out, len);
        out[0] = '/';
        len++;
    }
    *outLen = len;
    return 1;
}

/** The base's full path with any fragment or query removed. */
static size_t BasePathLength(const char *base, size_t baseLen)
{
    size_t i;

    for (i = 0u; i < baseLen; i++) {
        if (base[i] == '#' || base[i] == '?') {
            return i;
        }
    }
    return baseLen;
}

/**
 * Classify and resolve one reference.
 *
 * The order of the checks *is* the algorithm, so it is written in the order
 * the header documents: a scheme first (a `:` before any separator makes the
 * whole reference a URI), then the fragment, then the path.
 */
static HtmlNavigationTarget *TargetFromReference(
    const char *reference, size_t referenceLen, const char *base,
    size_t baseLen)
{
    const char *scheme = NULL;
    size_t schemeLen = 0u;
    const char *fragment = NULL;
    size_t fragmentLen = 0u;
    size_t pathLen;
    size_t scan;
    size_t i;
    int hasFragment = 0;

    if (baseLen > RoboHtmlTargetMaxReference) {
        baseLen = RoboHtmlTargetMaxReference;
    }

    /* A control byte anywhere is a refusal, checked first so no later branch
     * can be reached with one. */
    for (i = 0u; i < referenceLen; i++) {
        if (IsRefusedByte(reference[i])) {
            return TargetNew(RoboHtmlTargetInvalid, 0u);
        }
    }
    for (i = 0u; i < baseLen; i++) {
        if (base && IsRefusedByte(base[i])) {
            return TargetNew(RoboHtmlTargetInvalid, 0u);
        }
    }

    /* --- scheme ------------------------------------------------------ */
    for (scan = 0u; scan < referenceLen; scan++) {
        char c = reference[scan];

        if (c == ':') {
            scheme = reference;
            schemeLen = scan;
            break;
        }
        if (c == '/' || c == '?' || c == '#') {
            break;
        }
    }
    if (scheme) {
        HtmlNavigationTarget *t;
        size_t off;
        char *lowered;

        if (schemeLen == 0u || !AsciiIsAlpha(scheme[0])) {
            /* `:rest`, or a scheme starting with a digit: the URL grammar
             * says a first path segment containing `:` makes the string a
             * URL, and a URL with no nameable scheme is a failure. */
            return TargetNew(RoboHtmlTargetInvalid, 0u);
        }
        for (i = 1u; i < schemeLen; i++) {
            if (!AsciiIsSchemeChar(scheme[i])) {
                return TargetNew(RoboHtmlTargetInvalid, 0u);
            }
        }
        /*
         * The URI is carried verbatim — it is already absolute, and
         * normalizing one is a URL library's job. The scheme is lowercased
         * because keyword matching is case-insensitive and lowercase is what
         * the policy table is keyed on.
         */
        t = TargetNew(RoboHtmlTargetExternal,
                       referenceLen + schemeLen + 8u);
        if (!t) {
            return NULL;
        }
        off = TargetTextPut(t, reference, referenceLen);
        t->referenceOff = off;
        t->referenceLen = off ? referenceLen : 0u;
        t->resolvedOff = off;
        t->resolvedLen = off ? referenceLen : 0u;
        lowered = (char *)Malloc(schemeLen + 1u);
        if (!lowered) {
            HtmlNavigationTargetDestroy(t);
            return NULL;
        }
        for (i = 0u; i < schemeLen; i++) {
            lowered[i] = AsciiLower(scheme[i]);
        }
        lowered[schemeLen] = '\0';
        off = TargetTextPut(t, lowered, schemeLen);
        RoboFree(lowered);
        t->schemeOff = off;
        t->schemeLen = off ? schemeLen : 0u;
        return t;
    }

    /* --- fragment ---------------------------------------------------- */
    for (i = 0u; i < referenceLen; i++) {
        if (reference[i] == '#') {
            hasFragment = 1;
            fragment = reference + i + 1u;
            fragmentLen = referenceLen - i - 1u;
            break;
        }
    }
    pathLen = hasFragment ? (size_t)(fragment - reference) - 1u
                            : referenceLen;

    /*
     * A protocol-relative reference (`//host/path`) is refused rather than
     * resolved. It names a network location that only the base's scheme could
     * complete, and a document in this runtime has no scheme — so the
     * alternatives are to guess a scheme (wrong) or to reinterpret somebody
     * else's host name as an internal path (dangerous). Refusing is the only
     * honest answer.
     */
    if (pathLen >= 2u && reference[0] == '/' && reference[1] == '/') {
        return TargetNew(RoboHtmlTargetInvalid, 0u);
    }

    /* --- fragment only ----------------------------------------------- */
    if (pathLen == 0u && hasFragment) {
        size_t basePath = BasePathLength(base, baseLen);
        HtmlNavigationTarget *t =
            TargetNew(RoboHtmlTargetFragment,
                       basePath + fragmentLen + 8u);
        size_t off;

        if (!t) {
            return NULL;
        }
        if (base && basePath > 0u) {
            off = TargetTextPut(t, base, basePath);
            t->resolvedOff = off;
            t->resolvedLen = off ? basePath : 0u;
        }
        off = TargetTextPut(t, fragment, fragmentLen);
        t->fragmentOff = off;
        t->fragmentLen = off ? fragmentLen : 0u;
        return t;
    }

    /* --- a path, with an optional query and fragment ------------------ */
    {
        size_t queryStart = pathLen;
        size_t basePath = BasePathLength(base, baseLen);
        size_t bodyLen = 0u;
        size_t queryLen;
        char *scratch;
        /*
         * Room for the base directory, the path and then the query appended to
         * the resolved result, plus the separators ResolvePath can add.
         */
        size_t scratchCap = basePath + (pathLen * 2u) + 16u;
        HtmlNavigationTarget *t;
        size_t off;
        size_t need;

        for (i = 0u; i < pathLen; i++) {
            if (reference[i] == '?') {
                queryStart = i;
                break;
            }
        }
        queryLen = pathLen - queryStart;

        /* `?query` with no path keeps the current *file*, not its directory,
         * so it is answered from the base path directly rather than by
         * resolving an empty path against the base's directory. */
        if (queryStart == 0u && queryLen > 0u) {
            /*
             * A bare `?query` keeps the current *file* rather than resolving
             * an empty path against its directory, so it is answered from the
             * base path directly. With no base at all there is no file to
             * keep, and the query is the whole target.
             *
             * File and query are assembled in one buffer and written as one
             * string, because `resolved` has to be a single contiguous run:
             * writing them as two fields would leave the query outside the
             * resolution it belongs to.
             */
            need = basePath + queryLen + referenceLen + 6u;
            t = TargetNew(RoboHtmlTargetRoute, need);
            if (!t) {
                return NULL;
            }
            scratch = (char *)Malloc(basePath + queryLen + 1u);
            if (!scratch) {
                HtmlNavigationTargetDestroy(t);
                return NULL;
            }
            if (base && basePath > 0u) {
                CopySpan(scratch, base, basePath);
            }
            CopySpan(scratch + basePath, reference + queryStart, queryLen);
            bodyLen = basePath + queryLen;
            off = TargetTextPut(t, scratch, bodyLen);
            t->resolvedOff = off;
            t->resolvedLen = off ? bodyLen : 0u;
            RoboFree(scratch);
        } else {
            scratch = (char *)Malloc(scratchCap);
            if (!scratch) {
                return NULL;
            }
            if (!ResolvePath(base, baseLen, reference, queryStart, scratch,
                              scratchCap, &bodyLen)) {
                RoboFree(scratch);
                return NULL;
            }
            /*
             * The query belongs to the resolved path, so it is appended to it
             * here rather than written as a field of its own — the resolved
             * string is what a caller navigates to, query and all.
             */
            if (queryLen > 0u) {
                if (bodyLen + queryLen + 1u > scratchCap) {
                    RoboFree(scratch);
                    return NULL;
                }
                CopySpan(scratch + bodyLen, reference + queryStart,
                          queryLen);
                bodyLen += queryLen;
            }
            need = bodyLen + referenceLen + fragmentLen + 6u;
            t = TargetNew(RoboHtmlTargetRoute, need);
            if (!t) {
                RoboFree(scratch);
                return NULL;
            }
            off = TargetTextPut(t, scratch, bodyLen);
            t->resolvedOff = off;
            t->resolvedLen = off ? bodyLen : 0u;
            RoboFree(scratch);
        }
        if (hasFragment) {
            off = TargetTextPut(t, fragment, fragmentLen);
            t->fragmentOff = off;
            t->fragmentLen = off ? fragmentLen : 0u;
        }
        /* The reference travels verbatim beside the resolution, so a caller
         * can always see what the document wrote next to what it meant. */
        off = TargetTextPut(t, reference, referenceLen);
        t->referenceOff = off;
        t->referenceLen = off ? referenceLen : 0u;
        return t;
    }
}

Result HtmlNavigationTargetCreate(
    const char *href, size_t hrefLength, const char *base, size_t baseLength,
    HtmlNavigationTarget **out, HtmlDiag *diag)
{
    const char *ref;
    size_t refLen;
    size_t begin;
    size_t end;
    HtmlNavigationTarget *target;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument, "out target is NULL");
    }
    *out = NULL;
    if (!href && hrefLength > 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument,
                              "href is NULL with non-zero length");
    }

    /* Surrounding ASCII whitespace is not part of the reference. */
    begin = 0u;
    end = hrefLength;
    while (begin < end && AsciiIsSpace(href[begin])) {
        begin++;
    }
    while (end > begin && AsciiIsSpace(href[end - 1u])) {
        end--;
    }
    /* `href` is NULL when it is absent, and `href + 0` is undefined behaviour
     * even at a zero offset — so an absent reference stays NULL rather than
     * becoming "the trimmed pointer". Only a non-empty `refLen` is read past
     * the early return below, and that implies a non-NULL `href`. */
    ref = href ? href + begin : NULL;
    refLen = end - begin;

    if (refLen == 0u) {
        /* Absent, empty and whitespace-only all mean "nothing to navigate
         * to", which is a state and not an error. */
        target = TargetNew(RoboHtmlTargetNone, 0u);
        if (!target) {
            return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                                  RoboOutOfMemory, "target allocation");
        }
        *out = target;
        return RoboOk;
    }
    if (refLen > RoboHtmlTargetMaxReference) {
        /* Refused at the boundary: sizing a resolution from an unbounded
         * attribute value is exactly what a hostile document would want. */
        target = TargetNew(RoboHtmlTargetInvalid, 0u);
        if (!target) {
            return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                                  RoboOutOfMemory, "target allocation");
        }
        *out = target;
        return RoboOk;
    }

    target = TargetFromReference(ref, refLen, base, baseLength);
    if (!target) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboOutOfMemory, "target allocation");
    }
    *out = target;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Element side                                                        */
/* ------------------------------------------------------------------ */

Result HtmlElementAnchorAttributes(
    const HtmlNodeHandle *element, HtmlAnchorAttributes *out,
    HtmlDiag *diag)
{
    size_t length = 0u;
    const char *value;

    if (!element || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument, "NULL element or out");
    }
    memset(out, 0, sizeof(*out));
    /*
     * Two questions in a fixed order, because their answers differ: "is this
     * still an element?" (a stale handle or a destroyed document is
     * NOT_FOUND) and "is it an anchor?" (a live <p> is NOT_SUPPORTED).
     * Collapsing them would report a dead node as "not a link", which is a
     * misleading thing to say about a node that no longer exists.
     */
    if (!HtmlNodeMatchesFilter(element, RoboHtmlNodeFilterElements)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotFound,
                              "element is not a live element");
    }
    if (!HtmlElementIsTag(element, RoboHtmlTagA)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotSupported,
                              "element is not an anchor");
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = HtmlElementAttributeValue(element, name, &length);    \
        out->field##Present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##Length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(href, "href");
    TAKE_ATTR(target, "target");
    TAKE_ATTR(rel, "rel");
    TAKE_ATTR(download, "download");
    TAKE_ATTR(title, "title");
#undef TAKE_ATTR

    /*
     * "usable" applies the same rule the resolver does — present, and
     * something left once surrounding whitespace and control bytes are
     * discounted — so `<a href="  ">` is unusable here *and* resolves to NONE
     * there. One rule, two readers, one answer.
     */
    if (out->hrefPresent) {
        size_t begin = 0u;
        size_t end = out->hrefLength;
        size_t i;

        while (begin < end && AsciiIsSpace(out->href[begin])) {
            begin++;
        }
        while (end > begin && AsciiIsSpace(out->href[end - 1u])) {
            end--;
        }
        for (i = begin; i < end; i++) {
            if (IsRefusedByte(out->href[i])) {
                end = begin;
                break;
            }
        }
        out->hrefUsable = (end > begin);
    }
    return RoboOk;
}

Result HtmlElementNavigationTarget(
    const HtmlNodeHandle *element,
    HtmlNavigationTarget **out, HtmlDiag *diag)
{
    HtmlAnchorAttributes attrs;
    HtmlDocumentId documentId;
    const char *base;
    Result r;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument, "out target is NULL");
    }
    *out = NULL;
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument, "element is NULL");
    }
    if (!HtmlNodeHandleIsWellformed(element)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotFound,
                              "element handle is not well formed");
    }
    r = HtmlElementAnchorAttributes(element, &attrs, diag);
    if (r != RoboOk) {
        return r;
    }
    /*
     * The document's own reference is the base. Absent or empty is a normal
     * in-memory document, not an error: relative references then resolve
     * against nothing, which the resolver handles by clamping rather than
     * refusing.
     */
    documentId = HtmlNodeHandleDocument(element);
    base = HtmlDocumentSourceName(documentId);
    return HtmlNavigationTargetCreate(
        attrs.href, attrs.hrefLength, base, base ? strlen(base) : 0u, out,
        diag);
}

/* ------------------------------------------------------------------ */
/* Fragment lookup                                                     */
/* ------------------------------------------------------------------ */

typedef struct fragmentSearch {
    const char *fragment;
    size_t length;
} FragmentSearch;

static int FragmentMatches(const HtmlNodeView *view, void *user)
{
    const FragmentSearch *search = (const FragmentSearch *)user;
    HtmlAttribute attr;
    size_t i;

    if (!search || search->length == 0u) {
        /*
         * An empty fragment names the top of the document, which is not an
         * element — so there is nothing here for it to match. Returning the
         * first element would be a lie and returning the document node would
         * invent a "top" element; not matching is the honest answer.
         */
        return 0;
    }
    if (HtmlElementFindAttribute(&view->handle, "id", &attr, NULL) !=
        RoboOk) {
        return 0;
    }
    if (attr.valueLength != search->length) {
        return 0;
    }
    for (i = 0u; i < search->length; i++) {
        if (attr.value[i] != search->fragment[i]) {
            return 0;
        }
    }
    return 1;
}

Result HtmlNavigationFindFragment(
    HtmlDocumentId documentId, const char *fragment,
    size_t fragmentLength, HtmlNodeHandle *out, HtmlDiag *diag)
{
    HtmlNodeHandle root;
    FragmentSearch search;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument, "out handle is NULL");
    }
    HtmlNodeHandleClear(out);
    if (!fragment && fragmentLength > 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument,
                              "fragment is NULL with non-zero length");
    }
    if (!HtmlDocumentIsLive(documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "document is not live");
    }
    root = HtmlDocumentRoot(documentId);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "document has no usable root");
    }
    search.fragment = fragment;
    search.length = fragmentLength;
    /*
     * `find_first` rather than `find_descendant`: the document node itself is
     * never an element, so "at or below" and "inside" cannot differ here, and
     * starting at the root is what makes the result document order.
     */
    return HtmlNodeFindFirst(&root, RoboHtmlNodeFilterElements,
                                     FragmentMatches, &search, out, diag);
}

/* ------------------------------------------------------------------ */
/* Handler seam                                                        */
/* ------------------------------------------------------------------ */

void HtmlNavigationHandlerInit(HtmlNavigationHandler *handler)
{
    if (handler) {
        handler->navigate = NULL;
        handler->userdata = NULL;
    }
}

int HtmlNavigationHandlerIsUsable(
    const HtmlNavigationHandler *handler)
{
    return handler != NULL && handler->navigate != NULL;
}

/* ------------------------------------------------------------------ */
/* Activation                                                          */
/* ------------------------------------------------------------------ */

void HtmlNavigationOutcomeClear(HtmlNavigationOutcome *out)
{
    if (!out) {
        return;
    }
    HtmlNavigationTargetDestroy(out->target);
    memset(out, 0, sizeof(*out));
    HtmlNodeHandleClear(&out->fragmentNode);
}

/**
 * Adopt @p target into @p out and fill the target-derived report fields.
 *
 * Called once per activation so every path — including the refusals that
 * never reach the seam — reports the same view of the same target. The
 * outcome takes ownership, which is what keeps the reported strings valid
 * for as long as the caller holds the outcome.
 */
static void OutcomeAdoptTarget(HtmlNavigationOutcome *out,
                                HtmlNavigationTarget *target)
{
    size_t length = 0u;

    out->target = target;
    out->targetKind = HtmlNavigationTargetKind(target);
    out->resolved = HtmlNavigationTargetResolved(target, &length);
    out->resolvedLength = out->resolved ? length : 0u;
    out->fragment = HtmlNavigationTargetFragment(target, &length);
    out->fragmentLength = out->fragment ? length : 0u;
    out->scheme = HtmlNavigationTargetScheme(target, &length);
    out->schemeLength = out->scheme ? length : 0u;
    out->handlerCalled = 0;
    out->handlerResult = RoboOk;
    out->fragmentFound = 0;
    HtmlNodeHandleClear(&out->fragmentNode);
}

/**
 * Ask the seam, and translate its answer.
 *
 * A handler accepts by returning RoboOk and leaving @p want set; anything
 * else — or a kind it changed — is a decline. The caller's `handlerResult`
 * records what happened, so "the router said no" is distinguishable from
 * "there is no router".
 */
static HtmlNavigationOutcomeKind ConsultHandler(
    const HtmlNavigationHandler *handler,
    const HtmlNavigationRequest *request,
    HtmlNavigationOutcomeKind want,
    HtmlNavigationOutcome *out, HtmlDiag *diag)
{
    HtmlNavigationOutcomeKind accepted = want;

    out->handlerCalled = 1;
    out->handlerResult =
        handler->navigate(handler->userdata, request, &accepted, diag);
    if (out->handlerResult == RoboOk && accepted == want) {
        return want;
    }
    return RoboHtmlNavigationUnsupported;
}

Result HtmlElementActivate(
    const HtmlNodeHandle *element,
    const HtmlNavigationHandler *handler,
    HtmlNavigationOutcome *out, HtmlDiag *diag)
{
    HtmlAnchorAttributes attrs;
    HtmlNavigationTarget *target = NULL;
    HtmlNavigationOutcome local;
    HtmlNavigationOutcome *result;
    HtmlDocumentId documentId;
    HtmlNavigationRequest request;
    const char *base;
    Result r;

    if (out) {
        /* A zeroed outcome is what every failure path leaves behind, and it
         * is safe to clear: the caller can always clear what it was given. */
        memset(out, 0, sizeof(*out));
        HtmlNodeHandleClear(&out->fragmentNode);
        result = out;
    } else {
        result = &local;
    }
    if (!out) {
        memset(result, 0, sizeof(*result));
        HtmlNodeHandleClear(&result->fragmentNode);
    }

    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor,
                              RoboInvalidArgument, "element is NULL");
    }
    if (!HtmlNodeHandleIsWellformed(element)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotFound,
                              "element handle is not well formed");
    }

    /* 1. It must be an anchor. A caller that activates a <p> has a bug, and
     *    reporting the bug beats reporting "no target". */
    r = HtmlElementAnchorAttributes(element, &attrs, diag);
    if (r != RoboOk) {
        return r;
    }

    /* 2. Resolve against the document's own reference. */
    documentId = HtmlNodeHandleDocument(element);
    base = HtmlDocumentSourceName(documentId);
    r = HtmlNavigationTargetCreate(attrs.href, attrs.hrefLength, base,
                                          base ? strlen(base) : 0u, &target,
                                          diag);
    if (r != RoboOk) {
        return r;
    }
    OutcomeAdoptTarget(result, target);

    /* 3. Build the request. */
    memset(&request, 0, sizeof(request));
    request.documentId = documentId;
    request.anchor = *element;
    request.target = target;
    request.base = base ? base : "";
    request.baseLength = base ? strlen(base) : 0u;
    request.targetAttribute = attrs.target;
    request.targetLength = attrs.targetLength;
    request.targetContext =
        HtmlLinkTargetContextFor(attrs.target, attrs.targetLength);
    request.rel = attrs.rel;
    request.relLength = attrs.relLength;
    request.downloadRequested = attrs.downloadPresent;
    request.downloadName = attrs.download;
    request.downloadNameLength = attrs.downloadLength;
    request.title = attrs.title;
    request.titleLength = attrs.titleLength;
    request.reason = RoboHtmlNavigationReasonActivation;

    /* 4-7. Decide. Nothing here performs I/O, and nothing here can report a
     *      navigation that did not happen. */
    switch (result->targetKind) {
    case RoboHtmlTargetNone:
        /* A link with no href is not a link. That is a state, and the only
         * outcome this phase reports without asking anybody. */
        result->kind = RoboHtmlNavigationNoTarget;
        break;

    case RoboHtmlTargetInvalid:
        /* There is no request to construct: the reference could not be
         * resolved, so nothing is delegated and nothing is claimed. */
        result->kind = RoboHtmlNavigationRejected;
        break;

    case RoboHtmlTargetFragment: {
        /*
         * The one navigation this phase can complete. A fragment needs no
         * I/O — it names an element in a tree we already hold — so it is
         * answered here and never delegated, which is also why it works with
         * no handler installed.
         */
        HtmlNodeHandle found;

        r = HtmlNavigationFindFragment(
            documentId, result->fragment, result->fragmentLength, &found,
            NULL);
        if (r == RoboOk) {
            result->kind = RoboHtmlNavigationFragment;
            result->fragmentNode = found;
            result->fragmentFound = 1;
        } else if (r == RoboNotFound) {
            result->kind = RoboHtmlNavigationFragmentMissing;
        } else {
            HtmlNavigationOutcomeClear(result);
            if (out) {
                HtmlNodeHandleClear(&out->fragmentNode);
            }
            return r;
        }
        break;
    }

    case RoboHtmlTargetRoute:
        /*
         * `download` asks to *save* the target rather than go to it. Saving
         * needs a filesystem capability this runtime does not have, so the
         * intent is reported rather than silently ignored — and the seam is
         * not consulted, because handing a router a save request it cannot
         * perform would be worse than telling the truth.
         */
        if (request.downloadRequested) {
            result->kind = RoboHtmlNavigationDownloadPending;
            break;
        }
        if (!HtmlNavigationHandlerIsUsable(handler)) {
            /* No router installed: this runtime cannot perform internal
             * navigation yet, and saying so is the whole point. */
            result->kind = RoboHtmlNavigationUnsupported;
            break;
        }
        result->kind = ConsultHandler(handler, &request,
                                       RoboHtmlNavigationRoute, result,
                                       diag);
        break;

    case RoboHtmlTargetExternal:
        /*
         * The phase's central security rule. A scheme this runtime does not
         * permit is refused *before* the seam is consulted: `javascript:`
         * never reaches a handler, and neither does `file:` or `data:`. An
         * anchor cannot be used to reach a subsystem the document was never
         * granted, whatever the application installed.
         */
        if (!HtmlNavigationSchemeIsPermitted(result->scheme,
                                                     result->schemeLength)) {
            result->kind = RoboHtmlNavigationUnsupported;
            break;
        }
        if (request.downloadRequested) {
            result->kind = RoboHtmlNavigationDownloadPending;
            break;
        }
        if (!HtmlNavigationHandlerIsUsable(handler)) {
            /* Permitted but unreachable: `https` needs the network stack,
             * which is a later phase. UNSUPPORTED is the honest answer, and
             * the one that keeps this phase from claiming a fetch. */
            result->kind = RoboHtmlNavigationUnsupported;
            break;
        }
        result->kind = ConsultHandler(handler, &request,
                                       RoboHtmlNavigationExternal, result,
                                       diag);
        break;

    default:
        result->kind = RoboHtmlNavigationUnsupported;
        break;
    }

    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Debug output                                                        */
/* ------------------------------------------------------------------ */

typedef struct textBuilder {
    char *out;
    size_t capacity;
    size_t length;
} TextBuilder;

static void TbInit(TextBuilder *b, char *out, size_t capacity)
{
    b->out = out;
    b->capacity = capacity;
    b->length = 0u;
    if (out && capacity > 0u) {
        out[0] = '\0';
    }
}

static void TbText(TextBuilder *b, const char *text)
{
    if (!b->out || b->capacity == 0u || !text) {
        return;
    }
    while (*text != '\0' && b->length + 1u < b->capacity) {
        b->out[b->length++] = *text++;
    }
    b->out[b->length] = '\0';
}

static void TbBytes(TextBuilder *b, const char *bytes, size_t length)
{
    if (!b->out || b->capacity == 0u) {
        return;
    }
    if (!bytes) {
        TbText(b, "(null)");
        return;
    }
    while (length > 0u && b->length + 1u < b->capacity) {
        b->out[b->length++] = *bytes++;
        length--;
    }
    b->out[b->length] = '\0';
}

size_t HtmlNavigationTargetDescribe(
    const HtmlNavigationTarget *target, char *out, size_t capacity)
{
    TextBuilder b;
    const char *s;
    size_t length = 0u;

    TbInit(&b, out, capacity);
    TbText(&b, "target kind=");
    TbText(&b,
            HtmlTargetKindName(HtmlNavigationTargetKind(target)));
    if (!target) {
        return b.length;
    }
    s = HtmlNavigationTargetReference(target, &length);
    if (s) {
        TbText(&b, " reference=\"");
        TbBytes(&b, s, length);
        TbText(&b, "\"");
    }
    s = HtmlNavigationTargetResolved(target, &length);
    if (s) {
        TbText(&b, " resolved=\"");
        TbBytes(&b, s, length);
        TbText(&b, "\"");
    }
    s = HtmlNavigationTargetFragment(target, &length);
    if (s) {
        TbText(&b, " fragment=\"#");
        TbBytes(&b, s, length);
        TbText(&b, "\"");
    }
    s = HtmlNavigationTargetScheme(target, &length);
    if (s) {
        TbText(&b, " scheme=");
        TbBytes(&b, s, length);
        TbText(&b, " permitted=");
        TbText(&b, HtmlNavigationSchemeIsPermitted(s, length) ? "yes"
                                                                       : "no");
    }
    return b.length;
}

size_t HtmlElementAnchorDescribe(
    const HtmlNodeHandle *element,
    const HtmlNavigationTarget *target, char *out, size_t capacity)
{
    TextBuilder b;
    HtmlAnchorAttributes attrs;
    HtmlTargetKind kind = RoboHtmlTargetNone;

    TbInit(&b, out, capacity);
    if (!element ||
        HtmlElementAnchorAttributes(element, &attrs, NULL) != RoboOk) {
        TbText(&b, "a (not an anchor)");
        return b.length;
    }
    TbText(&b, "a");
    if (attrs.hrefPresent) {
        TbText(&b, " href=\"");
        TbBytes(&b, attrs.href, attrs.hrefLength);
        TbText(&b, "\"");
    }
    if (target) {
        const char *resolved =
            HtmlNavigationTargetResolved(target, NULL);

        kind = HtmlNavigationTargetKind(target);
        if (resolved) {
            TbText(&b, " resolved=\"");
            TbBytes(&b, resolved, strlen(resolved));
            TbText(&b, "\"");
        }
    }
    TbText(&b, " target=");
    TbText(&b, HtmlLinkTargetContextName(
                     HtmlLinkTargetContextFor(attrs.target,
                                                       attrs.targetLength)));
    TbText(&b, " rel=\"");
    if (attrs.relPresent) {
        TbBytes(&b, attrs.rel, attrs.relLength);
    }
    TbText(&b, "\"");
    if (attrs.downloadPresent) {
        TbText(&b, " download=\"");
        TbBytes(&b, attrs.download, attrs.downloadLength);
        TbText(&b, "\"");
    }
    TbText(&b, " state=");
    if (!attrs.hrefPresent) {
        TbText(&b, "inert");
    } else if (!attrs.hrefUsable) {
        TbText(&b, "empty");
    } else if (kind == RoboHtmlTargetInvalid) {
        TbText(&b, "rejected");
    } else if (kind == RoboHtmlTargetExternal) {
        const char *scheme = HtmlNavigationTargetScheme(target, NULL);
        size_t schemeLen = 0u;

        if (scheme) {
            schemeLen = strlen(scheme);
        }
        TbText(&b, HtmlNavigationSchemeIsPermitted(scheme, schemeLen)
                         ? "external"
                         : "refused");
    } else {
        TbText(&b, "activatable");
    }
    return b.length;
}
