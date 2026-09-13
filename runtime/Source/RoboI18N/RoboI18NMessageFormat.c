/*
 * RoboI18N RoboMsgFmt parser + formatter (Goal 4.12).
 */

#include "RoboI18N/RoboI18NMessageFormat.h"

#include "RoboI18N/RoboI18NCalendar.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NMessageValidate.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboI18NTimeZone.h"
#include "RoboMem/RoboMem.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef enum mf_node_kind {
    MF_LIT = 0,
    MF_SIMPLE = 1,
    MF_NUMBER = 2,
    MF_DATE = 3,
    MF_TIME = 4,
    MF_SELECT = 5,
    MF_PLURAL = 6,
    MF_ORDINAL = 7,
    MF_POUND = 8
} mf_node_kind_t;

typedef struct mf_node mf_node_t;

typedef struct mf_branch {
    int is_exact; /* 1 => exact_value used */
    int64_t exact_value;
    char key[ROBO_I18N_MF_NAME_MAX]; /* select / plural category */
    mf_node_t *body;                 /* linked list of pieces */
    struct mf_branch *next;
} mf_branch_t;

struct mf_node {
    mf_node_kind_t kind;
    mf_node_t *next;
    char name[ROBO_I18N_MF_NAME_MAX];
    const char *lit; /* into owned template copy */
    size_t lit_len;
    robo_i18n_cal_format_style_t style; /* date/time */
    mf_branch_t *branches;
    uint16_t branch_count;
    int has_other;
};

struct robo_i18n_mf_message {
    char *template_copy;
    size_t template_len;
    uint32_t syntax_version;
    mf_node_t *root; /* linked list */
    mf_node_t *node_pool;
    uint32_t node_count;
    uint32_t node_cap;
    mf_branch_t *branch_pool;
    uint32_t branch_count;
    uint32_t branch_cap;
};

typedef struct parse_ctx {
    const char *s;
    size_t len;
    size_t i;
    int depth;
    robo_i18n_mf_message_t *msg;
    int in_plural; /* allow # */
    uint32_t nesting_max;
    uint32_t branches_max;
    /* first error (for diagnostics) */
    int has_err;
    size_t err_offset;
    int err_code; /* robo_i18n_mf_diag_code-compatible int */
    char err_construct[32];
    char err_arg[ROBO_I18N_MF_NAME_MAX];
    char err_msg[160];
} parse_ctx_t;

typedef struct fmt_ctx {
    const robo_i18n_locale_t *locale;
    const robo_i18n_mf_args_t *args;
    char *buf;
    size_t cap;
    size_t len;
    int depth;
    const robo_i18n_mf_arg_t *plural_arg; /* for # */
} fmt_ctx_t;

static void mf_note_err(parse_ctx_t *cx,
                        int code,
                        const char *construct,
                        const char *arg,
                        const char *msg)
{
    if (!cx || cx->has_err) {
        return;
    }
    cx->has_err = 1;
    cx->err_offset = cx->i;
    cx->err_code = code;
    cx->err_construct[0] = '\0';
    cx->err_arg[0] = '\0';
    cx->err_msg[0] = '\0';
    if (construct) {
        strncpy(cx->err_construct, construct, sizeof(cx->err_construct) - 1u);
    }
    if (arg) {
        strncpy(cx->err_arg, arg, sizeof(cx->err_arg) - 1u);
    }
    if (msg) {
        strncpy(cx->err_msg, msg, sizeof(cx->err_msg) - 1u);
    }
}

static robo_result_t mf_fail(parse_ctx_t *cx,
                             int code,
                             const char *construct,
                             const char *arg,
                             const char *msg)
{
    mf_note_err(cx, code, construct, arg, msg);
    if (code == ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION) {
        return ROBO_NOT_SUPPORTED;
    }
    if (code == ROBO_I18N_MF_DIAG_TOO_LARGE || code == ROBO_I18N_MF_DIAG_NESTING) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_PARSE_ERROR;
}

