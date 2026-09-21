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

typedef enum mfNodeKind {
    MF_LIT = 0,
    MF_SIMPLE = 1,
    MF_NUMBER = 2,
    MF_DATE = 3,
    MF_TIME = 4,
    MF_SELECT = 5,
    MF_PLURAL = 6,
    MF_ORDINAL = 7,
    MF_POUND = 8
} MfNodeKind;

typedef struct mfNode MfNode;

typedef struct mfBranch {
    int isExact; /* 1 => exactValue used */
    int64_t exactValue;
    char key[RoboI18nMfNameMax]; /* select / plural category */
    MfNode *body;                 /* linked list of pieces */
    struct mfBranch *next;
} MfBranch;

struct mfNode {
    MfNodeKind kind;
    MfNode *next;
    char name[RoboI18nMfNameMax];
    const char *lit; /* into owned template copy */
    size_t litLen;
    I18nCalFormatStyle style; /* date/time */
    MfBranch *branches;
    uint16_t branchCount;
    int hasOther;
};

struct I18nMfMessage {
    char *templateCopy;
    size_t templateLen;
    uint32_t syntaxVersion;
    MfNode *root; /* linked list */
    MfNode *nodePool;
    uint32_t nodeCount;
    uint32_t nodeCap;
    MfBranch *branchPool;
    uint32_t branchCount;
    uint32_t branchCap;
};

typedef struct parseCtx {
    const char *s;
    size_t len;
    size_t i;
    int depth;
    I18nMfMessage *msg;
    int inPlural; /* allow # */
    uint32_t nestingMax;
    uint32_t branchesMax;
    /* first error (for diagnostics) */
    int hasErr;
    size_t errOffset;
    int errCode; /* I18nMfDiagCode-compatible int */
    char errConstruct[32];
    char errArg[RoboI18nMfNameMax];
    char errMsg[160];
} ParseCtx;

typedef struct fmtCtx {
    const I18nLocale *locale;
    const I18nMfArgs *args;
    char *buf;
    size_t cap;
    size_t len;
    int depth;
    const I18nMfArg *pluralArg; /* for # */
} FmtCtx;

static void MfNoteErr(ParseCtx *cx,
                        int code,
                        const char *construct,
                        const char *arg,
                        const char *msg)
{
    if (!cx || cx->hasErr) {
        return;
    }
    cx->hasErr = 1;
    cx->errOffset = cx->i;
    cx->errCode = code;
    cx->errConstruct[0] = '\0';
    cx->errArg[0] = '\0';
    cx->errMsg[0] = '\0';
    if (construct) {
        strncpy(cx->errConstruct, construct, sizeof(cx->errConstruct) - 1u);
    }
    if (arg) {
        strncpy(cx->errArg, arg, sizeof(cx->errArg) - 1u);
    }
    if (msg) {
        strncpy(cx->errMsg, msg, sizeof(cx->errMsg) - 1u);
    }
}

static Result MfFail(ParseCtx *cx,
                             int code,
                             const char *construct,
                             const char *arg,
                             const char *msg)
{
    MfNoteErr(cx, code, construct, arg, msg);
    if (code == RoboI18nMfDiagUnsupportedVersion) {
        return RoboNotSupported;
    }
    if (code == RoboI18nMfDiagTooLarge || code == RoboI18nMfDiagNesting) {
        return RoboParseError;
    }
    return RoboParseError;
}

static int BranchDuplicate(const MfBranch *head, const MfBranch *br)
{
    const MfBranch *p;
    for (p = head; p && p != br; p = p->next) {
        if (br->isExact) {
            if (p->isExact && p->exactValue == br->exactValue) {
                return 1;
            }
        } else if (!p->isExact && strcmp(p->key, br->key) == 0) {
            return 1;
        }
    }
    return 0;
}

