/*
 * RoboCSS selector matching against the real RoboHTML DOM (Phase 9.4) —
 * css.dom.*
 *
 * The CSS-side suites match through a host test double, which is what lets them
 * cover every combinator and every structural pseudo-class exhaustively without a
 * document. This suite is the other half: it implements the same seam over the
 * *actual* DOM the 8.x tree builder produces and re-runs the selectors through
 * it. A matcher that only worked against a hand-shaped fixture would pass those
 * suites and fail here, and the difference is exactly what this file is for.
 *
 * Three things only a real tree can prove, and all three are asserted below:
 *
 *   Text between elements is invisible to `+`, `~`, `:first-child` and
 *   `:nth-child`. The checkpoint document is written one element per line, so
 *   every pair of sibling elements has a text node between them. Sibling
 *   counting that counted *nodes* would find no adjacent pairs at all.
 *
 *   Selector matching does not disturb the document. Matching reads; the tree
 *   version and the node count are asserted unchanged, and the visual checkpoint
 *   suite re-renders the same document afterwards.
 *
 *   The relations come from the DOM's own links rather than from a copy. After a
 *   structural mutation the host is rebuilt and the new shape is what matches,
 *   which is what makes the rebuild requirement real rather than decorative.
 *
 * The adapter below is the whole of the RoboCSS↔RoboHTML boundary, and it is
 * deliberately the only place in the runtime where the two subsystems meet.
 */

#include "RoboCssTestSupport.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboDemo/RoboDemoHtml.h"
#include "RoboGraphics/RoboGraphics.h"

/* ------------------------------------------------------------------ */
/* Small text helpers                                                 */
/* ------------------------------------------------------------------ */

/*
 * The host suite has no formatted output (the test framework deliberately does
 * not depend on stdio), so selectors and markup for generated fixtures are
 * assembled from pieces. Both helpers are bounds-checked rather than trusting
 * the caller's arithmetic.
 */

/** How many levels the deep-tree fixture nests. Deeper than any comfortable
 *  recursion budget, well inside the document's node ceiling. */
#define CSS_DOM_DEEP_LEVELS 96u

/** Write @p value in decimal (no padding) and NUL-terminate. Returns the digits. */
static size_t css_dom_write_uint(char *out, size_t capacity, unsigned value)
{
    char digits[16];
    size_t count = 0u;
    size_t i;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    ROBO_CHECK_EQ_INT(count + 1u <= capacity, 1);
    for (i = 0u; i < count; i++) {
        out[i] = digits[count - 1u - i];
    }
    out[count] = '\0';
    return count;
}

/** Write level @p level's id ("dN"). Returns its length; the result is NUL-free. */
static size_t css_dom_write_id(char *out, size_t capacity, unsigned level)
{
    out[0] = 'd';
    return css_dom_write_uint(out + 1u, capacity - 1u, level) + 1u;
}

/** Write the opening tag of level @p level. Returns the bytes written, no NUL. */
static size_t css_dom_write_div(char *out, size_t capacity, unsigned level)
{
    size_t at = 0u;

    ROBO_CHECK_EQ_INT(12u <= capacity, 1);
    memcpy(out, "<div id=\"", 9u);
    at = 9u;
    at += css_dom_write_id(out + at, capacity - at, level);
    out[at++] = '"';
    out[at++] = '>';
    return at;
}

/** `out = prefix + id + suffix`, bounds-checked. */
static void css_dom_join(char *out,
                         size_t capacity,
                         const char *prefix,
                         const char *id,
                         const char *suffix)
{
    ROBO_CHECK_EQ_INT(strlen(prefix) + strlen(id) + strlen(suffix) + 1u <= capacity, 1);
    strcpy(out, prefix);
    strcat(out, id);
    strcat(out, suffix);
}

/* ------------------------------------------------------------------ */
/* The RoboHTML host adapter                                          */
/* ------------------------------------------------------------------ */

/*
 * Caps rather than truncation. An element with more classes or attributes than
 * this fails the fixture loudly, because a host that silently dropped the
 * eleventh attribute would turn `[data-x]` into a wrong answer instead of an
 * unsupported one. The numbers are above anything the checkpoint or the fixtures
 * use by a wide margin.
 */
#define CSS_DOM_MAX_CLASSES 8u
#define CSS_DOM_MAX_ATTRIBUTES 16u

typedef struct css_dom_element {
    robo_html_node_handle_t handle;
    /* Element relations, resolved once at build time. */
    robo_css_element_ref_t parent;
    robo_css_element_ref_t previous;
    robo_css_element_ref_t next;
    robo_css_slice_t tag; /* borrowed from the document */
    int has_id;
    robo_css_slice_t id; /* borrowed from the document */
    /* Owned arrays of borrowed slices: stable for the host's lifetime, which is
     * what makes robo_css_element_view_t safe to fill for one element while the
     * matcher is still using another (`:not()` recurses). */
    robo_css_slice_t classes[CSS_DOM_MAX_CLASSES];
    size_t class_count;
    robo_css_attribute_t attributes[CSS_DOM_MAX_ATTRIBUTES];
    size_t attribute_count;
} css_dom_element_t;

typedef struct css_dom_host {
    robo_html_document_id_t document;
    css_dom_element_t *elements; /* owned; ref is index + 1 */
    size_t count;
    size_t capacity;
    robo_css_element_host_t host;
} css_dom_host_t;

