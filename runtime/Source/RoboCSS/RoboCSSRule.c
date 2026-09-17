/*
 * RoboCSS rule model (Phase 9.1) — see RoboCSSRule.h.
 *
 * Disposal disposes both alternatives rather than switching on the kind. That
 * is not laziness: a rule is a tagged union whose inactive half is always
 * empty, and disposing an empty collection is defined to be a no-op, so
 * disposing both is the version that stays correct even for a record that was
 * built half way and then abandoned.
 */

#include "RoboCSS/RoboCSSRule.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

int robo_css_rule_kind_is_known(robo_css_rule_kind_t kind)
{
    return kind >= ROBO_CSS_RULE_KIND_NONE &&
           kind < (robo_css_rule_kind_t)ROBO_CSS_RULE_KIND_COUNT;
}

const char *robo_css_rule_kind_name(robo_css_rule_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_RULE_KIND_NONE:
        return "none";
    case ROBO_CSS_RULE_KIND_STYLE:
        return "style";
    case ROBO_CSS_RULE_KIND_AT:
        return "at";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* At-rules                                                            */
/* ------------------------------------------------------------------ */

void robo_css_at_rule_init(robo_css_at_rule_t *at_rule)
{
    if (!at_rule) {
        return;
    }
    at_rule->name = robo_css_slice_empty();
    at_rule->range = robo_css_range_at(0u);
    at_rule->prelude_range = robo_css_range_at(0u);
    at_rule->prelude = robo_css_slice_empty();
    at_rule->has_block = 0;
    at_rule->children = NULL;
}

void robo_css_at_rule_dispose(robo_css_at_rule_t *at_rule)
{
    if (!at_rule) {
        return;
    }
    if (at_rule->children) {
        robo_css_rule_list_dispose(at_rule->children);
        robo_free(at_rule->children);
    }
    robo_css_at_rule_init(at_rule);
}

robo_result_t robo_css_at_rule_make(robo_css_at_rule_t *at_rule,
                                    robo_css_slice_t name,
                                    robo_css_diag_t *diag)
{
    if (!at_rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "at-rule is NULL");
    }
    if (robo_css_slice_is_empty(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "an at-rule needs a name");
    }
    robo_css_at_rule_dispose(at_rule);
    at_rule->name = name;
    return ROBO_OK;
}

robo_result_t robo_css_at_rule_set_prelude(robo_css_at_rule_t *at_rule,
                                          robo_css_slice_t prelude,
                                          int has_block,
                                          robo_css_diag_t *diag)
{
    if (!at_rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "at-rule is NULL");
    }
    if (robo_css_slice_is_empty(at_rule->name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "at-rule has not been named");
    }
    /* An at-rule that had a block and no longer does must not keep the rules
     * that lived in it: turning `@media { p { } }` into `@import url(x);` by
     * clearing the flag would otherwise leave orphaned children behind. */
    if (at_rule->has_block && !has_block && at_rule->children) {
        robo_css_rule_list_dispose(at_rule->children);
        robo_free(at_rule->children);
        at_rule->children = NULL;
    }
    at_rule->prelude = prelude;
    at_rule->has_block = has_block ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_at_rule_append_rule(robo_css_at_rule_t *at_rule,
                                          robo_css_rule_t *rule,
                                          robo_css_diag_t *diag)
{
    if (!at_rule || !rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "at-rule or rule is NULL");
    }
    if (rule->kind == ROBO_CSS_RULE_KIND_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "rule has not been made");
    }
    if (!at_rule->has_block) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "at-rule has no block to add rules to");
    }
    if (!at_rule->children) {
        at_rule->children = (robo_css_rule_list_t *)robo_calloc(1u,
                                                               sizeof(robo_css_rule_list_t));
        if (!at_rule->children) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_OUT_OF_MEMORY,
                                 "could not allocate the at-rule's rule list");
        }
        robo_css_rule_list_init(at_rule->children);
    }
    return robo_css_rule_list_append(at_rule->children, rule, diag);
}

size_t robo_css_at_rule_rule_count(const robo_css_at_rule_t *at_rule)
{
    if (!at_rule || !at_rule->children) {
        return 0u;
    }
    return robo_css_rule_list_count(at_rule->children);
}

const robo_css_rule_t *robo_css_at_rule_rule_at(const robo_css_at_rule_t *at_rule,
                                                size_t index)
{
    if (!at_rule || !at_rule->children) {
        return NULL;
    }
    return robo_css_rule_list_at(at_rule->children, index);
}

/* ------------------------------------------------------------------ */
/* Style rules                                                         */
/* ------------------------------------------------------------------ */