static int branch_duplicate(const mf_branch_t *head, const mf_branch_t *br)
{
    const mf_branch_t *p;
    for (p = head; p && p != br; p = p->next) {
        if (br->is_exact) {
            if (p->is_exact && p->exact_value == br->exact_value) {
                return 1;
            }
        } else if (!p->is_exact && strcmp(p->key, br->key) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int utf8_ok(const char *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x80u) {
            i++;
        } else if ((c >> 5) == 0x6u && i + 1 < n && (((unsigned char)p[i + 1]) & 0xC0u) == 0x80u) {
            i += 2;
        } else if ((c >> 4) == 0xEu && i + 2 < n &&
                   (((unsigned char)p[i + 1]) & 0xC0u) == 0x80u &&
                   (((unsigned char)p[i + 2]) & 0xC0u) == 0x80u) {
            i += 3;
        } else if ((c >> 3) == 0x1Eu && i + 3 < n &&
                   (((unsigned char)p[i + 1]) & 0xC0u) == 0x80u &&
                   (((unsigned char)p[i + 2]) & 0xC0u) == 0x80u &&
                   (((unsigned char)p[i + 3]) & 0xC0u) == 0x80u) {
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static void skip_ws(parse_ctx_t *cx)
{
    while (cx->i < cx->len) {
        char c = cx->s[cx->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            cx->i++;
        } else {
            break;
        }
    }
}

static mf_node_t *alloc_node(robo_i18n_mf_message_t *msg)
{
    mf_node_t *n;
    if (msg->node_count >= msg->node_cap) {
        return NULL; /* fixed capacity; no realloc (keeps pointers stable) */
    }
    n = &msg->node_pool[msg->node_count++];
    memset(n, 0, sizeof(*n));
    return n;
}

static mf_branch_t *alloc_branch(robo_i18n_mf_message_t *msg)
{
    mf_branch_t *b;
    if (msg->branch_count >= msg->branch_cap) {
        return NULL;
    }
    b = &msg->branch_pool[msg->branch_count++];
    memset(b, 0, sizeof(*b));
    return b;
}

static robo_result_t parse_ident(parse_ctx_t *cx, char *out, size_t out_cap)
{
    size_t n = 0;
    if (cx->i >= cx->len || !is_ident_start(cx->s[cx->i])) {
        return ROBO_PARSE_ERROR;
    }
    while (cx->i < cx->len && is_ident(cx->s[cx->i])) {
        if (n + 1 >= out_cap) {
            return ROBO_PARSE_ERROR;
        }
        out[n++] = cx->s[cx->i++];
    }
    out[n] = '\0';
    return ROBO_OK;
}

static robo_result_t parse_pattern(parse_ctx_t *cx, mf_node_t **out_head);

static robo_result_t parse_branches(parse_ctx_t *cx, mf_node_t *arg_node, int plural_mode)
{
    uint16_t count = 0;
    mf_branch_t *head = NULL;
    mf_branch_t *tail = NULL;
    int saw_other = 0;
    uint32_t branches_max =
        cx->branches_max ? cx->branches_max : ROBO_I18N_MF_BRANCHES_MAX;
    uint32_t nesting_max = cx->nesting_max ? cx->nesting_max : ROBO_I18N_MF_NESTING_MAX;

    for (;;) {
        mf_branch_t *br;
        skip_ws(cx);
        if (cx->i >= cx->len) {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, plural_mode ? "plural" : "select",
                           arg_node->name, "unexpected end in branches");
        }
        if (cx->s[cx->i] == '}') {
            break;
        }
        if (count >= branches_max) {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_TOO_LARGE, plural_mode ? "plural" : "select",
                           arg_node->name, "too many branches");
        }
        br = alloc_branch(cx->msg);
        if (!br) {
            return ROBO_OUT_OF_MEMORY;
        }
        if (cx->s[cx->i] == '=') {
            int64_t v = 0;
            int neg = 0;
            if (!plural_mode) {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_INVALID_SELECTOR, "select",
                               arg_node->name, "exact selector only valid on plural");
            }
            cx->i++;
            if (cx->i < cx->len && cx->s[cx->i] == '-') {
                neg = 1;
                cx->i++;
            }
            if (cx->i >= cx->len || cx->s[cx->i] < '0' || cx->s[cx->i] > '9') {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_INVALID_SELECTOR, "plural",
                               arg_node->name, "invalid exact selector");
            }
            while (cx->i < cx->len && cx->s[cx->i] >= '0' && cx->s[cx->i] <= '9') {
                int d = cx->s[cx->i++] - '0';
                if (v > (INT64_MAX - d) / 10) {
                    return mf_fail(cx, ROBO_I18N_MF_DIAG_INVALID_SELECTOR, "plural",
                                   arg_node->name, "exact selector overflow");
                }
                v = v * 10 + d;
            }
            br->is_exact = 1;
            br->exact_value = neg ? -v : v;
            br->key[0] = '\0';
        } else {
            if (parse_ident(cx, br->key, sizeof(br->key)) != ROBO_OK) {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_INVALID_SELECTOR,
                               plural_mode ? "plural" : "select", arg_node->name,
                               "invalid branch key");
            }
            if (strcmp(br->key, "other") == 0) {
                saw_other = 1;
            }
        }
        if (branch_duplicate(head, br)) {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_DUPLICATE_BRANCH,
                           plural_mode ? "plural" : "select", arg_node->name,
                           "duplicate selector branch");
        }
        skip_ws(cx);
        if (cx->i >= cx->len || cx->s[cx->i] != '{') {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, plural_mode ? "plural" : "select",
                           arg_node->name, "expected '{' after branch key");
        }
        cx->i++;
        {
            int prev = cx->in_plural;
            parse_ctx_t nested = *cx;
            nested.depth = cx->depth + 1;
            nested.in_plural = plural_mode ? 1 : cx->in_plural;
            if (nested.depth > (int)nesting_max) {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_NESTING, plural_mode ? "plural" : "select",
                               arg_node->name, "nesting depth exceeded");
            }
            if (parse_pattern(&nested, &br->body) != ROBO_OK) {
                if (nested.has_err && !cx->has_err) {
                    *cx = nested;
                }
                return nested.has_err ? ROBO_PARSE_ERROR : ROBO_PARSE_ERROR;
            }
            cx->i = nested.i;
            cx->in_plural = prev;
            if (nested.has_err) {
                cx->has_err = nested.has_err;
                cx->err_offset = nested.err_offset;
                cx->err_code = nested.err_code;
                memcpy(cx->err_construct, nested.err_construct, sizeof(cx->err_construct));
                memcpy(cx->err_arg, nested.err_arg, sizeof(cx->err_arg));
                memcpy(cx->err_msg, nested.err_msg, sizeof(cx->err_msg));
            }
        }
        skip_ws(cx);
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, plural_mode ? "plural" : "select",
                           arg_node->name, "expected '}' to close branch");
        }
        cx->i++;
        if (!head) {
            head = br;
        } else {
            tail->next = br;
        }
        tail = br;
        count++;
    }
    if (count == 0 || !saw_other) {
        return mf_fail(cx, ROBO_I18N_MF_DIAG_MISSING_OTHER, plural_mode ? "plural" : "select",
                       arg_node->name, "missing required 'other' branch");
    }
    arg_node->branches = head;
    arg_node->branch_count = count;
    arg_node->has_other = 1;
    return ROBO_OK;
}

