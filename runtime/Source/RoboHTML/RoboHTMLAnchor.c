/*
 * RoboHTML Phase 8.13 — anchors and navigation.
 *
 * Three layers, each with one job:
 *
 *   attributes   read `href` / `target` / `rel` / `download` / `title` off an
 *                element. No resolution, no navigation.
 *   target       robo_html_navigation_target_t — one resolved reference, plus
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

static int ascii_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int ascii_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int ascii_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Scheme-name characters after the first: ALPHA / DIGIT / "+" / "-" / ".". */
static int ascii_is_scheme_char(char c)
{
    return ascii_is_alpha(c) || ascii_is_digit(c) || c == '+' || c == '-' ||
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
static int is_refused_byte(char c)
{
    unsigned char u = (unsigned char)c;

    return u < 0x20u || u == 0x7Fu;
}

static void copy_span(char *dst, const char *src, size_t length)
{
    if (length > 0u) {
        memcpy(dst, src, length);
    }
}

/* ------------------------------------------------------------------ */
/* Kind / context / reason vocabularies                                */
/* ------------------------------------------------------------------ */

int robo_html_target_kind_is_known(robo_html_target_kind_t kind)
{
    return kind >= ROBO_HTML_TARGET_NONE && kind <= ROBO_HTML_TARGET_INVALID;
}

const char *robo_html_target_kind_name(robo_html_target_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_TARGET_NONE:
        return "none";
    case ROBO_HTML_TARGET_ROUTE:
        return "route";
    case ROBO_HTML_TARGET_FRAGMENT:
        return "fragment";
    case ROBO_HTML_TARGET_EXTERNAL:
        return "external";
    case ROBO_HTML_TARGET_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

int robo_html_link_target_context_is_known(robo_html_link_target_context_t c)
{
    return c >= ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT &&
           c <= ROBO_HTML_LINK_TARGET_CONTEXT_UNKNOWN;
}

const char *robo_html_link_target_context_name(
    robo_html_link_target_context_t context)
{
    switch (context) {
    case ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT:
        return "current";
    case ROBO_HTML_LINK_TARGET_CONTEXT_NAMED:
        return "named";
    case ROBO_HTML_LINK_TARGET_CONTEXT_NEW:
        return "new";
    case ROBO_HTML_LINK_TARGET_CONTEXT_PARENT:
        return "parent";
    case ROBO_HTML_LINK_TARGET_CONTEXT_TOP:
        return "top";
    case ROBO_HTML_LINK_TARGET_CONTEXT_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

/** Case-insensitive comparison of @p a_len bytes against a NUL-terminated @p b. */
static int span_eq_ascii_ci(const char *a, size_t a_len, const char *b)
{
    size_t i;

    if (!a || !b) {
        return 0;
    }
    for (i = 0u; i < a_len; i++) {
        if (b[i] == '\0' || ascii_lower(a[i]) != ascii_lower(b[i])) {
            return 0;
        }
    }
    return b[a_len] == '\0';
}

robo_html_link_target_context_t robo_html_link_target_context_for(
    const char *target, size_t length)
{
    if (!target || length == 0u) {
        return ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT;
    }
    if (span_eq_ascii_ci(target, length, "_self")) {
        return ROBO_HTML_LINK_TARGET_CONTEXT_CURRENT;
    }
    if (span_eq_ascii_ci(target, length, "_blank")) {
        return ROBO_HTML_LINK_TARGET_CONTEXT_NEW;
    }
    if (span_eq_ascii_ci(target, length, "_parent")) {
        return ROBO_HTML_LINK_TARGET_CONTEXT_PARENT;
    }
    if (span_eq_ascii_ci(target, length, "_top")) {
        return ROBO_HTML_LINK_TARGET_CONTEXT_TOP;
    }
    /*
     * Any other `_`-prefixed name is reserved by the spec for a keyword this
     * runtime does not define. Reporting it as NAMED would claim the document
     * named a context when it actually asked for one we cannot honour, and
     * that distinction is the only thing this classification is for.
     */
    if (target[0] == '_') {
        return ROBO_HTML_LINK_TARGET_CONTEXT_UNKNOWN;
    }
    return ROBO_HTML_LINK_TARGET_CONTEXT_NAMED;
}

int robo_html_link_rel_contains_token(const char *rel, size_t rel_length,
                                      const char *token)
{
    size_t token_length;
    size_t i;

    if (!rel || !token) {
        return 0;
    }
    token_length = strlen(token);
    if (token_length == 0u) {
        return 0;
    }
    i = 0u;
    while (i < rel_length) {
        size_t start;

        while (i < rel_length && ascii_is_space(rel[i])) {
            i++;
        }
        start = i;
        while (i < rel_length && !ascii_is_space(rel[i])) {
            i++;
        }
        if (i > start && span_eq_ascii_ci(rel + start, i - start, token)) {
            return 1;
        }
    }
    return 0;
}

int robo_html_navigation_reason_is_known(robo_html_navigation_reason_t reason)
{
    return reason >= ROBO_HTML_NAVIGATION_REASON_NONE &&
           reason <= ROBO_HTML_NAVIGATION_REASON_PROGRAMMATIC;
}

const char *robo_html_navigation_reason_name(robo_html_navigation_reason_t r)
{
    switch (r) {
    case ROBO_HTML_NAVIGATION_REASON_NONE:
        return "none";
    case ROBO_HTML_NAVIGATION_REASON_ACTIVATION:
        return "activation";
    case ROBO_HTML_NAVIGATION_REASON_PROGRAMMATIC:
        return "programmatic";
    default:
        return "unknown";
    }
}

int robo_html_navigation_outcome_kind_is_known(
    robo_html_navigation_outcome_kind_t kind)
{
    return kind >= ROBO_HTML_NAVIGATION_NONE &&
           kind <= ROBO_HTML_NAVIGATION_REJECTED;
}

const char *robo_html_navigation_outcome_kind_name(
    robo_html_navigation_outcome_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_NAVIGATION_NONE:
        return "none";
    case ROBO_HTML_NAVIGATION_NO_TARGET:
        return "no-target";
    case ROBO_HTML_NAVIGATION_ROUTE:
        return "route";
    case ROBO_HTML_NAVIGATION_FRAGMENT:
        return "fragment";
    case ROBO_HTML_NAVIGATION_FRAGMENT_MISSING:
        return "fragment-missing";
    case ROBO_HTML_NAVIGATION_EXTERNAL:
        return "external";
    case ROBO_HTML_NAVIGATION_UNSUPPORTED:
        return "unsupported";
    case ROBO_HTML_NAVIGATION_DOWNLOAD_PENDING:
        return "download-pending";
    case ROBO_HTML_NAVIGATION_REJECTED:
        return "rejected";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Scheme policy                                                       */
/* ------------------------------------------------------------------ */

typedef struct scheme_policy {
    const char *name;
    int known;
    int permitted;
} scheme_policy_t;

/*
 * The whole scheme table, and the reason it is a table rather than a switch:
 * `known` and `permitted` are two columns, and the interesting rows are the
 * ones where they disagree. `file` and `data` are understood and refused;
 * `javascript` is understood and refused *hard*, because an anchor that could
 * reach script execution is the one thing a link activation must never do.
 */
static const scheme_policy_t k_schemes[] = {
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

#define K_SCHEME_COUNT (sizeof(k_schemes) / sizeof(k_schemes[0]))

static const scheme_policy_t *scheme_lookup(const char *scheme, size_t length)
{
    size_t i;
    size_t j;

    if (!scheme || length == 0u) {
        return NULL;
    }
    for (i = 0u; i < K_SCHEME_COUNT; i++) {
        const char *name = k_schemes[i].name;

        for (j = 0u; j < length; j++) {
            if (name[j] == '\0' || ascii_lower(scheme[j]) != name[j]) {
                break;
            }
        }
        if (j == length && name[j] == '\0') {
            return &k_schemes[i];
        }
    }
    return NULL;
}

int robo_html_navigation_scheme_is_known(const char *scheme, size_t length)
{
    return scheme_lookup(scheme, length) != NULL;
}

int robo_html_navigation_scheme_is_permitted(const char *scheme, size_t length)
{
    const scheme_policy_t *policy = scheme_lookup(scheme, length);

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
struct robo_html_navigation_target {
    robo_html_target_kind_t kind;
    size_t reference_off;
    size_t reference_len;
    size_t resolved_off;
    size_t resolved_len;
    size_t fragment_off;
    size_t fragment_len;
    size_t scheme_off;
    size_t scheme_len;
    size_t capacity;
    size_t used;
    char *text;
};

static const char *text_field(const robo_html_navigation_target_t *t,
                              size_t off)
{
    if (!t || off == 0u || !t->text) {
        return NULL;
    }
    return t->text + (off - 1u);
}

/**
 * Create a target with room for @p text_bytes of string data.
 *
 * @p text_bytes counts what will be written, not the terminators; one extra
 * byte covers each writer's terminator. 0 gives a target whose every string
 * is absent, which is exactly what kind NONE needs.
 */
static robo_html_navigation_target_t *target_new(
    robo_html_target_kind_t kind, size_t text_bytes)
{
    robo_html_navigation_target_t *t;

    if (text_bytes > ROBO_HTML_TARGET_MAX_REFERENCE * 4u) {
        return NULL;
    }
    t = (robo_html_navigation_target_t *)robo_calloc(
        1u, sizeof(robo_html_navigation_target_t));
    if (!t) {
        return NULL;
    }
    t->kind = kind;
    if (text_bytes > 0u) {
        t->text = (char *)robo_calloc(1u, text_bytes + 1u);
        if (!t->text) {
            robo_free(t);
            return NULL;
        }
        t->capacity = text_bytes + 1u;
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
static size_t target_text_put(robo_html_navigation_target_t *t,
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
    copy_span(t->text + t->used, bytes, length);
    t->used += length;
    t->text[t->used] = '\0';
    /* Step past the terminator so the next field cannot overwrite it. */
    t->used += 1u;
    return off;
}

void robo_html_navigation_target_destroy(robo_html_navigation_target_t *target)
{
    if (!target) {
        return;
    }
    robo_free(target->text);
    robo_free(target);
}

robo_html_target_kind_t robo_html_navigation_target_kind(
    const robo_html_navigation_target_t *target)
{
    return target ? target->kind : ROBO_HTML_TARGET_NONE;
}

const char *robo_html_navigation_target_reference(
    const robo_html_navigation_target_t *target, size_t *out_length)
{
    if (out_length) {
        *out_length = target ? target->reference_len : 0u;
    }
    return text_field(target, target ? target->reference_off : 0u);
}

const char *robo_html_navigation_target_resolved(
    const robo_html_navigation_target_t *target, size_t *out_length)
{
    if (out_length) {
        *out_length = target ? target->resolved_len : 0u;
    }
    return text_field(target, target ? target->resolved_off : 0u);
}

const char *robo_html_navigation_target_fragment(
    const robo_html_navigation_target_t *target, size_t *out_length)
{
    if (out_length) {
        *out_length = target ? target->fragment_len : 0u;
    }
    return text_field(target, target ? target->fragment_off : 0u);
}

const char *robo_html_navigation_target_scheme(
    const robo_html_navigation_target_t *target, size_t *out_length)
{
    if (out_length) {
        *out_length = target ? target->scheme_len : 0u;
    }
    return text_field(target, target ? target->scheme_off : 0u);
}

int robo_html_navigation_target_is_internal(
    const robo_html_navigation_target_t *target)
{
    if (!target) {
        return 0;
    }
    return target->kind == ROBO_HTML_TARGET_ROUTE ||
           target->kind == ROBO_HTML_TARGET_FRAGMENT;
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
static void body_push(char *body, size_t *len, size_t cap, const char *seg,
                      size_t seg_len)
{
    if (*len > cap) {
        return;
    }
    if (seg_len == 0u) {
        return;
    }
    if (*len + seg_len + 1u > cap) {
        *len = cap + 1u;
        return;
    }
    if (*len > 0u) {
        body[(*len)++] = '/';
    }
    copy_span(body + *len, seg, seg_len);
    *len += seg_len;
}

/* Only ever called with a length that is within the buffer: body_feed stops
 * as soon as body_push has raised the overflow sentinel. */
static void body_pop(char *body, size_t *len)
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
static void body_feed(char *body, size_t *len, size_t cap, const char *path,
                      size_t path_len)
{
    size_t i = 0u;

    while (i < path_len && *len <= cap) {
        size_t start;

        while (i < path_len && path[i] == '/') {
            i++;
        }
        start = i;
        while (i < path_len && path[i] != '/') {
            i++;
        }
        if (i == start) {
            continue;
        }
        if (i - start == 1u && path[start] == '.') {
            continue;
        }
        if (i - start == 2u && path[start] == '.' && path[start + 1u] == '.') {
            body_pop(body, len);
            continue;
        }
        body_push(body, len, cap, path + start, i - start);
    }
}

/** Whether the reference's path asked to end in a directory separator. */
static int path_ends_with_separator(const char *path, size_t path_len)
{
    size_t i = path_len;

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
static int resolve_path(const char *base, size_t base_len, const char *path,
                        size_t path_len, char *out, size_t cap,
                        size_t *out_len)
{
    size_t len = 0u;
    int rooted = 0;

    if (path_len > 0u && path[0] == '/') {
        /* Root-relative: the base contributes nothing. */
        rooted = 1;
        body_feed(out, &len, cap, path, path_len);
    } else {
        if (base && base_len > 0u) {
            size_t base_path_len = base_len;
            size_t i;

            for (i = 0u; i < base_path_len; i++) {
                if (base[i] == '#' || base[i] == '?') {
                    base_path_len = i;
                    break;
                }
            }
            if (base_path_len > 0u && base[0] == '/') {
                rooted = 1;
            }
            /* Keep only the directory: drop everything after the last
             * separator, and the separator with it. */
            while (base_path_len > 0u && base[base_path_len - 1u] != '/') {
                base_path_len--;
            }
            body_feed(out, &len, cap, base, base_path_len);
        }
        body_feed(out, &len, cap, path, path_len);
    }
    if (len > cap) {
        return 0;
    }
    /*
     * A trailing separator is part of the reference's meaning — `/a/` and
     * `/a` are different targets — so it survives resolution.
     */
    if (path_ends_with_separator(path, path_len)) {
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
    *out_len = len;
    return 1;
}

/** The base's full path with any fragment or query removed. */
static size_t base_path_length(const char *base, size_t base_len)
{
    size_t i;

    for (i = 0u; i < base_len; i++) {
        if (base[i] == '#' || base[i] == '?') {
            return i;
        }
    }
    return base_len;
}

/**
 * Classify and resolve one reference.
 *
 * The order of the checks *is* the algorithm, so it is written in the order
 * the header documents: a scheme first (a `:` before any separator makes the
 * whole reference a URI), then the fragment, then the path.
 */
static robo_html_navigation_target_t *target_from_reference(
    const char *reference, size_t reference_len, const char *base,
    size_t base_len)
{
    const char *scheme = NULL;
    size_t scheme_len = 0u;
    const char *fragment = NULL;
    size_t fragment_len = 0u;
    size_t path_len;
    size_t scan;
    size_t i;
    int has_fragment = 0;

    if (base_len > ROBO_HTML_TARGET_MAX_REFERENCE) {
        base_len = ROBO_HTML_TARGET_MAX_REFERENCE;
    }

    /* A control byte anywhere is a refusal, checked first so no later branch
     * can be reached with one. */
    for (i = 0u; i < reference_len; i++) {
        if (is_refused_byte(reference[i])) {
            return target_new(ROBO_HTML_TARGET_INVALID, 0u);
        }
    }
    for (i = 0u; i < base_len; i++) {
        if (base && is_refused_byte(base[i])) {
            return target_new(ROBO_HTML_TARGET_INVALID, 0u);
        }
    }

    /* --- scheme ------------------------------------------------------ */
    for (scan = 0u; scan < reference_len; scan++) {
        char c = reference[scan];

        if (c == ':') {
            scheme = reference;
            scheme_len = scan;
            break;
        }
        if (c == '/' || c == '?' || c == '#') {
            break;
        }
    }
    if (scheme) {
        robo_html_navigation_target_t *t;
        size_t off;
        char *lowered;

        if (scheme_len == 0u || !ascii_is_alpha(scheme[0])) {
            /* `:rest`, or a scheme starting with a digit: the URL grammar
             * says a first path segment containing `:` makes the string a
             * URL, and a URL with no nameable scheme is a failure. */
            return target_new(ROBO_HTML_TARGET_INVALID, 0u);
        }
        for (i = 1u; i < scheme_len; i++) {
            if (!ascii_is_scheme_char(scheme[i])) {
                return target_new(ROBO_HTML_TARGET_INVALID, 0u);
            }
        }
        /*
         * The URI is carried verbatim — it is already absolute, and
         * normalizing one is a URL library's job. The scheme is lowercased
         * because keyword matching is case-insensitive and lowercase is what
         * the policy table is keyed on.
         */
        t = target_new(ROBO_HTML_TARGET_EXTERNAL,
                       reference_len + scheme_len + 8u);
        if (!t) {
            return NULL;
        }
        off = target_text_put(t, reference, reference_len);
        t->reference_off = off;
        t->reference_len = off ? reference_len : 0u;
        t->resolved_off = off;
        t->resolved_len = off ? reference_len : 0u;
        lowered = (char *)robo_malloc(scheme_len + 1u);
        if (!lowered) {
            robo_html_navigation_target_destroy(t);
            return NULL;
        }
        for (i = 0u; i < scheme_len; i++) {
            lowered[i] = ascii_lower(scheme[i]);
        }
        lowered[scheme_len] = '\0';
        off = target_text_put(t, lowered, scheme_len);
        robo_free(lowered);
        t->scheme_off = off;
        t->scheme_len = off ? scheme_len : 0u;
        return t;
    }

    /* --- fragment ---------------------------------------------------- */
    for (i = 0u; i < reference_len; i++) {
        if (reference[i] == '#') {
            has_fragment = 1;
            fragment = reference + i + 1u;
            fragment_len = reference_len - i - 1u;
            break;
        }
    }
    path_len = has_fragment ? (size_t)(fragment - reference) - 1u
                            : reference_len;

    /*
     * A protocol-relative reference (`//host/path`) is refused rather than
     * resolved. It names a network location that only the base's scheme could
     * complete, and a document in this runtime has no scheme — so the
     * alternatives are to guess a scheme (wrong) or to reinterpret somebody
     * else's host name as an internal path (dangerous). Refusing is the only
     * honest answer.
     */
    if (path_len >= 2u && reference[0] == '/' && reference[1] == '/') {
        return target_new(ROBO_HTML_TARGET_INVALID, 0u);
    }

    /* --- fragment only ----------------------------------------------- */
    if (path_len == 0u && has_fragment) {
        size_t base_path = base_path_length(base, base_len);
        robo_html_navigation_target_t *t =
            target_new(ROBO_HTML_TARGET_FRAGMENT,
                       base_path + fragment_len + 8u);
        size_t off;

        if (!t) {
            return NULL;
        }
        if (base && base_path > 0u) {
            off = target_text_put(t, base, base_path);
            t->resolved_off = off;
            t->resolved_len = off ? base_path : 0u;
        }
        off = target_text_put(t, fragment, fragment_len);
        t->fragment_off = off;
        t->fragment_len = off ? fragment_len : 0u;
        return t;
    }

    /* --- a path, with an optional query and fragment ------------------ */
    {
        size_t query_start = path_len;
        size_t base_path = base_path_length(base, base_len);
        size_t body_len = 0u;
        size_t query_len;
        char *scratch;
        /*
         * Room for the base directory, the path and then the query appended to
         * the resolved result, plus the separators resolve_path can add.
         */
        size_t scratch_cap = base_path + (path_len * 2u) + 16u;
        robo_html_navigation_target_t *t;
        size_t off;
        size_t need;

        for (i = 0u; i < path_len; i++) {
            if (reference[i] == '?') {
                query_start = i;
                break;
            }
        }
        query_len = path_len - query_start;

        /* `?query` with no path keeps the current *file*, not its directory,
         * so it is answered from the base path directly rather than by
         * resolving an empty path against the base's directory. */
        if (query_start == 0u && query_len > 0u) {
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
            need = base_path + query_len + reference_len + 6u;
            t = target_new(ROBO_HTML_TARGET_ROUTE, need);
            if (!t) {
                return NULL;
            }
            scratch = (char *)robo_malloc(base_path + query_len + 1u);
            if (!scratch) {
                robo_html_navigation_target_destroy(t);
                return NULL;
            }
            if (base && base_path > 0u) {
                copy_span(scratch, base, base_path);
            }
            copy_span(scratch + base_path, reference + query_start, query_len);
            body_len = base_path + query_len;
            off = target_text_put(t, scratch, body_len);
            t->resolved_off = off;
            t->resolved_len = off ? body_len : 0u;
            robo_free(scratch);
        } else {
            scratch = (char *)robo_malloc(scratch_cap);
            if (!scratch) {
                return NULL;
            }
            if (!resolve_path(base, base_len, reference, query_start, scratch,
                              scratch_cap, &body_len)) {
                robo_free(scratch);
                return NULL;
            }
            /*
             * The query belongs to the resolved path, so it is appended to it
             * here rather than written as a field of its own — the resolved
             * string is what a caller navigates to, query and all.
             */
            if (query_len > 0u) {
                if (body_len + query_len + 1u > scratch_cap) {
                    robo_free(scratch);
                    return NULL;
                }
                copy_span(scratch + body_len, reference + query_start,
                          query_len);
                body_len += query_len;
            }
            need = body_len + reference_len + fragment_len + 6u;
            t = target_new(ROBO_HTML_TARGET_ROUTE, need);
            if (!t) {
                robo_free(scratch);
                return NULL;
            }
            off = target_text_put(t, scratch, body_len);
            t->resolved_off = off;
            t->resolved_len = off ? body_len : 0u;
            robo_free(scratch);
        }
        if (has_fragment) {
            off = target_text_put(t, fragment, fragment_len);
            t->fragment_off = off;
            t->fragment_len = off ? fragment_len : 0u;
        }
        /* The reference travels verbatim beside the resolution, so a caller
         * can always see what the document wrote next to what it meant. */
        off = target_text_put(t, reference, reference_len);
        t->reference_off = off;
        t->reference_len = off ? reference_len : 0u;
        return t;
    }
}

robo_result_t robo_html_navigation_target_create(
    const char *href, size_t href_length, const char *base, size_t base_length,
    robo_html_navigation_target_t **out, robo_html_diag_t *diag)
{
    const char *ref;
    size_t ref_len;
    size_t begin;
    size_t end;
    robo_html_navigation_target_t *target;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT, "out target is NULL");
    }
    *out = NULL;
    if (!href && href_length > 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT,
                              "href is NULL with non-zero length");
    }

    /* Surrounding ASCII whitespace is not part of the reference. */
    begin = 0u;
    end = href_length;
    while (begin < end && ascii_is_space(href[begin])) {
        begin++;
    }
    while (end > begin && ascii_is_space(href[end - 1u])) {
        end--;
    }
    ref = href + begin;
    ref_len = end - begin;

    if (ref_len == 0u) {
        /* Absent, empty and whitespace-only all mean "nothing to navigate
         * to", which is a state and not an error. */
        target = target_new(ROBO_HTML_TARGET_NONE, 0u);
        if (!target) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                                  ROBO_OUT_OF_MEMORY, "target allocation");
        }
        *out = target;
        return ROBO_OK;
    }
    if (ref_len > ROBO_HTML_TARGET_MAX_REFERENCE) {
        /* Refused at the boundary: sizing a resolution from an unbounded
         * attribute value is exactly what a hostile document would want. */
        target = target_new(ROBO_HTML_TARGET_INVALID, 0u);
        if (!target) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                                  ROBO_OUT_OF_MEMORY, "target allocation");
        }
        *out = target;
        return ROBO_OK;
    }

    target = target_from_reference(ref, ref_len, base, base_length);
    if (!target) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_OUT_OF_MEMORY, "target allocation");
    }
    *out = target;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Element side                                                        */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_anchor_attributes(
    const robo_html_node_handle_t *element, robo_html_anchor_attributes_t *out,
    robo_html_diag_t *diag)
{
    size_t length = 0u;
    const char *value;

    if (!element || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT, "NULL element or out");
    }
    memset(out, 0, sizeof(*out));
    /*
     * Two questions in a fixed order, because their answers differ: "is this
     * still an element?" (a stale handle or a destroyed document is
     * NOT_FOUND) and "is it an anchor?" (a live <p> is NOT_SUPPORTED).
     * Collapsing them would report a dead node as "not a link", which is a
     * misleading thing to say about a node that no longer exists.
     */
    if (!robo_html_node_matches_filter(element, ROBO_HTML_NODE_FILTER_ELEMENTS)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR, ROBO_NOT_FOUND,
                              "element is not a live element");
    }
    if (!robo_html_element_is_tag(element, ROBO_HTML_TAG_A)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR, ROBO_NOT_SUPPORTED,
                              "element is not an anchor");
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = robo_html_element_attribute_value(element, name, &length);    \
        out->field##_present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##_length = value ? length : 0u;                            \
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
    if (out->href_present) {
        size_t begin = 0u;
        size_t end = out->href_length;
        size_t i;

        while (begin < end && ascii_is_space(out->href[begin])) {
            begin++;
        }
        while (end > begin && ascii_is_space(out->href[end - 1u])) {
            end--;
        }
        for (i = begin; i < end; i++) {
            if (is_refused_byte(out->href[i])) {
                end = begin;
                break;
            }
        }
        out->href_usable = (end > begin);
    }
    return ROBO_OK;
}

robo_result_t robo_html_element_navigation_target(
    const robo_html_node_handle_t *element,
    robo_html_navigation_target_t **out, robo_html_diag_t *diag)
{
    robo_html_anchor_attributes_t attrs;
    robo_html_document_id_t document_id;
    const char *base;
    robo_result_t r;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT, "out target is NULL");
    }
    *out = NULL;
    if (!element) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT, "element is NULL");
    }
    if (!robo_html_node_handle_is_wellformed(element)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR, ROBO_NOT_FOUND,
                              "element handle is not well formed");
    }
    r = robo_html_element_anchor_attributes(element, &attrs, diag);
    if (r != ROBO_OK) {
        return r;
    }
    /*
     * The document's own reference is the base. Absent or empty is a normal
     * in-memory document, not an error: relative references then resolve
     * against nothing, which the resolver handles by clamping rather than
     * refusing.
     */
    document_id = robo_html_node_handle_document(element);
    base = robo_html_document_source_name(document_id);
    return robo_html_navigation_target_create(
        attrs.href, attrs.href_length, base, base ? strlen(base) : 0u, out,
        diag);
}