void robo_css_style_rule_init(robo_css_style_rule_t *rule)
{
    if (!rule) {
        return;
    }
    robo_css_selector_list_init(&rule->selectors);
    robo_css_declaration_list_init(&rule->declarations);
    rule->range = robo_css_range_at(0u);
    rule->prelude_range = robo_css_range_at(0u);
    rule->block_range = robo_css_range_at(0u);
}

void robo_css_style_rule_dispose(robo_css_style_rule_t *rule)
{
    if (!rule) {
        return;
    }
    robo_css_selector_list_dispose(&rule->selectors);
    robo_css_declaration_list_dispose(&rule->declarations);
    robo_css_style_rule_init(rule);
}

robo_result_t robo_css_style_rule_append_selector(robo_css_style_rule_t *rule,
                                                 robo_css_complex_selector_t *selector,
                                                 robo_css_diag_t *diag)
{
    if (!rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "rule is NULL");
    }
    return robo_css_selector_list_append(&rule->selectors, selector, diag);
}

robo_result_t robo_css_style_rule_append_declaration(
    robo_css_style_rule_t *rule,
    robo_css_declaration_t *declaration,
    robo_css_diag_t *diag)
{
    if (!rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "rule is NULL");
    }
    return robo_css_declaration_list_append(&rule->declarations, declaration, diag);
}

size_t robo_css_style_rule_selector_count(const robo_css_style_rule_t *rule)
{
    if (!rule) {
        return 0u;
    }
    return robo_css_selector_list_count(&rule->selectors);
}

const robo_css_selector_list_t *robo_css_style_rule_selectors(
    const robo_css_style_rule_t *rule)
{
    if (!rule) {
        return NULL;
    }
    return &rule->selectors;
}

size_t robo_css_style_rule_declaration_count(const robo_css_style_rule_t *rule)
{
    if (!rule) {
        return 0u;
    }
    return robo_css_declaration_list_count(&rule->declarations);
}

const robo_css_declaration_t *robo_css_style_rule_declaration(
    const robo_css_style_rule_t *rule,
    size_t index)
{
    if (!rule) {
        return NULL;
    }
    return robo_css_declaration_list_at(&rule->declarations, index);
}

const robo_css_declaration_list_t *robo_css_style_rule_declarations(
    const robo_css_style_rule_t *rule)
{
    if (!rule) {
        return NULL;
    }
    return &rule->declarations;
}

int robo_css_style_rule_is_populated(const robo_css_style_rule_t *rule)
{
    if (!rule) {
        return 0;
    }
    return robo_css_selector_list_is_set(&rule->selectors) &&
           !robo_css_declaration_list_is_empty(&rule->declarations);
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

void robo_css_rule_init(robo_css_rule_t *rule)
{
    if (!rule) {
        return;
    }
    rule->kind = ROBO_CSS_RULE_KIND_NONE;
    robo_css_style_rule_init(&rule->style);
    robo_css_at_rule_init(&rule->at);
}

void robo_css_rule_dispose(robo_css_rule_t *rule)
{
    if (!rule) {
        return;
    }
    robo_css_style_rule_dispose(&rule->style);
    robo_css_at_rule_dispose(&rule->at);
    rule->kind = ROBO_CSS_RULE_KIND_NONE;
}

robo_result_t robo_css_rule_make_style(robo_css_rule_t *rule, robo_css_diag_t *diag)
{
    if (!rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "rule is NULL");
    }
    robo_css_rule_dispose(rule);
    rule->kind = ROBO_CSS_RULE_KIND_STYLE;
    return ROBO_OK;
}

robo_result_t robo_css_rule_make_at(robo_css_rule_t *rule,
                                    robo_css_slice_t name,
                                    robo_css_diag_t *diag)
{
    robo_result_t result;

    if (!rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "rule is NULL");
    }
    robo_css_rule_dispose(rule);
    result = robo_css_at_rule_make(&rule->at, name, diag);
    if (result != ROBO_OK) {
        return result;
    }
    rule->kind = ROBO_CSS_RULE_KIND_AT;
    return ROBO_OK;
}

const robo_css_style_rule_t *robo_css_rule_style(const robo_css_rule_t *rule)
{
    if (!rule || rule->kind != ROBO_CSS_RULE_KIND_STYLE) {
        return NULL;
    }
    return &rule->style;
}

const robo_css_at_rule_t *robo_css_rule_at(const robo_css_rule_t *rule)
{
    if (!rule || rule->kind != ROBO_CSS_RULE_KIND_AT) {
        return NULL;
    }
    return &rule->at;
}

