/*
 * Unicode Bidirectional Algorithm — UAX #9 (Unicode 16.0.0).
 *
 * Faithful port of Tools/ucd/bidi_ref.py (BidiCharacterTest 91707/91707).
 */

#include "RoboUnicode/RoboUnicodeBidi.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

#define ROBO_BIDI_MAX_DEPTH 125
#define ROBO_BIDI_MAX_BRACKET_STACK 63
#define ROBO_BIDI_OVERRIDE_N 0
#define ROBO_BIDI_OVERRIDE_L 1
#define ROBO_BIDI_OVERRIDE_R 2

typedef struct robo_bidi_stack_entry {
    uint8_t level;
    uint8_t override_status; /* N / L / R */
    uint8_t isolate;
} robo_bidi_stack_entry_t;

typedef struct robo_bidi_irs {
    size_t *indices;
    size_t count;
    uint8_t level;
    uint8_t sos; /* ROBO_U_BC_L or ROBO_U_BC_R */
    uint8_t eos;
    int prev_level; /* -1 if none */
    int next_level;
    int use_para_eos;
} robo_bidi_irs_t;

const char *robo_unicode_bidi_ucd_version_string(void)
{
    return ROBO_BIDI_UCD_VERSION;
}

robo_unicode_bidi_class_t robo_unicode_bidi_class(robo_uchar32_t cp)
{
    return robo_bidi_class_property(cp);
}

static int robo_bidi_is_remove(robo_unicode_bidi_class_t t)
{
    return t == ROBO_U_BC_RLE || t == ROBO_U_BC_LRE || t == ROBO_U_BC_RLO ||
           t == ROBO_U_BC_LRO || t == ROBO_U_BC_PDF || t == ROBO_U_BC_BN;
}

static int robo_bidi_is_isolate_init(robo_unicode_bidi_class_t t)
{
    return t == ROBO_U_BC_LRI || t == ROBO_U_BC_RLI || t == ROBO_U_BC_FSI;
}

static int robo_bidi_is_ni(robo_unicode_bidi_class_t t)
{
    return t == ROBO_U_BC_B || t == ROBO_U_BC_S || t == ROBO_U_BC_WS ||
           t == ROBO_U_BC_ON || t == ROBO_U_BC_FSI || t == ROBO_U_BC_LRI ||
           t == ROBO_U_BC_RLI || t == ROBO_U_BC_PDI;
}

static int robo_bidi_is_strong_p2(robo_unicode_bidi_class_t t)
{
    return t == ROBO_U_BC_L || t == ROBO_U_BC_R || t == ROBO_U_BC_AL;
}

static int robo_bidi_least_odd_gt(int level)
{
    return ((level % 2) == 0) ? level + 1 : level + 2;
}

static int robo_bidi_least_even_gt(int level)
{
    return ((level % 2) == 1) ? level + 1 : level + 2;
}

static uint8_t robo_bidi_sos_eos_type(int level_a, int level_b)
{
    int m = level_a > level_b ? level_a : level_b;
    return ((m % 2) == 1) ? (uint8_t)ROBO_U_BC_R : (uint8_t)ROBO_U_BC_L;
}

static int robo_bidi_brackets_equal(uint32_t a, uint32_t b)
{
    if (a == b) {
        return 1;
    }
    /* U+232A RIGHT-POINTING ANGLE BRACKET <-> U+3009 RIGHT ANGLE BRACKET */
    if ((a == 0x232Au && b == 0x3009u) || (a == 0x3009u && b == 0x232Au)) {
        return 1;
    }
    return 0;
}

static void robo_bidi_matching_pdis(const uint8_t *types, size_t n, int *match)
{
    size_t i;
    for (i = 0; i < n; i++) {
        match[i] = -1;
    }
    for (i = 0; i < n; i++) {
        size_t j;
        int depth;
        if (!robo_bidi_is_isolate_init((robo_unicode_bidi_class_t)types[i])) {
            continue;
        }
        depth = 1;
        for (j = i + 1u; j < n; j++) {
            robo_unicode_bidi_class_t tj = (robo_unicode_bidi_class_t)types[j];
            if (robo_bidi_is_isolate_init(tj)) {
                depth++;
            } else if (tj == ROBO_U_BC_PDI) {
                depth--;
                if (depth == 0) {
                    match[i] = (int)j;
                    break;
                }
            }
        }
    }
}

static void robo_bidi_matching_initiators(const int *matching_pdi, size_t n, int *inv)
{
    size_t i;
    for (i = 0; i < n; i++) {
        inv[i] = -1;
    }
    for (i = 0; i < n; i++) {
        if (matching_pdi[i] >= 0) {
            inv[matching_pdi[i]] = (int)i;
        }
    }
}

static uint8_t robo_bidi_determine_paragraph_level(const uint8_t *types,
                                                   size_t start,
                                                   size_t end)
{
    size_t i = start;
    while (i < end) {
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)types[i];
        if (robo_bidi_is_isolate_init(t)) {
            int depth = 1;
            i++;
            while (i < end) {
                robo_unicode_bidi_class_t ti = (robo_unicode_bidi_class_t)types[i];
                if (robo_bidi_is_isolate_init(ti)) {
                    depth++;
                } else if (ti == ROBO_U_BC_PDI) {
                    depth--;
                    if (depth == 0) {
                        i++;
                        break;
                    }
                }
                i++;
            }
            continue;
        }
        if (robo_bidi_is_strong_p2(t)) {
            return (t == ROBO_U_BC_R || t == ROBO_U_BC_AL) ? 1u : 0u;
        }
        i++;
    }
    return 0u;
}