static robo_result_t parse_argument(parse_ctx_t *cx, mf_node_t **out)
{
    mf_node_t *node;
    char type[32];
    char name[ROBO_I18N_MF_NAME_MAX];

    if (cx->depth > (int)(cx->nesting_max ? cx->nesting_max : ROBO_I18N_MF_NESTING_MAX)) {
        return mf_fail(cx, ROBO_I18N_MF_DIAG_NESTING, "argument", NULL, "nesting depth exceeded");
    }
    skip_ws(cx);
    if (parse_ident(cx, name, sizeof(name)) != ROBO_OK) {
        return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, "argument", NULL, "missing argument name");
    }
    skip_ws(cx);
    node = alloc_node(cx->msg);
    if (!node) {
        return ROBO_OUT_OF_MEMORY;
    }
    memcpy(node->name, name, sizeof(name));
    node->style = ROBO_I18N_CAL_STYLE_MEDIUM;

    if (cx->i < cx->len && cx->s[cx->i] == '}') {
        cx->i++;
        node->kind = MF_SIMPLE;
        *out = node;
        return ROBO_OK;
    }
    if (cx->i >= cx->len || cx->s[cx->i] != ',') {
        return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, "argument", name, "expected ',' or '}'");
    }
    cx->i++;
    skip_ws(cx);
    if (parse_ident(cx, type, sizeof(type)) != ROBO_OK) {
        return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, "argument", name, "missing argument type");
    }
    skip_ws(cx);

    if (strcmp(type, "number") == 0) {
        node->kind = MF_NUMBER;
        if (cx->i < cx->len && cx->s[cx->i] == ',') {
            char sty[32];
            cx->i++;
            skip_ws(cx);
            if (parse_ident(cx, sty, sizeof(sty)) != ROBO_OK) {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_OPTION, "number", name,
                               "invalid number style");
            }
            skip_ws(cx);
        }
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, "number", name, "expected '}'");
        }
        cx->i++;
        *out = node;
        return ROBO_OK;
    }
    if (strcmp(type, "date") == 0 || strcmp(type, "time") == 0) {
        node->kind = (strcmp(type, "date") == 0) ? MF_DATE : MF_TIME;
        if (cx->i < cx->len && cx->s[cx->i] == ',') {
            char sty[32];
            cx->i++;
            skip_ws(cx);
            if (parse_ident(cx, sty, sizeof(sty)) != ROBO_OK) {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_OPTION, type, name,
                               "invalid date/time style");
            }
            if (strcmp(sty, "short") == 0) {
                node->style = ROBO_I18N_CAL_STYLE_SHORT;
            } else if (strcmp(sty, "medium") == 0) {
                node->style = ROBO_I18N_CAL_STYLE_MEDIUM;
            } else if (strcmp(sty, "long") == 0) {
                node->style = ROBO_I18N_CAL_STYLE_LONG;
            } else if (strcmp(sty, "full") == 0) {
                node->style = ROBO_I18N_CAL_STYLE_FULL;
            } else {
                return mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_OPTION, type, name,
                               "unsupported date/time style");
            }
            skip_ws(cx);
        }
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, type, name, "expected '}'");
        }
        cx->i++;
        *out = node;
        return ROBO_OK;
    }
    if (strcmp(type, "select") == 0 || strcmp(type, "plural") == 0 ||
        strcmp(type, "selectordinal") == 0) {
        int plural = (strcmp(type, "plural") == 0 || strcmp(type, "selectordinal") == 0);
        if (strcmp(type, "select") == 0) {
            node->kind = MF_SELECT;
        } else if (strcmp(type, "plural") == 0) {
            node->kind = MF_PLURAL;
        } else {
            node->kind = MF_ORDINAL;
        }
        if (cx->i >= cx->len || cx->s[cx->i] != ',') {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, type, name,
                           "expected ',' before branches");
        }
        cx->i++;
        {
            robo_result_t br = parse_branches(cx, node, plural);
            if (br != ROBO_OK) {
                return br;
            }
        }
        skip_ws(cx);
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return mf_fail(cx, ROBO_I18N_MF_DIAG_MALFORMED, type, name, "expected '}'");
        }
        cx->i++;
        *out = node;
        return ROBO_OK;
    }
    return mf_fail(cx, ROBO_I18N_MF_DIAG_UNKNOWN_TYPE, type, name, "unknown argument type");
}

static robo_result_t parse_pattern(parse_ctx_t *cx, mf_node_t **out_head)
{
    mf_node_t *head = NULL;
    mf_node_t *tail = NULL;

    while (cx->i < cx->len) {
        char c = cx->s[cx->i];

        if (c == '}') {
            break;
        }

        if (c == '#' && cx->in_plural) {
            mf_node_t *n = alloc_node(cx->msg);
            if (!n) {
                return ROBO_OUT_OF_MEMORY;
            }
            n->kind = MF_POUND;
            cx->i++;
            if (!head) {
                head = n;
            } else {
                tail->next = n;
            }
            tail = n;
            continue;
        }

        if (c == '{' && cx->i + 1 < cx->len && cx->s[cx->i + 1] == '{') {
            /* {{ → literal '{' (any depth) */
            static const char lbrace = '{';
            mf_node_t *lit = alloc_node(cx->msg);
            if (!lit) {
                return ROBO_OUT_OF_MEMORY;
            }
            lit->kind = MF_LIT;
            lit->lit = &lbrace;
            lit->lit_len = 1;
            cx->i += 2;
            if (!head) {
                head = lit;
            } else {
                tail->next = lit;
            }
            tail = lit;
            continue;
        }

        if (c == '{') {
            mf_node_t *n = NULL;
            cx->i++;
            if (parse_argument(cx, &n) != ROBO_OK) {
                return cx->has_err ? ROBO_PARSE_ERROR : ROBO_PARSE_ERROR;
            }
            if (!head) {
                head = n;
            } else {
                tail->next = n;
            }
            tail = n;
            continue;
        }

        /* literal; }} → } only at top-level (depth==0). Nested '}' always
         * closes the current pattern so constructs like
         * {g, select, other {They}} parse correctly. */
        {
            size_t start = cx->i;
            mf_node_t *lit;
            while (cx->i < cx->len) {
                char d = cx->s[cx->i];
                if (d == '{') {
                    break;
                }
                if (d == '}') {
                    if (cx->depth == 0 && cx->i + 1 < cx->len && cx->s[cx->i + 1] == '}') {
                        if (cx->i > start) {
                            lit = alloc_node(cx->msg);
                            if (!lit) {
                                return ROBO_OUT_OF_MEMORY;
                            }
                            lit->kind = MF_LIT;
                            lit->lit = cx->msg->template_copy + start;
                            lit->lit_len = cx->i - start;
                            if (!head) {
                                head = lit;
                            } else {
                                tail->next = lit;
                            }
                            tail = lit;
                        }
                        {
                            static const char rbrace = '}';
                            lit = alloc_node(cx->msg);
                            if (!lit) {
                                return ROBO_OUT_OF_MEMORY;
                            }
                            lit->kind = MF_LIT;
                            lit->lit = &rbrace;
                            lit->lit_len = 1;
                            if (!head) {
                                head = lit;
                            } else {
                                tail->next = lit;
                            }
                            tail = lit;
                        }
                        cx->i += 2;
                        start = cx->i;
                        continue;
                    }
                    break; /* nested close, or unexpected top-level '}' */
                }
                if (d == '#' && cx->in_plural) {
                    break;
                }
                cx->i++;
            }
            if (cx->i > start) {
                lit = alloc_node(cx->msg);
                if (!lit) {
                    return ROBO_OUT_OF_MEMORY;
                }
                lit->kind = MF_LIT;
                lit->lit = cx->msg->template_copy + start;
                lit->lit_len = cx->i - start;
                if (!head) {
                    head = lit;
                } else {
                    tail->next = lit;
                }
                tail = lit;
            }
        }
    }
    *out_head = head;
    return ROBO_OK;
}

