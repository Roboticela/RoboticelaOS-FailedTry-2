/*
 * RoboCSS object model (Phase 18.2) — see RoboCSSOM.h.
 *
 * Two halves and one idea each:
 *
 *   serialization   a sink that counts as it writes, so a writer can be asked
 *                   for its length, be given that many bytes, and produce the
 *                   text — with no partial rendering left behind when the
 *                   second attempt is still too small.
 *   the declaration one owned copy per property: the name's bytes, the value's
 *                   bytes (a private source the value borrows from), and the
 *                   parsed tree over them. Mutation is then safe by
 *                   construction — nothing a caller passes in is still needed
 *                   after the call returns.
 *
 * Everything else is the CSSOM's own rule, stated where it is applied rather
 * than here: a property set twice keeps its first position, names compare
 * ASCII case-insensitively, an empty value is a removal, and a selector name
 * that could be misread is escaped on the way out.
 */

#include "RoboCSS/RoboCSSOM.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The sink                                                            */
/* ------------------------------------------------------------------ */

/**
 * A destination that remembers how much it *would* have written.
 *
 * `buffer == NULL` is the measuring pass: nothing is stored and nothing
 * overflows, and `length` is the answer. When a buffer is present one byte is
 * held back for the NUL terminator, so `capacity` is the count a caller passes
 * to `sizeof` of a char array and the text always fits NUL-terminated.
 */
typedef struct OmSink {
    char *buffer;
    size_t capacity;
    size_t length;
    int overflow;
} OmSink;

static void OmSinkInit(OmSink *sink, char *buffer, size_t capacity)
{
    sink->buffer = buffer;
    sink->capacity = capacity;
    sink->length = 0u;
    sink->overflow = 0;
}

static void OmPut(OmSink *sink, const char *bytes, size_t length)
{
    if (length == 0u) {
        return;
    }
    if (sink->buffer) {
        if (sink->length + length < sink->capacity) {
            memcpy(sink->buffer + sink->length, bytes, length);
        } else {
            sink->overflow = 1;
        }
    }
    sink->length += length;
}

static void OmPutChar(OmSink *sink, char c)
{
    OmPut(sink, &c, 1u);
}

static void OmPutSlice(OmSink *sink, CssSlice slice)
{
    if (slice.bytes && slice.length > 0u) {
        OmPut(sink, (const char *)slice.bytes, slice.length);
    }
}

/**
 * Run a sink-first writer twice: once to measure, then once to fill.
 *
 * Two passes rather than one is what lets a too-small buffer be left
 * *untouched* rather than holding a truncated rendering the caller cannot tell
 * from a complete one — the single-pass version would have to write bytes it
 * then declared unwritten. The second pass is cheap: the first bounded the
 * work, and a buffer that fits is the case a caller asked for.
 */
#define OM_EMIT(writer, argument)                                             \
    do {                                                                      \
        OmSink omMeasure_;                                                    \
                                                                              \
        OmSinkInit(&omMeasure_, NULL, 0u);                                    \
        writer(&omMeasure_, argument);                                        \
        *outLength = omMeasure_.length;                                       \
        if (!buffer) {                                                        \
            return RoboOk;                                                    \
        }                                                                     \
        if (omMeasure_.length + 1u > capacity) {                              \
            return RoboNotSupported;                                          \
        }                                                                     \
        {                                                                     \
            OmSink omOut_;                                                    \
                                                                              \
            OmSinkInit(&omOut_, buffer, capacity);                            \
            writer(&omOut_, argument);                                        \
            buffer[omOut_.length] = '\0';                                     \
        }                                                                     \
        return RoboOk;                                                        \
    } while (0)

/** The validation every public writer shares. */
static Result OmCheckTarget(const char *buffer, size_t capacity,
                            size_t *outLength, CssDiag *diag)
{
    if (!outLength) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "output length is NULL");
    }
    if (!buffer && capacity != 0u) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "output buffer is NULL with non-zero capacity");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Identifier escaping                                                 */
/* ------------------------------------------------------------------ */