uint8_t robo_unicode_bidi_paragraph_direction(const robo_uchar32_t *cps, size_t len)
{
    size_t i;
    uint8_t *types;
    uint8_t pl;

    if (!cps || len == 0u) {
        return 0u;
    }
    types = (uint8_t *)robo_malloc(len);
    if (!types) {
        /* Fallback: scan without heap for first strong (isolate-aware). */
        size_t j = 0;
        while (j < len) {
            robo_unicode_bidi_class_t t = robo_bidi_class_property(cps[j]);
            if (robo_bidi_is_isolate_init(t)) {
                int depth = 1;
                j++;
                while (j < len) {
                    robo_unicode_bidi_class_t tj = robo_bidi_class_property(cps[j]);
                    if (robo_bidi_is_isolate_init(tj)) {
                        depth++;
                    } else if (tj == ROBO_U_BC_PDI) {
                        depth--;
                        if (depth == 0) {
                            j++;
                            break;
                        }
                    }
                    j++;
                }
                continue;
            }
            if (robo_bidi_is_strong_p2(t)) {
                return (t == ROBO_U_BC_R || t == ROBO_U_BC_AL) ? 1u : 0u;
            }
            j++;
        }
        return 0u;
    }
    for (i = 0; i < len; i++) {
        types[i] = (uint8_t)robo_bidi_class_property(cps[i]);
    }
    pl = robo_bidi_determine_paragraph_level(types, 0, len);
    robo_free(types);
    return pl;
}

static void robo_bidi_resolve_explicit(uint8_t *types,
                                       size_t n,
                                       uint8_t paragraph_level,
                                       const int *matching_pdi,
                                       int16_t *levels)
{
    robo_bidi_stack_entry_t stack[ROBO_BIDI_MAX_DEPTH + 2];
    size_t stack_len = 1;
    int overflow_isolate = 0;
    int overflow_embedding = 0;
    int valid_isolate = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        levels[i] = -1;
    }

    stack[0].level = paragraph_level;
    stack[0].override_status = ROBO_BIDI_OVERRIDE_N;
    stack[0].isolate = 0;

    i = 0;
    while (i < n) {
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)types[i];
        robo_bidi_stack_entry_t *top = &stack[stack_len - 1u];
        int new_level;

        if (t == ROBO_U_BC_RLE) {
            new_level = robo_bidi_least_odd_gt((int)top->level);
            if (new_level <= ROBO_BIDI_MAX_DEPTH && overflow_isolate == 0 &&
                overflow_embedding == 0) {
                stack[stack_len].level = (uint8_t)new_level;
                stack[stack_len].override_status = ROBO_BIDI_OVERRIDE_N;
                stack[stack_len].isolate = 0;
                stack_len++;
            } else if (overflow_isolate == 0) {
                overflow_embedding++;
            }
            i++;
            continue;
        }
        if (t == ROBO_U_BC_LRE) {
            new_level = robo_bidi_least_even_gt((int)top->level);
            if (new_level <= ROBO_BIDI_MAX_DEPTH && overflow_isolate == 0 &&
                overflow_embedding == 0) {
                stack[stack_len].level = (uint8_t)new_level;
                stack[stack_len].override_status = ROBO_BIDI_OVERRIDE_N;
                stack[stack_len].isolate = 0;
                stack_len++;
            } else if (overflow_isolate == 0) {
                overflow_embedding++;
            }
            i++;
            continue;
        }
        if (t == ROBO_U_BC_RLO) {
            new_level = robo_bidi_least_odd_gt((int)top->level);
            if (new_level <= ROBO_BIDI_MAX_DEPTH && overflow_isolate == 0 &&
                overflow_embedding == 0) {
                stack[stack_len].level = (uint8_t)new_level;
                stack[stack_len].override_status = ROBO_BIDI_OVERRIDE_R;
                stack[stack_len].isolate = 0;
                stack_len++;
            } else if (overflow_isolate == 0) {
                overflow_embedding++;
            }
            i++;
            continue;
        }
        if (t == ROBO_U_BC_LRO) {
            new_level = robo_bidi_least_even_gt((int)top->level);
            if (new_level <= ROBO_BIDI_MAX_DEPTH && overflow_isolate == 0 &&
                overflow_embedding == 0) {
                stack[stack_len].level = (uint8_t)new_level;
                stack[stack_len].override_status = ROBO_BIDI_OVERRIDE_L;
                stack[stack_len].isolate = 0;
                stack_len++;
            } else if (overflow_isolate == 0) {
                overflow_embedding++;
            }
            i++;
            continue;
        }
        if (robo_bidi_is_isolate_init(t)) {
            robo_unicode_bidi_class_t effective = t;
            if (t == ROBO_U_BC_FSI) {
                int pdi = matching_pdi[i];
                size_t end = (pdi >= 0) ? (size_t)pdi : n;
                uint8_t pl =
                    robo_bidi_determine_paragraph_level(types, i + 1u, end);
                effective = (pl == 1u) ? ROBO_U_BC_RLI : ROBO_U_BC_LRI;
            }
            levels[i] = (int16_t)top->level;
            if (top->override_status == ROBO_BIDI_OVERRIDE_L) {
                types[i] = (uint8_t)ROBO_U_BC_L;
            } else if (top->override_status == ROBO_BIDI_OVERRIDE_R) {
                types[i] = (uint8_t)ROBO_U_BC_R;
            }
            if (effective == ROBO_U_BC_RLI) {
                new_level = robo_bidi_least_odd_gt((int)top->level);
            } else {
                new_level = robo_bidi_least_even_gt((int)top->level);
            }
            if (new_level <= ROBO_BIDI_MAX_DEPTH && overflow_isolate == 0 &&
                overflow_embedding == 0) {
                valid_isolate++;
                stack[stack_len].level = (uint8_t)new_level;
                stack[stack_len].override_status = ROBO_BIDI_OVERRIDE_N;
                stack[stack_len].isolate = 1;
                stack_len++;
            } else {
                overflow_isolate++;
            }
            i++;
            continue;
        }
        if (t == ROBO_U_BC_PDI) {
            if (overflow_isolate > 0) {
                overflow_isolate--;
            } else if (valid_isolate == 0) {
                /* unmatched PDI */
            } else {
                overflow_embedding = 0;
                while (stack_len > 0u && !stack[stack_len - 1u].isolate) {
                    stack_len--;
                }
                if (stack_len > 0u && stack[stack_len - 1u].isolate) {
                    stack_len--;
                    valid_isolate--;
                }
            }
            top = &stack[stack_len - 1u];
            levels[i] = (int16_t)top->level;
            if (top->override_status == ROBO_BIDI_OVERRIDE_L) {
                types[i] = (uint8_t)ROBO_U_BC_L;
            } else if (top->override_status == ROBO_BIDI_OVERRIDE_R) {
                types[i] = (uint8_t)ROBO_U_BC_R;
            }
            i++;
            continue;
        }
        if (t == ROBO_U_BC_PDF) {
            if (overflow_isolate > 0) {
                /* ignore */
            } else if (overflow_embedding > 0) {
                overflow_embedding--;
            } else if (stack_len >= 2u && !stack[stack_len - 1u].isolate) {
                stack_len--;
            }
            i++;
            continue;
        }
        if (t == ROBO_U_BC_B) {
            levels[i] = (int16_t)paragraph_level;
            stack_len = 1;
            stack[0].level = paragraph_level;
            stack[0].override_status = ROBO_BIDI_OVERRIDE_N;
            stack[0].isolate = 0;
            overflow_isolate = 0;
            overflow_embedding = 0;
            valid_isolate = 0;
            i++;
            continue;
        }
        if (t == ROBO_U_BC_BN) {
            i++;
            continue;
        }
        /* X6 */
        levels[i] = (int16_t)top->level;
        if (top->override_status == ROBO_BIDI_OVERRIDE_L) {
            types[i] = (uint8_t)ROBO_U_BC_L;
        } else if (top->override_status == ROBO_BIDI_OVERRIDE_R) {
            types[i] = (uint8_t)ROBO_U_BC_R;
        }
        i++;
    }
}