/* ------------------------------------------------------------------ */
/* Fragment lookup                                                     */
/* ------------------------------------------------------------------ */

typedef struct fragment_search {
    const char *fragment;
    size_t length;
} fragment_search_t;

static int fragment_matches(const robo_html_node_view_t *view, void *user)
{
    const fragment_search_t *search = (const fragment_search_t *)user;
    robo_html_attribute_t attr;
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
    if (robo_html_element_find_attribute(&view->handle, "id", &attr, NULL) !=
        ROBO_OK) {
        return 0;
    }
    if (attr.value_length != search->length) {
        return 0;
    }
    for (i = 0u; i < search->length; i++) {
        if (attr.value[i] != search->fragment[i]) {
            return 0;
        }
    }
    return 1;
}

robo_result_t robo_html_navigation_find_fragment(
    robo_html_document_id_t document_id, const char *fragment,
    size_t fragment_length, robo_html_node_handle_t *out, robo_html_diag_t *diag)
{
    robo_html_node_handle_t root;
    fragment_search_t search;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT, "out handle is NULL");
    }
    robo_html_node_handle_clear(out);
    if (!fragment && fragment_length > 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT,
                              "fragment is NULL with non-zero length");
    }
    if (!robo_html_document_is_live(document_id)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "document is not live");
    }
    root = robo_html_document_root(document_id);
    if (!robo_html_node_handle_is_wellformed(&root)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "document has no usable root");
    }
    search.fragment = fragment;
    search.length = fragment_length;
    /*
     * `find_first` rather than `find_descendant`: the document node itself is
     * never an element, so "at or below" and "inside" cannot differ here, and
     * starting at the root is what makes the result document order.
     */
    return robo_html_node_find_first(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                     fragment_matches, &search, out, diag);
}