void robo_i18n_mf_destroy(robo_i18n_mf_message_t *msg)
{
    if (!msg) {
        return;
    }
    robo_free(msg->template_copy);
    robo_free(msg->node_pool);
    robo_free(msg->branch_pool);
    robo_free(msg);
}

static void fill_diag_from_cx(const parse_ctx_t *cx,
                              const char *tmpl,
                              size_t tmpl_len,
                              robo_result_t result,
                              robo_i18n_mf_diagnostic_t *diag)
{
    size_t off;
    uint32_t line = 1u;
    uint32_t col = 1u;
    size_t i;

    if (!diag) {
        return;
    }
    robo_i18n_mf_diagnostic_reset(diag);
    diag->result = result;
    if (!cx || !cx->has_err) {
        if (result == ROBO_OK) {
            diag->code = ROBO_I18N_MF_DIAG_OK;
        } else if (result == ROBO_NOT_SUPPORTED) {
            diag->code = ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION;
        } else {
            diag->code = ROBO_I18N_MF_DIAG_MALFORMED;
        }
        return;
    }
    diag->code = (robo_i18n_mf_diag_code_t)cx->err_code;
    diag->severity = ROBO_I18N_MF_SEV_ERROR;
    diag->byte_offset = cx->err_offset;
    strncpy(diag->construct, cx->err_construct, sizeof(diag->construct) - 1u);
    strncpy(diag->argument_name, cx->err_arg, sizeof(diag->argument_name) - 1u);
    strncpy(diag->explanation, cx->err_msg, sizeof(diag->explanation) - 1u);
    off = cx->err_offset;
    if (tmpl && off <= tmpl_len) {
        for (i = 0; i < off; i++) {
            if (tmpl[i] == '\n') {
                line++;
                col = 1u;
            } else {
                col++;
            }
        }
        diag->line = line;
        diag->column = col;
    }
}

static int consume_version_directive(const char **ps, size_t *plen, uint32_t *out_ver,
                                     parse_ctx_t *cx)
{
    const char *s = *ps;
    size_t len = *plen;
    size_t i = 0;
    uint32_t ver = 0;

    if (len < 10u || !(s[0] == '#' && s[1] == '!' && s[2] == 'r')) {
        *out_ver = ROBO_I18N_MF_SYNTAX_VERSION;
        return 1;
    }
    /* #!robo-mf/N\n */
    if (len < 11u || memcmp(s, "#!robo-mf/", 10) != 0) {
        mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION, "version", NULL,
                "malformed version directive");
        return 0;
    }
    i = 10;
    if (i >= len || s[i] < '0' || s[i] > '9') {
        mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION, "version", NULL,
                "missing version number");
        return 0;
    }
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        ver = ver * 10u + (uint32_t)(s[i] - '0');
        if (ver > 1000u) {
            mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION, "version", NULL,
                    "version too large");
            return 0;
        }
        i++;
    }
    if (i < len && (s[i] == '\r' || s[i] == '\n')) {
        if (s[i] == '\r') {
            i++;
        }
        if (i < len && s[i] == '\n') {
            i++;
        }
    } else if (i < len) {
        mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION, "version", NULL,
                "version directive must end line");
        return 0;
    }
    if (ver != ROBO_I18N_MF_SYNTAX_VERSION) {
        mf_fail(cx, ROBO_I18N_MF_DIAG_UNSUPPORTED_VERSION, "version", NULL,
                "unsupported message-format version");
        return 0;
    }
    *out_ver = ver;
    *ps = s + i;
    *plen = len - i;
    return 1;
}

