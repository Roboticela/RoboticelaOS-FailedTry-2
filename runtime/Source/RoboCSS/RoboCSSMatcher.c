/*
 * RoboCSS selector matching (Phase 9.4) — see Include/RoboCSS/RoboCSSMatcher.h.
 *
 * The whole implementation is one walk and one predicate:
 *
 *   match_from(selector, index, element)
 *       match compound[index] against element
 *       if index == 0, matched
 *       otherwise the combinator before it says how to find a candidate for
 *       compound[index-1]: the parent, some ancestor, the previous element
 *       sibling, or some earlier element sibling
 *
 * and a compound is a loop over its simple selectors against one view.
 *
 * Right-to-left is what makes this cheap in practice: the caller has an element
 * and wants to know whether it is the *subject* of the selector, which is the
 * right-most compound, so the first test is the one that rejects almost
 * everything. The recursion depth is the number of compounds, never the depth of
 * the tree, so no stylesheet can exhaust the stack however deep the document is.
 *
 * Two walks are bounded on purpose. Walking ancestors and walking siblings both
 * follow a host's links, and a host whose links describe a cycle rather than a
 * tree would otherwise spin forever. The bound is far above any possible
 * document (the DOM caps a document's nodes well below it), and exceeding it is
 * reported as a failure rather than as a silent "no match", because a truncated
 * walk and a real negative are different facts.
 */

#include "RoboCSS/RoboCSSMatcher.h"

#include "RoboCSSInternal.h"
/* For the parsed An+B form and its membership test, which the parser header owns
 * — the matcher consumes them and interprets nothing else of that header. */
#include "RoboCSS/RoboCSSSelectorParser.h"

/* ------------------------------------------------------------------ */
/* Bounds and state                                                    */
/* ------------------------------------------------------------------ */

/*
 * How many links one walk may follow before the host is judged to describe
 * something that is not a tree. A document cannot hold this many nodes, so a
 * walk that reaches it is a cycle, not a deep tree.
 */
#define MATCH_WALK_LIMIT 65536u

typedef enum match_relation {
    MATCH_RELATION_PARENT = 0,
    MATCH_RELATION_PREVIOUS = 1,
    MATCH_RELATION_NEXT = 2
} match_relation_t;

typedef struct match_context {
    const robo_css_element_host_t *host;
    robo_css_selector_match_t *out;
    int unusable; /* the matcher could not run at all */
} match_context_t;

void robo_css_selector_match_init(robo_css_selector_match_t *out)
{
    if (!out) {
        return;
    }
    out->matched = 0;
    out->unsupported = 0;
    out->condition = robo_css_slice_empty();
    out->selector_index = 0u;
    out->range = robo_css_range_at(0u);
}

/* ------------------------------------------------------------------ */
/* Relations                                                           */
/* ------------------------------------------------------------------ */

static int match_relation_available(const robo_css_element_host_t *host,
                                    match_relation_t relation)
{
    if (!host) {
        return 0;
    }
    switch (relation) {
    case MATCH_RELATION_PARENT:
        return host->parent != NULL;
    case MATCH_RELATION_PREVIOUS:
        return host->previous_sibling != NULL;
    case MATCH_RELATION_NEXT:
        return host->next_sibling != NULL;
    default:
        return 0;
    }
}

/*
 * Ask the host for one relation.
 *
 * An unavailable callback and a host that answers "there is none" both yield
 * ROBO_CSS_ELEMENT_REF_NONE, and the two are distinguished by `unsupported`:
 * the first is a condition this build cannot evaluate, the second is a fact
 * about the element (`:first-child` *is* "no previous sibling"). Getting that
 * wrong in either direction would be a wrong stylesheet, so every caller that
 * would read the answer as a *fact* goes through match_relation_answer below,
 * which refuses the answer when the relation was unavailable.
 */
