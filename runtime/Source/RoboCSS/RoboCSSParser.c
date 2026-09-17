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

robo_result_t robo_css_parser_init(robo_css_parser_t *parser,
                                  const robo_css_source_t *source,
                                  robo_css_origin_t origin,
                                  robo_css_diag_t *diag)
{
    if (!parser || !source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "parser or source is NULL");
    }
    if (!robo_css_origin_is_known(origin)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "unknown origin");
    }
    parser->source = source;
    parser->origin = origin;
    parser->offset = 0u;
    parser->has_error = 0;
    robo_css_error_init(&parser->error);
    return ROBO_OK;
}

void robo_css_parser_dispose(robo_css_parser_t *parser)
{
    if (!parser) {
        return;
    }
    parser->source = NULL;
    parser->origin = ROBO_CSS_ORIGIN_NONE;
    parser->offset = 0u;
    parser->has_error = 0;
    robo_css_error_init(&parser->error);
}

robo_result_t robo_css_parser_reset(robo_css_parser_t *parser,
                                   robo_css_diag_t *diag)
{
    if (!parser || !parser->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "parser is NULL or has no source");
    }
    parser->offset = 0u;
    parser->has_error = 0;
    robo_css_error_init(&parser->error);
    return ROBO_OK;
}

const robo_css_source_t *robo_css_parser_source(const robo_css_parser_t *parser)
{
    return parser ? parser->source : NULL;
}

robo_css_origin_t robo_css_parser_origin(const robo_css_parser_t *parser)
{
    return parser ? parser->origin : ROBO_CSS_ORIGIN_NONE;
}

size_t robo_css_parser_offset(const robo_css_parser_t *parser)
{
    return parser ? parser->offset : 0u;
}

size_t robo_css_parser_remaining(const robo_css_parser_t *parser)
{
    if (!parser || !parser->source) {
        return 0u;
    }
    if (parser->offset >= parser->source->length) {
        return 0u;
    }
    return parser->source->length - parser->offset;
}

int robo_css_parser_at_end(const robo_css_parser_t *parser)
{
    if (!parser || !parser->source) {
        return 1;
    }
    return parser->offset >= parser->source->length;
}

robo_result_t robo_css_parser_position(const robo_css_parser_t *parser,
                                      robo_css_position_t *out_position,
                                      robo_css_diag_t *diag)
{
    if (!parser || !parser->source || !out_position) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "parser, source or output is NULL");
    }
    if (robo_css_source_position(parser->source, parser->offset, out_position) != ROBO_OK) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "cursor offset is not a position in the source");
    }
    return ROBO_OK;
}

robo_result_t robo_css_parser_advance(robo_css_parser_t *parser,
                                      size_t count,
                                      robo_css_diag_t *diag)
{
    size_t remaining;

    if (!parser || !parser->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "parser is NULL or has no source");
    }
    remaining = robo_css_parser_remaining(parser);
    if (count > remaining) {
        /* Land at the end and say so: a recovery path gets a defined place to
         * continue from, and the caller still learns it over-ran. */
        parser->offset = parser->source->length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "advance went past the end of the source");
    }
    parser->offset += count;
    return ROBO_OK;
}

robo_result_t robo_css_parser_seek(robo_css_parser_t *parser,
                                  size_t offset,
                                  robo_css_diag_t *diag)
{
    if (!parser || !parser->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "parser is NULL or has no source");
    }
    if (!robo_css_source_offset_is_valid(parser->source, offset)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_INVALID_ARGUMENT,
                             "offset is past the end of the source");
    }
    parser->offset = offset;
    return ROBO_OK;
}

robo_css_range_t robo_css_parser_cursor_range(const robo_css_parser_t *parser)
{
    return robo_css_range_at(robo_css_parser_offset(parser));
}

/* ------------------------------------------------------------------ */
/* The first error                                                     */
/* ------------------------------------------------------------------ */

int robo_css_parser_record_error(robo_css_parser_t *parser,
                                 const robo_css_error_t *error)
{
    if (!parser || !error) {
        return 0;
    }
    if (!robo_css_error_kind_is_error(error->kind)) {
        return 0;
    }
    if (parser->has_error) {
        return 0; /* the first error is the one that explains the rest */
    }
    parser->has_error = 1;
    parser->error = *error;
    return 1;
}

int robo_css_parser_fail(robo_css_parser_t *parser,
                         robo_css_error_kind_t kind,
                         robo_result_t code,
                         const char *message)
{
    robo_css_error_t error;
    robo_css_position_t position;
    const robo_css_position_t *position_ptr = NULL;

    if (!parser) {
        return 0;
    }
    if (robo_css_parser_position(parser, &position, NULL) == ROBO_OK) {
        position_ptr = &position;
    }
    robo_css_error_set(&error, kind, code, position_ptr, message);
    return robo_css_parser_record_error(parser, &error);
}

int robo_css_parser_has_error(const robo_css_parser_t *parser)
{
    return parser ? parser->has_error : 0;
}

const robo_css_error_t *robo_css_parser_error(const robo_css_parser_t *parser)
{
    if (!parser || !parser->has_error) {
        return NULL;
    }
    return &parser->error;
}

int robo_css_parser_take_error(robo_css_parser_t *parser,
                               robo_css_error_t *out_error)
{
    if (!parser || !parser->has_error) {
        return 0;
    }
    if (out_error) {
        *out_error = parser->error;
    }
    parser->has_error = 0;
    robo_css_error_init(&parser->error);
    return 1;
}