/* ------------------------------------------------------------------ */
/* Handler seam                                                        */
/* ------------------------------------------------------------------ */

void robo_html_navigation_handler_init(robo_html_navigation_handler_t *handler)
{
    if (handler) {
        handler->navigate = NULL;
        handler->userdata = NULL;
    }
}

int robo_html_navigation_handler_is_usable(
    const robo_html_navigation_handler_t *handler)
{
    return handler != NULL && handler->navigate != NULL;
}

/* ------------------------------------------------------------------ */
/* Activation                                                          */
/* ------------------------------------------------------------------ */

void robo_html_navigation_outcome_clear(robo_html_navigation_outcome_t *out)
{
    if (!out) {
        return;
    }
    robo_html_navigation_target_destroy(out->target);
    memset(out, 0, sizeof(*out));
    robo_html_node_handle_clear(&out->fragment_node);
}

/**
 * Adopt @p target into @p out and fill the target-derived report fields.
 *
 * Called once per activation so every path — including the refusals that
 * never reach the seam — reports the same view of the same target. The
 * outcome takes ownership, which is what keeps the reported strings valid
 * for as long as the caller holds the outcome.
 */
static void outcome_adopt_target(robo_html_navigation_outcome_t *out,
                                robo_html_navigation_target_t *target)
{
    size_t length = 0u;

    out->target = target;
    out->target_kind = robo_html_navigation_target_kind(target);
    out->resolved = robo_html_navigation_target_resolved(target, &length);
    out->resolved_length = out->resolved ? length : 0u;
    out->fragment = robo_html_navigation_target_fragment(target, &length);
    out->fragment_length = out->fragment ? length : 0u;
    out->scheme = robo_html_navigation_target_scheme(target, &length);
    out->scheme_length = out->scheme ? length : 0u;
    out->handler_called = 0;
    out->handler_result = ROBO_OK;
    out->fragment_found = 0;
    robo_html_node_handle_clear(&out->fragment_node);
}