static void robo_bidi_apply_W(uint8_t *types, const size_t *indices, size_t m, uint8_t sos)
{
    size_t k;
    uint8_t *change;

    if (m == 0u) {
        return;
    }

#define T(k_) (((k_) == (size_t)-1) ? (sos) : types[indices[(k_)]])
#define SET(k_, v_) (types[indices[(k_)]] = (uint8_t)(v_))

    /* W1 */
    for (k = 0; k < m; k++) {
        if ((robo_unicode_bidi_class_t)T(k) != ROBO_U_BC_NSM) {
            continue;
        }
        if (k == 0u) {
            SET(k, sos);
        } else {
            robo_unicode_bidi_class_t prev = (robo_unicode_bidi_class_t)T(k - 1u);
            if (robo_bidi_is_isolate_init(prev) || prev == ROBO_U_BC_PDI) {
                SET(k, ROBO_U_BC_ON);
            } else {
                SET(k, prev);
            }
        }
    }

    /* W2 */
    for (k = 0; k < m; k++) {
        size_t j;
        uint8_t strong;
        if ((robo_unicode_bidi_class_t)T(k) != ROBO_U_BC_EN) {
            continue;
        }
        if (k == 0u) {
            strong = sos;
        } else {
            j = k - 1u;
            for (;;) {
                robo_unicode_bidi_class_t tj = (robo_unicode_bidi_class_t)T(j);
                if (tj == ROBO_U_BC_R || tj == ROBO_U_BC_L || tj == ROBO_U_BC_AL) {
                    strong = (uint8_t)tj;
                    break;
                }
                if (j == 0u) {
                    strong = sos;
                    break;
                }
                j--;
            }
        }
        if (strong == (uint8_t)ROBO_U_BC_AL) {
            SET(k, ROBO_U_BC_AN);
        }
    }

    /* W3 */
    for (k = 0; k < m; k++) {
        if ((robo_unicode_bidi_class_t)T(k) == ROBO_U_BC_AL) {
            SET(k, ROBO_U_BC_R);
        }
    }

    /* W4 */
    for (k = 1; k + 1u < m; k++) {
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)T(k);
        if (t == ROBO_U_BC_ES) {
            if ((robo_unicode_bidi_class_t)T(k - 1u) == ROBO_U_BC_EN &&
                (robo_unicode_bidi_class_t)T(k + 1u) == ROBO_U_BC_EN) {
                SET(k, ROBO_U_BC_EN);
            }
        } else if (t == ROBO_U_BC_CS) {
            robo_unicode_bidi_class_t left = (robo_unicode_bidi_class_t)T(k - 1u);
            robo_unicode_bidi_class_t right = (robo_unicode_bidi_class_t)T(k + 1u);
            if (left == ROBO_U_BC_EN && right == ROBO_U_BC_EN) {
                SET(k, ROBO_U_BC_EN);
            } else if (left == ROBO_U_BC_AN && right == ROBO_U_BC_AN) {
                SET(k, ROBO_U_BC_AN);
            }
        }
    }

    /* W5 */
    change = (uint8_t *)robo_calloc(m, 1u);
    if (change) {
        for (k = 0; k < m; k++) {
            size_t j;
            if ((robo_unicode_bidi_class_t)T(k) != ROBO_U_BC_ET) {
                continue;
            }
            j = k;
            while (j > 0u && (robo_unicode_bidi_class_t)T(j - 1u) == ROBO_U_BC_ET) {
                j--;
            }
            if (j > 0u && (robo_unicode_bidi_class_t)T(j - 1u) == ROBO_U_BC_EN) {
                change[k] = 1;
                continue;
            }
            j = k + 1u;
            while (j < m && (robo_unicode_bidi_class_t)T(j) == ROBO_U_BC_ET) {
                j++;
            }
            if (j < m && (robo_unicode_bidi_class_t)T(j) == ROBO_U_BC_EN) {
                change[k] = 1;
            }
        }
        for (k = 0; k < m; k++) {
            if (change[k]) {
                SET(k, ROBO_U_BC_EN);
            }
        }
        robo_free(change);
    } else {
        /* OOM: still apply W5 with inline scan (slower, no alloc). */
        for (k = 0; k < m; k++) {
            size_t j;
            int hit = 0;
            if ((robo_unicode_bidi_class_t)T(k) != ROBO_U_BC_ET) {
                continue;
            }
            j = k;
            while (j > 0u && (robo_unicode_bidi_class_t)T(j - 1u) == ROBO_U_BC_ET) {
                j--;
            }
            if (j > 0u && (robo_unicode_bidi_class_t)T(j - 1u) == ROBO_U_BC_EN) {
                hit = 1;
            } else {
                j = k + 1u;
                while (j < m && (robo_unicode_bidi_class_t)T(j) == ROBO_U_BC_ET) {
                    j++;
                }
                if (j < m && (robo_unicode_bidi_class_t)T(j) == ROBO_U_BC_EN) {
                    hit = 1;
                }
            }
            if (hit) {
                SET(k, ROBO_U_BC_EN);
            }
        }
    }

    /* W6 */
    for (k = 0; k < m; k++) {
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)T(k);
        if (t == ROBO_U_BC_ES || t == ROBO_U_BC_CS || t == ROBO_U_BC_ET) {
            SET(k, ROBO_U_BC_ON);
        }
    }

    /* W7 */
    for (k = 0; k < m; k++) {
        size_t j;
        uint8_t strong;
        if ((robo_unicode_bidi_class_t)T(k) != ROBO_U_BC_EN) {
            continue;
        }
        if (k == 0u) {
            strong = sos;
        } else {
            j = k - 1u;
            for (;;) {
                robo_unicode_bidi_class_t tj = (robo_unicode_bidi_class_t)T(j);
                if (tj == ROBO_U_BC_R || tj == ROBO_U_BC_L) {
                    strong = (uint8_t)tj;
                    break;
                }
                if (j == 0u) {
                    strong = sos;
                    break;
                }
                j--;
            }
        }
        if (strong == (uint8_t)ROBO_U_BC_L) {
            SET(k, ROBO_U_BC_L);
        }
    }