static int IsIdentStart(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int IsIdent(char c)
{
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

static int Utf8Ok(const char *p, size_t n)
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

static void SkipWs(ParseCtx *cx)
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

static MfNode *AllocNode(I18nMfMessage *msg)
{
    MfNode *n;
    if (msg->nodeCount >= msg->nodeCap) {
        return NULL; /* fixed capacity; no realloc (keeps pointers stable) */
    }
    n = &msg->nodePool[msg->nodeCount++];
    memset(n, 0, sizeof(*n));
    return n;
}

static MfBranch *AllocBranch(I18nMfMessage *msg)
{
    MfBranch *b;
    if (msg->branchCount >= msg->branchCap) {
        return NULL;
    }
    b = &msg->branchPool[msg->branchCount++];
    memset(b, 0, sizeof(*b));
    return b;
}

static Result ParseIdent(ParseCtx *cx, char *out, size_t outCap)
{
    size_t n = 0;
    if (cx->i >= cx->len || !IsIdentStart(cx->s[cx->i])) {
        return RoboParseError;
    }
    while (cx->i < cx->len && IsIdent(cx->s[cx->i])) {
        if (n + 1 >= outCap) {
            return RoboParseError;
        }
        out[n++] = cx->s[cx->i++];
    }
    out[n] = '\0';
    return RoboOk;
}

static Result ParsePattern(ParseCtx *cx, MfNode **outHead);

static Result ParseBranches(ParseCtx *cx, MfNode *argNode, int pluralMode)
{
    uint16_t count = 0;
    MfBranch *head = NULL;
    MfBranch *tail = NULL;
    int sawOther = 0;
    uint32_t branchesMax =
        cx->branchesMax ? cx->branchesMax : RoboI18nMfBranchesMax;
    uint32_t nestingMax = cx->nestingMax ? cx->nestingMax : RoboI18nMfNestingMax;

    for (;;) {
        MfBranch *br;
        SkipWs(cx);
        if (cx->i >= cx->len) {
            return MfFail(cx, RoboI18nMfDiagMalformed, pluralMode ? "plural" : "select",
                           argNode->name, "unexpected end in branches");
        }
        if (cx->s[cx->i] == '}') {
            break;
        }
        if (count >= branchesMax) {
            return MfFail(cx, RoboI18nMfDiagTooLarge, pluralMode ? "plural" : "select",
                           argNode->name, "too many branches");
        }
        br = AllocBranch(cx->msg);
        if (!br) {
            return RoboOutOfMemory;
        }
        if (cx->s[cx->i] == '=') {
            int64_t v = 0;
            int neg = 0;
            if (!pluralMode) {
                return MfFail(cx, RoboI18nMfDiagInvalidSelector, "select",
                               argNode->name, "exact selector only valid on plural");
            }
            cx->i++;
            if (cx->i < cx->len && cx->s[cx->i] == '-') {
                neg = 1;
                cx->i++;
            }
            if (cx->i >= cx->len || cx->s[cx->i] < '0' || cx->s[cx->i] > '9') {
                return MfFail(cx, RoboI18nMfDiagInvalidSelector, "plural",
                               argNode->name, "invalid exact selector");
            }
            while (cx->i < cx->len && cx->s[cx->i] >= '0' && cx->s[cx->i] <= '9') {
                int d = cx->s[cx->i++] - '0';
                if (v > (INT64_MAX - d) / 10) {
                    return MfFail(cx, RoboI18nMfDiagInvalidSelector, "plural",
                                   argNode->name, "exact selector overflow");
                }
                v = v * 10 + d;
            }
            br->isExact = 1;
            br->exactValue = neg ? -v : v;
            br->key[0] = '\0';
        } else {
            if (ParseIdent(cx, br->key, sizeof(br->key)) != RoboOk) {
                return MfFail(cx, RoboI18nMfDiagInvalidSelector,
                               pluralMode ? "plural" : "select", argNode->name,
                               "invalid branch key");
            }
            if (strcmp(br->key, "other") == 0) {
                sawOther = 1;
            }
        }
        if (BranchDuplicate(head, br)) {
            return MfFail(cx, RoboI18nMfDiagDuplicateBranch,
                           pluralMode ? "plural" : "select", argNode->name,
                           "duplicate selector branch");
        }
        SkipWs(cx);
        if (cx->i >= cx->len || cx->s[cx->i] != '{') {
            return MfFail(cx, RoboI18nMfDiagMalformed, pluralMode ? "plural" : "select",
                           argNode->name, "expected '{' after branch key");
        }
        cx->i++;
        {
            int prev = cx->inPlural;
            ParseCtx nested = *cx;
            nested.depth = cx->depth + 1;
            nested.inPlural = pluralMode ? 1 : cx->inPlural;
            if (nested.depth > (int)nestingMax) {
                return MfFail(cx, RoboI18nMfDiagNesting, pluralMode ? "plural" : "select",
                               argNode->name, "nesting depth exceeded");
            }
            if (ParsePattern(&nested, &br->body) != RoboOk) {
                if (nested.hasErr && !cx->hasErr) {
                    *cx = nested;
                }
                return nested.hasErr ? RoboParseError : RoboParseError;
            }
            cx->i = nested.i;
            cx->inPlural = prev;
            if (nested.hasErr) {
                cx->hasErr = nested.hasErr;
                cx->errOffset = nested.errOffset;
                cx->errCode = nested.errCode;
                memcpy(cx->errConstruct, nested.errConstruct, sizeof(cx->errConstruct));
                memcpy(cx->errArg, nested.errArg, sizeof(cx->errArg));
                memcpy(cx->errMsg, nested.errMsg, sizeof(cx->errMsg));
            }
        }
        SkipWs(cx);
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return MfFail(cx, RoboI18nMfDiagMalformed, pluralMode ? "plural" : "select",
                           argNode->name, "expected '}' to close branch");
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
    if (count == 0 || !sawOther) {
        return MfFail(cx, RoboI18nMfDiagMissingOther, pluralMode ? "plural" : "select",
                       argNode->name, "missing required 'other' branch");
    }
    argNode->branches = head;
    argNode->branchCount = count;
    argNode->hasOther = 1;
    return RoboOk;
}

static Result ParseArgument(ParseCtx *cx, MfNode **out)
{
    MfNode *node;
    char type[32];
    char name[RoboI18nMfNameMax];

    if (cx->depth > (int)(cx->nestingMax ? cx->nestingMax : RoboI18nMfNestingMax)) {
        return MfFail(cx, RoboI18nMfDiagNesting, "argument", NULL, "nesting depth exceeded");
    }
    SkipWs(cx);
    if (ParseIdent(cx, name, sizeof(name)) != RoboOk) {
        return MfFail(cx, RoboI18nMfDiagMalformed, "argument", NULL, "missing argument name");
    }
    SkipWs(cx);
    node = AllocNode(cx->msg);
    if (!node) {
        return RoboOutOfMemory;
    }
    memcpy(node->name, name, sizeof(name));
    node->style = RoboI18nCalStyleMedium;

    if (cx->i < cx->len && cx->s[cx->i] == '}') {
        cx->i++;
        node->kind = MF_SIMPLE;
        *out = node;
        return RoboOk;
    }
    if (cx->i >= cx->len || cx->s[cx->i] != ',') {
        return MfFail(cx, RoboI18nMfDiagMalformed, "argument", name, "expected ',' or '}'");
    }
    cx->i++;
    SkipWs(cx);
    if (ParseIdent(cx, type, sizeof(type)) != RoboOk) {
        return MfFail(cx, RoboI18nMfDiagMalformed, "argument", name, "missing argument type");
    }
    SkipWs(cx);

    if (strcmp(type, "number") == 0) {
        node->kind = MF_NUMBER;
        if (cx->i < cx->len && cx->s[cx->i] == ',') {
            char sty[32];
            cx->i++;
            SkipWs(cx);
            if (ParseIdent(cx, sty, sizeof(sty)) != RoboOk) {
                return MfFail(cx, RoboI18nMfDiagUnsupportedOption, "number", name,
                               "invalid number style");
            }
            SkipWs(cx);
        }
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return MfFail(cx, RoboI18nMfDiagMalformed, "number", name, "expected '}'");
        }
        cx->i++;
        *out = node;
        return RoboOk;
    }
    if (strcmp(type, "date") == 0 || strcmp(type, "time") == 0) {
        node->kind = (strcmp(type, "date") == 0) ? MF_DATE : MF_TIME;
        if (cx->i < cx->len && cx->s[cx->i] == ',') {
            char sty[32];
            cx->i++;
            SkipWs(cx);
            if (ParseIdent(cx, sty, sizeof(sty)) != RoboOk) {
                return MfFail(cx, RoboI18nMfDiagUnsupportedOption, type, name,
                               "invalid date/time style");
            }
            if (strcmp(sty, "short") == 0) {
                node->style = RoboI18nCalStyleShort;
            } else if (strcmp(sty, "medium") == 0) {
                node->style = RoboI18nCalStyleMedium;
            } else if (strcmp(sty, "long") == 0) {
                node->style = RoboI18nCalStyleLong;
            } else if (strcmp(sty, "full") == 0) {
                node->style = RoboI18nCalStyleFull;
            } else {
                return MfFail(cx, RoboI18nMfDiagUnsupportedOption, type, name,
                               "unsupported date/time style");
            }
            SkipWs(cx);
        }
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return MfFail(cx, RoboI18nMfDiagMalformed, type, name, "expected '}'");
        }
        cx->i++;
        *out = node;
        return RoboOk;
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
            return MfFail(cx, RoboI18nMfDiagMalformed, type, name,
                           "expected ',' before branches");
        }
        cx->i++;
        {
            Result br = ParseBranches(cx, node, plural);
            if (br != RoboOk) {
                return br;
            }
        }
        SkipWs(cx);
        if (cx->i >= cx->len || cx->s[cx->i] != '}') {
            return MfFail(cx, RoboI18nMfDiagMalformed, type, name, "expected '}'");
        }
        cx->i++;
        *out = node;
        return RoboOk;
    }
    return MfFail(cx, RoboI18nMfDiagUnknownType, type, name, "unknown argument type");
}