/**
 * Ask the seam, and translate its answer.
 *
 * A handler accepts by returning ROBO_OK and leaving @p want set; anything
 * else — or a kind it changed — is a decline. The caller's `handler_result`
 * records what happened, so "the router said no" is distinguishable from
 * "there is no router".
 */
static robo_html_navigation_outcome_kind_t consult_handler(
    const robo_html_navigation_handler_t *handler,
    const robo_html_navigation_request_t *request,
    robo_html_navigation_outcome_kind_t want,
    robo_html_navigation_outcome_t *out, robo_html_diag_t *diag)
{
    robo_html_navigation_outcome_kind_t accepted = want;

    out->handler_called = 1;
    out->handler_result =
        handler->navigate(handler->userdata, request, &accepted, diag);
    if (out->handler_result == ROBO_OK && accepted == want) {
        return want;
    }
    return ROBO_HTML_NAVIGATION_UNSUPPORTED;
}

robo_result_t robo_html_element_activate(
    const robo_html_node_handle_t *element,
    const robo_html_navigation_handler_t *handler,
    robo_html_navigation_outcome_t *out, robo_html_diag_t *diag)
{
    robo_html_anchor_attributes_t attrs;
    robo_html_navigation_target_t *target = NULL;
    robo_html_navigation_outcome_t local;
    robo_html_navigation_outcome_t *result;
    robo_html_document_id_t document_id;
    robo_html_navigation_request_t request;
    const char *base;
    robo_result_t r;

    if (out) {
        /* A zeroed outcome is what every failure path leaves behind, and it
         * is safe to clear: the caller can always clear what it was given. */
        memset(out, 0, sizeof(*out));
        robo_html_node_handle_clear(&out->fragment_node);
        result = out;
    } else {
        result = &local;
    }
    if (!out) {
        memset(result, 0, sizeof(*result));
        robo_html_node_handle_clear(&result->fragment_node);
    }

    if (!element) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR,
                              ROBO_INVALID_ARGUMENT, "element is NULL");
    }
    if (!robo_html_node_handle_is_wellformed(element)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ANCHOR, ROBO_NOT_FOUND,
                              "element handle is not well formed");
    }

    /* 1. It must be an anchor. A caller that activates a <p> has a bug, and
     *    reporting the bug beats reporting "no target". */
    r = robo_html_element_anchor_attributes(element, &attrs, diag);
    if (r != ROBO_OK) {
        return r;
    }

    /* 2. Resolve against the document's own reference. */
    document_id = robo_html_node_handle_document(element);
    base = robo_html_document_source_name(document_id);
    r = robo_html_navigation_target_create(attrs.href, attrs.href_length, base,
                                          base ? strlen(base) : 0u, &target,
                                          diag);
    if (r != ROBO_OK) {
        return r;
    }
    outcome_adopt_target(result, target);

    /* 3. Build the request. */
    memset(&request, 0, sizeof(request));
    request.document_id = document_id;
    request.anchor = *element;
    request.target = target;
    request.base = base ? base : "";
    request.base_length = base ? strlen(base) : 0u;
    request.target_attribute = attrs.target;
    request.target_length = attrs.target_length;
    request.target_context =
        robo_html_link_target_context_for(attrs.target, attrs.target_length);
    request.rel = attrs.rel;
    request.rel_length = attrs.rel_length;
    request.download_requested = attrs.download_present;
    request.download_name = attrs.download;
    request.download_name_length = attrs.download_length;
    request.title = attrs.title;
    request.title_length = attrs.title_length;
    request.reason = ROBO_HTML_NAVIGATION_REASON_ACTIVATION;

    /* 4-7. Decide. Nothing here performs I/O, and nothing here can report a
     *      navigation that did not happen. */
    switch (result->target_kind) {
    case ROBO_HTML_TARGET_NONE:
        /* A link with no href is not a link. That is a state, and the only
         * outcome this phase reports without asking anybody. */
        result->kind = ROBO_HTML_NAVIGATION_NO_TARGET;
        break;

    case ROBO_HTML_TARGET_INVALID:
        /* There is no request to construct: the reference could not be
         * resolved, so nothing is delegated and nothing is claimed. */
        result->kind = ROBO_HTML_NAVIGATION_REJECTED;
        break;

    case ROBO_HTML_TARGET_FRAGMENT: {
        /*
         * The one navigation this phase can complete. A fragment needs no
         * I/O — it names an element in a tree we already hold — so it is
         * answered here and never delegated, which is also why it works with
         * no handler installed.
         */
        robo_html_node_handle_t found;

        r = robo_html_navigation_find_fragment(
            document_id, result->fragment, result->fragment_length, &found,
            NULL);
        if (r == ROBO_OK) {
            result->kind = ROBO_HTML_NAVIGATION_FRAGMENT;
            result->fragment_node = found;
            result->fragment_found = 1;
        } else if (r == ROBO_NOT_FOUND) {
            result->kind = ROBO_HTML_NAVIGATION_FRAGMENT_MISSING;
        } else {
            robo_html_navigation_outcome_clear(result);
            if (out) {
                robo_html_node_handle_clear(&out->fragment_node);
            }
            return r;
        }
        break;
    }

    case ROBO_HTML_TARGET_ROUTE:
        /*
         * `download` asks to *save* the target rather than go to it. Saving
         * needs a filesystem capability this runtime does not have, so the
         * intent is reported rather than silently ignored — and the seam is
         * not consulted, because handing a router a save request it cannot
         * perform would be worse than telling the truth.
         */
        if (request.download_requested) {
            result->kind = ROBO_HTML_NAVIGATION_DOWNLOAD_PENDING;
            break;
        }
        if (!robo_html_navigation_handler_is_usable(handler)) {
            /* No router installed: this runtime cannot perform internal
             * navigation yet, and saying so is the whole point. */
            result->kind = ROBO_HTML_NAVIGATION_UNSUPPORTED;
            break;
        }
        result->kind = consult_handler(handler, &request,
                                       ROBO_HTML_NAVIGATION_ROUTE, result,
                                       diag);
        break;

    case ROBO_HTML_TARGET_EXTERNAL:
        /*
         * The phase's central security rule. A scheme this runtime does not
         * permit is refused *before* the seam is consulted: `javascript:`
         * never reaches a handler, and neither does `file:` or `data:`. An
         * anchor cannot be used to reach a subsystem the document was never
         * granted, whatever the application installed.
         */
        if (!robo_html_navigation_scheme_is_permitted(result->scheme,
                                                     result->scheme_length)) {
            result->kind = ROBO_HTML_NAVIGATION_UNSUPPORTED;
            break;
        }
        if (request.download_requested) {
            result->kind = ROBO_HTML_NAVIGATION_DOWNLOAD_PENDING;
            break;
        }
        if (!robo_html_navigation_handler_is_usable(handler)) {
            /* Permitted but unreachable: `https` needs the network stack,
             * which is a later phase. UNSUPPORTED is the honest answer, and
             * the one that keeps this phase from claiming a fetch. */
            result->kind = ROBO_HTML_NAVIGATION_UNSUPPORTED;
            break;
        }
        result->kind = consult_handler(handler, &request,
                                       ROBO_HTML_NAVIGATION_EXTERNAL, result,
                                       diag);
        break;

    default:
        result->kind = ROBO_HTML_NAVIGATION_UNSUPPORTED;
        break;
    }

    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Debug output                                                        */