/** Non-zero when @p c may appear in an identifier and needs no escape. */
static int OmIdentByteIsPlain(unsigned char c)
{
    return c == (unsigned char)'_' || c == (unsigned char)'-' ||
           (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
           (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
           (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
           c >= 0x80u;
}

/** A hex escape (`\3a `), which is always legal inside or at the start of one. */
static void OmPutHexEscape(OmSink *sink, unsigned char c)
{
    static const char digits[] = "0123456789abcdef";
    char out[8];
    char reversed[4];
    size_t digitsWritten = 0u;
    size_t outLength = 0u;
    unsigned value = (unsigned)c;

    while (value != 0u) {
        reversed[digitsWritten++] = digits[value & 0xFu];
        value >>= 4;
    }
    out[outLength++] = '\\';
    if (digitsWritten == 0u) {
        out[outLength++] = '0';
    } else {
        while (digitsWritten > 0u) {
            out[outLength++] = reversed[--digitsWritten];
        }
    }
    out[outLength++] = ' ';
    OmPut(sink, out, outLength);
}

/**
 * Write @p name as an identifier, escaping whatever must be escaped.
 *
 * A name that came out of the selector parser has already had its escapes
 * resolved (`.\66 oo` is the name `foo`), so writing it back raw would turn a
 * legitimate class into a different one the moment it held a `:` or a space.
 * Escaping here is what makes the output re-parse to the same selector, which
 * is the only promise this rendering makes.
 */
static void OmPutIdentifier(OmSink *sink, CssSlice name)
{
    size_t index;

    for (index = 0u; index < name.length; index++) {
        unsigned char c = name.bytes[index];
        int plain = OmIdentByteIsPlain(c);

        /* An identifier may not begin with a digit, nor with `-` followed by
         * one — both would read as a number. */
        if (index == 0u && c >= (unsigned char)'0' && c <= (unsigned char)'9') {
            plain = 0;
        }
        if (index == 0u && c == (unsigned char)'-' && name.length > 1u &&
            name.bytes[1] >= (unsigned char)'0' && name.bytes[1] <= (unsigned char)'9') {
            plain = 0;
        }
        if (plain) {
            OmPutChar(sink, (char)c);
        } else {
            OmPutHexEscape(sink, c);
        }
    }
}

/** Write @p bytes as a CSS string literal, escaping `"` and `\`. */
static void OmPutStringLiteral(OmSink *sink, CssSlice text)
{
    size_t index;

    OmPutChar(sink, '"');
    for (index = 0u; index < text.length; index++) {
        unsigned char c = text.bytes[index];

        if (c == (unsigned char)'"' || c == (unsigned char)'\\') {
            OmPutChar(sink, '\\');
            OmPutChar(sink, (char)c);
        } else if (c == (unsigned char)'\n') {
            OmPut(sink, "\\a ", 3u);
        } else {
            OmPutChar(sink, (char)c);
        }
    }
    OmPutChar(sink, '"');
}

/* ------------------------------------------------------------------ */
/* Component values                                                    */
/* ------------------------------------------------------------------ */

static int OmIsTriviaValue(const CssValue *value)
{
    return value && value->kind == RoboCssValueKindToken &&
           (value->token.type == RoboCssTokenWhitespace ||
            value->token.type == RoboCssTokenComment);
}

static char OmBlockOpenChar(CssTokenType type)
{
    switch (type) {
    case RoboCssTokenLeftParen:
        return '(';
    case RoboCssTokenLeftSquare:
        return '[';
    case RoboCssTokenLeftCurly:
        return '{';
    default:
        return '\0';
    }
}

static char OmBlockCloseChar(CssTokenType type)
{
    switch (type) {
    case RoboCssTokenRightParen:
        return ')';
    case RoboCssTokenRightSquare:
        return ']';
    case RoboCssTokenRightCurly:
        return '}';
    default:
        return '\0';
    }
}

static void OmWriteValue(OmSink *sink, const CssValue *value);

/**
 * Write a container's children with CSS's own separation rule: a single space
 * where whitespace separated two components, a single space after a comma (so
 * `1,2,3` renders `1, 2, 3`), and nothing where the author wrote nothing —
 * which keeps `calc(1px+2px)` from becoming a different expression.
 */
static void OmWriteNodes(OmSink *sink, const CssValue *container)
{
    size_t index;
    int wroteAny = 0;
    int pendingSpace = 0;
    int previousWasComma = 0;

    for (index = 0u; index < CssValueChildCount(container); index++) {
        const CssValue *child = CssValueChild(container, index);
        int isComma;

        if (OmIsTriviaValue(child)) {
            if (child->token.type == RoboCssTokenWhitespace) {
                pendingSpace = 1;
            }
            continue;
        }
        if (wroteAny && (pendingSpace || previousWasComma)) {
            OmPutChar(sink, ' ');
        }
        OmWriteValue(sink, child);
        wroteAny = 1;
        pendingSpace = 0;
        isComma = child->kind == RoboCssValueKindToken &&
                  child->token.type == RoboCssTokenComma;
        previousWasComma = isComma;
    }
}

static void OmWriteValue(OmSink *sink, const CssValue *value)
{
    if (!value || value->kind == RoboCssValueKindNone) {
        return;
    }
    switch (value->kind) {
    case RoboCssValueKindToken:
        OmPutSlice(sink, value->token.text);
        return;
    case RoboCssValueKindFunction:
        OmPutSlice(sink, value->name);
        OmPutChar(sink, '(');
        OmWriteNodes(sink, value);
        OmPutChar(sink, ')');
        return;
    case RoboCssValueKindBlock: {
        char open = OmBlockOpenChar(value->open);
        char close = OmBlockCloseChar(value->close);

        if (close == '\0') {
            close = (open == '(') ? ')' : (open == '[' ? ']' : (open == '{' ? '}' : '\0'));
        }
        if (open != '\0') {
            OmPutChar(sink, open);
        }
        OmWriteNodes(sink, value);
        if (open != '\0' && close != '\0') {
            OmPutChar(sink, close);
        }
        return;
    }
    case RoboCssValueKindList:
        OmWriteNodes(sink, value);
        return;
    default:
        return;
    }
}

Result CssOMWriteValue(const CssValue *value,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteValue, value);
}

/* ------------------------------------------------------------------ */
/* Selectors                                                           */
/* ------------------------------------------------------------------ */

static const char *OmAttributeMatchText(CssAttributeMatch match)
{
    switch (match) {
    case RoboCssAttributeMatchEquals:
        return "=";
    case RoboCssAttributeMatchIncludes:
        return "~=";
    case RoboCssAttributeMatchDash:
        return "|=";
    case RoboCssAttributeMatchPrefix:
        return "^=";
    case RoboCssAttributeMatchSuffix:
        return "$=";
    case RoboCssAttributeMatchSubstring:
        return "*=";
    case RoboCssAttributeMatchNone:
    default:
        return "";
    }
}

static const char *OmCombinatorText(CssCombinator combinator)
{
    switch (combinator) {
    case RoboCssCombinatorChild:
        return " > ";
    case RoboCssCombinatorNextSibling:
        return " + ";
    case RoboCssCombinatorSubsequentSibling:
        return " ~ ";
    case RoboCssCombinatorColumn:
        return " || ";
    case RoboCssCombinatorDescendant:
    case RoboCssCombinatorNone:
    default:
        return " ";
    }
}

static void OmWriteSelectorList(OmSink *sink, const CssSelectorList *list);

static void OmWriteSimpleSelector(OmSink *sink,
                                  const CssCompoundSelector *compound,
                                  size_t index)
{
    const CssSimpleSelector *simple = CssCompoundSelectorAt(compound, index);

    if (!simple) {
        return;
    }
    switch (simple->kind) {
    case RoboCssSimpleSelectorType:
        OmPutIdentifier(sink, simple->name);
        return;
    case RoboCssSimpleSelectorUniversal:
        OmPutChar(sink, '*');
        return;
    case RoboCssSimpleSelectorClass:
        OmPutChar(sink, '.');
        OmPutIdentifier(sink, simple->name);
        return;
    case RoboCssSimpleSelectorId:
        OmPutChar(sink, '#');
        OmPutIdentifier(sink, simple->name);
        return;
    case RoboCssSimpleSelectorAttribute:
        OmPutChar(sink, '[');
        OmPutIdentifier(sink, simple->attribute);
        OmPut(sink, OmAttributeMatchText(simple->match), strlen(OmAttributeMatchText(simple->match)));
        if (simple->match != RoboCssAttributeMatchNone) {
            if (simple->valueWasString) {
                OmPutStringLiteral(sink, simple->attributeValue);
            } else {
                OmPutIdentifier(sink, simple->attributeValue);
            }
        }
        if (simple->caseFlag == RoboCssAttributeCaseInsensitive) {
            OmPut(sink, " i", 2u);
        } else if (simple->caseFlag == RoboCssAttributeCaseSensitive) {
            OmPut(sink, " s", 2u);
        }
        OmPutChar(sink, ']');
        return;
    case RoboCssSimpleSelectorPseudoElement:
    case RoboCssSimpleSelectorPseudoClass: {
        const CssSelectorList *nested =
            CssCompoundSelectorArgumentOf(compound, index);

        OmPut(sink, simple->kind == RoboCssSimpleSelectorPseudoElement ? "::" : ":",
              simple->kind == RoboCssSimpleSelectorPseudoElement ? 2u : 1u);
        OmPutIdentifier(sink, simple->name);
        if (simple->argument.bytes && simple->argument.length > 0u) {
            OmPutChar(sink, '(');
            OmPutSlice(sink, simple->argument);
            OmPutChar(sink, ')');
        } else if (nested) {
            OmPutChar(sink, '(');
            OmWriteSelectorList(sink, nested);
            OmPutChar(sink, ')');
        }
        return;
    }
    default:
        return;
    }
}

static void OmWriteCompoundSelector(OmSink *sink, const CssCompoundSelector *compound)
{
    size_t index;

    for (index = 0u; index < CssCompoundSelectorCount(compound); index++) {
        OmWriteSimpleSelector(sink, compound, index);
    }
}

static void OmWriteComplexSelector(OmSink *sink, const CssComplexSelector *selector)
{
    size_t index;

    for (index = 0u; index < CssComplexSelectorCompoundCount(selector); index++) {
        OmWriteCompoundSelector(sink, CssComplexSelectorCompoundAt(selector, index));
        if (index + 1u < CssComplexSelectorCompoundCount(selector)) {
            const char *combinator =
                OmCombinatorText(CssComplexSelectorCombinatorAt(selector, index));
            OmPut(sink, combinator, strlen(combinator));
        }
    }
}

static void OmWriteSelectorList(OmSink *sink, const CssSelectorList *list)
{
    size_t index;

    for (index = 0u; index < CssSelectorListCount(list); index++) {
        if (index > 0u) {
            OmPut(sink, ", ", 2u);
        }
        OmWriteComplexSelector(sink, CssSelectorListAt(list, index));
    }
}

Result CssOMWriteSelectorList(const CssSelectorList *list,
                                           char *buffer,
                                           size_t capacity,
                                           size_t *outLength,
                                           CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteSelectorList, list);
}

Result CssOMWriteComplexSelector(const CssComplexSelector *selector,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *outLength,
                                              CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteComplexSelector, selector);
}