static Result ParsePattern(ParseCtx *cx, MfNode **outHead)
{
    MfNode *head = NULL;
    MfNode *tail = NULL;

    while (cx->i < cx->len) {
        char c = cx->s[cx->i];

        if (c == '}') {
            break;
        }

        if (c == '#' && cx->inPlural) {
            MfNode *n = AllocNode(cx->msg);
            if (!n) {
                return RoboOutOfMemory;
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
            MfNode *lit = AllocNode(cx->msg);
            if (!lit) {
                return RoboOutOfMemory;
            }
            lit->kind = MF_LIT;
            lit->lit = &lbrace;
            lit->litLen = 1;
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
            MfNode *n = NULL;
            cx->i++;
            if (ParseArgument(cx, &n) != RoboOk) {
                return cx->hasErr ? RoboParseError : RoboParseError;
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
            MfNode *lit;
            while (cx->i < cx->len) {
                char d = cx->s[cx->i];
                if (d == '{') {
                    break;
                }
                if (d == '}') {
                    if (cx->depth == 0 && cx->i + 1 < cx->len && cx->s[cx->i + 1] == '}') {
                        if (cx->i > start) {
                            lit = AllocNode(cx->msg);
                            if (!lit) {
                                return RoboOutOfMemory;
                            }
                            lit->kind = MF_LIT;
                            lit->lit = cx->msg->templateCopy + start;
                            lit->litLen = cx->i - start;
                            if (!head) {
                                head = lit;
                            } else {
                                tail->next = lit;
                            }
                            tail = lit;
                        }
                        {
                            static const char rbrace = '}';
                            lit = AllocNode(cx->msg);
                            if (!lit) {
                                return RoboOutOfMemory;
                            }
                            lit->kind = MF_LIT;
                            lit->lit = &rbrace;
                            lit->litLen = 1;
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
                if (d == '#' && cx->inPlural) {
                    break;
                }
                cx->i++;
            }
            if (cx->i > start) {
                lit = AllocNode(cx->msg);
                if (!lit) {
                    return RoboOutOfMemory;
                }
                lit->kind = MF_LIT;
                lit->lit = cx->msg->templateCopy + start;
                lit->litLen = cx->i - start;
                if (!head) {
                    head = lit;
                } else {
                    tail->next = lit;
                }
                tail = lit;
            }
        }
    }
    *outHead = head;
    return RoboOk;
}

void I18nMfDestroy(I18nMfMessage *msg)
{
    if (!msg) {
        return;
    }
    RoboFree(msg->templateCopy);
    RoboFree(msg->nodePool);
    RoboFree(msg->branchPool);
    RoboFree(msg);
}

static void FillDiagFromCx(const ParseCtx *cx,
                              const char *tmpl,
                              size_t tmplLen,
                              Result result,
                              I18nMfDiagnostic *diag)
{
    size_t off;
    uint32_t line = 1u;
    uint32_t col = 1u;
    size_t i;

    if (!diag) {
        return;
    }
    I18nMfDiagnosticReset(diag);
    diag->result = result;
    if (!cx || !cx->hasErr) {
        if (result == RoboOk) {
            diag->code = RoboI18nMfDiagOk;
        } else if (result == RoboNotSupported) {
            diag->code = RoboI18nMfDiagUnsupportedVersion;
        } else {
            diag->code = RoboI18nMfDiagMalformed;
        }
        return;
    }
    diag->code = (I18nMfDiagCode)cx->errCode;
    diag->severity = RoboI18nMfSevError;
    diag->byteOffset = cx->errOffset;
    strncpy(diag->construct, cx->errConstruct, sizeof(diag->construct) - 1u);
    strncpy(diag->argumentName, cx->errArg, sizeof(diag->argumentName) - 1u);
    strncpy(diag->explanation, cx->errMsg, sizeof(diag->explanation) - 1u);
    off = cx->errOffset;
    if (tmpl && off <= tmplLen) {
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

static int ConsumeVersionDirective(const char **ps, size_t *plen, uint32_t *outVer,
                                     ParseCtx *cx)
{
    const char *s = *ps;
    size_t len = *plen;
    size_t i = 0;
    uint32_t ver = 0;

    if (len < 10u || !(s[0] == '#' && s[1] == '!' && s[2] == 'r')) {
        *outVer = RoboI18nMfSyntaxVersion;
        return 1;
    }
    /* #!robo-mf/N\n */
    if (len < 11u || memcmp(s, "#!robo-mf/", 10) != 0) {
        MfFail(cx, RoboI18nMfDiagUnsupportedVersion, "version", NULL,
                "malformed version directive");
        return 0;
    }
    i = 10;
    if (i >= len || s[i] < '0' || s[i] > '9') {
        MfFail(cx, RoboI18nMfDiagUnsupportedVersion, "version", NULL,
                "missing version number");
        return 0;
    }
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        ver = ver * 10u + (uint32_t)(s[i] - '0');
        if (ver > 1000u) {
            MfFail(cx, RoboI18nMfDiagUnsupportedVersion, "version", NULL,
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
        MfFail(cx, RoboI18nMfDiagUnsupportedVersion, "version", NULL,
                "version directive must end line");
        return 0;
    }
    if (ver != RoboI18nMfSyntaxVersion) {
        MfFail(cx, RoboI18nMfDiagUnsupportedVersion, "version", NULL,
                "unsupported message-format version");
        return 0;
    }
    *outVer = ver;
    *ps = s + i;
    *plen = len - i;
    return 1;
}

Result I18nMfParseEx(const char *utf8,
                                    size_t len,
                                    const I18nMfLimits *limits,
                                    I18nMfDiagnostic *diag,
                                    I18nMfMessage **out)
{
    I18nMfMessage *msg;
    ParseCtx cx;
    MfNode *root = NULL;
    Result pr;
    I18nMfLimits lim;
    const char *body;
    size_t bodyLen;
    uint32_t syntaxVer = RoboI18nMfSyntaxVersion;
    uint32_t templateMax;

    if (!utf8 || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    I18nMfLimitsInit(&lim);
    if (limits) {
        if (limits->templateMax) {
            lim.templateMax = limits->templateMax;
        }
        if (limits->nestingMax) {
            lim.nestingMax = limits->nestingMax;
        }
        if (limits->branchesMax) {
            lim.branchesMax = limits->branchesMax;
        }
        if (limits->argsMax) {
            lim.argsMax = limits->argsMax;
        }
        if (limits->outputMax) {
            lim.outputMax = limits->outputMax;
        }
        if (limits->diagnosticsMax) {
            lim.diagnosticsMax = limits->diagnosticsMax;
        }
    }
    templateMax = lim.templateMax ? lim.templateMax : RoboI18nMfTemplateMax;

    memset(&cx, 0, sizeof(cx));
    if (len > templateMax) {
        MfFail(&cx, RoboI18nMfDiagTooLarge, "template", NULL, "template too large");
        FillDiagFromCx(&cx, utf8, len, RoboParseError, diag);
        return RoboParseError;
    }
    if (!Utf8Ok(utf8, len)) {
        MfFail(&cx, RoboI18nMfDiagInvalidUtf8, "template", NULL, "invalid UTF-8");
        FillDiagFromCx(&cx, utf8, len, RoboParseError, diag);
        return RoboParseError;
    }

    body = utf8;
    bodyLen = len;
    if (!ConsumeVersionDirective(&body, &bodyLen, &syntaxVer, &cx)) {
        FillDiagFromCx(&cx, utf8, len, RoboNotSupported, diag);
        return RoboNotSupported;
    }

    msg = (I18nMfMessage *)RoboCalloc(1, sizeof(*msg));
    if (!msg) {
        return RoboOutOfMemory;
    }
    msg->templateCopy = (char *)Malloc(bodyLen + 1u);
    if (!msg->templateCopy) {
        RoboFree(msg);
        return RoboOutOfMemory;
    }
    if (bodyLen) {
        memcpy(msg->templateCopy, body, bodyLen);
    }
    msg->templateCopy[bodyLen] = '\0';
    msg->templateLen = bodyLen;
    msg->syntaxVersion = syntaxVer;
    msg->nodeCap = 2048u;
    msg->branchCap = 256u;
    msg->nodePool = (MfNode *)RoboCalloc(msg->nodeCap, sizeof(MfNode));
    msg->branchPool = (MfBranch *)RoboCalloc(msg->branchCap, sizeof(MfBranch));
    if (!msg->nodePool || !msg->branchPool) {
        I18nMfDestroy(msg);
        return RoboOutOfMemory;
    }

    memset(&cx, 0, sizeof(cx));
    cx.s = msg->templateCopy;
    cx.len = bodyLen;
    cx.msg = msg;
    cx.nestingMax = lim.nestingMax ? lim.nestingMax : RoboI18nMfNestingMax;
    cx.branchesMax = lim.branchesMax ? lim.branchesMax : RoboI18nMfBranchesMax;
    pr = ParsePattern(&cx, &root);
    if (pr != RoboOk) {
        if (!cx.hasErr) {
            MfFail(&cx, RoboI18nMfDiagMalformed, "pattern", NULL, "parse failed");
        }
        FillDiagFromCx(&cx, msg->templateCopy, bodyLen, pr, diag);
        I18nMfDestroy(msg);
        return pr == RoboOutOfMemory ? RoboOutOfMemory : RoboParseError;
    }
    SkipWs(&cx);
    if (cx.i != cx.len) {
        MfFail(&cx, RoboI18nMfDiagMalformed, "pattern", NULL, "unexpected trailing input");
        FillDiagFromCx(&cx, msg->templateCopy, bodyLen, RoboParseError, diag);
        I18nMfDestroy(msg);
        return RoboParseError;
    }
    msg->root = root;
    *out = msg;
    if (diag) {
        I18nMfDiagnosticReset(diag);
        diag->result = RoboOk;
        diag->code = RoboI18nMfDiagOk;
    }
    return RoboOk;
}

Result I18nMfParse(const char *utf8, size_t len, I18nMfMessage **out)
{
    return I18nMfParseEx(utf8, len, NULL, NULL, out);
}

Result I18nMfValidate(const char *utf8, size_t len)
{
    I18nMfMessage *msg = NULL;
    Result r = I18nMfParse(utf8, len, &msg);
    if (r == RoboOk) {
        I18nMfDestroy(msg);
    }
    return r;
}

static Result AnalyzeAdd(I18nMfContract *c, const char *name, uint32_t usage)
{
    uint32_t i;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->args[i].name, name) == 0) {
            uint32_t merged = c->args[i].usage | usage;
            if (!I18nMfUsageCompatible(c->args[i].usage, usage)) {
                return RoboParseError;
            }
            c->args[i].usage = merged;
            return RoboOk;
        }
    }
    if (c->count >= RoboI18nMfArgsMax) {
        return RoboParseError;
    }
    strncpy(c->args[c->count].name, name, RoboI18nMfNameMax - 1u);
    c->args[c->count].name[RoboI18nMfNameMax - 1u] = '\0';
    c->args[c->count].usage = usage;
    c->count++;
    return RoboOk;
}

static Result AnalyzeNodes(const MfNode *node, I18nMfContract *c)
{
    for (; node; node = node->next) {
        Result r = RoboOk;
        const MfBranch *b;
        switch (node->kind) {
        case MF_LIT:
        case MF_POUND:
            break;
        case MF_SIMPLE:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsageString);
            break;
        case MF_NUMBER:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsageNumber);
            break;
        case MF_DATE:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsageDate);
            break;
        case MF_TIME:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsageTime);
            break;
        case MF_SELECT:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsageSelect);
            if (r != RoboOk) {
                return r;
            }
            for (b = node->branches; b; b = b->next) {
                r = AnalyzeNodes(b->body, c);
                if (r != RoboOk) {
                    return r;
                }
            }
            break;
        case MF_PLURAL:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsagePlural | RoboI18nMfUsageNumber);
            if (r != RoboOk) {
                return r;
            }
            for (b = node->branches; b; b = b->next) {
                r = AnalyzeNodes(b->body, c);
                if (r != RoboOk) {
                    return r;
                }
            }
            break;
        case MF_ORDINAL:
            r = AnalyzeAdd(c, node->name, RoboI18nMfUsageOrdinal | RoboI18nMfUsageNumber);
            if (r != RoboOk) {
                return r;
            }
            for (b = node->branches; b; b = b->next) {
                r = AnalyzeNodes(b->body, c);
                if (r != RoboOk) {
                    return r;
                }
            }
            break;
        default:
            return RoboError;
        }
        if (r != RoboOk) {
            return r;
        }
    }
    return RoboOk;
}

