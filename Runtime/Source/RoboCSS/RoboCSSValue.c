/*
 * RoboCSS component value model (Phase 9.1) — see RoboCSSValue.h.
 *
 * The one rule this file exists to enforce: a value's children are moved in,
 * never copied in. Every append grows the child array *first* and only then
 * transfers the child, so a failed append leaves the caller's value exactly as
 * it was — still owning everything it owned before the call.
 */

#include "RoboCSS/RoboCSSValue.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

void CssValueInit(CssValue *value)
{
    if (!value) {
        return;
    }
    value->kind = RoboCssValueKindNone;
    CssTokenInit(&value->token);
    value->name = CssSliceEmpty();
    value->open = RoboCssTokenNone;
    value->close = RoboCssTokenNone;
    value->range = CssRangeAt(0u);
    value->children.items = NULL;
    value->children.count = 0u;
    value->children.capacity = 0u;
}

void CssValueClearChildren(CssValue *value)
{
    size_t i;

    if (!value) {
        return;
    }
    for (i = 0u; i < value->children.count; i++) {
        CssValueDispose(&value->children.items[i]);
    }
    if (value->children.items) {
        RoboFree(value->children.items);
    }
    value->children.items = NULL;
    value->children.count = 0u;
    value->children.capacity = 0u;
}

void CssValueDispose(CssValue *value)
{
    if (!value) {
        return;
    }
    /* Depth is bounded by construction — the parser owns the nesting budget —
     * so the recursion here is bounded by what could be built, not by input. */
    CssValueClearChildren(value);
    CssValueInit(value);
}

Result CssValueMove(CssValue *value,
                                 CssValue *outValue,
                                 CssDiag *diag)
{
    if (!value || !outValue || value == outValue) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value or output is NULL, or they are the same");
    }
    CssValueDispose(outValue);
    *outValue = *value;
    CssValueInit(value);
    return RoboOk;
}

/**
 * Copy @p value's children into @p out, which already carries @p value's own
 * fields. Recurses only as deep as the parser allowed anything to be built.
 *
 * The array is grown to its final size before any child is written, so a
 * failure part-way through leaves nothing half-built for the caller to see —
 * the frame above disposes @p out and reports the failure.
 */
static int ValueCopyChildren(CssValue *out, const CssValue *value)
{
    size_t index;

    if (value->children.count == 0u) {
        return 1;
    }
    out->children.items = (CssValue *)Malloc(
        value->children.count * sizeof(CssValue));
    if (!out->children.items) {
        return 0;
    }
    out->children.capacity = value->children.count;
    for (index = 0u; index < value->children.count; index++) {
        /* Zeroed before use: CssValueCopy disposes its output first, and
         * the cleared byte pattern is a valid empty value by design (the rule
         * stated in RoboCSSValue.h). */
        memset(&out->children.items[index], 0, sizeof(CssValue));
        if (CssValueCopy(&out->children.items[index],
                                &value->children.items[index], NULL) != RoboOk) {
            return 0;
        }
        out->children.count = index + 1u;
    }
    return 1;
}

Result CssValueCopy(CssValue *outValue,
                                  const CssValue *value,
                                  CssDiag *diag)
{
    if (!outValue || !value || outValue == value) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value or output is NULL, or they are the same");
    }
    if (!CssValueKindIsKnown(value->kind)) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "unknown value kind");
    }
    CssValueDispose(outValue);
    /* The scalar fields first: token, name, open/close and range are all plain
     * values whose slices stay borrowed from the same bytes. */
    outValue->kind = value->kind;
    outValue->token = value->token;
    outValue->name = value->name;
    outValue->open = value->open;
    outValue->close = value->close;
    outValue->range = value->range;
    if (!ValueCopyChildren(outValue, value)) {
        /* Not a partial copy: the caller gets an empty value, so nothing it
         * might dispose is half-owned. */
        CssValueDispose(outValue);
        return RoboCssDiag(diag, RoboCssDomainValue, RoboOutOfMemory,
                             "could not copy the value tree");
    }
    return RoboOk;
}

int CssValueIsEmpty(const CssValue *value)
{
    return !value || value->kind == RoboCssValueKindNone;
}

int CssValueKindIsKnown(CssValueKind kind)
{
    return kind >= RoboCssValueKindNone &&
           kind < (CssValueKind)RoboCssValueKindCount;
}

