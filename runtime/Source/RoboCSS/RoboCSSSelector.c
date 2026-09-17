/*
 * RoboCSS selector model (Phase 9.1) — see RoboCSSSelector.h.
 *
 * Storage and vocabulary only: no matching, no specificity, and no parsing.
 * The three ownership levels (compound owns simples, complex owns compounds and
 * combinators, list owns complex selectors) each follow the subsystem's one
 * rule — children are moved in, never copied in — with the single exception of
 * simple selectors, which own nothing and are therefore copied.
 */

#include "RoboCSS/RoboCSSSelector.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

int robo_css_simple_selector_kind_is_known(robo_css_simple_selector_kind_t kind)
{
    return kind >= ROBO_CSS_SIMPLE_SELECTOR_NONE &&
           kind < (robo_css_simple_selector_kind_t)ROBO_CSS_SIMPLE_SELECTOR_KIND_COUNT;
}

const char *robo_css_simple_selector_kind_name(robo_css_simple_selector_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_SIMPLE_SELECTOR_NONE:
        return "none";
    case ROBO_CSS_SIMPLE_SELECTOR_TYPE:
        return "type";
    case ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL:
        return "universal";
    case ROBO_CSS_SIMPLE_SELECTOR_CLASS:
        return "class";
    case ROBO_CSS_SIMPLE_SELECTOR_ID:
        return "id";
    case ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE:
        return "attribute";
    case ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS:
        return "pseudo-class";
    case ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT:
        return "pseudo-element";
    default:
        return "unknown";
    }
}

int robo_css_attribute_match_is_known(robo_css_attribute_match_t match)
{
    return match >= ROBO_CSS_ATTRIBUTE_MATCH_NONE &&
           match < (robo_css_attribute_match_t)ROBO_CSS_ATTRIBUTE_MATCH_COUNT;
}

const char *robo_css_attribute_match_name(robo_css_attribute_match_t match)
{
    switch (match) {
    case ROBO_CSS_ATTRIBUTE_MATCH_NONE:
        return "none";
    case ROBO_CSS_ATTRIBUTE_MATCH_EQUALS:
        return "equals";
    case ROBO_CSS_ATTRIBUTE_MATCH_INCLUDES:
        return "includes";
    case ROBO_CSS_ATTRIBUTE_MATCH_DASH:
        return "dash";
    case ROBO_CSS_ATTRIBUTE_MATCH_PREFIX:
        return "prefix";
    case ROBO_CSS_ATTRIBUTE_MATCH_SUFFIX:
        return "suffix";
    case ROBO_CSS_ATTRIBUTE_MATCH_SUBSTRING:
        return "substring";
    default:
        return "unknown";
    }
}

int robo_css_attribute_case_is_known(robo_css_attribute_case_t flag)
{
    return flag >= ROBO_CSS_ATTRIBUTE_CASE_DEFAULT &&
           flag < (robo_css_attribute_case_t)ROBO_CSS_ATTRIBUTE_CASE_COUNT;
}

const char *robo_css_attribute_case_name(robo_css_attribute_case_t flag)
{
    switch (flag) {
    case ROBO_CSS_ATTRIBUTE_CASE_DEFAULT:
        return "default";
    case ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE:
        return "insensitive";
    case ROBO_CSS_ATTRIBUTE_CASE_SENSITIVE:
        return "sensitive";
    default:
        return "unknown";
    }
}

int robo_css_combinator_is_known(robo_css_combinator_t combinator)
{
    return combinator >= ROBO_CSS_COMBINATOR_NONE &&
           combinator < (robo_css_combinator_t)ROBO_CSS_COMBINATOR_COUNT;
}