robo_result_t robo_i18n_mf_parse_ex(const char *utf8,
                                    size_t len,
                                    const robo_i18n_mf_limits_t *limits,
                                    robo_i18n_mf_diagnostic_t *diag,
                                    robo_i18n_mf_message_t **out)
{
    robo_i18n_mf_message_t *msg;
    parse_ctx_t cx;
    mf_node_t *root = NULL;
    robo_result_t pr;
    robo_i18n_mf_limits_t lim;
    const char *body;
    size_t body_len;
    uint32_t syntax_ver = ROBO_I18N_MF_SYNTAX_VERSION;
    uint32_t template_max;

    if (!utf8 || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    robo_i18n_mf_limits_init(&lim);
    if (limits) {
        if (limits->template_max) {
            lim.template_max = limits->template_max;
        }
        if (limits->nesting_max) {
            lim.nesting_max = limits->nesting_max;
        }
        if (limits->branches_max) {
            lim.branches_max = limits->branches_max;
        }
        if (limits->args_max) {
            lim.args_max = limits->args_max;
        }
        if (limits->output_max) {
            lim.output_max = limits->output_max;
        }
        if (limits->diagnostics_max) {
            lim.diagnostics_max = limits->diagnostics_max;
        }
    }
    template_max = lim.template_max ? lim.template_max : ROBO_I18N_MF_TEMPLATE_MAX;

    memset(&cx, 0, sizeof(cx));
    if (len > template_max) {
        mf_fail(&cx, ROBO_I18N_MF_DIAG_TOO_LARGE, "template", NULL, "template too large");
        fill_diag_from_cx(&cx, utf8, len, ROBO_PARSE_ERROR, diag);
        return ROBO_PARSE_ERROR;
    }
    if (!utf8_ok(utf8, len)) {
        mf_fail(&cx, ROBO_I18N_MF_DIAG_INVALID_UTF8, "template", NULL, "invalid UTF-8");
        fill_diag_from_cx(&cx, utf8, len, ROBO_PARSE_ERROR, diag);
        return ROBO_PARSE_ERROR;
    }

    body = utf8;
    body_len = len;
    if (!consume_version_directive(&body, &body_len, &syntax_ver, &cx)) {
        fill_diag_from_cx(&cx, utf8, len, ROBO_NOT_SUPPORTED, diag);
        return ROBO_NOT_SUPPORTED;
    }

    msg = (robo_i18n_mf_message_t *)robo_calloc(1, sizeof(*msg));
    if (!msg) {
        return ROBO_OUT_OF_MEMORY;
    }
    msg->template_copy = (char *)robo_malloc(body_len + 1u);
    if (!msg->template_copy) {
        robo_free(msg);
        return ROBO_OUT_OF_MEMORY;
    }
    if (body_len) {
        memcpy(msg->template_copy, body, body_len);
    }
    msg->template_copy[body_len] = '\0';
    msg->template_len = body_len;
    msg->syntax_version = syntax_ver;
    msg->node_cap = 2048u;
    msg->branch_cap = 256u;
    msg->node_pool = (mf_node_t *)robo_calloc(msg->node_cap, sizeof(mf_node_t));
    msg->branch_pool = (mf_branch_t *)robo_calloc(msg->branch_cap, sizeof(mf_branch_t));
    if (!msg->node_pool || !msg->branch_pool) {
        robo_i18n_mf_destroy(msg);
        return ROBO_OUT_OF_MEMORY;
    }

    memset(&cx, 0, sizeof(cx));
    cx.s = msg->template_copy;
    cx.len = body_len;
    cx.msg = msg;
    cx.nesting_max = lim.nesting_max ? lim.nesting_max : ROBO_I18N_MF_NESTING_MAX;
    cx.branches_max = lim.branches_max ? lim.branches_max : ROBO_I18N_MF_BRANCHES_MAX;
    pr = parse_pattern(&cx, &root);
    if (pr != ROBO_OK) {
        if (!cx.has_err) {
            mf_fail(&cx, ROBO_I18N_MF_DIAG_MALFORMED, "pattern", NULL, "parse failed");
        }
        fill_diag_from_cx(&cx, msg->template_copy, body_len, pr, diag);
        robo_i18n_mf_destroy(msg);
        return pr == ROBO_OUT_OF_MEMORY ? ROBO_OUT_OF_MEMORY : ROBO_PARSE_ERROR;
    }
    skip_ws(&cx);
    if (cx.i != cx.len) {
        mf_fail(&cx, ROBO_I18N_MF_DIAG_MALFORMED, "pattern", NULL, "unexpected trailing input");
        fill_diag_from_cx(&cx, msg->template_copy, body_len, ROBO_PARSE_ERROR, diag);
        robo_i18n_mf_destroy(msg);
        return ROBO_PARSE_ERROR;
    }
    msg->root = root;
    *out = msg;
    if (diag) {
        robo_i18n_mf_diagnostic_reset(diag);
        diag->result = ROBO_OK;
        diag->code = ROBO_I18N_MF_DIAG_OK;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_mf_parse(const char *utf8, size_t len, robo_i18n_mf_message_t **out)
{
    return robo_i18n_mf_parse_ex(utf8, len, NULL, NULL, out);
}

robo_result_t robo_i18n_mf_validate(const char *utf8, size_t len)
{
    robo_i18n_mf_message_t *msg = NULL;
    robo_result_t r = robo_i18n_mf_parse(utf8, len, &msg);
    if (r == ROBO_OK) {
        robo_i18n_mf_destroy(msg);
    }
    return r;
}

static robo_result_t analyze_add(robo_i18n_mf_contract_t *c, const char *name, uint32_t usage)
{
    uint32_t i;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->args[i].name, name) == 0) {
            uint32_t merged = c->args[i].usage | usage;
            if (!robo_i18n_mf_usage_compatible(c->args[i].usage, usage)) {
                return ROBO_PARSE_ERROR;
            }
            c->args[i].usage = merged;
            return ROBO_OK;
        }
    }
    if (c->count >= ROBO_I18N_MF_ARGS_MAX) {
        return ROBO_PARSE_ERROR;
    }
    strncpy(c->args[c->count].name, name, ROBO_I18N_MF_NAME_MAX - 1u);
    c->args[c->count].name[ROBO_I18N_MF_NAME_MAX - 1u] = '\0';
    c->args[c->count].usage = usage;
    c->count++;
    return ROBO_OK;
}

static robo_result_t analyze_nodes(const mf_node_t *node, robo_i18n_mf_contract_t *c)
{
    for (; node; node = node->next) {
        robo_result_t r = ROBO_OK;
        const mf_branch_t *b;
        switch (node->kind) {
        case MF_LIT:
        case MF_POUND:
            break;
        case MF_SIMPLE:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_STRING);
            break;
        case MF_NUMBER:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_NUMBER);
            break;
        case MF_DATE:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_DATE);
            break;
        case MF_TIME:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_TIME);
            break;
        case MF_SELECT:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_SELECT);
            if (r != ROBO_OK) {
                return r;
            }
            for (b = node->branches; b; b = b->next) {
                r = analyze_nodes(b->body, c);
                if (r != ROBO_OK) {
                    return r;
                }
            }
            break;
        case MF_PLURAL:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_PLURAL | ROBO_I18N_MF_USAGE_NUMBER);
            if (r != ROBO_OK) {
                return r;
            }
            for (b = node->branches; b; b = b->next) {
                r = analyze_nodes(b->body, c);
                if (r != ROBO_OK) {
                    return r;
                }
            }
            break;
        case MF_ORDINAL:
            r = analyze_add(c, node->name, ROBO_I18N_MF_USAGE_ORDINAL | ROBO_I18N_MF_USAGE_NUMBER);
            if (r != ROBO_OK) {
                return r;
            }
            for (b = node->branches; b; b = b->next) {
                r = analyze_nodes(b->body, c);
                if (r != ROBO_OK) {
                    return r;
                }
            }
            break;
        default:
            return ROBO_ERROR;
        }
        if (r != ROBO_OK) {
            return r;
        }
    }
    return ROBO_OK;
}

static void sort_contract_args(robo_i18n_mf_contract_t *c)
{
    uint32_t i, j;
    for (i = 0; i + 1u < c->count; i++) {
        for (j = i + 1u; j < c->count; j++) {
            if (strcmp(c->args[j].name, c->args[i].name) < 0) {
                robo_i18n_mf_arg_req_t tmp = c->args[i];
                c->args[i] = c->args[j];
                c->args[j] = tmp;
            }
        }
    }
}