/* ------------------------------------------------------------------ */
/* Declarations                                                        */
/* ------------------------------------------------------------------ */

static void OmWriteDeclaration(OmSink *sink, const CssDeclaration *declaration)
{
    if (!declaration || declaration->state == RoboCssDeclarationStateNone) {
        return;
    }
    OmPutSlice(sink, declaration->name);
    OmPut(sink, ": ", 2u);
    OmWriteValue(sink, CssDeclarationValue(declaration));
    if (declaration->important) {
        OmPut(sink, " !important", 11u);
    }
    OmPutChar(sink, ';');
}

Result CssOMWriteDeclaration(const CssDeclaration *declaration,
                                          char *buffer,
                                          size_t capacity,
                                          size_t *outLength,
                                          CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteDeclaration, declaration);
}

static void OmWriteDeclarationList(OmSink *sink,
                                   const CssDeclarationList *list)
{
    size_t index;

    for (index = 0u; index < CssDeclarationListCount(list); index++) {
        if (index > 0u) {
            OmPutChar(sink, ' ');
        }
        OmWriteDeclaration(sink, CssDeclarationListAt(list, index));
    }
}

Result CssOMWriteDeclarationList(const CssDeclarationList *list,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *outLength,
                                              CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteDeclarationList, list);
}

/* ------------------------------------------------------------------ */
/* Rules and stylesheets                                               */
/* ------------------------------------------------------------------ */