static void SortContractArgs(I18nMfContract *c)
{
    uint32_t i, j;
    for (i = 0; i + 1u < c->count; i++) {
        for (j = i + 1u; j < c->count; j++) {
            if (strcmp(c->args[j].name, c->args[i].name) < 0) {
                I18nMfArgReq tmp = c->args[i];
                c->args[i] = c->args[j];
                c->args[j] = tmp;
            }
        }
    }
}

Result I18nMfAnalyze(const I18nMfMessage *msg,
                                   I18nMfContract *out)
{
    Result r;
    if (!msg || !out) {
        return RoboInvalidArgument;
    }
    I18nMfContractReset(out);
    out->syntaxVersion = msg->syntaxVersion ? msg->syntaxVersion : RoboI18nMfSyntaxVersion;
    r = AnalyzeNodes(msg->root, out);
    if (r != RoboOk) {
        return r == RoboParseError ? RoboParseError : r;
    }
    SortContractArgs(out);
    return RoboOk;
}

Result I18nMfAnalyzeTemplate(const char *utf8,
                                            size_t len,
                                            const I18nMfLimits *limits,
                                            I18nMfDiagnostic *diag,
                                            I18nMfContract *out)
{
    I18nMfMessage *msg = NULL;
    Result r;
    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nMfParseEx(utf8, len, limits, diag, &msg);
    if (r != RoboOk) {
        return r;
    }
    r = I18nMfAnalyze(msg, out);
    if (r != RoboOk && diag && !diag->explanation[0]) {
        diag->result = r;
        diag->code = RoboI18nMfDiagTypeConflict;
        diag->severity = RoboI18nMfSevError;
        strncpy(diag->explanation, "incompatible argument usages in template",
                sizeof(diag->explanation) - 1u);
    }
    I18nMfDestroy(msg);
    return r;
}