robo_result_t robo_i18n_mf_analyze(const robo_i18n_mf_message_t *msg,
                                   robo_i18n_mf_contract_t *out)
{
    robo_result_t r;
    if (!msg || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_mf_contract_reset(out);
    out->syntax_version = msg->syntax_version ? msg->syntax_version : ROBO_I18N_MF_SYNTAX_VERSION;
    r = analyze_nodes(msg->root, out);
    if (r != ROBO_OK) {
        return r == ROBO_PARSE_ERROR ? ROBO_PARSE_ERROR : r;
    }
    sort_contract_args(out);
    return ROBO_OK;
}

robo_result_t robo_i18n_mf_analyze_template(const char *utf8,
                                            size_t len,
                                            const robo_i18n_mf_limits_t *limits,
                                            robo_i18n_mf_diagnostic_t *diag,
                                            robo_i18n_mf_contract_t *out)
{
    robo_i18n_mf_message_t *msg = NULL;
    robo_result_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_mf_parse_ex(utf8, len, limits, diag, &msg);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_mf_analyze(msg, out);
    if (r != ROBO_OK && diag && !diag->explanation[0]) {
        diag->result = r;
        diag->code = ROBO_I18N_MF_DIAG_TYPE_CONFLICT;
        diag->severity = ROBO_I18N_MF_SEV_ERROR;
        strncpy(diag->explanation, "incompatible argument usages in template",
                sizeof(diag->explanation) - 1u);
    }
    robo_i18n_mf_destroy(msg);
    return r;
}

/* ---------- formatting ---------- */


static const robo_i18n_mf_arg_t *find_arg(const robo_i18n_mf_args_t *args,
                                         const char *name)
{
    size_t i;
    if (!args || !args->items) {
        return NULL;
    }
    for (i = 0; i < args->count; i++) {
        if (strcmp(args->items[i].name, name) == 0) {
            return &args->items[i];
        }
    }
    return NULL;
}

static robo_result_t append_bytes(fmt_ctx_t *fx, const char *p, size_t n)
{
    if (fx->len + n > ROBO_I18N_MF_OUTPUT_MAX) {
        return ROBO_ERROR;
    }
    if (fx->buf && fx->cap > 0) {
        if (fx->len + n + 1u > fx->cap) {
            fx->len += n;
            return ROBO_ERROR;
        }
        if (n) {
            memcpy(fx->buf + fx->len, p, n);
        }
    }
    fx->len += n;
    return ROBO_OK;
}

static robo_result_t arg_to_decimal(const robo_i18n_mf_arg_t *a, robo_i18n_decimal_t *out)
{
    if (!a || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    switch (a->kind) {
    case ROBO_I18N_MF_VAL_I64:
        return robo_i18n_decimal_from_i64(a->u.i64, out);
    case ROBO_I18N_MF_VAL_U64:
        return robo_i18n_decimal_from_u64(a->u.u64, out);
    case ROBO_I18N_MF_VAL_DECIMAL:
        *out = a->u.decimal;
        return ROBO_OK;
    case ROBO_I18N_MF_VAL_DOUBLE:
        return robo_i18n_decimal_from_double(a->u.dbl, out);
    case ROBO_I18N_MF_VAL_BOOL:
        return robo_i18n_decimal_from_i64(a->u.boolean ? 1 : 0, out);
    default:
        return ROBO_INVALID_ARGUMENT;
    }
}

static robo_result_t arg_select_key(const robo_i18n_mf_arg_t *a, char *buf, size_t cap)
{
    size_t n;
    if (!a || !buf || cap < 2) {
        return ROBO_INVALID_ARGUMENT;
    }
    switch (a->kind) {
    case ROBO_I18N_MF_VAL_STRING: {
        const char *p = a->u.str.ptr ? a->u.str.ptr : "";
        n = a->u.str.len ? a->u.str.len : strlen(p);
        if (n + 1 > cap) {
            return ROBO_ERROR;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        return ROBO_OK;
    }
    case ROBO_I18N_MF_VAL_BOOL:
        strncpy(buf, a->u.boolean ? "true" : "false", cap - 1);
        buf[cap - 1] = '\0';
        return ROBO_OK;
    case ROBO_I18N_MF_VAL_I64: {
        char tmp[32];
        int neg = a->u.i64 < 0;
        uint64_t v = neg ? (uint64_t)(-(a->u.i64 + 1)) + 1u : (uint64_t)a->u.i64;
        char *t = tmp + sizeof(tmp);
        *--t = '\0';
        do {
            *--t = (char)('0' + (v % 10u));
            v /= 10u;
        } while (v);
        if (neg) {
            *--t = '-';
        }
        n = strlen(t);
        if (n + 1 > cap) {
            return ROBO_ERROR;
        }
        memcpy(buf, t, n + 1);
        return ROBO_OK;
    }
    default:
        return ROBO_INVALID_ARGUMENT;
    }
}

static int decimal_is_exact_i64(const robo_i18n_decimal_t *d, int64_t *out)
{
    /* Use plural operands path: only match exact when no fraction digits visible */
    robo_i18n_plural_operands_t ops;
    if (robo_i18n_plural_operands_from_decimal(d, &ops) != ROBO_OK) {
        return 0;
    }
    if (ops.v != 0) {
        return 0;
    }
    {
        int64_t v = 0;
        size_t i;
        for (i = 0; i < ops.i_len; i++) {
            int digit = ops.i_digits[i] - '0';
            if (v > (INT64_MAX - digit) / 10) {
                return 0;
            }
            v = v * 10 + digit;
        }
        if (ops.negative_input) {
            v = -v;
        }
        *out = v;
        return 1;
    }
}

static robo_result_t format_nodes(fmt_ctx_t *fx, const mf_node_t *node);

static robo_result_t format_number_arg(fmt_ctx_t *fx, const robo_i18n_mf_arg_t *a)
{
    robo_i18n_number_formatter_t nf;
    robo_i18n_decimal_t dec;
    char tmp[256];
    size_t n = 0;
    robo_result_t r;

    if (arg_to_decimal(a, &dec) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_i18n_number_formatter_init(&nf, fx->locale, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    r = robo_i18n_number_format(&nf, &dec, tmp, sizeof(tmp), &n);
    robo_i18n_number_formatter_destroy(&nf);
    if (r != ROBO_OK) {
        return r;
    }
    return append_bytes(fx, tmp, n);
}

static robo_result_t format_date_arg(fmt_ctx_t *fx,
                                     const robo_i18n_mf_arg_t *a,
                                     mf_node_kind_t kind,
                                     robo_i18n_cal_format_style_t style)
{
    robo_i18n_datetime_formatter_t df;
    robo_i18n_dtfmt_options_t opts;
    robo_i18n_calendar_t cal;
    char tmp[256];
    size_t n = 0;
    robo_result_t r;

    robo_i18n_dtfmt_options_init(&opts);
    if (kind == MF_DATE) {
        opts.date_style = style;
    } else {
        opts.time_style = style;
    }
    if (robo_i18n_calendar_from_id("gregorian", 9, &cal) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (robo_i18n_datetime_formatter_init(
            &df, fx->locale, &cal, NULL,
            kind == MF_DATE ? ROBO_I18N_DTFMT_DATE : ROBO_I18N_DTFMT_TIME, &opts) !=
        ROBO_OK) {
        return ROBO_ERROR;
    }
    if (kind == MF_DATE) {
        robo_i18n_date_t date;
        if (a->kind == ROBO_I18N_MF_VAL_DATE) {
            date = a->u.date;
        } else if (a->kind == ROBO_I18N_MF_VAL_DATETIME) {
            date = a->u.datetime.date;
        } else {
            robo_i18n_datetime_formatter_destroy(&df);
            return ROBO_INVALID_ARGUMENT;
        }
        r = robo_i18n_datetime_format_date(&df, &date, tmp, sizeof(tmp), &n);
    } else {
        robo_i18n_time_t time;
        if (a->kind == ROBO_I18N_MF_VAL_TIME) {
            time = a->u.time;
        } else if (a->kind == ROBO_I18N_MF_VAL_DATETIME) {
            time = a->u.datetime.time;
        } else {
            robo_i18n_datetime_formatter_destroy(&df);
            return ROBO_INVALID_ARGUMENT;
        }
        r = robo_i18n_datetime_format_time(&df, &time, tmp, sizeof(tmp), &n);
    }
    robo_i18n_datetime_formatter_destroy(&df);
    if (r != ROBO_OK) {
        return r;
    }
    return append_bytes(fx, tmp, n);
}

static const mf_branch_t *pick_select_branch(const mf_node_t *node, const char *key)
{
    const mf_branch_t *b;
    const mf_branch_t *other = NULL;
    for (b = node->branches; b; b = b->next) {
        if (b->is_exact) {
            continue;
        }
        if (strcmp(b->key, "other") == 0) {
            other = b;
        }
        if (strcmp(b->key, key) == 0) {
            return b;
        }
    }
    return other;
}

static const mf_branch_t *pick_plural_branch(const mf_node_t *node,
                                             const robo_i18n_locale_t *locale,
                                             const robo_i18n_decimal_t *dec,
                                             int ordinal)
{
    const mf_branch_t *b;
    int64_t exact = 0;
    const mf_branch_t *other = NULL;
    robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
    const char *cat_name;

    if (decimal_is_exact_i64(dec, &exact)) {
        for (b = node->branches; b; b = b->next) {
            if (b->is_exact && b->exact_value == exact) {
                return b;
            }
        }
    }
    if (ordinal) {
        if (robo_i18n_plural_ordinal(locale, dec, &cat) != ROBO_OK) {
            cat = ROBO_I18N_PLURAL_OTHER;
        }
    } else {
        if (robo_i18n_plural_cardinal(locale, dec, &cat) != ROBO_OK) {
            cat = ROBO_I18N_PLURAL_OTHER;
        }
    }
    cat_name = robo_i18n_plural_category_name(cat);
    for (b = node->branches; b; b = b->next) {
        if (b->is_exact) {
            continue;
        }
        if (strcmp(b->key, "other") == 0) {
            other = b;
        }
        if (cat_name && strcmp(b->key, cat_name) == 0) {
            return b;
        }
    }
    return other;
}

static robo_result_t format_nodes(fmt_ctx_t *fx, const mf_node_t *node)
{
    if (fx->depth > (int)ROBO_I18N_MF_NESTING_MAX) {
        return ROBO_PARSE_ERROR;
    }
    for (; node; node = node->next) {
        robo_result_t r;
        switch (node->kind) {
        case MF_LIT:
            r = append_bytes(fx, node->lit, node->lit_len);
            if (r != ROBO_OK) {
                return r;
            }
            break;
        case MF_POUND: {
            if (!fx->plural_arg) {
                return ROBO_ERROR;
            }
            r = format_number_arg(fx, fx->plural_arg);
            if (r != ROBO_OK) {
                return r;
            }
            break;
        }
        case MF_SIMPLE: {
            const robo_i18n_mf_arg_t *a = find_arg(fx->args, node->name);
            size_t n;
            const char *p;
            if (!a) {
                return ROBO_NOT_FOUND;
            }
            if (a->kind == ROBO_I18N_MF_VAL_STRING) {
                p = a->u.str.ptr ? a->u.str.ptr : "";
                n = a->u.str.len ? a->u.str.len : strlen(p);
                r = append_bytes(fx, p, n);
            } else if (a->kind == ROBO_I18N_MF_VAL_I64 || a->kind == ROBO_I18N_MF_VAL_U64 ||
                       a->kind == ROBO_I18N_MF_VAL_DECIMAL || a->kind == ROBO_I18N_MF_VAL_DOUBLE ||
                       a->kind == ROBO_I18N_MF_VAL_BOOL) {
                r = format_number_arg(fx, a);
            } else {
                return ROBO_INVALID_ARGUMENT;
            }
            if (r != ROBO_OK) {
                return r;
            }
            break;
        }
        case MF_NUMBER: {
            const robo_i18n_mf_arg_t *a = find_arg(fx->args, node->name);
            if (!a) {
                return ROBO_NOT_FOUND;
            }
            r = format_number_arg(fx, a);
            if (r != ROBO_OK) {
                return r;
            }
            break;
        }
        case MF_DATE:
        case MF_TIME: {
            const robo_i18n_mf_arg_t *a = find_arg(fx->args, node->name);
            if (!a) {
                return ROBO_NOT_FOUND;
            }
            r = format_date_arg(fx, a, node->kind, node->style);
            if (r != ROBO_OK) {
                return r;
            }
            break;
        }
        case MF_SELECT: {
            const robo_i18n_mf_arg_t *a = find_arg(fx->args, node->name);
            char key[128];
            const mf_branch_t *br;
            if (!a) {
                return ROBO_NOT_FOUND;
            }
            if (arg_select_key(a, key, sizeof(key)) != ROBO_OK) {
                return ROBO_INVALID_ARGUMENT;
            }
            br = pick_select_branch(node, key);
            if (!br) {
                return ROBO_NOT_FOUND;
            }
            fx->depth++;
            r = format_nodes(fx, br->body);
            fx->depth--;
            if (r != ROBO_OK) {
                return r;
            }
            break;
        }
        case MF_PLURAL:
        case MF_ORDINAL: {
            const robo_i18n_mf_arg_t *a = find_arg(fx->args, node->name);
            robo_i18n_decimal_t dec;
            const mf_branch_t *br;
            const robo_i18n_mf_arg_t *prev;
            if (!a) {
                return ROBO_NOT_FOUND;
            }
            if (arg_to_decimal(a, &dec) != ROBO_OK) {
                return ROBO_INVALID_ARGUMENT;
            }
            br = pick_plural_branch(node, fx->locale, &dec, node->kind == MF_ORDINAL);
            if (!br) {
                return ROBO_NOT_FOUND;
            }
            prev = fx->plural_arg;
            fx->plural_arg = a;
            fx->depth++;
            r = format_nodes(fx, br->body);
            fx->depth--;
            fx->plural_arg = prev;
            if (r != ROBO_OK) {
                return r;
            }
            break;
        }
        default:
            return ROBO_ERROR;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_mf_format(const robo_i18n_mf_message_t *msg,
                                  const robo_i18n_locale_t *locale,
                                  const robo_i18n_mf_args_t *args,
                                  char *out,
                                  size_t out_cap,
                                  size_t *out_len)
{
    fmt_ctx_t fx;
    robo_result_t r;

    if (!msg || !locale || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (args && args->count > ROBO_I18N_MF_ARGS_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&fx, 0, sizeof(fx));
    fx.locale = locale;
    fx.args = args;
    fx.buf = out;
    fx.cap = out_cap;
    r = format_nodes(&fx, msg->root);
    *out_len = fx.len;
    if (r == ROBO_OK) {
        if (out && out_cap > 0) {
            if (fx.len + 1u > out_cap) {
                *out_len = fx.len + 1u;
                return ROBO_ERROR;
            }
            out[fx.len] = '\0';
        }
    } else if (r == ROBO_ERROR && fx.len + 1u > out_cap && out_cap > 0) {
        *out_len = fx.len + 1u;
    }
    return r;
}

robo_result_t robo_i18n_mf_format_to_string(const robo_i18n_mf_message_t *msg,
                                            const robo_i18n_locale_t *locale,
                                            const robo_i18n_mf_args_t *args,
                                            robo_string_t *out)
{
    char buf[2048];
    size_t n = 0;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_mf_format(msg, locale, args, buf, sizeof(buf), &n);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_string_append(out, buf, n);
}

robo_result_t robo_i18n_mf_format_template(const char *utf8,
                                           size_t len,
                                           const robo_i18n_locale_t *locale,
                                           const robo_i18n_mf_args_t *args,
                                           char *out,
                                           size_t out_cap,
                                           size_t *out_len)
{
    robo_i18n_mf_message_t *msg = NULL;
    robo_result_t r = robo_i18n_mf_parse(utf8, len, &msg);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_mf_format(msg, locale, args, out, out_cap, out_len);
    robo_i18n_mf_destroy(msg);
    return r;
}

robo_result_t robo_i18n_message_format_mf(const robo_i18n_catalog_t *catalog,
                                          const char *message_id,
                                          size_t id_len,
                                          const char *context,
                                          size_t context_len,
                                          const robo_i18n_locale_t *locale,
                                          const robo_i18n_mf_args_t *args,
                                          const robo_i18n_msg_options_t *options,
                                          char *out,
                                          size_t out_cap,
                                          size_t *out_len)
{
    char tmpl[ROBO_I18N_MF_TEMPLATE_MAX];
    size_t n = 0;
    robo_result_t r;

    r = robo_i18n_message_get(catalog, message_id, id_len, context, context_len, locale, NULL,
                              options, tmpl, sizeof(tmpl), &n);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_mf_format_template(tmpl, n, locale, args, out, out_cap, out_len);
}

static void set_name(robo_i18n_mf_arg_t *a, const char *name)
{
    size_t n;
    memset(a, 0, sizeof(*a));
    if (!name) {
        return;
    }
    n = strlen(name);
    if (n >= ROBO_I18N_MF_NAME_MAX) {
        n = ROBO_I18N_MF_NAME_MAX - 1u;
    }
    memcpy(a->name, name, n);
    a->name[n] = '\0';
}

void robo_i18n_mf_arg_set_string(robo_i18n_mf_arg_t *a,
                                 const char *name,
                                 const char *utf8,
                                 size_t len)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_STRING;
    a->u.str.ptr = utf8;
    a->u.str.len = len;
}

void robo_i18n_mf_arg_set_i64(robo_i18n_mf_arg_t *a, const char *name, int64_t v)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_I64;
    a->u.i64 = v;
}

void robo_i18n_mf_arg_set_u64(robo_i18n_mf_arg_t *a, const char *name, uint64_t v)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_U64;
    a->u.u64 = v;
}

void robo_i18n_mf_arg_set_decimal(robo_i18n_mf_arg_t *a,
                                  const char *name,
                                  const robo_i18n_decimal_t *d)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_DECIMAL;
    if (d) {
        a->u.decimal = *d;
    }
}

void robo_i18n_mf_arg_set_bool(robo_i18n_mf_arg_t *a, const char *name, int v)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_BOOL;
    a->u.boolean = v ? 1 : 0;
}

void robo_i18n_mf_arg_set_date(robo_i18n_mf_arg_t *a,
                               const char *name,
                               const robo_i18n_date_t *d)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_DATE;
    if (d) {
        a->u.date = *d;
    }
}

void robo_i18n_mf_arg_set_time(robo_i18n_mf_arg_t *a,
                               const char *name,
                               const robo_i18n_time_t *t)
{
    set_name(a, name);
    a->kind = ROBO_I18N_MF_VAL_TIME;
    if (t) {
        a->u.time = *t;
    }
}