static void OmWritePrelude(OmSink *sink, const CssAtRule *at)
{
    const CssValue *values = CssAtRulePreludeValues(at);

    if (values && CssValueChildCount(values) > 0u) {
        OmPutChar(sink, ' ');
        OmWriteValue(sink, values);
        return;
    }
    if (at->prelude.bytes && at->prelude.length > 0u) {
        /* The prelude's raw text: trim the edges, because the slice is the
         * bytes the author wrote and the spaces around them are not content. */
        size_t begin = 0u;
        size_t end = at->prelude.length;

        while (begin < end && CssAsciiIsSpace(at->prelude.bytes[begin])) {
            begin++;
        }
        while (end > begin && CssAsciiIsSpace(at->prelude.bytes[end - 1u])) {
            end--;
        }
        if (end > begin) {
            OmPutChar(sink, ' ');
            OmPut(sink, (const char *)at->prelude.bytes + begin, end - begin);
        }
    }
}

static void OmWriteRule(OmSink *sink, const CssRule *rule)
{
    if (!rule) {
        return;
    }
    if (rule->kind == RoboCssRuleKindStyle) {
        const CssStyleRule *style = CssRuleStyle(rule);
        size_t index;

        /* Prefer the parsed selector list, and fall back to the prelude's
         * component values when selectors were never parsed (the stylesheet
         * grammar keeps a rule's prelude as a value tree and leaves the
         * selector list empty until 9.4 fills it). Rendering the prelude is
         * the same text, and it keeps a rule printable straight out of a
         * parse rather than only after a second step. */
        if (CssStyleRuleSelectorCount(style) == 0u) {
            OmWriteValue(sink, CssStyleRulePreludeValues(style));
        } else {
            OmWriteSelectorList(sink, CssStyleRuleSelectors(style));
        }
        if (CssStyleRuleDeclarationCount(style) == 0u) {
            OmPut(sink, " { }", 4u);
            return;
        }
        OmPut(sink, " { ", 3u);
        for (index = 0u; index < CssStyleRuleDeclarationCount(style); index++) {
            if (index > 0u) {
                OmPutChar(sink, ' ');
            }
            OmWriteDeclaration(sink, CssStyleRuleDeclaration(style, index));
        }
        OmPut(sink, " }", 2u);
        return;
    }
    if (rule->kind == RoboCssRuleKindAt) {
        const CssAtRule *at = CssRuleAt(rule);
        size_t index;

        OmPutChar(sink, '@');
        OmPutSlice(sink, at->name);
        OmWritePrelude(sink, at);
        if (!at->hasBlock) {
            OmPutChar(sink, ';');
            return;
        }
        if (CssAtRuleRuleCount(at) == 0u && CssAtRuleDeclarationCount(at) == 0u) {
            OmPut(sink, " { }", 4u);
            return;
        }
        OmPut(sink, " { ", 3u);
        for (index = 0u; index < CssAtRuleRuleCount(at); index++) {
            if (index > 0u) {
                OmPutChar(sink, ' ');
            }
            OmWriteRule(sink, CssAtRuleRuleAt(at, index));
        }
        for (index = 0u; index < CssAtRuleDeclarationCount(at); index++) {
            if (index > 0u) {
                OmPutChar(sink, ' ');
            }
            OmWriteDeclaration(sink, CssAtRuleDeclaration(at, index));
        }
        OmPut(sink, " }", 2u);
        return;
    }
}