/* ---------- formatting ---------- */


static const I18nMfArg *FindArg(const I18nMfArgs *args,
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

static Result AppendBytes(FmtCtx *fx, const char *p, size_t n)
{
    if (fx->len + n > RoboI18nMfOutputMax) {
        return RoboError;
    }
    if (fx->buf && fx->cap > 0) {
        if (fx->len + n + 1u > fx->cap) {
            fx->len += n;
            return RoboError;
        }
        if (n) {
            memcpy(fx->buf + fx->len, p, n);
        }
    }
    fx->len += n;
    return RoboOk;
}

static Result ArgToDecimal(const I18nMfArg *a, I18nDecimal *out)
{
    if (!a || !out) {
        return RoboInvalidArgument;
    }
    switch (a->kind) {
    case RoboI18nMfValI64:
        return I18nDecimalFromI64(a->u.i64, out);
    case RoboI18nMfValU64:
        return I18nDecimalFromU64(a->u.u64, out);
    case RoboI18nMfValDecimal:
        *out = a->u.decimal;
        return RoboOk;
    case RoboI18nMfValDouble:
        return I18nDecimalFromDouble(a->u.dbl, out);
    case RoboI18nMfValBool:
        return I18nDecimalFromI64(a->u.boolean ? 1 : 0, out);
    default:
        return RoboInvalidArgument;
    }
}

static Result ArgSelectKey(const I18nMfArg *a, char *buf, size_t cap)
{
    size_t n;
    if (!a || !buf || cap < 2) {
        return RoboInvalidArgument;
    }
    switch (a->kind) {
    case RoboI18nMfValString: {
        const char *p = a->u.str.ptr ? a->u.str.ptr : "";
        n = a->u.str.len ? a->u.str.len : strlen(p);
        if (n + 1 > cap) {
            return RoboError;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        return RoboOk;
    }
    case RoboI18nMfValBool:
        strncpy(buf, a->u.boolean ? "true" : "false", cap - 1);
        buf[cap - 1] = '\0';
        return RoboOk;
    case RoboI18nMfValI64: {
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
            return RoboError;
        }
        memcpy(buf, t, n + 1);
        return RoboOk;
    }
    default:
        return RoboInvalidArgument;
    }
}

static int DecimalIsExactI64(const I18nDecimal *d, int64_t *out)
{
    /* Use plural operands path: only match exact when no fraction digits visible */
    I18nPluralOperands ops;
    if (I18nPluralOperandsFromDecimal(d, &ops) != RoboOk) {
        return 0;
    }
    if (ops.v != 0) {
        return 0;
    }
    {
        int64_t v = 0;
        size_t i;
        for (i = 0; i < ops.iLen; i++) {
            int digit = ops.iDigits[i] - '0';
            if (v > (INT64_MAX - digit) / 10) {
                return 0;
            }
            v = v * 10 + digit;
        }
        if (ops.negativeInput) {
            v = -v;
        }
        *out = v;
        return 1;
    }
}

static Result FormatNodes(FmtCtx *fx, const MfNode *node);

static Result FormatNumberArg(FmtCtx *fx, const I18nMfArg *a)
{
    I18nNumberFormatter nf;
    I18nDecimal dec;
    char tmp[256];
    size_t n = 0;
    Result r;

    if (ArgToDecimal(a, &dec) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (I18nNumberFormatterInit(&nf, fx->locale, NULL) != RoboOk) {
        return RoboError;
    }
    r = I18nNumberFormat(&nf, &dec, tmp, sizeof(tmp), &n);
    I18nNumberFormatterDestroy(&nf);
    if (r != RoboOk) {
        return r;
    }
    return AppendBytes(fx, tmp, n);
}

static Result FormatDateArg(FmtCtx *fx,
                                     const I18nMfArg *a,
                                     MfNodeKind kind,
                                     I18nCalFormatStyle style)
{
    I18nDatetimeFormatter df;
    I18nDtfmtOptions opts;
    I18nCalendar cal;
    char tmp[256];
    size_t n = 0;
    Result r;

    I18nDtfmtOptionsInit(&opts);
    if (kind == MF_DATE) {
        opts.dateStyle = style;
    } else {
        opts.timeStyle = style;
    }
    if (I18nCalendarFromId("gregorian", 9, &cal) != RoboOk) {
        return RoboError;
    }
    if (I18nDatetimeFormatterInit(
            &df, fx->locale, &cal, NULL,
            kind == MF_DATE ? RoboI18nDtfmtDate : RoboI18nDtfmtTime, &opts) !=
        RoboOk) {
        return RoboError;
    }
    if (kind == MF_DATE) {
        I18nDate date;
        if (a->kind == RoboI18nMfValDate) {
            date = a->u.date;
        } else if (a->kind == RoboI18nMfValDatetime) {
            date = a->u.datetime.date;
        } else {
            I18nDatetimeFormatterDestroy(&df);
            return RoboInvalidArgument;
        }
        r = I18nDatetimeFormatDate(&df, &date, tmp, sizeof(tmp), &n);
    } else {
        I18nTime time;
        if (a->kind == RoboI18nMfValTime) {
            time = a->u.time;
        } else if (a->kind == RoboI18nMfValDatetime) {
            time = a->u.datetime.time;
        } else {
            I18nDatetimeFormatterDestroy(&df);
            return RoboInvalidArgument;
        }
        r = I18nDatetimeFormatTime(&df, &time, tmp, sizeof(tmp), &n);
    }
    I18nDatetimeFormatterDestroy(&df);
    if (r != RoboOk) {
        return r;
    }
    return AppendBytes(fx, tmp, n);
}

static const MfBranch *PickSelectBranch(const MfNode *node, const char *key)
{
    const MfBranch *b;
    const MfBranch *other = NULL;
    for (b = node->branches; b; b = b->next) {
        if (b->isExact) {
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

static const MfBranch *PickPluralBranch(const MfNode *node,
                                             const I18nLocale *locale,
                                             const I18nDecimal *dec,
                                             int ordinal)
{
    const MfBranch *b;
    int64_t exact = 0;
    const MfBranch *other = NULL;
    I18nPluralCategory cat = RoboI18nPluralOther;
    const char *catName;

    if (DecimalIsExactI64(dec, &exact)) {
        for (b = node->branches; b; b = b->next) {
            if (b->isExact && b->exactValue == exact) {
                return b;
            }
        }
    }
    if (ordinal) {
        if (I18nPluralOrdinal(locale, dec, &cat) != RoboOk) {
            cat = RoboI18nPluralOther;
        }
    } else {
        if (I18nPluralCardinal(locale, dec, &cat) != RoboOk) {
            cat = RoboI18nPluralOther;
        }
    }
    catName = I18nPluralCategoryName(cat);
    for (b = node->branches; b; b = b->next) {
        if (b->isExact) {
            continue;
        }
        if (strcmp(b->key, "other") == 0) {
            other = b;
        }
        if (catName && strcmp(b->key, catName) == 0) {
            return b;
        }
    }
    return other;
}

static Result FormatNodes(FmtCtx *fx, const MfNode *node)
{
    if (fx->depth > (int)RoboI18nMfNestingMax) {
        return RoboParseError;
    }
    for (; node; node = node->next) {
        Result r;
        switch (node->kind) {
        case MF_LIT:
            r = AppendBytes(fx, node->lit, node->litLen);
            if (r != RoboOk) {
                return r;
            }
            break;
        case MF_POUND: {
            if (!fx->pluralArg) {
                return RoboError;
            }
            r = FormatNumberArg(fx, fx->pluralArg);
            if (r != RoboOk) {
                return r;
            }
            break;
        }
        case MF_SIMPLE: {
            const I18nMfArg *a = FindArg(fx->args, node->name);
            size_t n;
            const char *p;
            if (!a) {
                return RoboNotFound;
            }
            if (a->kind == RoboI18nMfValString) {
                p = a->u.str.ptr ? a->u.str.ptr : "";
                n = a->u.str.len ? a->u.str.len : strlen(p);
                r = AppendBytes(fx, p, n);
            } else if (a->kind == RoboI18nMfValI64 || a->kind == RoboI18nMfValU64 ||
                       a->kind == RoboI18nMfValDecimal || a->kind == RoboI18nMfValDouble ||
                       a->kind == RoboI18nMfValBool) {
                r = FormatNumberArg(fx, a);
            } else {
                return RoboInvalidArgument;
            }
            if (r != RoboOk) {
                return r;
            }
            break;
        }
        case MF_NUMBER: {
            const I18nMfArg *a = FindArg(fx->args, node->name);
            if (!a) {
                return RoboNotFound;
            }
            r = FormatNumberArg(fx, a);
            if (r != RoboOk) {
                return r;
            }
            break;
        }
        case MF_DATE:
        case MF_TIME: {
            const I18nMfArg *a = FindArg(fx->args, node->name);
            if (!a) {
                return RoboNotFound;
            }
            r = FormatDateArg(fx, a, node->kind, node->style);
            if (r != RoboOk) {
                return r;
            }
            break;
        }
        case MF_SELECT: {
            const I18nMfArg *a = FindArg(fx->args, node->name);
            char key[128];
            const MfBranch *br;
            if (!a) {
                return RoboNotFound;
            }
            if (ArgSelectKey(a, key, sizeof(key)) != RoboOk) {
                return RoboInvalidArgument;
            }
            br = PickSelectBranch(node, key);
            if (!br) {
                return RoboNotFound;
            }
            fx->depth++;
            r = FormatNodes(fx, br->body);
            fx->depth--;
            if (r != RoboOk) {
                return r;
            }
            break;
        }
        case MF_PLURAL:
        case MF_ORDINAL: {
            const I18nMfArg *a = FindArg(fx->args, node->name);
            I18nDecimal dec;
            const MfBranch *br;
            const I18nMfArg *prev;
            if (!a) {
                return RoboNotFound;
            }
            if (ArgToDecimal(a, &dec) != RoboOk) {
                return RoboInvalidArgument;
            }
            br = PickPluralBranch(node, fx->locale, &dec, node->kind == MF_ORDINAL);
            if (!br) {
                return RoboNotFound;
            }
            prev = fx->pluralArg;
            fx->pluralArg = a;
            fx->depth++;
            r = FormatNodes(fx, br->body);
            fx->depth--;
            fx->pluralArg = prev;
            if (r != RoboOk) {
                return r;
            }
            break;
        }
        default:
            return RoboError;
        }
    }
    return RoboOk;
}

Result I18nMfFormat(const I18nMfMessage *msg,
                                  const I18nLocale *locale,
                                  const I18nMfArgs *args,
                                  char *out,
                                  size_t outCap,
                                  size_t *outLen)
{
    FmtCtx fx;
    Result r;

    if (!msg || !locale || !outLen) {
        return RoboInvalidArgument;
    }
    if (args && args->count > RoboI18nMfArgsMax) {
        return RoboInvalidArgument;
    }
    memset(&fx, 0, sizeof(fx));
    fx.locale = locale;
    fx.args = args;
    fx.buf = out;
    fx.cap = outCap;
    r = FormatNodes(&fx, msg->root);
    *outLen = fx.len;
    if (r == RoboOk) {
        if (out && outCap > 0) {
            if (fx.len + 1u > outCap) {
                *outLen = fx.len + 1u;
                return RoboError;
            }
            out[fx.len] = '\0';
        }
    } else if (r == RoboError && fx.len + 1u > outCap && outCap > 0) {
        *outLen = fx.len + 1u;
    }
    return r;
}

Result I18nMfFormatToString(const I18nMfMessage *msg,
                                            const I18nLocale *locale,
                                            const I18nMfArgs *args,
                                            String *out)
{
    char buf[2048];
    size_t n = 0;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = I18nMfFormat(msg, locale, args, buf, sizeof(buf), &n);
    if (r != RoboOk) {
        return r;
    }
    return StringAppend(out, buf, n);
}

Result I18nMfFormatTemplate(const char *utf8,
                                           size_t len,
                                           const I18nLocale *locale,
                                           const I18nMfArgs *args,
                                           char *out,
                                           size_t outCap,
                                           size_t *outLen)
{
    I18nMfMessage *msg = NULL;
    Result r = I18nMfParse(utf8, len, &msg);
    if (r != RoboOk) {
        return r;
    }
    r = I18nMfFormat(msg, locale, args, out, outCap, outLen);
    I18nMfDestroy(msg);
    return r;
}

Result I18nMessageFormatMf(const I18nCatalog *catalog,
                                          const char *messageId,
                                          size_t idLen,
                                          const char *context,
                                          size_t contextLen,
                                          const I18nLocale *locale,
                                          const I18nMfArgs *args,
                                          const I18nMsgOptions *options,
                                          char *out,
                                          size_t outCap,
                                          size_t *outLen)
{
    char tmpl[RoboI18nMfTemplateMax];
    size_t n = 0;
    Result r;

    r = I18nMessageGet(catalog, messageId, idLen, context, contextLen, locale, NULL,
                              options, tmpl, sizeof(tmpl), &n);
    if (r != RoboOk) {
        return r;
    }
    return I18nMfFormatTemplate(tmpl, n, locale, args, out, outCap, outLen);
}

static void SetName(I18nMfArg *a, const char *name)
{
    size_t n;
    memset(a, 0, sizeof(*a));
    if (!name) {
        return;
    }
    n = strlen(name);
    if (n >= RoboI18nMfNameMax) {
        n = RoboI18nMfNameMax - 1u;
    }
    memcpy(a->name, name, n);
    a->name[n] = '\0';
}

void I18nMfArgSetString(I18nMfArg *a,
                                 const char *name,
                                 const char *utf8,
                                 size_t len)
{
    SetName(a, name);
    a->kind = RoboI18nMfValString;
    a->u.str.ptr = utf8;
    a->u.str.len = len;
}

void I18nMfArgSetI64(I18nMfArg *a, const char *name, int64_t v)
{
    SetName(a, name);
    a->kind = RoboI18nMfValI64;
    a->u.i64 = v;
}

void I18nMfArgSetU64(I18nMfArg *a, const char *name, uint64_t v)
{
    SetName(a, name);
    a->kind = RoboI18nMfValU64;
    a->u.u64 = v;
}

void I18nMfArgSetDecimal(I18nMfArg *a,
                                  const char *name,
                                  const I18nDecimal *d)
{
    SetName(a, name);
    a->kind = RoboI18nMfValDecimal;
    if (d) {
        a->u.decimal = *d;
    }
}

void I18nMfArgSetBool(I18nMfArg *a, const char *name, int v)
{
    SetName(a, name);
    a->kind = RoboI18nMfValBool;
    a->u.boolean = v ? 1 : 0;
}

void I18nMfArgSetDate(I18nMfArg *a,
                               const char *name,
                               const I18nDate *d)
{
    SetName(a, name);
    a->kind = RoboI18nMfValDate;
    if (d) {
        a->u.date = *d;
    }
}

void I18nMfArgSetTime(I18nMfArg *a,
                               const char *name,
                               const I18nTime *t)
{
    SetName(a, name);
    a->kind = RoboI18nMfValTime;
    if (t) {
        a->u.time = *t;
    }
}
