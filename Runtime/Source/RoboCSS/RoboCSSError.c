/*
 * RoboCSS error vocabulary (Phase 9.1) — see RoboCSSError.h.
 */

#include "RoboCSS/RoboCSSError.h"

int CssErrorKindIsKnown(CssErrorKind kind)
{
    return kind >= RoboCssErrorKindNone &&
           kind < (CssErrorKind)RoboCssErrorKindCount;
}

const char *CssErrorKindName(CssErrorKind kind)
{
    switch (kind) {
    case RoboCssErrorKindNone:
        return "none";
    case RoboCssErrorKindUnsupported:
        return "unsupported";
    case RoboCssErrorKindRecoverableSyntax:
        return "recoverable-syntax";
    case RoboCssErrorKindInvalidToken:
        return "invalid-token";
    case RoboCssErrorKindMalformedDeclaration:
        return "malformed-declaration";
    case RoboCssErrorKindMalformedRule:
        return "malformed-rule";
    case RoboCssErrorKindUnexpectedEof:
        return "unexpected-eof";
    case RoboCssErrorKindOutOfMemory:
        return "out-of-memory";
    default:
        return "unknown";
    }
}

int CssErrorKindIsError(CssErrorKind kind)
{
    return kind != RoboCssErrorKindNone;
}

int CssErrorKindIsRecoverable(CssErrorKind kind)
{
    switch (kind) {
    case RoboCssErrorKindRecoverableSyntax:
    case RoboCssErrorKindInvalidToken:
    case RoboCssErrorKindMalformedDeclaration:
    case RoboCssErrorKindMalformedRule:
    case RoboCssErrorKindUnexpectedEof:
        return 1;
    default:
        return 0;
    }
}

int CssErrorKindIsFatal(CssErrorKind kind)
{
    return kind == RoboCssErrorKindUnsupported ||
           kind == RoboCssErrorKindOutOfMemory;
}

void CssErrorInit(CssError *error)
{
    if (!error) {
        return;
    }
    error->kind = RoboCssErrorKindNone;
    error->code = RoboOk;
    error->position = CssPositionNone();
    error->message = NULL;
}

Result CssErrorSet(CssError *error,
                                 CssErrorKind kind,
                                 Result code,
                                 const CssPosition *position,
                                 const char *message)
{
    if (!error) {
        return RoboInvalidArgument;
    }
    error->kind = CssErrorKindIsKnown(kind) ? kind : RoboCssErrorKindNone;
    error->code = code;
    error->position = position ? *position : CssPositionNone();
    error->message = message;
    return code;
}

int CssErrorIsError(const CssError *error)
{
    if (!error) {
        return 0;
    }
    return error->kind != RoboCssErrorKindNone;
}

const char *CssErrorMessage(const CssError *error)
{
    if (!error || !error->message) {
        return "";
    }
    return error->message;
}