const char *CssValueKindName(CssValueKind kind)
{
    switch (kind) {
    case RoboCssValueKindNone:
        return "none";
    case RoboCssValueKindToken:
        return "token";
    case RoboCssValueKindFunction:
        return "function";
    case RoboCssValueKindBlock:
        return "block";
    case RoboCssValueKindList:
        return "list";
    default:
        return "unknown";
    }
}

int CssValueKindHoldsChildren(CssValueKind kind)
{
    return kind == RoboCssValueKindFunction || kind == RoboCssValueKindBlock ||
           kind == RoboCssValueKindList;
}

Result CssValueMakeToken(CssValue *value,
                                       const CssToken *token,
                                       CssDiag *diag)
{
    if (!value || !token) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value or token is NULL");
    }
    if (!CssTokenTypeIsKnown(token->type)) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "unknown token type");
    }
    CssValueDispose(value);
    value->kind = RoboCssValueKindToken;
    value->token = *token;
    value->range = token->range;
    return RoboOk;
}

Result CssValueMakeList(CssValue *value,
                                      CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value is NULL");
    }
    CssValueDispose(value);
    value->kind = RoboCssValueKindList;
    return RoboOk;
}

Result CssValueMakeFunction(CssValue *value,
                                          CssSlice name,
                                          CssRange range,
                                          CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value is NULL");
    }
    if (CssSliceIsEmpty(name)) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "a function value needs a name");
    }
    CssValueDispose(value);
    value->kind = RoboCssValueKindFunction;
    value->name = name;
    value->range = range;
    return RoboOk;
}

Result CssValueMakeBlock(CssValue *value,
                                       CssTokenType openType,
                                       CssRange range,
                                       CssDiag *diag)
{
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value is NULL");
    }
    if (!CssTokenTypeIsBlockStart(openType)) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "a block value needs an opening bracket type");
    }
    CssValueDispose(value);
    value->kind = RoboCssValueKindBlock;
    value->open = openType;
    (void)CssTokenTypeMatchingClose(openType, &value->close);
    value->range = range;
    return RoboOk;
}

Result CssValueAppend(CssValue *value,
                                   CssValue *child,
                                   CssDiag *diag)
{
    if (!value || !child || value == child) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "value or child is NULL, or they are the same");
    }
    if (!CssValueKindHoldsChildren(value->kind)) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboInvalidArgument,
                             "this value kind does not hold children");
    }
    if (!CssGrow((void **)&value->children.items, &value->children.capacity,
                       value->children.count, sizeof(CssValue))) {
        return RoboCssDiag(diag, RoboCssDomainValue, RoboOutOfMemory,
                             "could not grow the child array");
    }
    /* Grow first, move second: a failure above leaves @p child the caller's. */
    value->children.items[value->children.count] = *child;
    value->children.count++;
    CssValueInit(child);
    return RoboOk;
}

size_t CssValueChildCount(const CssValue *value)
{
    if (!value) {
        return 0u;
    }
    return value->children.count;
}

const CssValue *CssValueChild(const CssValue *value,
                                             size_t index)
{
    if (!value || index >= value->children.count) {
        return NULL;
    }
    return &value->children.items[index];
}

size_t CssValueDepth(const CssValue *value)
{
    size_t i;
    size_t deepest = 0u;

    if (!value || value->kind == RoboCssValueKindNone) {
        return 0u;
    }
    for (i = 0u; i < value->children.count; i++) {
        size_t depth = CssValueDepth(&value->children.items[i]);

        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest + 1u;
}

size_t CssValueTreeCount(const CssValue *value)
{
    size_t i;
    size_t total = 0u;

    if (!value || value->kind == RoboCssValueKindNone) {
        return 0u;
    }
    for (i = 0u; i < value->children.count; i++) {
        total += CssValueTreeCount(&value->children.items[i]);
    }
    return total + 1u;
}

CssToken CssValueToken(const CssValue *value)
{
    CssToken none;

    if (!value || value->kind != RoboCssValueKindToken) {
        CssTokenInit(&none);
        return none;
    }
    return value->token;
}

CssSlice CssValueTokenText(const CssValue *value)
{
    if (!value || value->kind != RoboCssValueKindToken) {
        return CssSliceEmpty();
    }
    return value->token.text;
}

CssSlice CssValueName(const CssValue *value)
{
    if (!value) {
        return CssSliceEmpty();
    }
    return value->name;
}

CssRange CssValueRange(const CssValue *value)
{
    if (!value) {
        return CssRangeAt(0u);
    }
    return value->range;
}
