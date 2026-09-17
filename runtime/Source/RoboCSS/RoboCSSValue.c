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

void robo_css_value_init(robo_css_value_t *value)
{
    if (!value) {
        return;
    }
    value->kind = ROBO_CSS_VALUE_KIND_NONE;
    robo_css_token_init(&value->token);
    value->name = robo_css_slice_empty();
    value->open = ROBO_CSS_TOKEN_NONE;
    value->close = ROBO_CSS_TOKEN_NONE;
    value->range = robo_css_range_at(0u);
    value->children.items = NULL;
    value->children.count = 0u;
    value->children.capacity = 0u;
}

void robo_css_value_clear_children(robo_css_value_t *value)
{
    size_t i;

    if (!value) {
        return;
    }
    for (i = 0u; i < value->children.count; i++) {
        robo_css_value_dispose(&value->children.items[i]);
    }
    if (value->children.items) {
        robo_free(value->children.items);
    }
    value->children.items = NULL;
    value->children.count = 0u;
    value->children.capacity = 0u;
}

void robo_css_value_dispose(robo_css_value_t *value)
{
    if (!value) {
        return;
    }
    /* Depth is bounded by construction — the parser owns the nesting budget —
     * so the recursion here is bounded by what could be built, not by input. */
    robo_css_value_clear_children(value);
    robo_css_value_init(value);
}

robo_result_t robo_css_value_move(robo_css_value_t *value,
                                 robo_css_value_t *out_value,
                                 robo_css_diag_t *diag)
{
    if (!value || !out_value || value == out_value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value or output is NULL, or they are the same");
    }
    robo_css_value_dispose(out_value);
    *out_value = *value;
    robo_css_value_init(value);
    return ROBO_OK;
}

/**
 * Copy @p value's children into @p out, which already carries @p value's own
 * fields. Recurses only as deep as the parser allowed anything to be built.
 *
 * The array is grown to its final size before any child is written, so a
 * failure part-way through leaves nothing half-built for the caller to see —
 * the frame above disposes @p out and reports the failure.
 */
static int value_copy_children(robo_css_value_t *out, const robo_css_value_t *value)
{
    size_t index;

    if (value->children.count == 0u) {
        return 1;
    }
    out->children.items = (robo_css_value_t *)robo_malloc(
        value->children.count * sizeof(robo_css_value_t));
    if (!out->children.items) {
        return 0;
    }
    out->children.capacity = value->children.count;
    for (index = 0u; index < value->children.count; index++) {
        /* Zeroed before use: robo_css_value_copy disposes its output first, and
         * the cleared byte pattern is a valid empty value by design (the rule
         * stated in RoboCSSValue.h). */
        memset(&out->children.items[index], 0, sizeof(robo_css_value_t));
        if (robo_css_value_copy(&out->children.items[index],
                                &value->children.items[index], NULL) != ROBO_OK) {
            return 0;
        }
        out->children.count = index + 1u;
    }
    return 1;
}

robo_result_t robo_css_value_copy(robo_css_value_t *out_value,
                                  const robo_css_value_t *value,
                                  robo_css_diag_t *diag)
{
    if (!out_value || !value || out_value == value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value or output is NULL, or they are the same");
    }
    if (!robo_css_value_kind_is_known(value->kind)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "unknown value kind");
    }
    robo_css_value_dispose(out_value);
    /* The scalar fields first: token, name, open/close and range are all plain
     * values whose slices stay borrowed from the same bytes. */
    out_value->kind = value->kind;
    out_value->token = value->token;
    out_value->name = value->name;
    out_value->open = value->open;
    out_value->close = value->close;
    out_value->range = value->range;
    if (!value_copy_children(out_value, value)) {
        /* Not a partial copy: the caller gets an empty value, so nothing it
         * might dispose is half-owned. */
        robo_css_value_dispose(out_value);
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_OUT_OF_MEMORY,
                             "could not copy the value tree");
    }
    return ROBO_OK;
}

int robo_css_value_is_empty(const robo_css_value_t *value)
{
    return !value || value->kind == ROBO_CSS_VALUE_KIND_NONE;
}

int robo_css_value_kind_is_known(robo_css_value_kind_t kind)
{
    return kind >= ROBO_CSS_VALUE_KIND_NONE &&
           kind < (robo_css_value_kind_t)ROBO_CSS_VALUE_KIND_COUNT;
}

const char *robo_css_value_kind_name(robo_css_value_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_VALUE_KIND_NONE:
        return "none";
    case ROBO_CSS_VALUE_KIND_TOKEN:
        return "token";
    case ROBO_CSS_VALUE_KIND_FUNCTION:
        return "function";
    case ROBO_CSS_VALUE_KIND_BLOCK:
        return "block";
    case ROBO_CSS_VALUE_KIND_LIST:
        return "list";
    default:
        return "unknown";
    }
}