const char *robo_css_combinator_name(robo_css_combinator_t combinator)
{
    switch (combinator) {
    case ROBO_CSS_COMBINATOR_NONE:
        return "none";
    case ROBO_CSS_COMBINATOR_DESCENDANT:
        return "descendant";
    case ROBO_CSS_COMBINATOR_CHILD:
        return "child";
    case ROBO_CSS_COMBINATOR_NEXT_SIBLING:
        return "next-sibling";
    case ROBO_CSS_COMBINATOR_SUBSEQUENT_SIBLING:
        return "subsequent-sibling";
    case ROBO_CSS_COMBINATOR_COLUMN:
        return "column";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Simple selectors                                                    */
/* ------------------------------------------------------------------ */

void robo_css_simple_selector_init(robo_css_simple_selector_t *selector)
{
    if (!selector) {
        return;
    }
    selector->kind = ROBO_CSS_SIMPLE_SELECTOR_NONE;
    selector->range = robo_css_range_at(0u);
    selector->name = robo_css_slice_empty();
    selector->attribute = robo_css_slice_empty();
    selector->attribute_value = robo_css_slice_empty();
    selector->match = ROBO_CSS_ATTRIBUTE_MATCH_NONE;
    selector->case_flag = ROBO_CSS_ATTRIBUTE_CASE_DEFAULT;
    selector->value_was_string = 0;
    selector->argument = robo_css_slice_empty();
}

void robo_css_simple_selector_dispose(robo_css_simple_selector_t *selector)
{
    robo_css_simple_selector_init(selector);
}

/** Non-zero when a simple selector of @p kind must carry a name. */
static int simple_kind_needs_name(robo_css_simple_selector_kind_t kind)
{
    return kind == ROBO_CSS_SIMPLE_SELECTOR_TYPE ||
           kind == ROBO_CSS_SIMPLE_SELECTOR_CLASS ||
           kind == ROBO_CSS_SIMPLE_SELECTOR_ID ||
           kind == ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS ||
           kind == ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT;
}

robo_result_t robo_css_simple_selector_make(robo_css_simple_selector_t *selector,
                                            robo_css_simple_selector_kind_t kind,
                                            robo_css_slice_t name,
                                            robo_css_diag_t *diag)
{
    if (!selector) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "selector is NULL");
    }
    if (!robo_css_simple_selector_kind_is_known(kind) ||
        kind == ROBO_CSS_SIMPLE_SELECTOR_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "unknown or absent simple selector kind");
    }
    /* ATTRIBUTE carries its name through set_attribute, which also has to record
     * the comparison; making one here without it would leave the record half
     * described, so a name is not required for it and is not rejected either. */
    if (simple_kind_needs_name(kind) && robo_css_slice_is_empty(name)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "this simple selector kind needs a name");
    }
    robo_css_simple_selector_dispose(selector);
    selector->kind = kind;
    selector->name = name;
    return ROBO_OK;
}

robo_result_t robo_css_simple_selector_set_attribute(
    robo_css_simple_selector_t *selector,
    robo_css_slice_t attribute,
    robo_css_attribute_match_t match,
    robo_css_slice_t value,
    robo_css_attribute_case_t case_flag,
    robo_css_diag_t *diag)
{
    if (!selector) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "selector is NULL");
    }
    if (selector->kind != ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "not an attribute selector");
    }
    if (robo_css_slice_is_empty(attribute)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "an attribute selector needs an attribute name");
    }
    if (!robo_css_attribute_match_is_known(match)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "unknown attribute match operator");
    }
    if (!robo_css_attribute_case_is_known(case_flag)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "unknown attribute case flag");
    }
    /* Presence is the absence of a comparison, not a comparison against
     * nothing: `[a]` and `[a=""]` are different selectors and must stay
     * distinguishable, which is why a comparison needs a value. */
    if (match != ROBO_CSS_ATTRIBUTE_MATCH_NONE && robo_css_slice_is_absent(value)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "a comparison needs a value");
    }
    selector->attribute = attribute;
    selector->match = match;
    selector->attribute_value = value;
    selector->case_flag = case_flag;
    return ROBO_OK;
}

robo_result_t robo_css_simple_selector_set_argument(
    robo_css_simple_selector_t *selector,
    robo_css_slice_t argument,
    robo_css_diag_t *diag)
{
    if (!selector) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "selector is NULL");
    }
    if (selector->kind != ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS &&
        selector->kind != ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "only a pseudo selector takes an argument");
    }
    selector->argument = argument;
    return ROBO_OK;
}