static void OmWriteRuleList(OmSink *sink, const CssRuleList *list)
{
    size_t index;

    for (index = 0u; index < CssRuleListCount(list); index++) {
        if (index > 0u) {
            OmPutChar(sink, '\n');
        }
        OmWriteRule(sink, CssRuleListAt(list, index));
    }
}

Result CssOMWriteRule(const CssRule *rule,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteRule, rule);
}

Result CssOMWriteRuleList(const CssRuleList *list,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *outLength,
                                       CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteRuleList, list);
}

Result CssOMWriteStylesheet(CssStylesheetId id,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *outLength,
                                         CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboNotFound,
                           "stylesheet id does not resolve");
    }
    OM_EMIT(OmWriteRuleList, &slot->rules);
}

/* ------------------------------------------------------------------ */
/* The mutable style declaration                                       */
/* ------------------------------------------------------------------ */

/** One property: an owned name, an owned value source, and its parsed tree. */
typedef struct CssOMEntry {
    char *name;
    size_t nameLength;
    int important;
    /** Owns the value's bytes (KIND_COPY); `value` borrows them. */
    CssSource valueText;
    /** Parsed from @p valueText; slices borrow it, so the two move together. */
    CssValue value;
    int hasValue;
} CssOMEntry;

struct CssStyleDeclaration {
    CssOMEntry *items;
    size_t count;
    size_t capacity;
};

static void OmEntryInit(CssOMEntry *entry)
{
    memset(entry, 0, sizeof *entry);
    CssSourceInit(&entry->valueText);
    CssValueInit(&entry->value);
}

static void OmEntryDispose(CssOMEntry *entry)
{
    if (!entry) {
        return;
    }
    if (entry->name) {
        RoboFree(entry->name);
    }
    CssValueDispose(&entry->value);
    CssSourceDispose(&entry->valueText);
    OmEntryInit(entry);
}

static size_t OmFindEntry(const CssStyleDeclaration *declaration,
                          const char *name)
{
    size_t index;
    size_t length;

    if (!declaration || !name) {
        return (size_t)-1;
    }
    length = strlen(name);
    if (length == 0u) {
        return (size_t)-1;
    }
    for (index = 0u; index < declaration->count; index++) {
        if (declaration->items[index].nameLength == length &&
            CssBytesEqualAsciiCi((const uint8_t *)declaration->items[index].name,
                                 length, name)) {
            return index;
        }
    }
    return (size_t)-1;
}

static int OmNameIsUsable(const char *name)
{
    size_t index;
    size_t length;

    if (!name) {
        return 0;
    }
    length = strlen(name);
    if (length == 0u || length > RoboCssPropertyNameMax) {
        return 0;
    }
    for (index = 0u; index < length; index++) {
        uint8_t c = (uint8_t)name[index];

        if (c == (uint8_t)':' || c == (uint8_t)'{' || c == (uint8_t)'}' ||
            CssAsciiIsSpace(c)) {
            return 0;
        }
    }
    return 1;
}

/** ""/NULL/`important` (ASCII case-insensitive) → 1; anything else → 0. */
static int OmPriorityIsKnown(const char *priority, int *outImportant)
{
    *outImportant = 0;
    if (!priority || priority[0] == '\0') {
        return 1;
    }
    if (CssBytesEqualAsciiCi((const uint8_t *)priority, strlen(priority),
                             "important")) {
        *outImportant = 1;
        return 1;
    }
    return 0;
}