#undef T
#undef SET
}

static uint8_t robo_bidi_n0_strong(robo_unicode_bidi_class_t t)
{
    if (t == ROBO_U_BC_L) {
        return (uint8_t)ROBO_U_BC_L;
    }
    if (t == ROBO_U_BC_R || t == ROBO_U_BC_EN || t == ROBO_U_BC_AN) {
        return (uint8_t)ROBO_U_BC_R;
    }
    return 0xFFu; /* none */
}

static size_t robo_bidi_identify_bracket_pairs(const robo_uchar32_t *codepoints,
                                               const uint8_t *types,
                                               const size_t *indices,
                                               size_t m,
                                               size_t *pair_open,
                                               size_t *pair_close,
                                               size_t pair_cap)
{
    uint32_t stack_closer[ROBO_BIDI_MAX_BRACKET_STACK];
    size_t stack_open[ROBO_BIDI_MAX_BRACKET_STACK];
    size_t stack_len = 0;
    size_t pair_count = 0;
    size_t ki;

    for (ki = 0; ki < m; ki++) {
        size_t pos = indices[ki];
        uint32_t pair = 0;
        int is_open = 0;
        robo_uchar32_t cp;

        if ((robo_unicode_bidi_class_t)types[pos] != ROBO_U_BC_ON) {
            continue;
        }
        cp = codepoints[pos];
        if (!robo_bidi_bracket_lookup(cp, &pair, &is_open)) {
            continue;
        }
        if (is_open) {
            if (stack_len >= ROBO_BIDI_MAX_BRACKET_STACK) {
                return 0; /* BD16 abort */
            }
            stack_closer[stack_len] = pair;
            stack_open[stack_len] = pos;
            stack_len++;
        } else {
            size_t si;
            if (stack_len == 0u) {
                continue;
            }
            for (si = stack_len; si > 0u; si--) {
                size_t s = si - 1u;
                if (robo_bidi_brackets_equal(cp, stack_closer[s])) {
                    if (pair_count < pair_cap) {
                        pair_open[pair_count] = stack_open[s];
                        pair_close[pair_count] = pos;
                        pair_count++;
                    }
                    stack_len = s;
                    break;
                }
            }
        }
    }

    /* Sort pairs by opening position (insertion sort; m is small). */
    {
        size_t a;
        for (a = 1; a < pair_count; a++) {
            size_t o = pair_open[a];
            size_t c = pair_close[a];
            size_t b = a;
            while (b > 0u && pair_open[b - 1u] > o) {
                pair_open[b] = pair_open[b - 1u];
                pair_close[b] = pair_close[b - 1u];
                b--;
            }
            pair_open[b] = o;
            pair_close[b] = c;
        }
    }
    return pair_count;
}