static robo_result_t css_dom_view_of(void *userdata,
                                     robo_css_element_ref_t ref,
                                     robo_css_element_view_t *out)
{
    css_dom_host_t *dom = (css_dom_host_t *)userdata;
    const css_dom_element_t *element;

    if (!dom || !out || ref == ROBO_CSS_ELEMENT_REF_NONE || ref > dom->count) {
        return ROBO_NOT_FOUND;
    }
    element = &dom->elements[ref - 1u];
    robo_css_element_view_init(out);
    if (robo_css_element_view_set_element(out, ref, element->tag, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    /* The id is recorded only when the attribute exists — which is the same
     * answer either way, since `#name` can never name an empty id. Recording the
     * distinction keeps the host honest about what the DOM said. */
    if (element->has_id &&
        robo_css_element_view_set_id(out, element->id, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (robo_css_element_view_set_classes(out, element->classes, element->class_count,
                                          NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    if (robo_css_element_view_set_attributes(out, element->attributes,
                                             element->attribute_count, NULL) != ROBO_OK) {
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

static robo_css_element_ref_t css_dom_parent_of(void *userdata,
                                                robo_css_element_ref_t ref)
{
    css_dom_host_t *dom = (css_dom_host_t *)userdata;

    if (!dom || ref == ROBO_CSS_ELEMENT_REF_NONE || ref > dom->count) {
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    return dom->elements[ref - 1u].parent;
}

static robo_css_element_ref_t css_dom_previous_of(void *userdata,
                                                  robo_css_element_ref_t ref)
{
    css_dom_host_t *dom = (css_dom_host_t *)userdata;

    if (!dom || ref == ROBO_CSS_ELEMENT_REF_NONE || ref > dom->count) {
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    return dom->elements[ref - 1u].previous;
}

static robo_css_element_ref_t css_dom_next_of(void *userdata,
                                              robo_css_element_ref_t ref)
{
    css_dom_host_t *dom = (css_dom_host_t *)userdata;

    if (!dom || ref == ROBO_CSS_ELEMENT_REF_NONE || ref > dom->count) {
        return ROBO_CSS_ELEMENT_REF_NONE;
    }
    return dom->elements[ref - 1u].next;
}

/** Ref of the element @p handle names, or NONE. */
static robo_css_element_ref_t css_dom_ref_of_handle(const css_dom_host_t *dom,
                                                    const robo_html_node_handle_t *handle)
{
    size_t i;

    for (i = 0u; i < dom->count; i++) {
        if (robo_html_node_handle_equal(&dom->elements[i].handle, handle)) {
            return (robo_css_element_ref_t)(i + 1u);
        }
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

/*
 * The nearest *element* in one direction, which is where "element siblings, not
 * node siblings" lives: the DOM stores the newline between two elements as a
 * text node, and this is the walk that steps over it. Text and comments are
 * skipped; the document node ends the walk, because it is not an element and has
 * no element above it.
 */
static robo_css_element_ref_t css_dom_element_in_direction(
    const css_dom_host_t *dom,
    robo_html_node_handle_t node,
    int forward)
{
    size_t steps = 0u;

    while (steps++ < dom->count + 2u) {
        robo_html_node_handle_t next;
        robo_html_node_kind_t kind;
        robo_result_t found;

        found = forward ? robo_html_node_next_sibling(&node, &next, NULL)
                        : robo_html_node_prev_sibling(&node, &next, NULL);
        if (found != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (robo_html_node_handle_resolve(&next, &kind) != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (kind == ROBO_HTML_NODE_KIND_ELEMENT) {
            return css_dom_ref_of_handle(dom, &next);
        }
        node = next;
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

/** The nearest element ancestor of @p node, or NONE at the top of the tree. */
static robo_css_element_ref_t css_dom_ancestor_of(const css_dom_host_t *dom,
                                                  robo_html_node_handle_t node)
{
    size_t steps = 0u;

    while (steps++ < dom->count + 2u) {
        robo_html_node_handle_t parent;
        robo_html_node_kind_t kind;

        if (robo_html_node_parent(&node, &parent, NULL) != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (robo_html_node_handle_resolve(&parent, &kind) != ROBO_OK) {
            return ROBO_CSS_ELEMENT_REF_NONE;
        }
        if (kind == ROBO_HTML_NODE_KIND_ELEMENT) {
            return css_dom_ref_of_handle(dom, &parent);
        }
        node = parent;
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

static robo_result_t css_dom_collect_visitor(const robo_html_node_view_t *view,
                                             size_t depth,
                                             void *user)
{
    css_dom_host_t *dom = (css_dom_host_t *)user;
    css_dom_element_t *element;
    robo_html_attribute_t attribute;
    size_t i;
    int truncated = 0;

    (void)depth;
    ROBO_CHECK_EQ_INT(dom->count < dom->capacity, 1);
    element = &dom->elements[dom->count];
    memset(element, 0, sizeof(*element));
    element->handle = view->handle;
    element->tag = robo_css_slice_from_text(view->name ? view->name : "");

    for (i = 0u; robo_html_element_attribute_at(&view->handle, i, &attribute, NULL) == ROBO_OK;
         i++) {
        if (attribute.name_length == 2u && attribute.name[0] == 'i' &&
            attribute.name[1] == 'd') {
            element->has_id = 1;
            element->id = robo_css_slice_from_bytes((const uint8_t *)attribute.value,
                                                    attribute.value_length);
            continue;
        }
        if (attribute.name_length == 5u && memcmp(attribute.name, "class", 5u) == 0) {
            robo_css_slice_t value =
                robo_css_slice_from_bytes((const uint8_t *)attribute.value,
                                          attribute.value_length);

            element->class_count = robo_css_split_ascii_whitespace(
                value, element->classes, CSS_DOM_MAX_CLASSES, &truncated);
            ROBO_CHECK_EQ_INT(truncated, 0);
            continue;
        }
        ROBO_CHECK_EQ_INT(element->attribute_count < CSS_DOM_MAX_ATTRIBUTES, 1);
        element->attributes[element->attribute_count].name =
            robo_css_slice_from_bytes((const uint8_t *)attribute.name,
                                      attribute.name_length);
        element->attributes[element->attribute_count].value =
            robo_css_slice_from_bytes((const uint8_t *)attribute.value,
                                      attribute.value_length);
        element->attribute_count++;
    }

    dom->count++;
    return ROBO_OK;
}

/** Build @p dom over @p html: the real tree builder, then the real DOM links. */
static void css_dom_build(css_dom_host_t *dom, const char *html)
{
    robo_html_source_t source;
    robo_html_node_handle_t root;
    size_t i;

    css_fixture();
    memset(dom, 0, sizeof(*dom));
    ROBO_CHECK_EQ_INT(robo_html_init() == ROBO_OK ||
                          robo_html_init() == ROBO_ALREADY_EXISTS,
                      1);
    ROBO_CHECK_EQ_INT(robo_html_document_create(&dom->document, NULL), ROBO_OK);
    robo_html_source_init(&source);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&source, html, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_build_tree(dom->document, &source, NULL, NULL), ROBO_OK);

    /* One slot per node is a true upper bound on the element count, and it means
     * the element array never has to move while a view points into it. */
    dom->capacity = robo_html_document_node_count(dom->document) + 1u;
    dom->elements =
        (css_dom_element_t *)robo_calloc(dom->capacity, sizeof(css_dom_element_t));
    ROBO_CHECK(dom->elements != NULL);

    root = robo_html_document_root(dom->document);
    ROBO_CHECK_EQ_INT(robo_html_node_walk_pre_order(&root, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                                    css_dom_collect_visitor, dom, NULL),
                      ROBO_OK);
    for (i = 0u; i < dom->count; i++) {
        dom->elements[i].parent = css_dom_ancestor_of(dom, dom->elements[i].handle);
        dom->elements[i].previous =
            css_dom_element_in_direction(dom, dom->elements[i].handle, 0);
        dom->elements[i].next =
            css_dom_element_in_direction(dom, dom->elements[i].handle, 1);
    }

    robo_css_element_host_init(&dom->host);
    dom->host.userdata = dom;
    dom->host.view = css_dom_view_of;
    dom->host.parent = css_dom_parent_of;
    dom->host.previous_sibling = css_dom_previous_of;
    dom->host.next_sibling = css_dom_next_of;
}

static void css_dom_destroy(css_dom_host_t *dom)
{
    if (dom->elements) {
        robo_free(dom->elements);
        dom->elements = NULL;
    }
    if (robo_html_document_is_live(dom->document)) {
        ROBO_CHECK_EQ_INT(robo_html_document_destroy(dom->document, NULL), ROBO_OK);
    }
    dom->document = ROBO_HTML_DOCUMENT_ID_NONE;
    dom->count = 0u;
    dom->capacity = 0u;
}

/* ------------------------------------------------------------------ */
/* Queries                                                            */
/* ------------------------------------------------------------------ */

/** Does @p text match the element @p ref names? Asserts the attempt was decided. */
static int css_dom_match(css_dom_host_t *dom, const char *text, robo_css_element_ref_t ref)
{
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_selector_match_t result;
    int matched;

    css_select_parse(text, &list);
    robo_css_selector_match_init(&result);
    matched = robo_css_selector_list_matches(&list, &dom->host, ref, &result, NULL);
    /* A real DOM can answer every relation the seam defines, so nothing in this
     * suite may come back undecided. */
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
    robo_css_selector_list_dispose(&list);
    return matched;
}

/** How many elements of @p dom @p text matches. */
static size_t css_dom_count(css_dom_host_t *dom, const char *text)
{
    size_t hits = 0u;
    size_t i;

    for (i = 1u; i <= dom->count; i++) {
        if (css_dom_match(dom, text, i)) {
            hits++;
        }
    }
    return hits;
}

/**
 * The one element @p text matches — or 0, which happens both when it matched
 * nothing and when it matched more than one. The callers assert non-zero, so a
 * selector that grew an extra match fails the test rather than silently passing
 * on whichever element came first.
 */
static robo_css_element_ref_t css_dom_only(css_dom_host_t *dom, const char *text)
{
    robo_css_element_ref_t found = ROBO_CSS_ELEMENT_REF_NONE;
    size_t i;

    for (i = 1u; i <= dom->count; i++) {
        if (css_dom_match(dom, text, i)) {
            if (found != ROBO_CSS_ELEMENT_REF_NONE) {
                return ROBO_CSS_ELEMENT_REF_NONE;
            }
            found = i;
        }
    }
    return found;
}

static robo_css_element_ref_t css_dom_ref_by_id(const css_dom_host_t *dom, const char *id)
{
    robo_css_slice_t needle = robo_css_slice_from_text(id);
    size_t i;

    for (i = 0u; i < dom->count; i++) {
        if (dom->elements[i].has_id &&
            robo_css_slice_equals(dom->elements[i].id, needle)) {
            return (robo_css_element_ref_t)(i + 1u);
        }
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

/** The first element with tag @p tag, in document order, or 0. */
static robo_css_element_ref_t css_dom_ref_by_tag(const css_dom_host_t *dom,
                                                 const char *tag)
{
    robo_css_slice_t needle = robo_css_slice_from_text(tag);
    size_t i;

    for (i = 0u; i < dom->count; i++) {
        if (robo_css_slice_equals(dom->elements[i].tag, needle)) {
            return (robo_css_element_ref_t)(i + 1u);
        }
    }
    return ROBO_CSS_ELEMENT_REF_NONE;
}

/** Assert @p ref names an element with tag @p tag and, when given, id @p id. */
static void css_dom_is(const css_dom_host_t *dom,
                       robo_css_element_ref_t ref,
                       const char *tag,
                       const char *id)
{
    char text[64];
    const css_dom_element_t *element;

    ROBO_CHECK_EQ_INT(ref != ROBO_CSS_ELEMENT_REF_NONE, 1);
    element = &dom->elements[ref - 1u];
    text[0] = '\0';
    css_select_text(element->tag, text, sizeof(text));
    ROBO_CHECK_EQ_INT(strcmp(text, tag), 0);
    if (id) {
        ROBO_CHECK_EQ_INT(element->has_id, 1);
        text[0] = '\0';
        css_select_text(element->id, text, sizeof(text));
        ROBO_CHECK_EQ_INT(strcmp(text, id), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The visual checkpoint's document                                   */
/* ------------------------------------------------------------------ */

static void checkpoint_dom(css_dom_host_t *dom)
{
    css_dom_build(dom, robo_demo_default_html());
}

static void test_css_dom_checkpoint_tree(void)
{
    css_dom_host_t dom;

    /* The same source the visual checkpoint renders, through the same tree
     * builder — so the element count here is a statement about the document the
     * demo paints, not about a fixture of this suite's own. */
    checkpoint_dom(&dom);
    ROBO_CHECK_EQ_INT(dom.count, 29u);
    css_dom_is(&dom, 1u, "html", NULL);
    css_dom_is(&dom, css_dom_ref_by_tag(&dom, "form"), "form", NULL);
    css_dom_is(&dom, css_dom_ref_by_id(&dom, "Overview"), "p", "Overview");
    css_dom_is(&dom, css_dom_ref_by_id(&dom, "Country"), "select", "Country");
    ROBO_CHECK_EQ_INT(css_dom_ref_by_id(&dom, "Missing"), 0u);

    /* Every element is one element of the tree, which is what makes "ref is
     * index + 1" a usable identity in these tests. */
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(dom.document) > dom.count, 1);
    css_dom_destroy(&dom);
}

static void test_css_dom_checkpoint_descendant_and_child(void)
{
    css_dom_host_t dom;

    checkpoint_dom(&dom);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "head"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "html > head"), css_dom_ref_by_tag(&dom, "head"));
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "html > body"), css_dom_ref_by_tag(&dom, "body"));
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "html > body > h1"),
                      css_dom_ref_by_tag(&dom, "h1"));
    /* A `title` lives in `head`, so `body title` is nothing even though both
     * elements exist. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body title"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "head title"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > title"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html title"), 1u);

    /* A `div` selector matches nothing in this document, and the `*` in a
     * compound does not turn a near miss into a hit. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "div"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "*"), dom.count);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html *"), dom.count - 1u);
}

static void test_css_dom_checkpoint_siblings_ignore_text_nodes(void)
{
    css_dom_host_t dom;

    /*
     * The checkpoint is written one element per line, so there is a text node
     * between every pair of sibling elements. Node-sibling counting would find
     * no adjacent pairs here at all, and would also make every element's
     * `:first-child` false except the first line's — which is precisely what
     * these assertions rule out.
     */
    checkpoint_dom(&dom);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "h1 + p"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "h1 + p"),
                      (robo_css_element_ref_t)(css_dom_ref_by_tag(&dom, "h1") + 1u));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "p + p"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "p + a"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a + a"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a + p"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "h1 + a"), 0u);

    /* The form is a flat run of `label`, control, `label`, control, …, which is
     * the shape the field tests below depend on. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > label"), 7u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label + input"), 5u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label + textarea"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label + select"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label ~ input"), 5u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label ~ button"), 1u);

    /* `label:first-child` is the statement that the newline before the first
     * label is a text node and not a sibling. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > label:first-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "form > label:first-child"),
                      css_dom_ref_by_id(&dom, "DisplayName") - 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > :last-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "form > :last-child"),
                      css_dom_ref_by_tag(&dom, "button"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > h1:first-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > p:nth-child(2)"), 1u);
    /* Two elements are an only child: `title`, whose parent has one child, and
     * `html`, which has no siblings at all — an inclusive-sibling reading that
     * Selectors 4 and every engine share. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, ":only-child"), 2u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "title:only-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html:only-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "title:only-child"),
                      css_dom_ref_by_tag(&dom, "title"));
    /* Nothing inside `body` is an only child — `title` is in `head`, and every
     * other parent here has several children. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body :only-child"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "option:first-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "option:last-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "select > option + option"), 1u);

    css_dom_destroy(&dom);
}

static void test_css_dom_checkpoint_attributes_and_forms(void)
{
    css_dom_host_t dom;

    checkpoint_dom(&dom);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a[href]"), 2u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a[href=\"/Settings\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "a[href=\"/Settings\"]"),
                      css_dom_ref_by_tag(&dom, "a"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a[href^=\"#\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a[href$=\"Overview\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "a[href*=\"Sett\"]"), 1u);

    /* `required` and `checked` are boolean attributes: present with no value,
     * which is what `[required]` asks about and `[required=""]` does not. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[required]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "input[required]"),
                      css_dom_ref_by_id(&dom, "DisplayName"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[checked]"), 2u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "option[selected]"), 1u);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[type=\"text\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[type=\"password\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[type=\"number\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[type=\"checkbox\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[type=\"radio\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input[type=\"submit\"]"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "button[type=\"submit\"]"), 1u);

    /* `[class~=…]` is a word of the class attribute, and the class attribute is
     * the thing the class selector reads — the two must agree. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label[for]"), 7u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label[for=\"Notes\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form[action=\"/Register\"]"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form[enctype*=\"urlencoded\"]"), 1u);

    /* The label/control pairing of Phase 8.14, expressed as a selector: every
     * control that follows a label in the form. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > label + input"), 5u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > label + button"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form button + button"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > form + button"), 1u);

    css_dom_destroy(&dom);
}

static void test_css_dom_checkpoint_structural(void)
{
    css_dom_host_t dom;

    checkpoint_dom(&dom);

    /* `:root` is the `html` element and nothing else, even though `head` and
     * `body` are equally parentless-looking from a node's point of view. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, ":root"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, ":root"), css_dom_ref_by_tag(&dom, "html"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "head:root"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body :root"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html:root > body"), 1u);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(1)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(2)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(3)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-last-child(1)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "body > :nth-last-child(1)"),
                      css_dom_ref_by_tag(&dom, "button") + 1u);
    /* body's children are h1, p, p, a, a, p#Overview, form, button: the `p`s
     * sit at 2, 3 and 6, so two of them are even-positioned and one odd. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > p:nth-child(odd)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > p:nth-child(even)"), 2u);
    /* The third child is the second bare paragraph — and #Overview, the third
     * `p`, is the sixth child, which is what makes the position the assertion
     * rather than the tag. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > p:nth-child(3)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > p:nth-child(3):not(#Overview)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "body > p:nth-child(6)"),
                      css_dom_ref_by_id(&dom, "Overview"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > input:nth-child(4)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "form > input:nth-child(5)"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "select > option:nth-child(2)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "select > option:nth-child(2)"),
                      css_dom_ref_by_tag(&dom, "option") + 1u);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "p:not(#Overview)"), 2u);
    /* Eight element children of `body`, three of which are `p`. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :not(p)"), 5u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "input:not([type=\"text\"])"), 4u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "label:not([for])"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > h1, body > form"), 2u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > head + body"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > body > h1 ~ p"), 3u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > body > h1 ~ a"), 2u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > body > h1 ~ form"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > body > form ~ button"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html > body > button ~ form"), 0u);

    css_dom_destroy(&dom);
}

static void test_css_dom_checkpoint_reports_unevaluatable(void)
{
    css_dom_host_t dom;
    robo_css_selector_list_t list = ROBO_CSS_CLEARED;
    robo_css_selector_match_t result;

    /* A real DOM answers every relation, so a negative here is a real negative.
     * What it still cannot answer is state — `:hover` — and that must stay
     * reported rather than becoming "does not match". */
    checkpoint_dom(&dom);

    css_select_parse("a:hover", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(
                          &list, &dom.host, css_dom_ref_by_tag(&dom, "a"), &result, NULL),
                      0);
    ROBO_CHECK_EQ_INT(result.matched, 0);
    ROBO_CHECK_EQ_INT(result.unsupported, 1);
    robo_css_selector_list_dispose(&list);

    css_select_parse("a::before", &list);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_matches(
                          &list, &dom.host, css_dom_ref_by_tag(&dom, "a"), &result, NULL),
                      0);
    ROBO_CHECK_EQ_INT(result.unsupported, 0);
    robo_css_selector_list_dispose(&list);

    css_dom_destroy(&dom);
}

static void test_css_dom_checkpoint_matching_does_not_mutate(void)
{
    css_dom_host_t dom;
    robo_html_node_handle_t body;
    uint32_t before = 0u;
    uint32_t after = 0u;
    size_t nodes_before;
    size_t i;

    checkpoint_dom(&dom);
    nodes_before = robo_html_document_node_count(dom.document);
    body = dom.elements[css_dom_ref_by_tag(&dom, "body") - 1u].handle;
    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(dom.document, &before, NULL), ROBO_OK);

    /* A sweep over the whole document, repeatedly: matching is a read, and the
     * tree must be byte-identical afterwards. */
    for (i = 0u; i < 3u; i++) {
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, "*"), dom.count);
        (void)css_dom_count(&dom, "form > label + input");
        (void)css_dom_count(&dom, "body > :nth-last-child(2)");
        (void)css_dom_count(&dom, "input:not([type]):not(#Country)");
    }

    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(dom.document, &after, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(body.node_id != ROBO_HTML_NODE_ID_NONE, 1);
    ROBO_CHECK_EQ_INT(before, after);
    ROBO_CHECK_EQ_INT(robo_html_document_node_count(dom.document), nodes_before);
    ROBO_CHECK_EQ_INT(dom.count, 29u);
    /* … and the tree audit itself agrees the links are still a tree. */
    {
        robo_html_tree_report_t report;

        ROBO_CHECK_EQ_INT(robo_html_document_validate_tree(dom.document, &report, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(report.ok, 1);
        ROBO_CHECK_EQ_INT(report.cycles, 0u);
        ROBO_CHECK_EQ_INT(report.bad_parents, 0u);
        ROBO_CHECK_EQ_INT(report.sibling_mismatches, 0u);
    }

    css_dom_destroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Text and comments between elements                                 */
/* ------------------------------------------------------------------ */

static void test_css_dom_text_and_comments_are_not_siblings(void)
{
    /*
     * The same property as the checkpoint's, made explicit and concentrated:
     * every relation here is separated by non-element nodes, so a host that
     * counted nodes would answer every one of them differently.
     */
    css_dom_host_t dom;
    robo_html_node_handle_t body;
    size_t child_nodes = 0u;

    css_dom_build(&dom,
                  "<html><body>\n"
                  "  <h1>Title</h1>\n"
                  "  <!-- a comment on its own line -->\n"
                  "  <p id=\"one\">One</p>\n"
                  "  \n"
                  "  <p id=\"two\">Two</p>\n"
                  "  <span id=\"three\">Three</span>\n"
                  "</body></html>\n");

    ROBO_CHECK_EQ_INT(dom.count, 7u);
    body = dom.elements[css_dom_ref_by_tag(&dom, "body") - 1u].handle;
    ROBO_CHECK_EQ_INT(robo_html_node_child_count(&body, &child_nodes, NULL), ROBO_OK);
    /* Eleven nodes for four elements: text runs, a blank line and a comment, all
     * of which the host must look straight through. Five or more of the seven
     * elements would be invisible to a host that counted nodes. */
    ROBO_CHECK_EQ_INT(child_nodes, 11u);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "h1 + p"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "h1 + p"), css_dom_ref_by_id(&dom, "one"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "p + p"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "p + p"), css_dom_ref_by_id(&dom, "two"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "p + span"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "h1 + span"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "h1 ~ #three"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "p ~ #three"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "span ~ p"), 0u);

    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :first-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "body > :first-child"),
                      css_dom_ref_by_tag(&dom, "h1"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "h1:only-child"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :last-child"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(2)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "body > :nth-child(2)"),
                      css_dom_ref_by_id(&dom, "one"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(3)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(4)"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(5)"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :only-child"), 0u);
    /* A comment is not an element, so it neither matches `*` here nor counts as
     * a sibling. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > *"), 4u);

    css_dom_destroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Deep trees                                                         */
/* ------------------------------------------------------------------ */

static void test_css_dom_deep_tree(void)
{
    /*
     * A reasonably deep document — deeper than any recursion budget that would
     * be comfortable, and exactly the shape that turns a bounded walk into a
     * stack overflow if the implementation recursed per tree level rather than
     * per compound.
     *
     * Every div carries `id="dN"` so an assertion can name the level it landed
     * on, and the innermost element is the only `<em>` in the document.
     */
    static char html[CSS_DOM_DEEP_LEVELS * 32u + 128u];
    css_dom_host_t dom;
    size_t at = 0u;
    size_t level;

    memcpy(html + at, "<html><body>", 12u);
    at += 12u;
    for (level = 1u; level <= CSS_DOM_DEEP_LEVELS; level++) {
        at += css_dom_write_div(html + at, sizeof(html) - at, (unsigned)level);
    }
    memcpy(html + at, "<em id=\"leaf\">deep</em>", 23u);
    at += 23u;
    for (level = 0u; level < CSS_DOM_DEEP_LEVELS; level++) {
        memcpy(html + at, "</div>", 6u);
        at += 6u;
    }
    memcpy(html + at, "</body></html>", 14u);
    at += 14u;
    html[at] = '\0';
    ROBO_CHECK_EQ_INT(at < sizeof(html), 1);

    css_dom_build(&dom, html);
    /* `html`, an implied `head`, `body`, every level, and the leaf. */
    ROBO_CHECK_EQ_INT(dom.count, CSS_DOM_DEEP_LEVELS + 4u);

    /* The deepest element is reachable from the root, and from every level in
     * between — which is the descendant walk climbing the whole chain. */
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "#leaf"), 1u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "html em"), css_dom_ref_by_id(&dom, "leaf"));
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "body em"), css_dom_ref_by_id(&dom, "leaf"));
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "div em"), css_dom_ref_by_id(&dom, "leaf"));
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "em"), css_dom_ref_by_id(&dom, "leaf"));
    /* A long complex selector, matched from the right-most compound down the
     * whole chain: the compound recursion is what bounds the stack, not the
     * tree. */
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "html body div div div div div div div em"),
                      css_dom_ref_by_id(&dom, "leaf"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "html body div div div div div div div span"),
                      0u);

    /* Every level is the first and last child of its parent, so every level
     * answers the structural pseudo-classes identically. */
    for (level = 1u; level <= CSS_DOM_DEEP_LEVELS; level++) {
        char id[16];
        char selector[64];

        css_dom_write_id(id, sizeof(id), (unsigned)level);
        css_dom_join(selector, sizeof(selector), "#", id, ":first-child");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 1u);
        css_dom_join(selector, sizeof(selector), "#", id, ":last-child");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 1u);
        css_dom_join(selector, sizeof(selector), "#", id, ":only-child");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 1u);
        css_dom_join(selector, sizeof(selector), "#", id, ":nth-child(1)");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 1u);
        css_dom_join(selector, sizeof(selector), "#", id, ":nth-child(2)");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 0u);
        css_dom_join(selector, sizeof(selector), "#", id, ":nth-last-child(1)");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 1u);
        css_dom_join(selector, sizeof(selector), "div#", id, ":root");
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 0u);
    }

    /* Each level's parent is the level above it, expressed as a child selector
     * and checked against its own id — a chain of `>` that only a correct
     * parent link can satisfy. */
    for (level = 2u; level <= CSS_DOM_DEEP_LEVELS; level++) {
        char parent_id[16];
        char child_id[16];
        char selector[64];

        css_dom_write_id(parent_id, sizeof(parent_id), (unsigned)(level - 1u));
        css_dom_write_id(child_id, sizeof(child_id), (unsigned)level);
        strcpy(selector, "#");
        strcat(selector, parent_id);
        strcat(selector, " > #");
        strcat(selector, child_id);
        ROBO_CHECK_EQ_INT(strlen(selector) < sizeof(selector), 1);
        ROBO_CHECK_EQ_INT(css_dom_count(&dom, selector), 1u);
    }

    css_dom_destroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Mutation                                                           */