robo_css_range_t robo_css_rule_range(const robo_css_rule_t *rule)
{
    if (!rule) {
        return robo_css_range_at(0u);
    }
    if (rule->kind == ROBO_CSS_RULE_KIND_STYLE) {
        return rule->style.range;
    }
    if (rule->kind == ROBO_CSS_RULE_KIND_AT) {
        return rule->at.range;
    }
    return robo_css_range_at(0u);
}

size_t robo_css_rule_tree_count(const robo_css_rule_t *rule)
{
    size_t total = 1u;
    size_t i;

    if (!rule || rule->kind == ROBO_CSS_RULE_KIND_NONE) {
        return 0u;
    }
    if (rule->kind == ROBO_CSS_RULE_KIND_AT) {
        for (i = 0u; i < robo_css_at_rule_rule_count(&rule->at); i++) {
            total += robo_css_rule_tree_count(robo_css_at_rule_rule_at(&rule->at, i));
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Rule lists                                                          */
/* ------------------------------------------------------------------ */

void robo_css_rule_list_init(robo_css_rule_list_t *list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

void robo_css_rule_list_clear(robo_css_rule_list_t *list)
{
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0u; i < list->count; i++) {
        robo_css_rule_dispose(&list->items[i]);
    }
    list->count = 0u;
}

void robo_css_rule_list_dispose(robo_css_rule_list_t *list)
{
    if (!list) {
        return;
    }
    robo_css_rule_list_clear(list);
    if (list->items) {
        robo_free(list->items);
    }
    robo_css_rule_list_init(list);
}

robo_result_t robo_css_rule_list_append(robo_css_rule_list_t *list,
                                       robo_css_rule_t *rule,
                                       robo_css_diag_t *diag)
{
    if (!list || !rule) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "list or rule is NULL");
    }
    if (rule->kind == ROBO_CSS_RULE_KIND_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_INVALID_ARGUMENT,
                             "rule has not been made");
    }
    if (!robo_css_grow((void **)&list->items, &list->capacity, list->count,
                       sizeof(robo_css_rule_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_RULE, ROBO_OUT_OF_MEMORY,
                             "could not grow the rule list");
    }
    list->items[list->count] = *rule;
    list->count++;
    robo_css_rule_init(rule);
    return ROBO_OK;
}

size_t robo_css_rule_list_count(const robo_css_rule_list_t *list)
{
    return list ? list->count : 0u;
}

const robo_css_rule_t *robo_css_rule_list_at(const robo_css_rule_list_t *list,
                                             size_t index)
{
    if (!list || index >= list->count) {
        return NULL;
    }
    return &list->items[index];
}

int robo_css_rule_list_is_empty(const robo_css_rule_list_t *list)
{
    return !list || list->count == 0u;
}

size_t robo_css_rule_list_tree_count(const robo_css_rule_list_t *list)
{
    size_t i;
    size_t total = 0u;

    if (!list) {
        return 0u;
    }
    for (i = 0u; i < list->count; i++) {
        total += robo_css_rule_tree_count(&list->items[i]);
    }
    return total;
}

size_t robo_css_rule_list_style_rule_count(const robo_css_rule_list_t *list)
{
    size_t i;
    size_t total = 0u;

    if (!list) {
        return 0u;
    }
    for (i = 0u; i < list->count; i++) {
        const robo_css_rule_t *rule = &list->items[i];

        if (rule->kind == ROBO_CSS_RULE_KIND_STYLE) {
            total++;
        } else if (rule->kind == ROBO_CSS_RULE_KIND_AT && rule->at.children) {
            total += robo_css_rule_list_style_rule_count(rule->at.children);
        }
    }
    return total;
}

/** Depth-first flattening; returns the number of rules found at and below @p list. */
static size_t flatten_rules(const robo_css_rule_list_t *list,
                            const robo_css_rule_t **out_rules,
                            size_t capacity,
                            size_t *written)
{
    size_t i;
    size_t total = 0u;

    for (i = 0u; i < list->count; i++) {
        const robo_css_rule_t *rule = &list->items[i];

        total++;
        if (out_rules && *written < capacity) {
            out_rules[*written] = rule;
            (*written)++;
        }
        if (rule->kind == ROBO_CSS_RULE_KIND_AT && rule->at.children) {
            total += flatten_rules(rule->at.children, out_rules, capacity, written);
        }
    }
    return total;
}

size_t robo_css_rule_list_flatten(const robo_css_rule_list_t *list,
                                  const robo_css_rule_t **out_rules,
                                  size_t capacity)
{
    size_t written = 0u;

    if (!list) {
        return 0u;
    }
    if (!out_rules) {
        capacity = 0u;
    }
    return flatten_rules(list, out_rules, capacity, &written);
}