static robo_css_element_ref_t match_relation(match_context_t *context,
                                             robo_css_element_ref_t element,
                                             match_relation_t relation)
{
    if (!match_relation_available(context->host, relation)) {
        context->out->unsupported = 1;
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    switch (relation) {
    case MATCH_RELATION_PARENT:
        return context->host->parent(context->host->userdata, element);
    case MATCH_RELATION_PREVIOUS:
        return context->host->previous_sibling(context->host->userdata, element);
    case MATCH_RELATION_NEXT:
        return context->host->next_sibling(context->host->userdata, element);
    default:
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
}

/**
 * Ask for one relation and say whether the answer means anything.
 *
 * @return 1 and writes the relation to @p out_ref when the host answered; 0 when
 *         the relation is unavailable, having set `unsupported` — in which case
 *         the caller must abandon the condition instead of reading
 *         ROBO_CSS_ELEMENT_REF_NONE as "there is none". `:root` and
 *         `:first-child` are exactly the conditions that turn that confusion into
 *         a matched element.
 */
static int match_relation_answer(match_context_t *context,
                                 robo_css_element_ref_t element,
                                 match_relation_t relation,
                                 robo_css_element_ref_t *out_ref)
{
    robo_css_element_ref_t ref = match_relation(context, element, relation);

    if (context->out->unsupported) {
        return 0;
    }
    *out_ref = ref;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Byte predicates                                                     */
/* ------------------------------------------------------------------ */

static int match_bytes_equal(robo_css_slice_t a, robo_css_slice_t b, int fold_ascii_case)
{
    size_t i;

    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    for (i = 0u; i < a.length; i++) {
        uint8_t left = a.bytes[i];
        uint8_t right = b.bytes[i];

        if (fold_ascii_case) {
            left = robo_css_ascii_lower(left);
            right = robo_css_ascii_lower(right);
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

static int match_bytes_starts_with(robo_css_slice_t value,
                                   robo_css_slice_t prefix,
                                   int fold_ascii_case)
{
    if (prefix.length > value.length || !value.bytes || !prefix.bytes) {
        return 0;
    }
    return match_bytes_equal(robo_css_slice_from_bytes(value.bytes, prefix.length),
                             prefix, fold_ascii_case);
}

static int match_bytes_ends_with(robo_css_slice_t value,
                                 robo_css_slice_t suffix,
                                 int fold_ascii_case)
{
    if (suffix.length > value.length || !value.bytes || !suffix.bytes) {
        return 0;
    }
    return match_bytes_equal(robo_css_slice_from_bytes(value.bytes + value.length -
                                                          suffix.length,
                                                      suffix.length),
                             suffix, fold_ascii_case);
}

static int match_bytes_contains(robo_css_slice_t value,
                                robo_css_slice_t needle,
                                int fold_ascii_case)
{
    size_t offset;

    if (needle.length == 0u) {
        return 0;
    }
    if (needle.length > value.length || !value.bytes || !needle.bytes) {
        return 0;
    }
    for (offset = 0u; offset + needle.length <= value.length; offset++) {
        if (match_bytes_equal(robo_css_slice_from_bytes(value.bytes + offset,
                                                        needle.length),
                              needle, fold_ascii_case)) {
            return 1;
        }
    }
    return 0;
}

/** Non-zero when @p needle is one CSS-whitespace-separated word of @p value. */
static int match_bytes_has_word(robo_css_slice_t value,
                                robo_css_slice_t needle,
                                int fold_ascii_case)
{
    size_t at = 0u;

    if (needle.length == 0u || !value.bytes) {
        return 0;
    }
    /*
     * The value is split here rather than through a helper because only the
     * split matters: a run of whitespace is one separator, leading and trailing
     * whitespace produce no empty word, and `class="a  b"` therefore has the
     * two words CSS says it has.
     */
    while (at < value.length) {
        size_t begin;
        size_t end;

        while (at < value.length && robo_css_ascii_is_space(value.bytes[at])) {
            at++;
        }
        begin = at;
        while (at < value.length && !robo_css_ascii_is_space(value.bytes[at])) {
            at++;
        }
        end = at;
        if (end > begin &&
            match_bytes_equal(robo_css_slice_from_bytes(value.bytes + begin,
                                                        end - begin),
                              needle, fold_ascii_case)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Attribute selectors                                                 */
/* ------------------------------------------------------------------ */

static int match_attribute(const match_context_t *context,
                           const robo_css_element_view_t *view,
                           const robo_css_simple_selector_t *simple)
{
    robo_css_slice_t value;
    int fold;

    (void)context;
    if (!robo_css_element_view_find_attribute(view, simple->attribute, &value)) {
        /* Absent is "no match" for every operator, presence included: `[a]` asks
         * whether a record exists, and none does. Nothing is inferred from an
         * empty value, which is why `[a=""]` and `[a]` stay different questions. */
        return 0;
    }
    fold = simple->case_flag == ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE;

    switch (simple->match) {
    case ROBO_CSS_ATTRIBUTE_MATCH_NONE:
        return 1;
    case ROBO_CSS_ATTRIBUTE_MATCH_EQUALS:
        return match_bytes_equal(value, simple->attribute_value, fold);
    case ROBO_CSS_ATTRIBUTE_MATCH_INCLUDES:
        return match_bytes_has_word(value, simple->attribute_value, fold);
    case ROBO_CSS_ATTRIBUTE_MATCH_DASH:
        /* `[lang|=en]` is `en` or `en-…`: the dash is part of the match, so an
         * empty operand cannot match anything (every value starts with `-`
         * against an empty prefix, which the specification forbids). */
        if (robo_css_slice_is_empty(simple->attribute_value)) {
            return 0;
        }
        if (match_bytes_equal(value, simple->attribute_value, fold)) {
            return 1;
        }
        if (!match_bytes_starts_with(value, simple->attribute_value, fold)) {
            return 0;
        }
        return value.length > simple->attribute_value.length &&
               value.bytes[simple->attribute_value.length] == (uint8_t)'-';
    case ROBO_CSS_ATTRIBUTE_MATCH_PREFIX:
        return !robo_css_slice_is_empty(simple->attribute_value) &&
               match_bytes_starts_with(value, simple->attribute_value, fold);
    case ROBO_CSS_ATTRIBUTE_MATCH_SUFFIX:
        return !robo_css_slice_is_empty(simple->attribute_value) &&
               match_bytes_ends_with(value, simple->attribute_value, fold);
    case ROBO_CSS_ATTRIBUTE_MATCH_SUBSTRING:
        return match_bytes_contains(value, simple->attribute_value, fold);
    default:
        /* An unknown operator on a hand-built selector: refused rather than
         * guessed at, and not reported as unsupported because the selector is
         * broken rather than ahead of this build. */
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Structural pseudo-classes                                           */
/* ------------------------------------------------------------------ */

/** 1-based position of @p element among its element siblings, or 0 on failure. */
static int match_element_index(match_context_t *context,
                               robo_css_element_ref_t element,
                               match_relation_t direction,
                               int *out_index)
{
    robo_css_element_ref_t cursor = element;
    size_t steps = 0u;
    int index = 1;

    for (;;) {
        robo_css_element_ref_t next = match_relation(context, cursor, direction);

        if (context->out->unsupported) {
            return 0;
        }
        if (next == ROBO_CSS_ELEMENT_REF_NONE) {
            break;
        }
        if (++steps > MATCH_WALK_LIMIT) {
            /* Not a tree. The count is meaningless, so the condition is reported
             * as unevaluatable rather than answered with whatever index the walk
             * had reached. */
            context->out->unsupported = 1;
            return 0;
        }
        cursor = next;
        index++;
    }
    *out_index = index;
    return 1;
}

static int match_structural_pseudo(match_context_t *context,
                                   robo_css_element_ref_t element,
                                   const robo_css_simple_selector_t *simple,
                                   const robo_css_selector_list_t *argument,
                                   int *out_handled)
{
    /*
     * Every one of these is a comparison against "there is no such element", so
     * each has to establish that the host can answer at all: a host with no
     * parent callback returns NONE for the same reason a root element does, and
     * reading the two the same way would report `:root` as matched on every
     * element of such a host.
     */
    robo_css_element_ref_t relation;

    *out_handled = 1;

    if (robo_css_simple_selector_is_pseudo_named(simple, "root")) {
        /* The document element is the one element with no parent element. The
         * seam says "no parent", so this needs no document and no special
         * "html" test. */
        if (!match_relation_answer(context, element, MATCH_RELATION_PARENT, &relation)) {
            return 0;
        }
        return relation == ROBO_CSS_ELEMENT_REF_NONE;
    }
    if (robo_css_simple_selector_is_pseudo_named(simple, "first-child")) {
        if (!match_relation_answer(context, element, MATCH_RELATION_PREVIOUS, &relation)) {
            return 0;
        }
        return relation == ROBO_CSS_ELEMENT_REF_NONE;
    }
    if (robo_css_simple_selector_is_pseudo_named(simple, "last-child")) {
        if (!match_relation_answer(context, element, MATCH_RELATION_NEXT, &relation)) {
            return 0;
        }
        return relation == ROBO_CSS_ELEMENT_REF_NONE;
    }
    if (robo_css_simple_selector_is_pseudo_named(simple, "only-child")) {
        if (!match_relation_answer(context, element, MATCH_RELATION_PREVIOUS, &relation) ||
            relation != ROBO_CSS_ELEMENT_REF_NONE) {
            return 0;
        }
        if (!match_relation_answer(context, element, MATCH_RELATION_NEXT, &relation)) {
            return 0;
        }
        return relation == ROBO_CSS_ELEMENT_REF_NONE;
    }
    if (robo_css_simple_selector_is_pseudo_named(simple, "nth-child") ||
        robo_css_simple_selector_is_pseudo_named(simple, "nth-last-child")) {
        robo_css_an_plus_b_t form;
        int from_end = robo_css_simple_selector_is_pseudo_named(simple, "nth-last-child");
        int index = 0;

        if (!robo_css_simple_selector_has_nth(simple)) {
            /* A hand-built `:nth-child()` with no parsed argument: the condition
             * exists but this build cannot evaluate it. */
            context->out->unsupported = 1;
            context->out->condition = simple->name;
            return 0;
        }
        form.a = robo_css_simple_selector_nth_a(simple);
        form.b = robo_css_simple_selector_nth_b(simple);
        if (!match_element_index(context, element,
                                 from_end ? MATCH_RELATION_NEXT
                                          : MATCH_RELATION_PREVIOUS,
                                 &index)) {
            return 0;
        }
        return robo_css_an_plus_b_matches(&form, index);
    }
    if (robo_css_simple_selector_is_pseudo_named(simple, "not")) {
        int inner;

        if (!argument) {
            /* `:not()` whose argument was never parsed: unevaluatable, and it
             * must not be read as "matches", which would apply rules the author
             * negated away. */
            context->out->unsupported = 1;
            context->out->condition = simple->name;
            return 0;
        }
        /* A record of its own: the inner attempt must not clear the outer one,
         * and its verdict is merged in explicitly afterwards. */
        robo_css_selector_match_t inner_match;

        inner = robo_css_selector_list_matches(argument, context->host, element,
                                              &inner_match, NULL);
        if (inner < 0) {
            context->unusable = 1;
            return 0;
        }
        if (inner_match.unsupported) {
            /* The argument could not be evaluated, so neither can its negation,
             * and it must not be read as "does not match" — that would apply the
             * very rules the author negated away. */
            context->out->unsupported = 1;
            if (robo_css_slice_is_absent(context->out->condition)) {
                context->out->condition = inner_match.condition;
            }
            return 0;
        }
        return !inner;
    }

    *out_handled = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Simple and compound selectors                                       */
/* ------------------------------------------------------------------ */

/* Declared before use: a complex selector's walk needs a compound, and a
 * compound's `:not()` needs a list. Both directions are real. */
static int match_from(match_context_t *context,
                      const robo_css_complex_selector_t *selector,
                      size_t index,
                      robo_css_element_ref_t element);

static int match_simple_against(match_context_t *context,
                                const robo_css_element_view_t *view,
                                robo_css_element_ref_t element,
                                const robo_css_compound_selector_t *compound,
                                size_t simple_index,
                                const robo_css_simple_selector_t *simple)
{
    switch (simple->kind) {
    case ROBO_CSS_SIMPLE_SELECTOR_TYPE:
        /* HTML element names are ASCII case-insensitive, and the DOM stores
         * lowercase, so `DIV` and `div` name the same element. The author's
         * spelling is what `name` holds; the comparison is what folds. */
        return match_bytes_equal(view->tag_name, simple->name, 1);
    case ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL:
        return 1;
    case ROBO_CSS_SIMPLE_SELECTOR_CLASS:
        /* Class names are case-sensitive in HTML, and a class attribute is a
         * whitespace-separated list — never a substring. The host hands over the
         * list, so this is a word comparison rather than a search. */
        return robo_css_element_view_has_class(view, simple->name);
    case ROBO_CSS_SIMPLE_SELECTOR_ID:
        return robo_css_element_view_id_equals(view, simple->name);
    case ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE:
        return match_attribute(context, view, simple);
    case ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT:
        /*
         * An element is not its own `::before`. `div::before` styles generated
         * content, which this build does not generate, so it matches no element
         * — and saying "no" is right rather than unsupported, because the
         * question was answered: this element is the div, not the content
         * before it. A later phase that generates content matches pseudo-elements
         * by giving the matcher a subject, and this case is where that lands.
         */
        return 0;
    case ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS: {
        const robo_css_selector_list_t *argument =
            compound ? robo_css_compound_selector_argument_of(compound, simple_index)
                     : NULL;
        int handled = 0;
        int result = match_structural_pseudo(context, element, simple, argument,
                                            &handled);

        if (!handled) {
            /* `:hover`, `:focus`, `:lang(en)`, `:nth-of-type(2)`, … — real
             * pseudo-classes whose state or subject this build does not have.
             * Reported, never assumed true. */
            context->out->unsupported = 1;
            context->out->condition = simple->name;
            return 0;
        }
        return result;
    }
    default:
        return 0;
    }
}

static int match_compound(match_context_t *context,
                          robo_css_element_ref_t element,
                          const robo_css_compound_selector_t *compound)
{
    robo_css_element_view_t view;
    size_t count;
    size_t i;

    if (element == ROBO_CSS_ELEMENT_REF_NONE || !compound ||
        robo_css_compound_selector_count(compound) == 0u) {
        return 0;
    }
    if (context->host->view(context->host->userdata, element, &view) != ROBO_OK) {
        /* The element is gone, or the host cannot describe it. Either way this
         * candidate matches nothing; the caller's walk continues with the next
         * one rather than failing the whole match. */
        return 0;
    }
    count = robo_css_compound_selector_count(compound);
    for (i = 0u; i < count; i++) {
        const robo_css_simple_selector_t *simple =
            robo_css_compound_selector_at(compound, i);

        if (!simple) {
            return 0;
        }
        if (!match_simple_against(context, &view, element, compound, i, simple)) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                   */
/* ------------------------------------------------------------------ */

static int match_walk(match_context_t *context,
                      const robo_css_complex_selector_t *selector,
                      size_t index,
                      robo_css_element_ref_t element,
                      match_relation_t relation)
{
    robo_css_element_ref_t candidate = element;
    size_t steps = 0u;

    for (;;) {
        candidate = match_relation(context, candidate, relation);
        if (context->out->unsupported) {
            return 0;
        }
        if (candidate == ROBO_CSS_ELEMENT_REF_NONE) {
            return 0;
        }
        if (++steps > MATCH_WALK_LIMIT) {
            /* The same: a host whose links cycle is not a document, and a
             * truncated walk must not be reported as `no match`. */
            context->out->unsupported = 1;
            return 0;
        }
        if (match_from(context, selector, index, candidate)) {
            return 1;
        }
        if (context->unusable) {
            return 0;
        }
    }
}

static int match_from(match_context_t *context,
                      const robo_css_complex_selector_t *selector,
                      size_t index,
                      robo_css_element_ref_t element)
{
    const robo_css_compound_selector_t *compound =
        robo_css_complex_selector_compound_at(selector, index);

    if (!match_compound(context, element, compound)) {
        return 0;
    }
    if (index == 0u) {
        return 1;
    }

    {
        robo_css_combinator_t combinator =
            robo_css_complex_selector_combinator_at(selector, index - 1u);

        switch (combinator) {
        case ROBO_CSS_COMBINATOR_CHILD: {
            robo_css_element_ref_t parent;

            if (!match_relation_answer(context, element, MATCH_RELATION_PARENT,
                                       &parent) ||
                parent == ROBO_CSS_ELEMENT_REF_NONE) {
                return 0;
            }
            return match_from(context, selector, index - 1u, parent);
        }
        case ROBO_CSS_COMBINATOR_DESCENDANT:
            return match_walk(context, selector, index - 1u, element,
                              MATCH_RELATION_PARENT);
        case ROBO_CSS_COMBINATOR_NEXT_SIBLING: {
            robo_css_element_ref_t previous;

            if (!match_relation_answer(context, element, MATCH_RELATION_PREVIOUS,
                                       &previous) ||
                previous == ROBO_CSS_ELEMENT_REF_NONE) {
                return 0;
            }
            return match_from(context, selector, index - 1u, previous);
        }
        case ROBO_CSS_COMBINATOR_SUBSEQUENT_SIBLING:
            return match_walk(context, selector, index - 1u, element,
                              MATCH_RELATION_PREVIOUS);
        case ROBO_CSS_COMBINATOR_COLUMN:
            /* `||` selects table columns, which need a table model the DOM does
             * not have yet. The combinator parses and is represented; matching it
             * is reported rather than invented. */
            context->out->unsupported = 1;
            return 0;
        default:
            /* NONE between two compounds is a structural hole; the
             * well-formedness check rejects it before the walk starts. */
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

static int match_start(const robo_css_element_host_t *host,
                       robo_css_element_ref_t element,
                       robo_css_selector_match_t *out,
                       match_context_t *context)
{
    if (!out || !host || !robo_css_element_host_is_usable(host)) {
        return -1;
    }
    robo_css_selector_match_init(out);
    if (element == ROBO_CSS_ELEMENT_REF_NONE) {
        return 0;
    }
    context->host = host;
    context->out = out;
    context->unusable = 0;
    return 1;
}

int robo_css_simple_selector_matches(const robo_css_simple_selector_t *simple,
                                     const robo_css_compound_selector_t *compound,
                                     size_t simple_index,
                                     const robo_css_element_host_t *host,
                                     robo_css_element_ref_t element,
                                     robo_css_selector_match_t *out,
                                     robo_css_diag_t *diag)
{
    match_context_t context;
    robo_css_element_view_t view;
    int started;

    (void)diag;
    if (!simple) {
        return -1;
    }
    started = match_start(host, element, out, &context);
    if (started <= 0) {
        return started;
    }
    if (host->view(host->userdata, element, &view) != ROBO_OK) {
        return 0;
    }
    if (match_simple_against(&context, &view, element, compound, simple_index,
                             simple)) {
        out->matched = 1;
    }
    return out->matched;
}

int robo_css_compound_selector_matches(const robo_css_compound_selector_t *compound,
                                       const robo_css_element_host_t *host,
                                       robo_css_element_ref_t element,
                                       robo_css_selector_match_t *out,
                                       robo_css_diag_t *diag)
{
    match_context_t context;
    int started;

    (void)diag;
    if (!compound) {
        return -1;
    }
    started = match_start(host, element, out, &context);
    if (started <= 0) {
        return started;
    }
    if (match_compound(&context, element, compound)) {
        out->matched = 1;
    }
    return out->matched;
}

int robo_css_complex_selector_matches(const robo_css_complex_selector_t *selector,
                                      const robo_css_element_host_t *host,
                                      robo_css_element_ref_t element,
                                      robo_css_selector_match_t *out,
                                      robo_css_diag_t *diag)
{
    match_context_t context;
    int started;
    size_t last;

    (void)diag;
    if (!selector) {
        return -1;
    }
    started = match_start(host, element, out, &context);
    if (started <= 0) {
        return started;
    }
    if (!robo_css_complex_selector_is_well_formed(selector)) {
        /* A combinator count that does not fit the compounds is a broken record,
         * not a selector that fails to match. */
        return -1;
    }
    last = robo_css_complex_selector_compound_count(selector) - 1u;
    if (match_from(&context, selector, last, element)) {
        out->matched = 1;
        out->range = selector->range;
    }
    return out->matched;
}

int robo_css_selector_list_matches(const robo_css_selector_list_t *list,
                                   const robo_css_element_host_t *host,
                                   robo_css_element_ref_t element,
                                   robo_css_selector_match_t *out,
                                   robo_css_diag_t *diag)
{
    size_t count;
    size_t i;

    (void)diag;
    if (!list) {
        return -1;
    }
    if (!out || !host || !robo_css_element_host_is_usable(host)) {
        return -1;
    }
    robo_css_selector_match_init(out);
    if (element == ROBO_CSS_ELEMENT_REF_NONE) {
        return 0;
    }

    count = robo_css_selector_list_count(list);
    for (i = 0u; i < count; i++) {
        const robo_css_complex_selector_t *selector =
            robo_css_selector_list_at(list, i);
        robo_css_selector_match_t attempt;
        match_context_t context;

        robo_css_selector_match_init(&attempt);
        context.host = host;
        context.out = &attempt;
        context.unusable = 0;

        if (!robo_css_complex_selector_is_well_formed(selector)) {
            continue;
        }
        if (match_from(&context, selector,
                       robo_css_complex_selector_compound_count(selector) - 1u,
                       element)) {
            out->matched = 1;
            out->selector_index = i;
            out->range = selector->range;
            /* A list that matched is not re-evaluated, but everything the
             * attempt found is still reported: an unsupported condition in a
             * selector that was tried before the matching one explains a
             * negative a caller might reach by asking about that selector
             * directly. */
            out->unsupported = attempt.unsupported || out->unsupported;
            if (robo_css_slice_is_absent(out->condition)) {
                out->condition = attempt.condition;
            }
            return 1;
        }
        if (context.unusable) {
            return -1;
        }
        if (attempt.unsupported && !out->unsupported) {
            out->unsupported = 1;
            out->condition = attempt.condition;
        }
    }
    return 0;
}