/* ------------------------------------------------------------------ */

static void test_css_dom_rebuild_after_mutation(void)
{
    /*
     * The host resolves its relations once, from the DOM's own links, so a
     * structural change makes them stale and the fix is to build a new host. This
     * test does exactly that and asserts the *new* shape, which is what makes the
     * requirement a contract rather than a footnote: relations that were cached
     * as values could not be refreshed at all.
     */
    css_dom_host_t dom;
    robo_html_node_handle_t body;
    robo_html_node_handle_t second;
    uint32_t version_before = 0u;
    uint32_t version_after = 0u;

    css_dom_build(&dom,
                  "<html><body><h1>T</h1><p id=\"one\">One</p>"
                  "<p id=\"two\">Two</p></body></html>");
    /* `html`, an implied `head`, `body`, `h1`, `p#one` and `p#two`. */
    ROBO_CHECK_EQ_INT(dom.count, 6u);
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "#two:last-child"),
                      css_dom_ref_by_id(&dom, "two"));
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "#one + p"), css_dom_ref_by_id(&dom, "two"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(3)"), 1u);

    body = dom.elements[css_dom_ref_by_tag(&dom, "body") - 1u].handle;
    second = dom.elements[css_dom_ref_by_id(&dom, "two") - 1u].handle;
    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(dom.document, &version_before, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_remove_child(&body, &second, NULL), ROBO_OK);
    /* The mutation moved the tree version — which is the fact that says the
     * host's resolved relations are now stale, and the reason it is rebuilt. */
    ROBO_CHECK_EQ_INT(robo_html_node_tree_version(dom.document, &version_after, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(version_after != version_before, 1);

    css_dom_destroy(&dom);
    css_dom_build(&dom,
                  "<html><body><h1>T</h1><p id=\"one\">One</p>"
                  "<p id=\"two\">Two</p></body></html>");
    /* Rebuilt from the unmutated source: the same answers, which shows the
     * teardown leaves nothing behind. */
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "#one + p"), css_dom_ref_by_id(&dom, "two"));
    css_dom_destroy(&dom);

    /* And now the mutated document itself, rebuilt from scratch. */
    css_dom_build(&dom, "<html><body><h1>T</h1><p id=\"one\">One</p></body></html>");
    ROBO_CHECK_EQ_INT(css_dom_only(&dom, "#one:last-child"),
                      css_dom_ref_by_id(&dom, "one"));
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "#one + p"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(3)"), 0u);
    ROBO_CHECK_EQ_INT(css_dom_count(&dom, "body > :nth-child(2)"), 1u);

    css_dom_destroy(&dom);
}