int robo_css_value_kind_holds_children(robo_css_value_kind_t kind)
{
    return kind == ROBO_CSS_VALUE_KIND_FUNCTION || kind == ROBO_CSS_VALUE_KIND_BLOCK ||
           kind == ROBO_CSS_VALUE_KIND_LIST;
}

robo_result_t robo_css_value_make_token(robo_css_value_t *value,
                                       const robo_css_token_t *token,
                                       robo_css_diag_t *diag)
{
    if (!value || !token) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value or token is NULL");
    }
    if (!robo_css_token_type_is_known(token->type)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "unknown token type");
    }
    robo_css_value_dispose(value);
    value->kind = ROBO_CSS_VALUE_KIND_TOKEN;
    value->token = *token;
    value->range = token->range;
    return ROBO_OK;
}

robo_result_t robo_css_value_make_list(robo_css_value_t *value,
                                      robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_value_dispose(value);
    value->kind = ROBO_CSS_VALUE_KIND_LIST;
    return ROBO_OK;
}

robo_result_t robo_css_value_make_function(robo_css_value_t *value,
                                          robo_css_slice_t name,
                                          robo_css_range_t range,
                                          robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    if (robo_css_slice_is_empty(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "a function value needs a name");
    }
    robo_css_value_dispose(value);
    value->kind = ROBO_CSS_VALUE_KIND_FUNCTION;
    value->name = name;
    value->range = range;
    return ROBO_OK;
}

robo_result_t robo_css_value_make_block(robo_css_value_t *value,
                                       robo_css_token_type_t open_type,
                                       robo_css_range_t range,
                                       robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    if (!robo_css_token_type_is_block_start(open_type)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "a block value needs an opening bracket type");
    }
    robo_css_value_dispose(value);
    value->kind = ROBO_CSS_VALUE_KIND_BLOCK;
    value->open = open_type;
    (void)robo_css_token_type_matching_close(open_type, &value->close);
    value->range = range;
    return ROBO_OK;
}

robo_result_t robo_css_value_append(robo_css_value_t *value,
                                   robo_css_value_t *child,
                                   robo_css_diag_t *diag)
{
    if (!value || !child || value == child) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "value or child is NULL, or they are the same");
    }
    if (!robo_css_value_kind_holds_children(value->kind)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_INVALID_ARGUMENT,
                             "this value kind does not hold children");
    }
    if (!robo_css_grow((void **)&value->children.items, &value->children.capacity,
                       value->children.count, sizeof(robo_css_value_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_VALUE, ROBO_OUT_OF_MEMORY,
                             "could not grow the child array");
    }
    /* Grow first, move second: a failure above leaves @p child the caller's. */
    value->children.items[value->children.count] = *child;
    value->children.count++;
    robo_css_value_init(child);
    return ROBO_OK;
}

size_t robo_css_value_child_count(const robo_css_value_t *value)
{
    if (!value) {
        return 0u;
    }
    return value->children.count;
}

const robo_css_value_t *robo_css_value_child(const robo_css_value_t *value,
                                             size_t index)
{
    if (!value || index >= value->children.count) {
        return NULL;
    }
    return &value->children.items[index];
}

size_t robo_css_value_depth(const robo_css_value_t *value)
{
    size_t i;
    size_t deepest = 0u;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return 0u;
    }
    for (i = 0u; i < value->children.count; i++) {
        size_t depth = robo_css_value_depth(&value->children.items[i]);

        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest + 1u;
}

size_t robo_css_value_tree_count(const robo_css_value_t *value)
{
    size_t i;
    size_t total = 0u;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return 0u;
    }
    for (i = 0u; i < value->children.count; i++) {
        total += robo_css_value_tree_count(&value->children.items[i]);
    }
    return total + 1u;
}

robo_css_token_t robo_css_value_token(const robo_css_value_t *value)
{
    robo_css_token_t none;

    if (!value || value->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        robo_css_token_init(&none);
        return none;
    }
    return value->token;
}

robo_css_slice_t robo_css_value_token_text(const robo_css_value_t *value)
{
    if (!value || value->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return robo_css_slice_empty();
    }
    return value->token.text;
}

robo_css_slice_t robo_css_value_name(const robo_css_value_t *value)
{
    if (!value) {
        return robo_css_slice_empty();
    }
    return value->name;
}

robo_css_range_t robo_css_value_range(const robo_css_value_t *value)
{
    if (!value) {
        return robo_css_range_at(0u);
    }
    return value->range;
}