int robo_css_simple_selector_is_set(const robo_css_simple_selector_t *selector)
{
    return selector && selector->kind != ROBO_CSS_SIMPLE_SELECTOR_NONE;
}

/* ------------------------------------------------------------------ */
/* Compound selectors                                                  */
/* ------------------------------------------------------------------ */

void robo_css_compound_selector_init(robo_css_compound_selector_t *compound)
{
    if (!compound) {
        return;
    }
    compound->items = NULL;
    compound->count = 0u;
    compound->capacity = 0u;
    compound->range = robo_css_range_at(0u);
}

void robo_css_compound_selector_dispose(robo_css_compound_selector_t *compound)
{
    if (!compound) {
        return;
    }
    /* Simple selectors own nothing, so there is nothing to dispose element by
     * element; the array itself is the only allocation. */
    if (compound->items) {
        robo_free(compound->items);
    }
    robo_css_compound_selector_init(compound);
}

robo_result_t robo_css_compound_selector_append(
    robo_css_compound_selector_t *compound,
    const robo_css_simple_selector_t *selector,
    robo_css_diag_t *diag)
{
    if (!compound || !selector) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "compound or selector is NULL");
    }
    if (!robo_css_simple_selector_is_set(selector)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "selector is not set");
    }
    if (!robo_css_grow((void **)&compound->items, &compound->capacity, compound->count,
                       sizeof(robo_css_simple_selector_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_OUT_OF_MEMORY,
                             "could not grow the simple selector array");
    }
    compound->items[compound->count] = *selector;
    compound->count++;
    return ROBO_OK;
}

size_t robo_css_compound_selector_count(const robo_css_compound_selector_t *compound)
{
    return compound ? compound->count : 0u;
}

const robo_css_simple_selector_t *robo_css_compound_selector_at(
    const robo_css_compound_selector_t *compound,
    size_t index)
{
    if (!compound || index >= compound->count) {
        return NULL;
    }
    return &compound->items[index];
}

int robo_css_compound_selector_is_set(const robo_css_compound_selector_t *compound)
{
    return compound && compound->count > 0u;
}

void robo_css_compound_selector_seal_range(robo_css_compound_selector_t *compound)
{
    size_t i;

    if (!compound || compound->count == 0u) {
        if (compound) {
            compound->range = robo_css_range_at(0u);
        }
        return;
    }
    compound->range = compound->items[0].range;
    for (i = 1u; i < compound->count; i++) {
        if (compound->items[i].range.begin < compound->range.begin) {
            compound->range.begin = compound->items[i].range.begin;
        }
        if (compound->items[i].range.end > compound->range.end) {
            compound->range.end = compound->items[i].range.end;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                   */
/* ------------------------------------------------------------------ */

void robo_css_complex_selector_init(robo_css_complex_selector_t *selector)
{
    if (!selector) {
        return;
    }
    selector->compounds = NULL;
    selector->compound_count = 0u;
    selector->compound_capacity = 0u;
    selector->combinators = NULL;
    selector->combinator_count = 0u;
    selector->combinator_capacity = 0u;
    selector->range = robo_css_range_at(0u);
}

void robo_css_complex_selector_dispose(robo_css_complex_selector_t *selector)
{
    size_t i;

    if (!selector) {
        return;
    }
    for (i = 0u; i < selector->compound_count; i++) {
        robo_css_compound_selector_dispose(&selector->compounds[i]);
    }
    if (selector->compounds) {
        robo_free(selector->compounds);
    }
    if (selector->combinators) {
        robo_free(selector->combinators);
    }
    robo_css_complex_selector_init(selector);
}

robo_result_t robo_css_complex_selector_append_compound(
    robo_css_complex_selector_t *selector,
    robo_css_compound_selector_t *compound,
    robo_css_diag_t *diag)
{
    if (!selector || !compound) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "selector or compound is NULL");
    }
    if (!robo_css_compound_selector_is_set(compound)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "compound selector is empty");
    }
    if (!robo_css_grow((void **)&selector->compounds, &selector->compound_capacity,
                       selector->compound_count, sizeof(robo_css_compound_selector_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_OUT_OF_MEMORY,
                             "could not grow the compound selector array");
    }
    selector->compounds[selector->compound_count] = *compound;
    selector->compound_count++;
    robo_css_compound_selector_init(compound);
    return ROBO_OK;
}

robo_result_t robo_css_complex_selector_append_combinator(
    robo_css_complex_selector_t *selector,
    robo_css_combinator_t combinator,
    robo_css_diag_t *diag)
{
    if (!selector) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "selector is NULL");
    }
    if (!robo_css_combinator_is_known(combinator) ||
        combinator == ROBO_CSS_COMBINATOR_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "a combinator must be one of the defined kinds");
    }
    if (!robo_css_grow((void **)&selector->combinators, &selector->combinator_capacity,
                       selector->combinator_count, sizeof(robo_css_combinator_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_OUT_OF_MEMORY,
                             "could not grow the combinator array");
    }
    selector->combinators[selector->combinator_count] = combinator;
    selector->combinator_count++;
    return ROBO_OK;
}