/**
 * Build @p out from an owned copy of @p name and @p valueText.
 *
 * @p outUsable is 0 — with the object left empty and @p outError carrying the
 * grammar's report — when the value could not be read or read to nothing. That
 * is not a failure of the call: an unreadable value is CSS's problem and a
 * property that produced nothing is simply not a property.
 */
static Result OmEntryBuild(CssOMEntry *out,
                           const char *name,
                           size_t nameLength,
                           const char *valueText,
                           size_t valueLength,
                           int important,
                           int *outUsable,
                           CssError *outError,
                           CssDiag *diag)
{
    CssSource source = RoboCssCleared;
    CssValue parsed = RoboCssCleared;
    CssError error = RoboCssCleared;
    Result rc;

    OmEntryInit(out);
    *outUsable = 0;
    out->name = (char *)Realloc(NULL, nameLength + 1u);
    if (!out->name) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboOutOfMemory,
                           "property name could not be copied");
    }
    memcpy(out->name, name, nameLength);
    out->name[nameLength] = '\0';
    out->nameLength = nameLength;
    out->important = important;

    rc = CssSourceCopy(&source, (const uint8_t *)valueText, valueLength, diag);
    if (rc != RoboOk) {
        OmEntryDispose(out);
        return rc;
    }
    CssErrorInit(&error);
    rc = CssGrammarParseValue(&parsed, &source, RoboCssOriginInline, &error);
    if (outError) {
        *outError = error;
    }
    if (rc != RoboOk) {
        CssValueDispose(&parsed);
        CssSourceDispose(&source);
        OmEntryDispose(out);
        return rc;
    }
    /* A value that produced nothing — no tree at all, or a container with no
     * children (`color: ` and a whitespace-only value both land here) — is not
     * a property with an empty value; it is no property. */
    if (CssValueIsEmpty(&parsed) || CssValueChildCount(&parsed) == 0u) {
        CssValueDispose(&parsed);
        CssSourceDispose(&source);
        OmEntryDispose(out);
        return RoboOk;
    }
    out->valueText = source;
    out->value = parsed;
    out->hasValue = 1;
    *outUsable = 1;
    return RoboOk;
}

Result CssStyleDeclarationCreate(CssStyleDeclaration **outDeclaration,
                                             CssDiag *diag)
{
    CssStyleDeclaration *declaration;

    if (!outDeclaration) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "output is NULL");
    }
    *outDeclaration = NULL;
    declaration = (CssStyleDeclaration *)Realloc(NULL, sizeof *declaration);
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboOutOfMemory,
                           "style declaration could not be allocated");
    }
    declaration->items = NULL;
    declaration->count = 0u;
    declaration->capacity = 0u;
    *outDeclaration = declaration;
    return RoboOk;
}

void CssStyleDeclarationClear(CssStyleDeclaration *declaration)
{
    size_t index;

    if (!declaration) {
        return;
    }
    for (index = 0u; index < declaration->count; index++) {
        OmEntryDispose(&declaration->items[index]);
    }
    declaration->count = 0u;
}

void CssStyleDeclarationDestroy(CssStyleDeclaration *declaration)
{
    if (!declaration) {
        return;
    }
    CssStyleDeclarationClear(declaration);
    if (declaration->items) {
        RoboFree(declaration->items);
    }
    RoboFree(declaration);
}

Result CssStyleDeclarationParseText(CssStyleDeclaration *declaration,
                                                 const char *text,
                                                 size_t length,
                                                 CssError *outError,
                                                 CssDiag *diag)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    size_t index;
    Result rc;

    if (outError) {
        CssErrorInit(outError);
    }
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "style declaration is NULL");
    }
    if (!text && length > 0u) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "text is NULL with a non-zero length");
    }
    CssStyleDeclarationClear(declaration);
    CssDeclarationListInit(&list);
    CssErrorInit(&error);
    rc = CssDeclarationListParseText(text ? text : "", length,
                                     RoboCssOriginInline, &list, &error);
    if (outError) {
        *outError = error;
    }
    if (rc != RoboOk) {
        CssDeclarationListDispose(&list);
        return rc;
    }
    for (index = 0u; index < CssDeclarationListCount(&list); index++) {
        const CssDeclaration *source = CssDeclarationListAt(&list, index);
        CssSlice name;
        char *valueText = NULL;
        size_t valueLength = 0u;
        CssOMEntry built;
        int usable = 0;

        if (!CssDeclarationIsUsable(source)) {
            continue;
        }
        name = CssDeclarationName(source);
        if (CssSliceIsEmpty(name)) {
            continue;
        }
        rc = CssOMWriteValue(CssDeclarationValue(source), NULL, 0u,
                             &valueLength, diag);
        if (rc != RoboOk) {
            CssDeclarationListDispose(&list);
            return rc;
        }
        valueText = (char *)Realloc(NULL, valueLength + 1u);
        if (!valueText) {
            CssDeclarationListDispose(&list);
            CssStyleDeclarationClear(declaration);
            return RoboCssDiag(diag, RoboCssDomainCssom, RoboOutOfMemory,
                               "value text could not be allocated");
        }
        rc = CssOMWriteValue(CssDeclarationValue(source), valueText,
                             valueLength + 1u, &valueLength, diag);
        if (rc != RoboOk) {
            RoboFree(valueText);
            CssDeclarationListDispose(&list);
            CssStyleDeclarationClear(declaration);
            return rc;
        }
        rc = OmEntryBuild(&built, (const char *)name.bytes, name.length,
                          valueText, valueLength, source->important, &usable,
                          NULL, diag);
        RoboFree(valueText);
        if (rc != RoboOk) {
            CssDeclarationListDispose(&list);
            CssStyleDeclarationClear(declaration);
            return rc;
        }
        if (!usable) {
            continue;
        }
        if (!CssGrow((void **)&declaration->items, &declaration->capacity,
                     declaration->count, sizeof(CssOMEntry))) {
            OmEntryDispose(&built);
            CssDeclarationListDispose(&list);
            CssStyleDeclarationClear(declaration);
            return RoboCssDiag(diag, RoboCssDomainCssom, RoboOutOfMemory,
                               "the property list could not grow");
        }
        declaration->items[declaration->count++] = built;
    }
    CssDeclarationListDispose(&list);
    return RoboOk;
}

