/*
 * RoboCSS parser cursor (Phase 9.1) — see RoboCSSParser.h.
 *
 * The cursor is deliberately small: a source it borrows, an offset that is
 * always inside it, and the first error. Everything a real parser adds on top
 * (lookahead, an open-block stack, a nesting budget, a reconsume flag) is 9.2's
 * and 9.3's, and guessing at it here would fix decisions those phases are
 * entitled to make.
 */

#include "RoboCSS/RoboCSSParser.h"

#include "RoboCSSInternal.h"

Result CssParserInit(CssParser *parser,
                                  const CssSource *source,
                                  CssOrigin origin,
                                  CssDiag *diag)
{
    if (!parser || !source) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "parser or source is NULL");
    }
    if (!CssOriginIsKnown(origin)) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "unknown origin");
    }
    parser->source = source;
    parser->origin = origin;
    parser->offset = 0u;
    parser->hasError = 0;
    CssErrorInit(&parser->error);
    return RoboOk;
}

void CssParserDispose(CssParser *parser)
{
    if (!parser) {
        return;
    }
    parser->source = NULL;
    parser->origin = RoboCssOriginNone;
    parser->offset = 0u;
    parser->hasError = 0;
    CssErrorInit(&parser->error);
}

Result CssParserReset(CssParser *parser,
                                   CssDiag *diag)
{
    if (!parser || !parser->source) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "parser is NULL or has no source");
    }
    parser->offset = 0u;
    parser->hasError = 0;
    CssErrorInit(&parser->error);
    return RoboOk;
}

const CssSource *CssParserSource(const CssParser *parser)
{
    return parser ? parser->source : NULL;
}

CssOrigin CssParserOrigin(const CssParser *parser)
{
    return parser ? parser->origin : RoboCssOriginNone;
}

size_t CssParserOffset(const CssParser *parser)
{
    return parser ? parser->offset : 0u;
}

size_t CssParserRemaining(const CssParser *parser)
{
    if (!parser || !parser->source) {
        return 0u;
    }
    if (parser->offset >= parser->source->length) {
        return 0u;
    }
    return parser->source->length - parser->offset;
}

int CssParserAtEnd(const CssParser *parser)
{
    if (!parser || !parser->source) {
        return 1;
    }
    return parser->offset >= parser->source->length;
}

Result CssParserPosition(const CssParser *parser,
                                      CssPosition *outPosition,
                                      CssDiag *diag)
{
    if (!parser || !parser->source || !outPosition) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "parser, source or output is NULL");
    }
    if (CssSourcePosition(parser->source, parser->offset, outPosition) != RoboOk) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "cursor offset is not a position in the source");
    }
    return RoboOk;
}

Result CssParserAdvance(CssParser *parser,
                                      size_t count,
                                      CssDiag *diag)
{
    size_t remaining;

    if (!parser || !parser->source) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "parser is NULL or has no source");
    }
    remaining = CssParserRemaining(parser);
    if (count > remaining) {
        /* Land at the end and say so: a recovery path gets a defined place to
         * continue from, and the caller still learns it over-ran. */
        parser->offset = parser->source->length;
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "advance went past the end of the source");
    }
    parser->offset += count;
    return RoboOk;
}

Result CssParserSeek(CssParser *parser,
                                  size_t offset,
                                  CssDiag *diag)
{
    if (!parser || !parser->source) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "parser is NULL or has no source");
    }
    if (!CssSourceOffsetIsValid(parser->source, offset)) {
        return RoboCssDiag(diag, RoboCssDomainParser, RoboInvalidArgument,
                             "offset is past the end of the source");
    }
    parser->offset = offset;
    return RoboOk;
}

CssRange CssParserCursorRange(const CssParser *parser)
{
    return CssRangeAt(CssParserOffset(parser));
}

/* ------------------------------------------------------------------ */
/* The first error                                                     */
/* ------------------------------------------------------------------ */

int CssParserRecordError(CssParser *parser,
                                 const CssError *error)
{
    if (!parser || !error) {
        return 0;
    }
    if (!CssErrorKindIsError(error->kind)) {
        return 0;
    }
    if (parser->hasError) {
        return 0; /* the first error is the one that explains the rest */
    }
    parser->hasError = 1;
    parser->error = *error;
    return 1;
}

int CssParserFail(CssParser *parser,
                         CssErrorKind kind,
                         Result code,
                         const char *message)
{
    CssError error;
    CssPosition position;
    const CssPosition *positionPtr = NULL;

    if (!parser) {
        return 0;
    }
    if (CssParserPosition(parser, &position, NULL) == RoboOk) {
        positionPtr = &position;
    }
    CssErrorSet(&error, kind, code, positionPtr, message);
    return CssParserRecordError(parser, &error);
}

int CssParserHasError(const CssParser *parser)
{
    return parser ? parser->hasError : 0;
}

const CssError *CssParserError(const CssParser *parser)
{
    if (!parser || !parser->hasError) {
        return NULL;
    }
    return &parser->error;
}

int CssParserTakeError(CssParser *parser,
                               CssError *outError)
{
    if (!parser || !parser->hasError) {
        return 0;
    }
    if (outError) {
        *outError = parser->error;
    }
    parser->hasError = 0;
    CssErrorInit(&parser->error);
    return 1;
}