size_t robo_css_complex_selector_compound_count(
    const robo_css_complex_selector_t *selector)
{
    return selector ? selector->compound_count : 0u;
}

const robo_css_compound_selector_t *robo_css_complex_selector_compound_at(
    const robo_css_complex_selector_t *selector,
    size_t index)
{
    if (!selector || index >= selector->compound_count) {
        return NULL;
    }
    return &selector->compounds[index];
}

size_t robo_css_complex_selector_combinator_count(
    const robo_css_complex_selector_t *selector)
{
    return selector ? selector->combinator_count : 0u;
}

robo_css_combinator_t robo_css_complex_selector_combinator_at(
    const robo_css_complex_selector_t *selector,
    size_t index)
{
    if (!selector || index >= selector->combinator_count) {
        return ROBO_CSS_COMBINATOR_NONE;
    }
    return selector->combinators[index];
}

int robo_css_complex_selector_is_well_formed(
    const robo_css_complex_selector_t *selector)
{
    if (!selector || selector->compound_count == 0u) {
        return 0;
    }
    return selector->combinator_count == selector->compound_count - 1u;
}

/* ------------------------------------------------------------------ */
/* Selector lists                                                      */
/* ------------------------------------------------------------------ */

void robo_css_selector_list_init(robo_css_selector_list_t *list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
    list->range = robo_css_range_at(0u);
}

void robo_css_selector_list_dispose(robo_css_selector_list_t *list)
{
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0u; i < list->count; i++) {
        robo_css_complex_selector_dispose(&list->items[i]);
    }
    if (list->items) {
        robo_free(list->items);
    }
    robo_css_selector_list_init(list);
}

robo_result_t robo_css_selector_list_append(robo_css_selector_list_t *list,
                                            robo_css_complex_selector_t *selector,
                                            robo_css_diag_t *diag)
{
    if (!list || !selector) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "list or selector is NULL");
    }
    if (selector->compound_count == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_INVALID_ARGUMENT,
                             "complex selector has no compounds");
    }
    if (!robo_css_grow((void **)&list->items, &list->capacity, list->count,
                       sizeof(robo_css_complex_selector_t))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SELECTOR, ROBO_OUT_OF_MEMORY,
                             "could not grow the selector list");
    }
    list->items[list->count] = *selector;
    list->count++;
    robo_css_complex_selector_init(selector);
    return ROBO_OK;
}

size_t robo_css_selector_list_count(const robo_css_selector_list_t *list)
{
    return list ? list->count : 0u;
}

const robo_css_complex_selector_t *robo_css_selector_list_at(
    const robo_css_selector_list_t *list,
    size_t index)
{
    if (!list || index >= list->count) {
        return NULL;
    }
    return &list->items[index];
}

int robo_css_selector_list_is_set(const robo_css_selector_list_t *list)
{
    return list && list->count > 0u;
}

int robo_css_selector_list_is_well_formed(const robo_css_selector_list_t *list)
{
    size_t i;

    if (!list || list->count == 0u) {
        return 0;
    }
    for (i = 0u; i < list->count; i++) {
        if (!robo_css_complex_selector_is_well_formed(&list->items[i])) {
            return 0;
        }
    }
    return 1;
}