Result CssStyleDeclarationSetProperty(CssStyleDeclaration *declaration,
                                                   const char *name,
                                                   const char *valueText,
                                                   const char *priority,
                                                   CssError *outError,
                                                   CssDiag *diag)
{
    int important = 0;
    CssOMEntry built;
    int usable = 0;
    size_t index;
    Result rc;

    if (outError) {
        CssErrorInit(outError);
    }
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "style declaration is NULL");
    }
    if (!OmNameIsUsable(name)) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "property name is unusable");
    }
    if (!OmPriorityIsKnown(priority, &important)) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "priority is neither empty nor important");
    }
    /* An empty value is CSSOM's removal: a property that was not there is not
     * an error, and one that was is removed. */
    if (!valueText || valueText[0] == '\0') {
        rc = CssStyleDeclarationRemoveProperty(declaration, name, NULL, 0u,
                                               NULL, diag);
        return (rc == RoboNotFound) ? RoboOk : rc;
    }
    rc = OmEntryBuild(&built, name, strlen(name), valueText, strlen(valueText),
                      important, &usable, outError, diag);
    if (rc != RoboOk) {
        return rc;
    }
    if (!usable) {
        /* Reported through outError; the object is unchanged. */
        return RoboOk;
    }
    index = OmFindEntry(declaration, name);
    if (index != (size_t)-1) {
        OmEntryDispose(&declaration->items[index]);
        declaration->items[index] = built;
        return RoboOk;
    }
    if (!CssGrow((void **)&declaration->items, &declaration->capacity,
                 declaration->count, sizeof(CssOMEntry))) {
        OmEntryDispose(&built);
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboOutOfMemory,
                           "the property list could not grow");
    }
    declaration->items[declaration->count++] = built;
    return RoboOk;
}

Result CssStyleDeclarationRemoveProperty(CssStyleDeclaration *declaration,
                                                      const char *name,
                                                      char *outValue,
                                                      size_t outCapacity,
                                                      size_t *outLength,
                                                      CssDiag *diag)
{
    size_t index;
    Result rc = RoboOk;

    if (outLength) {
        *outLength = 0u;
    }
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "style declaration is NULL");
    }
    if (!name) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "property name is NULL");
    }
    index = OmFindEntry(declaration, name);
    if (index == (size_t)-1) {
        return RoboNotFound;
    }
    if (outValue) {
        rc = CssOMWriteValue(&declaration->items[index].value, outValue,
                             outCapacity, outLength, diag);
    }
    OmEntryDispose(&declaration->items[index]);
    if (index + 1u < declaration->count) {
        memmove(&declaration->items[index], &declaration->items[index + 1u],
                (declaration->count - index - 1u) * sizeof(CssOMEntry));
    }
    declaration->count--;
    return rc;
}

size_t CssStyleDeclarationLength(const CssStyleDeclaration *declaration)
{
    return declaration ? declaration->count : 0u;
}

CssSlice CssStyleDeclarationNameAt(const CssStyleDeclaration *declaration,
                                              size_t index)
{
    if (!declaration || index >= declaration->count) {
        return CssSliceEmpty();
    }
    return CssSliceFromBytes((const uint8_t *)declaration->items[index].name,
                             declaration->items[index].nameLength);
}

int CssStyleDeclarationContains(const CssStyleDeclaration *declaration,
                                           const char *name)
{
    return OmFindEntry(declaration, name) != (size_t)-1;
}