/* ------------------------------------------------------------------ */

typedef struct text_builder {
    char *out;
    size_t capacity;
    size_t length;
} text_builder_t;

static void tb_init(text_builder_t *b, char *out, size_t capacity)
{
    b->out = out;
    b->capacity = capacity;
    b->length = 0u;
    if (out && capacity > 0u) {
        out[0] = '\0';
    }
}

static void tb_text(text_builder_t *b, const char *text)
{
    if (!b->out || b->capacity == 0u || !text) {
        return;
    }
    while (*text != '\0' && b->length + 1u < b->capacity) {
        b->out[b->length++] = *text++;
    }
    b->out[b->length] = '\0';
}

static void tb_bytes(text_builder_t *b, const char *bytes, size_t length)
{
    if (!b->out || b->capacity == 0u) {
        return;
    }
    if (!bytes) {
        tb_text(b, "(null)");
        return;
    }
    while (length > 0u && b->length + 1u < b->capacity) {
        b->out[b->length++] = *bytes++;
        length--;
    }
    b->out[b->length] = '\0';
}

size_t robo_html_navigation_target_describe(
    const robo_html_navigation_target_t *target, char *out, size_t capacity)
{
    text_builder_t b;
    const char *s;
    size_t length = 0u;

    tb_init(&b, out, capacity);
    tb_text(&b, "target kind=");
    tb_text(&b,
            robo_html_target_kind_name(robo_html_navigation_target_kind(target)));
    if (!target) {
        return b.length;
    }
    s = robo_html_navigation_target_reference(target, &length);
    if (s) {
        tb_text(&b, " reference=\"");
        tb_bytes(&b, s, length);
        tb_text(&b, "\"");
    }
    s = robo_html_navigation_target_resolved(target, &length);
    if (s) {
        tb_text(&b, " resolved=\"");
        tb_bytes(&b, s, length);
        tb_text(&b, "\"");
    }
    s = robo_html_navigation_target_fragment(target, &length);
    if (s) {
        tb_text(&b, " fragment=\"#");
        tb_bytes(&b, s, length);
        tb_text(&b, "\"");
    }
    s = robo_html_navigation_target_scheme(target, &length);
    if (s) {
        tb_text(&b, " scheme=");
        tb_bytes(&b, s, length);
        tb_text(&b, " permitted=");
        tb_text(&b, robo_html_navigation_scheme_is_permitted(s, length) ? "yes"
                                                                       : "no");
    }
    return b.length;
}