static void robo_bidi_apply_N(const robo_uchar32_t *codepoints,
                              uint8_t *types,
                              const size_t *indices,
                              size_t m,
                              uint8_t sos,
                              uint8_t eos,
                              uint8_t embedding_level,
                              const uint8_t *was_nsm)
{
    size_t k;
    uint8_t e;
    size_t *pair_open;
    size_t *pair_close;
    size_t pair_count;
    size_t pi;

    if (m == 0u) {
        return;
    }

    e = ((embedding_level % 2u) == 1u) ? (uint8_t)ROBO_U_BC_R
                                       : (uint8_t)ROBO_U_BC_L;

    pair_open = (size_t *)robo_malloc(m * sizeof(size_t));
    pair_close = (size_t *)robo_malloc(m * sizeof(size_t));
    if (!pair_open || !pair_close) {
        robo_free(pair_open);
        robo_free(pair_close);
        return;
    }

    pair_count = robo_bidi_identify_bracket_pairs(
        codepoints, types, indices, m, pair_open, pair_close, m);

    for (pi = 0; pi < pair_count; pi++) {
        size_t open_pos = pair_open[pi];
        size_t close_pos = pair_close[pi];
        uint8_t strong_content = 0xFFu;
        int has_opposite = 0;
        int inside = 0;
        uint8_t new_t;
        size_t local_k;

        for (k = 0; k < m; k++) {
            size_t pos = indices[k];
            uint8_t s;
            if (pos == open_pos) {
                inside = 1;
                continue;
            }
            if (pos == close_pos) {
                break;
            }
            if (!inside) {
                continue;
            }
            s = robo_bidi_n0_strong((robo_unicode_bidi_class_t)types[pos]);
            if (s == 0xFFu) {
                continue;
            }
            if (s == e) {
                strong_content = e;
                break;
            }
            has_opposite = 1;
        }

        if (strong_content == e) {
            new_t = e;
        } else if (has_opposite) {
            uint8_t preceding = 0xFFu;
            size_t rk;
            for (rk = m; rk > 0u; rk--) {
                size_t pos = indices[rk - 1u];
                uint8_t s;
                if (pos >= open_pos) {
                    continue;
                }
                s = robo_bidi_n0_strong((robo_unicode_bidi_class_t)types[pos]);
                if (s != 0xFFu) {
                    preceding = s;
                    break;
                }
            }
            if (preceding == 0xFFu) {
                preceding = sos;
            }
            {
                uint8_t o = (e == (uint8_t)ROBO_U_BC_R) ? (uint8_t)ROBO_U_BC_L
                                                        : (uint8_t)ROBO_U_BC_R;
                if (preceding == o) {
                    new_t = o;
                } else {
                    new_t = e;
                }
            }
        } else {
            continue; /* N0d */
        }

        types[open_pos] = new_t;
        types[close_pos] = new_t;

        for (local_k = 0; local_k < m; local_k++) {
            if (indices[local_k] == open_pos || indices[local_k] == close_pos) {
                size_t nk = local_k + 1u;
                while (nk < m && was_nsm[indices[nk]]) {
                    types[indices[nk]] = new_t;
                    nk++;
                }
            }
        }
    }

    /* N1 / N2 */
    k = 0;
    while (k < m) {
        size_t end;
        uint8_t left;
        uint8_t right;
        uint8_t new_t;
        size_t j;
        uint8_t left_t;
        uint8_t right_t;

        if (!robo_bidi_is_ni((robo_unicode_bidi_class_t)types[indices[k]])) {
            k++;
            continue;
        }
        end = k;
        while (end < m &&
               robo_bidi_is_ni((robo_unicode_bidi_class_t)types[indices[end]])) {
            end++;
        }

        if (k == 0u) {
            left_t = sos;
        } else {
            left_t = types[indices[k - 1u]];
        }
        if (end >= m) {
            right_t = eos;
        } else {
            right_t = types[indices[end]];
        }
        left = robo_bidi_n0_strong((robo_unicode_bidi_class_t)left_t);
        right = robo_bidi_n0_strong((robo_unicode_bidi_class_t)right_t);
        if (left != 0xFFu && left == right) {
            new_t = left;
        } else {
            new_t = e;
        }
        for (j = k; j < end; j++) {
            types[indices[j]] = new_t;
        }
        k = end;
    }

    robo_free(pair_open);
    robo_free(pair_close);
}

static void robo_bidi_apply_I(const uint8_t *types, int16_t *levels, const size_t *indices, size_t m)
{
    size_t ki;
    for (ki = 0; ki < m; ki++) {
        size_t pos = indices[ki];
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)types[pos];
        int lev = (int)levels[pos];
        if ((lev % 2) == 0) {
            if (t == ROBO_U_BC_R) {
                levels[pos] = (int16_t)(lev + 1);
            } else if (t == ROBO_U_BC_AN || t == ROBO_U_BC_EN) {
                levels[pos] = (int16_t)(lev + 2);
            }
        } else {
            if (t == ROBO_U_BC_L || t == ROBO_U_BC_EN || t == ROBO_U_BC_AN) {
                levels[pos] = (int16_t)(lev + 1);
            }
        }
    }
}

static void robo_bidi_apply_L1(int16_t *levels,
                               const uint8_t *original_types,
                               size_t n,
                               uint8_t paragraph_level)
{
    size_t i;
    size_t j;

    for (i = 0; i < n; i++) {
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)original_types[i];
        if (t == ROBO_U_BC_S || t == ROBO_U_BC_B) {
            levels[i] = (int16_t)paragraph_level;
        }
    }

    for (i = 0; i < n; i++) {
        robo_unicode_bidi_class_t t = (robo_unicode_bidi_class_t)original_types[i];
        if (t != ROBO_U_BC_S && t != ROBO_U_BC_B) {
            continue;
        }
        j = i;
        while (j > 0u) {
            robo_unicode_bidi_class_t tj;
            j--;
            tj = (robo_unicode_bidi_class_t)original_types[j];
            if (tj == ROBO_U_BC_WS || robo_bidi_is_isolate_init(tj) ||
                tj == ROBO_U_BC_PDI) {
                levels[j] = (int16_t)paragraph_level;
            } else {
                break;
            }
        }
    }

    j = n;
    while (j > 0u) {
        robo_unicode_bidi_class_t tj;
        j--;
        tj = (robo_unicode_bidi_class_t)original_types[j];
        if (tj == ROBO_U_BC_WS || robo_bidi_is_isolate_init(tj) ||
            tj == ROBO_U_BC_PDI) {
            levels[j] = (int16_t)paragraph_level;
        } else {
            break;
        }
    }
}

robo_result_t robo_unicode_bidi_reorder(const uint8_t *levels,
                                        size_t len,
                                        size_t *visual_to_logical,
                                        size_t *visible_count)
{
    size_t i;
    size_t vis;
    int max_level;
    int level;

    if (!levels || !visual_to_logical || !visible_count) {
        return ROBO_INVALID_ARGUMENT;
    }

    vis = 0;
    for (i = 0; i < len; i++) {
        if (levels[i] != ROBO_BIDI_LEVEL_REMOVED) {
            visual_to_logical[vis++] = i;
        }
    }
    *visible_count = vis;
    if (vis == 0u) {
        return ROBO_OK;
    }

    max_level = 0;
    for (i = 0; i < vis; i++) {
        int lv = (int)levels[visual_to_logical[i]];
        if (lv > max_level) {
            max_level = lv;
        }
    }

    for (level = max_level; level >= 1; level--) {
        size_t p = 0;
        while (p < vis) {
            if ((int)levels[visual_to_logical[p]] >= level) {
                size_t q = p;
                while (q < vis && (int)levels[visual_to_logical[q]] >= level) {
                    q++;
                }
                /* reverse [p, q) */
                {
                    size_t a = p;
                    size_t b = q;
                    while (a + 1u < b) {
                        size_t tmp;
                        b--;
                        tmp = visual_to_logical[a];
                        visual_to_logical[a] = visual_to_logical[b];
                        visual_to_logical[b] = tmp;
                        a++;
                    }
                }
                p = q;
            } else {
                p++;
            }
        }
    }
    return ROBO_OK;
}