Result CssStyleDeclarationGetPropertyValue(
    const CssStyleDeclaration *declaration,
    const char *name,
    char *buffer,
    size_t capacity,
    size_t *outLength,
    CssDiag *diag)
{
    size_t index;
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "style declaration is NULL");
    }
    index = OmFindEntry(declaration, name);
    if (index == (size_t)-1) {
        *outLength = 0u;
        return RoboNotFound;
    }
    return CssOMWriteValue(&declaration->items[index].value, buffer, capacity,
                           outLength, diag);
}

int CssStyleDeclarationGetPropertyPriority(
    const CssStyleDeclaration *declaration,
    const char *name)
{
    size_t index = OmFindEntry(declaration, name);

    if (index == (size_t)-1) {
        return 0;
    }
    return declaration->items[index].important;
}

static void OmWriteStyleDeclaration(OmSink *sink,
                                    const CssStyleDeclaration *declaration)
{
    size_t index;

    for (index = 0u; index < CssStyleDeclarationLength(declaration); index++) {
        const CssOMEntry *entry = &declaration->items[index];

        if (index > 0u) {
            OmPutChar(sink, ' ');
        }
        OmPut(sink, entry->name, entry->nameLength);
        OmPut(sink, ": ", 2u);
        OmWriteValue(sink, &entry->value);
        if (entry->important) {
            OmPut(sink, " !important", 11u);
        }
        OmPutChar(sink, ';');
    }
}

Result CssStyleDeclarationCssText(const CssStyleDeclaration *declaration,
                                                 char *buffer,
                                                 size_t capacity,
                                                 size_t *outLength,
                                                 CssDiag *diag)
{
    Result rc = OmCheckTarget(buffer, capacity, outLength, diag);

    if (rc != RoboOk) {
        return rc;
    }
    OM_EMIT(OmWriteStyleDeclaration, declaration);
}

/* ------------------------------------------------------------------ */
/* Rule editing                                                        */
/* ------------------------------------------------------------------ */

Result CssOMStylesheetInsertRule(CssStylesheetId id,
                                             const char *ruleText,
                                             size_t length,
                                             size_t index,
                                             CssError *outError,
                                             CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);
    CssSource source = RoboCssCleared;
    CssRuleList parsed = RoboCssCleared;
    CssError error = RoboCssCleared;
    size_t at;
    Result rc;

    if (outError) {
        CssErrorInit(outError);
    }
    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboNotFound,
                           "stylesheet id does not resolve");
    }
    if (!ruleText && length > 0u) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "rule text is NULL with a non-zero length");
    }
    CssRuleListInit(&parsed);
    CssErrorInit(&error);
    rc = CssSourceSetMemory(&source, (const uint8_t *)(ruleText ? ruleText : ""),
                            length, diag);
    if (rc != RoboOk) {
        return rc;
    }
    rc = CssGrammarParseStylesheet(&parsed, &source, slot->origin, &error);
    if (outError) {
        *outError = error;
    }
    if (rc != RoboOk) {
        CssRuleListDispose(&parsed);
        return rc;
    }
    if (CssRuleListCount(&parsed) != 1u) {
        CssRuleListDispose(&parsed);
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboInvalidArgument,
                           "insertRule text must be exactly one rule");
    }
    if (!CssGrow((void **)&slot->rules.items, &slot->rules.capacity,
                 slot->rules.count, sizeof(CssRule))) {
        CssRuleListDispose(&parsed);
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboOutOfMemory,
                           "the stylesheet rule list could not grow");
    }
    at = (index > slot->rules.count) ? slot->rules.count : index;
    if (at < slot->rules.count) {
        memmove(&slot->rules.items[at + 1u], &slot->rules.items[at],
                (slot->rules.count - at) * sizeof(CssRule));
    }
    slot->rules.items[at] = parsed.items[0];
    /* The rule was moved, so the source record must not dispose what it no
     * longer owns; clearing it leaves the list safe to dispose below. */
    CssRuleInit(&parsed.items[0]);
    slot->rules.count++;
    CssRuleListDispose(&parsed);
    return RoboOk;
}

Result CssOMStylesheetDeleteRule(CssStylesheetId id,
                                             size_t index,
                                             CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboNotFound,
                           "stylesheet id does not resolve");
    }
    if (index >= slot->rules.count) {
        return RoboCssDiag(diag, RoboCssDomainCssom, RoboNotFound,
                           "rule index is out of range");
    }
    CssRuleDispose(&slot->rules.items[index]);
    if (index + 1u < slot->rules.count) {
        memmove(&slot->rules.items[index], &slot->rules.items[index + 1u],
                (slot->rules.count - index - 1u) * sizeof(CssRule));
    }
    slot->rules.count--;
    return RoboOk;
}