size_t robo_html_element_anchor_describe(
    const robo_html_node_handle_t *element,
    const robo_html_navigation_target_t *target, char *out, size_t capacity)
{
    text_builder_t b;
    robo_html_anchor_attributes_t attrs;
    robo_html_target_kind_t kind = ROBO_HTML_TARGET_NONE;

    tb_init(&b, out, capacity);
    if (!element ||
        robo_html_element_anchor_attributes(element, &attrs, NULL) != ROBO_OK) {
        tb_text(&b, "a (not an anchor)");
        return b.length;
    }
    tb_text(&b, "a");
    if (attrs.href_present) {
        tb_text(&b, " href=\"");
        tb_bytes(&b, attrs.href, attrs.href_length);
        tb_text(&b, "\"");
    }
    if (target) {
        const char *resolved =
            robo_html_navigation_target_resolved(target, NULL);

        kind = robo_html_navigation_target_kind(target);
        if (resolved) {
            tb_text(&b, " resolved=\"");
            tb_bytes(&b, resolved, strlen(resolved));
            tb_text(&b, "\"");
        }
    }
    tb_text(&b, " target=");
    tb_text(&b, robo_html_link_target_context_name(
                     robo_html_link_target_context_for(attrs.target,
                                                       attrs.target_length)));
    tb_text(&b, " rel=\"");
    if (attrs.rel_present) {
        tb_bytes(&b, attrs.rel, attrs.rel_length);
    }
    tb_text(&b, "\"");
    if (attrs.download_present) {
        tb_text(&b, " download=\"");
        tb_bytes(&b, attrs.download, attrs.download_length);
        tb_text(&b, "\"");
    }
    tb_text(&b, " state=");
    if (!attrs.href_present) {
        tb_text(&b, "inert");
    } else if (!attrs.href_usable) {
        tb_text(&b, "empty");
    } else if (kind == ROBO_HTML_TARGET_INVALID) {
        tb_text(&b, "rejected");
    } else if (kind == ROBO_HTML_TARGET_EXTERNAL) {
        const char *scheme = robo_html_navigation_target_scheme(target, NULL);
        size_t scheme_len = 0u;

        if (scheme) {
            scheme_len = strlen(scheme);
        }
        tb_text(&b, robo_html_navigation_scheme_is_permitted(scheme, scheme_len)
                         ? "external"
                         : "refused");
    } else {
        tb_text(&b, "activatable");
    }
    return b.length;
}