static robo_result_t robo_bidi_build_irs(const int16_t *levels,
                                         const uint8_t *original_types,
                                         const int *matching_pdi,
                                         const int *matching_init,
                                         size_t n,
                                         robo_bidi_irs_t **out_seqs,
                                         size_t *out_seq_count)
{
    size_t *kept = NULL;
    size_t kept_count = 0;
    size_t *run_of = NULL;
    size_t *run_first = NULL;
    size_t *run_len = NULL;
    size_t run_count = 0;
    uint8_t *assigned = NULL;
    size_t **seq_runs = NULL;
    size_t *seq_run_counts = NULL;
    size_t seq_count = 0;
    size_t i;
    size_t ri;
    robo_bidi_irs_t *seqs = NULL;
    robo_result_t rc = ROBO_OK;

    *out_seqs = NULL;
    *out_seq_count = 0;

    kept = (size_t *)robo_malloc(n * sizeof(size_t));
    run_of = (size_t *)robo_malloc(n * sizeof(size_t));
    if (!kept || !run_of) {
        rc = ROBO_OUT_OF_MEMORY;
        goto done;
    }
    for (i = 0; i < n; i++) {
        run_of[i] = (size_t)-1;
        if (!robo_bidi_is_remove((robo_unicode_bidi_class_t)original_types[i])) {
            kept[kept_count++] = i;
        }
    }
    if (kept_count == 0u) {
        goto done;
    }

    /* Count level runs */
    run_count = 1;
    for (i = 1; i < kept_count; i++) {
        if (levels[kept[i]] != levels[kept[i - 1u]]) {
            run_count++;
        }
    }
    run_first = (size_t *)robo_malloc(run_count * sizeof(size_t));
    run_len = (size_t *)robo_malloc(run_count * sizeof(size_t));
    assigned = (uint8_t *)robo_calloc(run_count, 1u);
    if (!run_first || !run_len || !assigned) {
        rc = ROBO_OUT_OF_MEMORY;
        goto done;
    }

    {
        size_t r = 0;
        run_first[0] = 0;
        run_len[0] = 1;
        run_of[kept[0]] = 0;
        for (i = 1; i < kept_count; i++) {
            if (levels[kept[i]] == levels[kept[run_first[r]]]) {
                run_len[r]++;
            } else {
                r++;
                run_first[r] = i;
                run_len[r] = 1;
            }
            run_of[kept[i]] = r;
        }
    }

    seq_runs = (size_t **)robo_calloc(run_count, sizeof(size_t *));
    seq_run_counts = (size_t *)robo_calloc(run_count, sizeof(size_t));
    if (!seq_runs || !seq_run_counts) {
        rc = ROBO_OUT_OF_MEMORY;
        goto done;
    }

    for (ri = 0; ri < run_count; ri++) {
        size_t first_kept_i;
        size_t first_char;
        size_t *seq;
        size_t seq_len;

        if (assigned[ri]) {
            continue;
        }
        first_kept_i = run_first[ri];
        first_char = kept[first_kept_i];
        if ((robo_unicode_bidi_class_t)original_types[first_char] == ROBO_U_BC_PDI &&
            matching_init[first_char] >= 0) {
            continue;
        }

        seq = (size_t *)robo_malloc(run_count * sizeof(size_t));
        if (!seq) {
            rc = ROBO_OUT_OF_MEMORY;
            goto done;
        }
        seq_len = 0;
        seq[seq_len++] = ri;
        assigned[ri] = 1;

        for (;;) {
            size_t last_run = seq[seq_len - 1u];
            size_t last_kept_i = run_first[last_run] + run_len[last_run] - 1u;
            size_t last_char = kept[last_kept_i];
            int pdi;
            size_t rj;

            if (!robo_bidi_is_isolate_init(
                    (robo_unicode_bidi_class_t)original_types[last_char]) ||
                matching_pdi[last_char] < 0) {
                break;
            }
            pdi = matching_pdi[last_char];
            rj = run_of[(size_t)pdi];
            if (rj == (size_t)-1 || assigned[rj]) {
                break;
            }
            if (kept[run_first[rj]] != (size_t)pdi) {
                break;
            }
            seq[seq_len++] = rj;
            assigned[rj] = 1;
        }
        seq_runs[seq_count] = seq;
        seq_run_counts[seq_count] = seq_len;
        seq_count++;
        seq = NULL; /* owned by seq_runs */
    }

    for (ri = 0; ri < run_count; ri++) {
        size_t *seq;
        if (assigned[ri]) {
            continue;
        }
        seq = (size_t *)robo_malloc(sizeof(size_t));
        if (!seq) {
            rc = ROBO_OUT_OF_MEMORY;
            goto done;
        }
        seq[0] = ri;
        assigned[ri] = 1;
        seq_runs[seq_count] = seq;
        seq_run_counts[seq_count] = 1;
        seq_count++;
    }

    seqs = (robo_bidi_irs_t *)robo_calloc(seq_count, sizeof(robo_bidi_irs_t));
    if (!seqs) {
        rc = ROBO_OUT_OF_MEMORY;
        goto done;
    }

    for (i = 0; i < seq_count; i++) {
        size_t total = 0;
        size_t s;
        size_t *indices;
        size_t w = 0;
        size_t first;
        size_t last;
        int prev_level = -1;
        int next_level = -1;
        int use_para_eos = 0;
        size_t j;

        for (s = 0; s < seq_run_counts[i]; s++) {
            total += run_len[seq_runs[i][s]];
        }
        indices = (size_t *)robo_malloc(total * sizeof(size_t));
        if (!indices) {
            rc = ROBO_OUT_OF_MEMORY;
            /* free already built */
            {
                size_t z;
                for (z = 0; z < i; z++) {
                    robo_free(seqs[z].indices);
                }
                robo_free(seqs);
                seqs = NULL;
            }
            goto done;
        }
        for (s = 0; s < seq_run_counts[i]; s++) {
            size_t r = seq_runs[i][s];
            size_t t;
            for (t = 0; t < run_len[r]; t++) {
                indices[w++] = kept[run_first[r] + t];
            }
        }
        seqs[i].indices = indices;
        seqs[i].count = total;
        seqs[i].level = (uint8_t)levels[indices[0]];

        first = indices[0];
        last = indices[total - 1u];
        for (j = first; j > 0u;) {
            j--;
            if (!robo_bidi_is_remove((robo_unicode_bidi_class_t)original_types[j])) {
                prev_level = (int)levels[j];
                break;
            }
        }
        if (robo_bidi_is_isolate_init(
                (robo_unicode_bidi_class_t)original_types[last]) &&
            matching_pdi[last] < 0) {
            use_para_eos = 1;
        } else {
            for (j = last + 1u; j < n; j++) {
                if (!robo_bidi_is_remove(
                        (robo_unicode_bidi_class_t)original_types[j])) {
                    next_level = (int)levels[j];
                    break;
                }
            }
            if (next_level < 0) {
                use_para_eos = 1;
            }
        }
        seqs[i].prev_level = prev_level;
        seqs[i].next_level = next_level;
        seqs[i].use_para_eos = use_para_eos;
    }

    *out_seqs = seqs;
    *out_seq_count = seq_count;
    seqs = NULL; /* ownership transferred */

done:
    if (seq_runs) {
        size_t z;
        for (z = 0; z < seq_count; z++) {
            robo_free(seq_runs[z]);
        }
        /* If we failed mid-way after allocating more seq_runs entries beyond
         * seq_count that were never recorded — all recorded are freed above.
         * Also free any seq allocated during the loop that was assigned to
         * seq_runs only after success; on OOM after seq malloc but before
         * assign, free that seq — handled by not assigning. */
        robo_free(seq_runs);
    }
    robo_free(seq_run_counts);
    robo_free(assigned);
    robo_free(run_first);
    robo_free(run_len);
    robo_free(run_of);
    robo_free(kept);
    if (rc != ROBO_OK && seqs) {
        size_t z;
        for (z = 0; z < seq_count; z++) {
            robo_free(seqs[z].indices);
        }
        robo_free(seqs);
    }
    return rc;
}