/* ------------------------------------------------------------------ */
/* The visual vertical slice                                         */
/* ------------------------------------------------------------------ */

/** An RGBA8 surface the painter can write and the test can read. */
static robo_gfx_surface_t *css_dom_surface(int32_t width, int32_t height)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    if (robo_gfx_surface_create(&desc, &surface) != ROBO_OK) {
        return NULL;
    }
    return surface;
}

static void test_css_dom_selectors_do_not_disturb_the_picture(void)
{
    /*
     * The visual vertical slice, end to end, with selector matching inserted in
     * the middle of it: the checkpoint document is built once, painted, matched
     * against with the whole selector repertoire, and painted again from the same
     * document. The two frames must be byte-identical.
     *
     * This is the strongest form of "selectors do not change what you see",
     * because the comparison is the pixels the demo adapter actually produces
     * rather than a claim about the tree the adapter reads. A matcher that
     * mutated an attribute, reordered a child or left a borrowed slice dangling
     * long enough to corrupt the payload arena would fail here and nowhere else.
     */
    static const char *const repertoire[] = {
        "*", "html", "body > *", ":root", ":first-child", ":last-child",
        ":only-child", ":nth-child(2n+1)", ":nth-last-child(odd)",
        "label + input", "label ~ input", "h1 + p", "p + p", "a + a",
        "form > label", "form > :last-child", "select > option:last-child",
        "input[required]", "input[checked]", "option[selected]",
        "input[type=\"checkbox\"]", "a[href^=\"#\"]", "a[href$=\"Settings\"]",
        "body > h1 ~ p", "html > body > form ~ button", "#Overview",
        ":not(p)", "body > :not(form)", "p:not(#Overview)",
        "input:not([type=\"text\"])", ":not(:not(.missing))",
        "form > label:first-child", "form > label:nth-child(3)"
    };
    css_dom_host_t dom;
    robo_demo_page_t *before = NULL;
    robo_demo_page_t *after = NULL;
    robo_gfx_surface_t *first = NULL;
    robo_gfx_surface_t *second = NULL;
    size_t stride;
    size_t i;
    uint32_t row;

    css_dom_build(&dom, robo_demo_default_html());
    ROBO_CHECK_EQ_INT(dom.count, 29u);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(dom.document, &before, NULL),
                      ROBO_OK);
    ROBO_CHECK(before != NULL);
    first = css_dom_surface(900, 800);
    second = css_dom_surface(900, 800);
    ROBO_CHECK(first != NULL && second != NULL);
    if (!before || !first || !second) {
        robo_demo_page_destroy(before);
        robo_gfx_surface_release(first);
        robo_gfx_surface_release(second);
        css_dom_destroy(&dom);
        return;
    }
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(before, first, NULL, NULL), ROBO_OK);

    /* The whole repertoire, against every element, twice over. */
    for (i = 0u; i < sizeof(repertoire) / sizeof(repertoire[0]); i++) {
        robo_css_element_ref_t ref;

        for (ref = 1u; ref <= dom.count; ref++) {
            (void)css_dom_match(&dom, repertoire[i], ref);
        }
    }
    /* And a malformed selector in the same breath: a rejected one must leave the
     * document no worse off than a matched one. */
    (void)css_select_parse_rejected("div >");
    (void)css_select_parse_rejected("[attr=");
    (void)css_select_parse_rejected(":nth-child(");

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(dom.document, &after, NULL),
                      ROBO_OK);
    ROBO_CHECK(after != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(after, second, NULL, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(after),
                      robo_demo_page_block_count(before));
    ROBO_CHECK_STR_EQ(robo_demo_page_title(after), robo_demo_page_title(before));
    stride = robo_gfx_surface_stride(first);
    ROBO_CHECK_EQ_INT(stride, robo_gfx_surface_stride(second));
    for (row = 0u; row < 800u; row++) {
        const uint8_t *left =
            robo_gfx_surface_pixels_const(first) + (size_t)row * stride;
        const uint8_t *right =
            robo_gfx_surface_pixels_const(second) + (size_t)row * stride;

        if (memcmp(left, right, stride) != 0) {
            ROBO_CHECK_MSG(0, "selector matching changed the rendered frame");
            break;
        }
    }

    robo_demo_page_destroy(before);
    robo_demo_page_destroy(after);
    robo_gfx_surface_release(first);
    robo_gfx_surface_release(second);
    css_dom_destroy(&dom);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                        */
/* ------------------------------------------------------------------ */

static void test_css_dom_matching_is_deterministic(void)
{
    /* Two hosts built independently over the same source must agree on every
     * selector, element for element. Matching that depended on allocation order,
     * address values or walk history would show up here. */
    static const char *const selectors[] = {
        "*", "html", "body > *", "form > *", "label + input", "p:nth-child(2)",
        "input[type=\"text\"]", "a[href^=\"#\"]", "#Overview", ":root",
        "form > label:not(:first-child)", "body > :nth-last-child(2)",
        "html > body > h1 ~ p", "select option:last-child"
    };
    css_dom_host_t first;
    css_dom_host_t second;
    size_t i;

    checkpoint_dom(&first);
    checkpoint_dom(&second);
    ROBO_CHECK_EQ_INT(first.count, second.count);
    for (i = 0u; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        robo_css_element_ref_t ref;

        ROBO_CHECK_EQ_INT(css_dom_count(&first, selectors[i]),
                          css_dom_count(&second, selectors[i]));
        for (ref = 1u; ref <= first.count; ref++) {
            ROBO_CHECK_EQ_INT(css_dom_match(&first, selectors[i], ref),
                              css_dom_match(&second, selectors[i], ref));
        }
    }
    css_dom_destroy(&first);
    css_dom_destroy(&second);
}

/* ------------------------------------------------------------------ */
/* The cascade over the real DOM (Phase 9.5)                           */
/* ------------------------------------------------------------------ */

/*
 * The matching suites already prove the matcher against the real tree, so what
 * is left for the cascade is the two things only a real document has.
 *
 * The first is the element's own `style` attribute. It lives in the DOM and
 * nowhere else: RoboCSS asks for its *text* through the seam, parses it, and
 * keeps none of it, so the only way to prove the boundary works is to hand the
 * callback a real document and check that what comes back is what the attribute
 * says. The stub cannot test that, because the stub is the thing under test.
 *
 * The second is an ordering produced by a tree the caller did not shape. A
 * descendant combinator here has to reach through ancestors the host resolves
 * from real parent links, and a sibling through the text nodes the tree builder
 * really inserted — so a cascade that got its relations from a convenient copy
 * of the document would still collect the wrong set of rules.
 */

/** The DOM's own `style` attribute, which is what the inline boundary hands over. */
static robo_result_t css_dom_inline_style(void *userdata,
                                         robo_css_element_ref_t ref,
                                         robo_css_slice_t *out_text)
{
    css_dom_host_t *dom = (css_dom_host_t *)userdata;
    robo_html_attribute_t attribute;
    size_t index;

    if (!dom || ref == ROBO_CSS_ELEMENT_REF_NONE || ref > dom->count) {
        return ROBO_NOT_FOUND;
    }
    for (index = 0u;
         robo_html_element_attribute_at(&dom->elements[ref - 1u].handle, index,
                                        &attribute, NULL) == ROBO_OK;
         index++) {
        if (attribute.name_length == 5u && memcmp(attribute.name, "style", 5u) == 0) {
            *out_text = robo_css_slice_from_bytes((const uint8_t *)attribute.value,
                                                  attribute.value_length);
            return ROBO_OK;
        }
    }
    /* No `style` attribute is a *different* answer from an empty one, and the
     * DOM is where that distinction is real rather than theoretical. */
    return ROBO_NOT_FOUND;
}

typedef struct css_dom_cascade {
    css_dom_host_t dom;
    robo_css_style_context_t *context;
    /* 9.8's two halves, so the fixture is a complete host: identity for property
     * names, and CSS's own facts about them (does it inherit by default, what is
     * its initial value). A context without them can match and resolve; only a
     * context with them can *compute*. */
    robo_css_property_table_t *table;
    robo_css_property_registry_t *registry;
} css_dom_cascade_t;

/** Build @p html, then a context over the real document and its own attributes. */
static void css_dom_cascade_begin(css_dom_cascade_t *fixture, const char *html)
{
    css_dom_build(&fixture->dom, html);
    fixture->context = NULL;
    fixture->table = NULL;
    fixture->registry = NULL;
    ROBO_CHECK_EQ_INT(robo_css_style_context_create(&fixture->context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_element_host(fixture->context,
                                                             &fixture->dom.host, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_inline_style(
                          fixture->context, css_dom_inline_style, &fixture->dom, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_table_create(&fixture->table, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&fixture->registry, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_property_registry_apply_to_table(fixture->registry,
                                                               fixture->table, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_table(fixture->context,
                                                               fixture->table, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_set_property_registry(fixture->context,
                                                                  fixture->registry, NULL),
                      ROBO_OK);
}

static void css_dom_cascade_end(css_dom_cascade_t *fixture)
{
    robo_css_style_context_destroy(fixture->context);
    fixture->context = NULL;
    robo_css_property_registry_destroy(fixture->registry);
    fixture->registry = NULL;
    robo_css_property_table_destroy(fixture->table);
    fixture->table = NULL;
    css_dom_destroy(&fixture->dom);
}

/** Parse @p css as an author sheet, parse its rules' selectors, add it. */
static robo_css_stylesheet_id_t css_dom_cascade_sheet(css_dom_cascade_t *fixture,
                                                      const char *css)
{
    robo_css_stylesheet_id_t id = css_parse_sheet(css, NULL);
    size_t index;

    for (index = 0u; index < robo_css_stylesheet_rule_count(id); index++) {
        robo_css_style_rule_t *rule = (robo_css_style_rule_t *)robo_css_rule_style(
            robo_css_stylesheet_rule(id, index));

        if (rule) {
            robo_css_error_t error = ROBO_CSS_CLEARED;

            ROBO_CHECK_EQ_INT(robo_css_style_rule_parse_selectors(
                                  rule, robo_css_stylesheet_source(id), &error, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_add_sheet(fixture->context,
                                                      ROBO_CSS_ORIGIN_AUTHOR, id, NULL),
                      ROBO_OK);
    return id;
}

static void css_dom_cascade_collect(css_dom_cascade_t *fixture,
                                    robo_css_element_ref_t ref,
                                    robo_css_cascade_t *out)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(
                          fixture->context, ref, out, &error, NULL),
                      ROBO_OK);
}

/** Compute @p ref's style through the chain door, asserting the call succeeded. */
static void css_dom_compute(css_dom_cascade_t *fixture,
                            robo_css_element_ref_t ref,
                            robo_css_computed_style_t **out_style)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(out_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(fixture->context, ref, *out_style,
                                                          &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_error_is_error(&error), 0);
}

/** The winning value's first token, as a NUL-terminated string. */
static void css_dom_cascade_winner(const robo_css_cascade_t *cascade,
                                   const char *name,
                                   char *out,
                                   size_t capacity)
{
    const robo_css_cascade_candidate_t *winner = NULL;
    const robo_css_value_t *value;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    ROBO_CHECK_EQ_INT(robo_css_cascade_resolve_name(
                          cascade, robo_css_slice_from_text(name), &winner),
                      ROBO_CSS_CASCADE_VERDICT_DECIDED);
    if (!winner) {
        return;
    }
    value = robo_css_declaration_value(winner->declaration);
    if (value && value->kind == ROBO_CSS_VALUE_KIND_LIST && value->children.count > 0u) {
        value = &value->children.items[0];
    }
    if (value && value->kind == ROBO_CSS_VALUE_KIND_TOKEN) {
        css_select_text(value->token.text, out, capacity);
    }
}

static void test_css_dom_cascade_inline_attribute_comes_from_the_dom(void)
{
    static const char *const html =
        "<div id=\"Card\" style=\"color: inline-style\"><p id=\"Text\">hello</p></div>";
    css_dom_cascade_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    robo_css_element_ref_t card;
    robo_css_element_ref_t text;
    char value[32];

    css_dom_cascade_begin(&fixture, html);
    sheet = css_dom_cascade_sheet(&fixture, "#Card { color: from-sheet; padding: 8px }");
    card = css_dom_ref_by_id(&fixture.dom, "Card");
    text = css_dom_ref_by_id(&fixture.dom, "Text");
    ROBO_CHECK_EQ_INT(card != ROBO_CSS_ELEMENT_REF_NONE, 1);

    css_dom_cascade_collect(&fixture, card, &cascade);
    /* The attribute was found, handed over as text, and parsed by RoboCSS — the
     * DOM still owns the bytes it came from. */
    ROBO_CHECK_EQ_INT(cascade.inline_checked, 1);
    ROBO_CHECK_EQ_INT(cascade.inline_present, 1);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 0);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    /* The sheet's `color` and `padding`, then the attribute's `color` — which
     * is where it belongs, last, because inline is the strongest normal level. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    css_dom_cascade_winner(&cascade, "color", value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "inline-style");
    /* The sheet's uncontested property is still decided, so the inline level did
     * not swallow the whole cascade. */
    css_dom_cascade_winner(&cascade, "padding", value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "8px");
    /* The inline candidate really is the inline one, not a lucky author rule. */
    ROBO_CHECK_EQ_INT(cascade.items[2].origin, ROBO_CSS_ORIGIN_INLINE);
    ROBO_CHECK_EQ_INT(cascade.items[2].level, ROBO_CSS_CASCADE_LEVEL_INLINE);
    ROBO_CHECK_EQ_INT(cascade.items[2].declaration_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[2].sheet, ROBO_CSS_STYLESHEET_ID_NONE);
    robo_css_cascade_dispose(&cascade);

    /* The nested element has no `style` attribute at all. */
    css_dom_cascade_collect(&fixture, text, &cascade);
    ROBO_CHECK_EQ_INT(cascade.inline_checked, 1);
    ROBO_CHECK_EQ_INT(cascade.inline_present, 0);
    ROBO_CHECK_EQ_INT(cascade.has_inline_error, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 0u);
    robo_css_cascade_dispose(&cascade);

    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

static void test_css_dom_cascade_uses_the_real_tree_relations(void)
{
    static const char *const html =
        "<section id=\"Outer\"><div class=\"inner\"><p id=\"Deep\">x</p></div></section>";
    css_dom_cascade_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    robo_css_element_ref_t deep;
    char value[32];

    css_dom_cascade_begin(&fixture, html);
    /* Three rules that match the same element by different routes: its id, a
     * descendant chain, and a child chain that must *not* match. Which two
     * arrive is a statement about the relations the cascade read from the DOM. */
    sheet = css_dom_cascade_sheet(&fixture,
                                  "#Deep { color: by-id }\n"
                                  ".inner p { color: by-descendant }\n"
                                  "#Outer > p { color: by-child }");
    deep = css_dom_ref_by_id(&fixture.dom, "Deep");
    ROBO_CHECK_EQ_INT(deep != ROBO_CSS_ELEMENT_REF_NONE, 1);

    css_dom_cascade_collect(&fixture, deep, &cascade);
    ROBO_CHECK_EQ_INT(cascade.rules_scanned, 3u);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 2u);
    ROBO_CHECK_EQ_INT(cascade.rules_undecided, 0u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 2u);
    /* Two matched rules, one level: specificity decides, and it is the id rule —
     * (1,0,0) against the descendant chain's (0,1,1). The relations came from
     * the real tree, so the specificity is deciding between two selectors that
     * really did match the real element. */
    css_specificity_is(cascade.items[0].specificity, 1u, 0u, 0u);
    css_specificity_is(cascade.items[1].specificity, 0u, 1u, 1u);
    css_dom_cascade_winner(&cascade, "color", value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "by-id");
    /* The candidates carry the sheet and rule they came from, which is how a
     * caller reads back that the child rule was the one left out. */
    ROBO_CHECK_EQ_INT(cascade.items[0].rule_index, 0u);
    ROBO_CHECK_EQ_INT(cascade.items[1].rule_index, 1u);
    ROBO_CHECK_EQ_INT(cascade.items[1].selector_index, 0u);

    robo_css_cascade_dispose(&cascade);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

static void test_css_dom_cascade_importance_outranks_inline(void)
{
    static const char *const html = "<p id=\"Note\" style=\"color: inline\">x</p>";
    css_dom_cascade_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    robo_css_element_ref_t note;
    char value[32];

    css_dom_cascade_begin(&fixture, html);
    /* An author `!important` beats the element's own normal inline declaration,
     * and the inline one still beats the plain author rule. This is the one
     * ordering that needs the whole table rather than a single comparison, and a
     * real `style` attribute is what makes it a statement about CSS rather than
     * about a stub. */
    sheet = css_dom_cascade_sheet(&fixture,
                                  "p { color: plain }\n#Note { color: strong !important }");
    note = css_dom_ref_by_id(&fixture.dom, "Note");
    ROBO_CHECK_EQ_INT(note != ROBO_CSS_ELEMENT_REF_NONE, 1);

    css_dom_cascade_collect(&fixture, note, &cascade);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    ROBO_CHECK_EQ_INT(cascade.items[0].origin, ROBO_CSS_ORIGIN_AUTHOR);
    ROBO_CHECK_EQ_INT(cascade.items[1].origin, ROBO_CSS_ORIGIN_INLINE);
    ROBO_CHECK_EQ_INT(cascade.items[2].level,
                      ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT);
    css_dom_cascade_winner(&cascade, "color", value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "strong");
    robo_css_cascade_dispose(&cascade);

    /* Without the `!important`, the same three declarations resolve to the
     * inline one — so the assertion above is about importance and not about the
     * order the declarations happened to arrive in. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    robo_css_style_context_clear(fixture.context);
    sheet = css_dom_cascade_sheet(&fixture, "p { color: plain }\n#Note { color: weak }");
    css_dom_cascade_collect(&fixture, note, &cascade);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    css_dom_cascade_winner(&cascade, "color", value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "inline");

    robo_css_cascade_dispose(&cascade);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

static void test_css_dom_cascade_does_not_disturb_the_picture(void)
{
    /*
     * Collecting a cascade for every element of the checkpoint document must
     * leave the document exactly as it found it — including its `style`
     * attributes, which this is the first phase to read the *text* of. The
     * comparison is the pixels the demo adapter actually paints, twice, from the
     * same document: the strongest available form of "reading it changed
     * nothing".
     */
    css_dom_cascade_t fixture;
    robo_demo_page_t *before = NULL;
    robo_demo_page_t *after = NULL;
    robo_gfx_surface_t *first = NULL;
    robo_gfx_surface_t *second = NULL;
    robo_css_stylesheet_id_t sheet;
    size_t stride;
    uint32_t row;
    robo_css_element_ref_t ref;
    size_t collected = 0u;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    ROBO_CHECK_EQ_INT(fixture.dom.count, 29u);
    sheet = css_dom_cascade_sheet(&fixture,
                                  "* { color: red }\nhtml body > * { padding: 1px }\n"
                                  "form > label + input { color: blue }");

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(fixture.dom.document, &before,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK(before != NULL);
    first = css_dom_surface(900, 800);
    second = css_dom_surface(900, 800);
    ROBO_CHECK(first != NULL && second != NULL);
    if (!before || !first || !second) {
        robo_demo_page_destroy(before);
        robo_gfx_surface_release(first);
        robo_gfx_surface_release(second);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
        css_dom_cascade_end(&fixture);
        return;
    }
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(before, first, NULL, NULL), ROBO_OK);

    for (ref = 1u; ref <= fixture.dom.count; ref++) {
        robo_css_cascade_t cascade = ROBO_CSS_CLEARED;

        css_dom_cascade_collect(&fixture, ref, &cascade);
        /* Every element is reached and the document can answer every relation,
         * so no element may come back with an undecided rule. */
        ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
        ROBO_CHECK_EQ_INT(cascade.rules_undecided, 0u);
        if (robo_css_cascade_count(&cascade) > 0u) {
            collected++;
        }
        robo_css_cascade_dispose(&cascade);
    }
    ROBO_CHECK_EQ_INT(collected, 29u);

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(fixture.dom.document, &after,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK(after != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(after, second, NULL, NULL), ROBO_OK);

    ROBO_CHECK_STR_EQ(robo_demo_page_title(after), robo_demo_page_title(before));
    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(after),
                      robo_demo_page_block_count(before));
    stride = robo_gfx_surface_stride(first);
    ROBO_CHECK_EQ_INT(stride, robo_gfx_surface_stride(second));
    for (row = 0u; row < 800u; row++) {
        const uint8_t *left =
            robo_gfx_surface_pixels_const(first) + (size_t)row * stride;
        const uint8_t *right =
            robo_gfx_surface_pixels_const(second) + (size_t)row * stride;

        if (memcmp(left, right, stride) != 0) {
            ROBO_CHECK_MSG(0, "collecting a cascade changed the rendered frame");
            break;
        }
    }

    robo_demo_page_destroy(before);
    robo_demo_page_destroy(after);
    robo_gfx_surface_release(first);
    robo_gfx_surface_release(second);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

static void test_css_dom_specificity_picks_the_winner(void)
{
    /* The specification's own example, against the real tree: `p` (0,0,1),
     * `.title` (0,1,0) and `#App .title` (1,1,0) all match the same paragraph,
     * and the cascade must pick the third. Tokenize → parse → match → specificity
     * → cascade, over a real RoboHTML document rather than a host double. */
    static const char *const html =
        "<div id=\"App\"><p id=\"Title\" class=\"title\">Hello</p></div>";
    css_dom_cascade_t fixture;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    robo_css_element_ref_t paragraph;
    char value[32];

    css_dom_cascade_begin(&fixture, html);
    sheet = css_dom_cascade_sheet(&fixture,
                                  "p { color: from-type }\n"
                                  ".title { color: from-class }\n"
                                  "#App .title { color: from-both }");
    paragraph = css_dom_ref_by_id(&fixture.dom, "Title");
    ROBO_CHECK_EQ_INT(paragraph != ROBO_CSS_ELEMENT_REF_NONE, 1);

    css_dom_cascade_collect(&fixture, paragraph, &cascade);
    ROBO_CHECK_EQ_INT(cascade.rules_matched, 3u);
    ROBO_CHECK_EQ_INT(cascade.unsupported, 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_count(&cascade), 3u);
    /* Ascending specificity in document order, and the last one wins. */
    css_specificity_is(cascade.items[0].specificity, 0u, 0u, 1u);
    css_specificity_is(cascade.items[1].specificity, 0u, 1u, 0u);
    css_specificity_is(cascade.items[2].specificity, 1u, 1u, 0u);
    css_dom_cascade_winner(&cascade, "color", value, sizeof(value));
    ROBO_CHECK_STR_EQ(value, "from-both");

    robo_css_cascade_dispose(&cascade);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The whole pipeline, over the visual checkpoint's document (9.8)     */
/* ------------------------------------------------------------------ */

/**
 * `css.dom.computed.pipeline` — tokenize → parse → match → specificity →
 * cascade → inherit → compute, over the document the visual checkpoint paints.
 *
 * The CSS-side suite drives the same stages over a host test double, where an
 * element, its parent and its siblings are whatever the fixture says. This one
 * runs them over the real tree: the parent links come from RoboHTML, the
 * `style` attribute comes from the DOM's own attribute storage, the rules match
 * through real selectors, and the values that come out are the ones the
 * checkpoint document's styles really produce. A stage that only worked against
 * a hand-shaped fixture would pass that suite and fail here.
 *
 * Four things are worth proving, and all four need a *typed* value to say:
 * two selectors of different specificity produce different colours, a child
 * inherits its parent's **computed** length rather than a second reading of what
 * the parent specified, a value that means nothing for its property is reported
 * rather than dropped, and a custom property travels the whole path untouched.
 */
static void test_css_dom_computed_pipeline(void)
{
    css_dom_cascade_t fixture;
    robo_css_computed_style_t *overview = NULL;
    robo_css_computed_style_t *label = NULL;
    robo_css_computed_style_t *field = NULL;
    robo_css_computed_style_t *button = NULL;
    robo_css_element_ref_t paragraph;
    const robo_css_computed_value_t *value;
    robo_css_stylesheet_id_t sheet;
    robo_css_color_t color;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "body { color: green; font-size: 20px; --theme: #0E7AFF }\n"
                                  "p { color: blue }\n"
                                  "#Overview { color: red }\n"
                                  "form { width: 300px }\n"
                                  "label { font-size: inherit; width: inherit; display: inline }\n"
                                  "input { width: 12px }\n"
                                  "button { color: nonsense }");

    /* The document really has the elements every assertion below is about, and
     * `#Overview` is the paragraph the id selector is aimed at. */
    ROBO_CHECK(css_dom_ref_by_id(&fixture.dom, "Overview") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "label") != ROBO_CSS_ELEMENT_REF_NONE);

    /* Specificity decided which declaration applies, and the value that came out
     * is a colour: four channels, not the three letters `red`. */
    css_dom_compute(&fixture, css_dom_ref_by_id(&fixture.dom, "Overview"), &overview);
    css_computed_is(overview, "color", "#ff0000");
    css_computed_flags_are(overview, "color", "declared");
    value = robo_css_computed_style_computed_by_name(overview,
                                                     robo_css_slice_from_text("color"));
    ROBO_CHECK(value != NULL);
    ROBO_CHECK_EQ_INT(value ? (int)value->kind : -1, ROBO_CSS_COMPUTED_VALUE_COLOR);
    color = robo_css_computed_value_color(value);
    ROBO_CHECK_EQ_INT(color.red, 255u);
    ROBO_CHECK_EQ_INT(color.green, 0u);
    ROBO_CHECK_EQ_INT(color.blue, 0u);
    ROBO_CHECK_EQ_INT(color.alpha, 255u);

    /* The other paragraph has no id, so the type selector beats the inherited
     * body colour — and the element that carries it is a different one. */
    paragraph = css_dom_ref_by_tag(&fixture.dom, "p");
    ROBO_CHECK(paragraph != css_dom_ref_by_id(&fixture.dom, "Overview"));
    {
        robo_css_computed_style_t *other = NULL;

        css_dom_compute(&fixture, paragraph, &other);
        css_computed_is(other, "color", "#0000ff");
        css_computed_flags_are(other, "color", "declared");
        robo_css_computed_style_destroy(other);
    }

    /* An inherited value is the parent's *computed* value: `font-size: inherit`
     * on the label lands on the 20px the body computed, as a typed length, and
     * the length travelled through two levels that declare nothing. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "label"), &label);
    css_computed_is(label, "font-size", "20px");
    /* The provenance is the parent, not the declaration: what the label asked
     * for was *inheritance*, and the value it got is the body's, two levels up,
     * already computed. */
    css_computed_flags_are(label, "font-size", "inherited");
    /* `inherit` on a property that does **not** inherit by default still pulls
     * the parent's computed value — the keyword is what makes it happen, and
     * without it the label would have no `width` entry at all. */
    css_computed_is(label, "width", "300px");
    css_computed_flags_are(label, "width", "inherited");
    value = robo_css_computed_style_computed_by_name(label,
                                                     robo_css_slice_from_text("font-size"));
    ROBO_CHECK_EQ_INT(value ? (int)value->kind : -1, ROBO_CSS_COMPUTED_VALUE_LENGTH);
    ROBO_CHECK_EQ_INT(value ? (int)value->unit : -1, (int)ROBO_CSS_UNIT_PX);
    ROBO_CHECK_EQ_INT(value ? (int)value->number : -1, 20);
    css_computed_is(label, "display", "inline");
    /* Nothing declared a colour on the label, so it has the body's, inherited
     * and then computed. */
    css_computed_is(label, "color", "#008000");
    css_computed_flags_are(label, "color", "inherited");
    /* A custom property is an opaque token sequence, and inheriting one does not
     * interpret it. */
    css_computed_is(label, "--theme", "#0E7AFF");
    css_computed_flags_are(label, "--theme", "inherited+custom");

    /* A dimension is a length with a unit, not the four characters of `12px`. */
    css_dom_compute(&fixture, css_dom_ref_by_id(&fixture.dom, "DisplayName"), &field);
    css_computed_is(field, "width", "12px");

    /* `color: nonsense` is a *wrong* value, and the style says so instead of
     * hiding the declaration or inventing a colour for it. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "button"), &button);
    css_computed_is(button, "color", "unknown colour name");
    css_computed_flags_are(button, "color", "declared+invalid");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(button), 1);
    ROBO_CHECK(robo_css_computed_style_incomplete_count(button) >= 1u);
    /* One wrong value does not corrupt the rest of the style. `display` is
     * non-inherited and nothing declared it, so it has no *entry* — which is a
     * different answer from a wrong one, and the query door is where a
     * non-inherited property's "nothing declared it" answer lives. */
    css_computed_is(button, "display", "<absent>");
    {
        robo_css_computed_value_t initial = ROBO_CSS_CLEARED;
        int from_initial = 0;

        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              fixture.context, button, robo_css_slice_from_text("display"),
                              &initial, &from_initial, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(from_initial, 1);
        ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(initial.kind), "keyword");
        robo_css_computed_value_dispose(&initial);
    }
    /* The inherited half of the style is present and unharmed. */
    css_computed_is(button, "font-size", "20px");
    css_computed_flags_are(button, "font-size", "inherited");

    robo_css_computed_style_destroy(button);
    robo_css_computed_style_destroy(field);
    robo_css_computed_style_destroy(label);
    robo_css_computed_style_destroy(overview);
    robo_css_computed_style_destroy(NULL);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

/**
 * `css.dom.computed.does_not_disturb_the_picture` — the visual vertical slice
 * with the whole CSS pipeline inserted into the middle of it.
 *
 * The checkpoint document is built once, painted, put through every stage 9.8
 * adds (a stylesheet covering most of its elements, matched, cascaded,
 * inherited and *computed* for every element in the tree), and painted again
 * from the same document. The two frames must be byte-identical.
 *
 * This is the strongest form of "computing a style does not change what you
 * see", because the comparison is the pixels the demo adapter produces rather
 * than a claim about the tree it reads. A stage that mutated an attribute,
 * reordered a child, borrowed a slice past its source's life or left an
 * inherited value aliasing its parent's would fail here and nowhere else.
 */
static void test_css_dom_computed_does_not_disturb_the_picture(void)
{
    css_dom_cascade_t fixture;
    robo_demo_page_t *before = NULL;
    robo_demo_page_t *after = NULL;
    robo_gfx_surface_t *first = NULL;
    robo_gfx_surface_t *second = NULL;
    robo_css_stylesheet_id_t sheet;
    robo_css_element_ref_t ref;
    size_t stride;
    uint32_t row;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "* { color: blue }\n"
                                  "html > body { font-size: 18px; line-height: 1.4 }\n"
                                  "body > * + * { margin: 4px }\n"
                                  "p { color: #336699 }\n"
                                  "#Overview { color: red !important }\n"
                                  "form > label { font-size: inherit; display: inline }\n"
                                  "input, textarea, select { width: calc(100% - 2px) }\n"
                                  "button { color: nonsense }\n"
                                  "body { --theme: blue }");

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(fixture.dom.document, &before, NULL),
                      ROBO_OK);
    ROBO_CHECK(before != NULL);
    first = css_dom_surface(900, 800);
    second = css_dom_surface(900, 800);
    ROBO_CHECK(first != NULL && second != NULL);
    if (!before || !first || !second) {
        robo_demo_page_destroy(before);
        robo_gfx_surface_release(first);
        robo_gfx_surface_release(second);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
        css_dom_cascade_end(&fixture);
        return;
    }
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(before, first, NULL, NULL), ROBO_OK);

    /* Every element, through all seven stages, twice over. */
    for (ref = 1u; ref <= fixture.dom.count; ref++) {
        robo_css_computed_style_t *style = NULL;
        size_t pass;

        for (pass = 0u; pass < 2u; pass++) {
            css_dom_compute(&fixture, ref, &style);
            ROBO_CHECK(robo_css_computed_style_count(style) > 0u);
            robo_css_computed_style_destroy(style);
            style = NULL;
        }
    }

    ROBO_CHECK_EQ_INT(robo_demo_page_create_from_document(fixture.dom.document, &after, NULL),
                      ROBO_OK);
    ROBO_CHECK(after != NULL);
    ROBO_CHECK_EQ_INT(robo_demo_page_paint(after, second, NULL, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_demo_page_block_count(after),
                      robo_demo_page_block_count(before));
    ROBO_CHECK_STR_EQ(robo_demo_page_title(after), robo_demo_page_title(before));
    stride = robo_gfx_surface_stride(first);
    ROBO_CHECK_EQ_INT(stride, robo_gfx_surface_stride(second));
    for (row = 0u; row < 800u; row++) {
        const uint8_t *left =
            robo_gfx_surface_pixels_const(first) + (size_t)row * stride;
        const uint8_t *right =
            robo_gfx_surface_pixels_const(second) + (size_t)row * stride;

        if (memcmp(left, right, stride) != 0) {
            ROBO_CHECK_MSG(0, "computing a style changed the rendered frame");
            break;
        }
    }

    robo_demo_page_destroy(before);
    robo_demo_page_destroy(after);
    robo_gfx_surface_release(first);
    robo_gfx_surface_release(second);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

void robo_test_register_css_dom_select(void);

void robo_test_register_css_dom_select(void)
{
    robo_test_register("css.dom.checkpoint_tree", test_css_dom_checkpoint_tree);
    robo_test_register("css.dom.checkpoint_descendant_and_child",
                       test_css_dom_checkpoint_descendant_and_child);
    robo_test_register("css.dom.checkpoint_siblings_ignore_text_nodes",
                       test_css_dom_checkpoint_siblings_ignore_text_nodes);
    robo_test_register("css.dom.checkpoint_attributes_and_forms",
                       test_css_dom_checkpoint_attributes_and_forms);
    robo_test_register("css.dom.checkpoint_structural",
                       test_css_dom_checkpoint_structural);
    robo_test_register("css.dom.checkpoint_reports_unevaluatable",
                       test_css_dom_checkpoint_reports_unevaluatable);
    robo_test_register("css.dom.checkpoint_matching_does_not_mutate",
                       test_css_dom_checkpoint_matching_does_not_mutate);
    robo_test_register("css.dom.text_and_comments_are_not_siblings",
                       test_css_dom_text_and_comments_are_not_siblings);
    robo_test_register("css.dom.deep_tree", test_css_dom_deep_tree);
    robo_test_register("css.dom.rebuild_after_mutation",
                       test_css_dom_rebuild_after_mutation);
    robo_test_register("css.dom.matching_is_deterministic",
                       test_css_dom_matching_is_deterministic);
    robo_test_register("css.dom.selectors_do_not_disturb_the_picture",
                       test_css_dom_selectors_do_not_disturb_the_picture);
    robo_test_register("css.dom.cascade.inline_attribute_comes_from_the_dom",
                       test_css_dom_cascade_inline_attribute_comes_from_the_dom);
    robo_test_register("css.dom.cascade.uses_the_real_tree_relations",
                       test_css_dom_cascade_uses_the_real_tree_relations);
    robo_test_register("css.dom.cascade.importance_outranks_inline",
                       test_css_dom_cascade_importance_outranks_inline);
    robo_test_register("css.dom.cascade.does_not_disturb_the_picture",
                       test_css_dom_cascade_does_not_disturb_the_picture);
    robo_test_register("css.dom.specificity.picks_the_winner",
                       test_css_dom_specificity_picks_the_winner);
    robo_test_register("css.dom.computed.pipeline", test_css_dom_computed_pipeline);
    robo_test_register("css.dom.computed.does_not_disturb_the_picture",
                       test_css_dom_computed_does_not_disturb_the_picture);
}