static void robo_bidi_free_irs(robo_bidi_irs_t *seqs, size_t seq_count)
{
    size_t i;
    if (!seqs) {
        return;
    }
    for (i = 0; i < seq_count; i++) {
        robo_free(seqs[i].indices);
    }
    robo_free(seqs);
}

static robo_result_t robo_bidi_analyze_core(const robo_uchar32_t *cps,
                                            size_t n,
                                            robo_unicode_bidi_paragraph_dir_t para_dir,
                                            uint8_t *levels_out,
                                            uint8_t *paragraph_level_out,
                                            size_t *visual_to_logical,
                                            size_t *visible_count,
                                            int want_visual)
{
    uint8_t *original_types = NULL;
    uint8_t *types = NULL;
    uint8_t *was_nsm = NULL;
    uint8_t *removed = NULL;
    int16_t *levels = NULL;
    int *matching_pdi = NULL;
    int *matching_init = NULL;
    robo_bidi_irs_t *seqs = NULL;
    size_t seq_count = 0;
    uint8_t paragraph_level;
    size_t i;
    robo_result_t rc = ROBO_OK;
    size_t *vis_buf = NULL;
    size_t vis_count = 0;

    if ((!cps && n > 0u) || !levels_out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (para_dir > ROBO_BIDI_AUTO) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (want_visual && (!visual_to_logical || !visible_count)) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (n == 0u) {
        if (para_dir == ROBO_BIDI_RTL) {
            paragraph_level = 1u;
        } else {
            paragraph_level = 0u;
        }
        if (paragraph_level_out) {
            *paragraph_level_out = paragraph_level;
        }
        if (want_visual) {
            *visible_count = 0u;
        }
        return ROBO_OK;
    }

    original_types = (uint8_t *)robo_malloc(n);
    types = (uint8_t *)robo_malloc(n);
    was_nsm = (uint8_t *)robo_malloc(n);
    removed = (uint8_t *)robo_malloc(n);
    levels = (int16_t *)robo_malloc(n * sizeof(int16_t));
    matching_pdi = (int *)robo_malloc(n * sizeof(int));
    matching_init = (int *)robo_malloc(n * sizeof(int));
    if (!original_types || !types || !was_nsm || !removed || !levels ||
        !matching_pdi || !matching_init) {
        rc = ROBO_OUT_OF_MEMORY;
        goto cleanup;
    }

    for (i = 0; i < n; i++) {
        original_types[i] = (uint8_t)robo_bidi_class_property(cps[i]);
        types[i] = original_types[i];
        was_nsm[i] = (original_types[i] == (uint8_t)ROBO_U_BC_NSM) ? 1u : 0u;
        removed[i] =
            robo_bidi_is_remove((robo_unicode_bidi_class_t)original_types[i]) ? 1u
                                                                              : 0u;
    }

    robo_bidi_matching_pdis(original_types, n, matching_pdi);
    robo_bidi_matching_initiators(matching_pdi, n, matching_init);

    if (para_dir == ROBO_BIDI_AUTO) {
        paragraph_level = robo_bidi_determine_paragraph_level(types, 0, n);
    } else if (para_dir == ROBO_BIDI_RTL) {
        paragraph_level = 1u;
    } else {
        paragraph_level = 0u;
    }

    robo_bidi_resolve_explicit(types, n, paragraph_level, matching_pdi, levels);

    for (i = 0; i < n; i++) {
        if (removed[i] && levels[i] < 0) {
            levels[i] = 0;
        }
    }

    rc = robo_bidi_build_irs(
        levels, original_types, matching_pdi, matching_init, n, &seqs, &seq_count);
    if (rc != ROBO_OK) {
        goto cleanup;
    }

    for (i = 0; i < seq_count; i++) {
        int prev = seqs[i].prev_level;
        int nxt;
        size_t first = seqs[i].indices[0];
        size_t last = seqs[i].indices[seqs[i].count - 1u];
        if (prev < 0) {
            prev = (int)paragraph_level;
        }
        seqs[i].sos = robo_bidi_sos_eos_type((int)levels[first], prev);
        if (seqs[i].use_para_eos) {
            nxt = (int)paragraph_level;
        } else {
            nxt = seqs[i].next_level;
            if (nxt < 0) {
                nxt = (int)paragraph_level;
            }
        }
        seqs[i].eos = robo_bidi_sos_eos_type((int)levels[last], nxt);
    }

    for (i = 0; i < seq_count; i++) {
        robo_bidi_apply_W(types, seqs[i].indices, seqs[i].count, seqs[i].sos);
        robo_bidi_apply_N(cps,
                          types,
                          seqs[i].indices,
                          seqs[i].count,
                          seqs[i].sos,
                          seqs[i].eos,
                          seqs[i].level,
                          was_nsm);
        robo_bidi_apply_I(types, levels, seqs[i].indices, seqs[i].count);
    }

    robo_bidi_apply_L1(levels, original_types, n, paragraph_level);

    for (i = 0; i < n; i++) {
        if (removed[i]) {
            levels_out[i] = ROBO_BIDI_LEVEL_REMOVED;
        } else {
            levels_out[i] = (uint8_t)levels[i];
        }
    }

    if (paragraph_level_out) {
        *paragraph_level_out = paragraph_level;
    }

    if (want_visual) {
        vis_buf = visual_to_logical;
        rc = robo_unicode_bidi_reorder(levels_out, n, vis_buf, &vis_count);
        if (rc != ROBO_OK) {
            goto cleanup;
        }
        *visible_count = vis_count;
    }

cleanup:
    robo_bidi_free_irs(seqs, seq_count);
    robo_free(original_types);
    robo_free(types);
    robo_free(was_nsm);
    robo_free(removed);
    robo_free(levels);
    robo_free(matching_pdi);
    robo_free(matching_init);
    return rc;
}

void robo_unicode_bidi_result_destroy(robo_unicode_bidi_result_t *r)
{
    if (!r) {
        return;
    }
    robo_free(r->levels);
    robo_free(r->visual_to_logical);
    robo_free(r->byte_ends);
    r->levels = NULL;
    r->visual_to_logical = NULL;
    r->byte_ends = NULL;
    r->cp_count = 0;
    r->visible_count = 0;
    r->paragraph_level = 0;
}

robo_result_t robo_unicode_bidi_analyze(const robo_uchar32_t *cps,
                                        size_t len,
                                        robo_unicode_bidi_paragraph_dir_t para_dir,
                                        robo_unicode_bidi_result_t *out)
{
    uint8_t *levels = NULL;
    size_t *visual = NULL;
    size_t visible = 0;
    uint8_t para_level = 0;
    robo_result_t rc;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (!cps && len > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (len == 0u) {
        out->cp_count = 0;
        out->paragraph_level = (para_dir == ROBO_BIDI_RTL) ? 1u : 0u;
        out->visible_count = 0;
        return ROBO_OK;
    }

    levels = (uint8_t *)robo_malloc(len);
    visual = (size_t *)robo_malloc(len * sizeof(size_t));
    if (!levels || !visual) {
        robo_free(levels);
        robo_free(visual);
        return ROBO_OUT_OF_MEMORY;
    }

    rc = robo_bidi_analyze_core(
        cps, len, para_dir, levels, &para_level, visual, &visible, 1);
    if (rc != ROBO_OK) {
        robo_free(levels);
        robo_free(visual);
        return rc;
    }

    out->cp_count = len;
    out->paragraph_level = para_level;
    out->levels = levels;
    out->visual_to_logical = visual;
    out->visible_count = visible;
    out->byte_ends = NULL;
    return ROBO_OK;
}

robo_result_t robo_unicode_bidi_resolve_levels(const robo_uchar32_t *cps,
                                               size_t len,
                                               robo_unicode_bidi_paragraph_dir_t para_dir,
                                               uint8_t *levels_out,
                                               uint8_t *paragraph_level_out)
{
    return robo_bidi_analyze_core(
        cps, len, para_dir, levels_out, paragraph_level_out, NULL, NULL, 0);
}

robo_result_t robo_unicode_bidi_analyze_utf8(const char *src,
                                             size_t src_len,
                                             robo_unicode_mode_t mode,
                                             robo_unicode_bidi_paragraph_dir_t para_dir,
                                             robo_unicode_bidi_result_t *out)
{
    robo_uchar32_t *cps = NULL;
    size_t *byte_ends = NULL;
    size_t n = 0;
    size_t off = 0;
    size_t cap = 0;
    robo_result_t rc;
    robo_unicode_bidi_result_t tmp;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (!src && src_len > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (src_len == 0u) {
        return robo_unicode_bidi_analyze(NULL, 0, para_dir, out);
    }

    cap = src_len; /* upper bound: 1 CP per byte */
    cps = (robo_uchar32_t *)robo_malloc(cap * sizeof(robo_uchar32_t));
    byte_ends = (size_t *)robo_malloc(cap * sizeof(size_t));
    if (!cps || !byte_ends) {
        robo_free(cps);
        robo_free(byte_ends);
        return ROBO_OUT_OF_MEMORY;
    }

    while (off < src_len) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        robo_unicode_status_t st =
            robo_unicode_utf8_decode(src, src_len, off, mode, &cp, &next);
        if (st != ROBO_UNICODE_OK) {
            robo_free(cps);
            robo_free(byte_ends);
            return ROBO_PARSE_ERROR;
        }
        cps[n] = cp;
        byte_ends[n] = next;
        n++;
        off = next;
    }

    memset(&tmp, 0, sizeof(tmp));
    rc = robo_unicode_bidi_analyze(cps, n, para_dir, &tmp);
    robo_free(cps);
    if (rc != ROBO_OK) {
        robo_free(byte_ends);
        return rc;
    }

    *out = tmp;
    out->byte_ends = byte_ends;
    return ROBO_OK;
}
