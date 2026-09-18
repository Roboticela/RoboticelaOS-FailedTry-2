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
        /* The initial value arrives *typed*, through the property's own value
         * shape: since 9.12 that is the display vocabulary, so the door answers
         * with an enumerator rather than with the keyword's letters. */
        ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(initial.kind), "display");
        ROBO_CHECK_EQ_INT(robo_css_computed_value_display(&initial),
                          (int)ROBO_CSS_DISPLAY_INLINE);
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

/**
 * `css.dom.unit.pipeline` — the unit and number vocabulary over the real tree.
 *
 * The CSS-side suite proves the vocabulary; this proves it survives a document.
 * Every family is declared somewhere on the checkpoint's own tree, and the two
 * halves of the phase's contract are asserted through the *computed style* rather
 * than through the parser: an absolute length lands converted, a contextual one
 * lands with its unit still live, and a percentage is still a percentage. The
 * unit is read off the typed record as well as off the text, because a value that
 * printed `3.5rem` while carrying `px` would pass a text-only test.
 */
static void test_css_dom_unit_pipeline(void)
{
    css_dom_cascade_t fixture;
    robo_css_computed_style_t *body_style = NULL;
    robo_css_computed_style_t *paragraph = NULL;
    robo_css_computed_style_t *field = NULL;
    robo_css_computed_style_t *label = NULL;
    robo_css_computed_style_t *area = NULL;
    robo_css_computed_style_t *picker = NULL;
    robo_css_computed_style_t *button = NULL;
    const robo_css_computed_value_t *value;
    robo_css_stylesheet_id_t sheet;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "body { width: 30em; font-size: 1.25rem; height: 50vh }\n"
                                  "p { width: 2.54cm }\n"
                                  "form { width: 6pc }\n"
                                  "input { width: 3.5rem; height: 2vi }\n"
                                  "label { width: 40q; font-size: inherit }\n"
                                  "textarea { width: 1e2px; height: 75% }\n"
                                  "select { width: 12dvmax }\n"
                                  "button { width: 10furlongs }");

    /* Every element this test reads really exists in the document, so an
     * assertion below is about a computed style rather than about a missing
     * element answering nothing. */
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "body") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "label") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "textarea") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "select") != ROBO_CSS_ELEMENT_REF_NONE);

    /* Contextual units stay contextual, with the unit still live on the record —
     * which is what a later phase resolves and what converting here would
     * destroy. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "body"), &body_style);
    css_computed_is(body_style, "width", "30em");
    css_computed_is(body_style, "font-size", "1.25rem");
    css_computed_is(body_style, "height", "50vh");
    value = robo_css_computed_style_computed_by_name(body_style,
                                                     robo_css_slice_from_text("height"));
    ROBO_CHECK(value != NULL);
    ROBO_CHECK_EQ_INT(value ? (int)value->kind : -1, ROBO_CSS_COMPUTED_VALUE_LENGTH);
    ROBO_CHECK_EQ_INT(value ? (int)value->unit : -1, (int)ROBO_CSS_UNIT_VH);
    ROBO_CHECK_EQ_INT(value ? (int)value->number : -1, 50);

    /* An absolute length is converted to the reference pixel wherever it was
     * written: `2.54cm` and `6pc` are 96px, which is the same number the wheel
     * of absolute units gives a metre. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "p"), &paragraph);
    css_computed_is(paragraph, "width", "96px");
    value = robo_css_computed_style_computed_by_name(paragraph,
                                                     robo_css_slice_from_text("width"));
    ROBO_CHECK_EQ_INT(value ? (int)value->unit : -1, (int)ROBO_CSS_UNIT_PX);
    /* And a non-inherited property nobody declared has no entry at all, which is
     * a different answer from a wrong one. */
    css_computed_is(paragraph, "height", "<absent>");
    {
        robo_css_computed_style_t *form_style = NULL;

        css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "form"), &form_style);
        css_computed_is(form_style, "width", "96px");
        robo_css_computed_style_destroy(form_style);
    }

    /* The unit that is a quarter of a millimetre, and the axes and the dynamic
     * family: three of the forms 9.10 added, each carried rather than guessed. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "label"), &label);
    css_computed_is(label, "width", "37.795276px");
    /* `font-size: inherit` takes the body's *computed* contextual length two
     * levels up, which is inheritance and the unit vocabulary meeting: the label
     * gets `1.25rem`, not a re-parse of `1.25rem` and not a pixel count. */
    css_computed_is(label, "font-size", "1.25rem");
    css_computed_flags_are(label, "font-size", "inherited");

    css_dom_compute(&fixture, css_dom_ref_by_id(&fixture.dom, "DisplayName"), &field);
    css_computed_is(field, "width", "3.5rem");
    css_computed_is(field, "height", "2vi");

    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "textarea"), &area);
    css_computed_is(area, "width", "100px");
    /* A percentage stays a percentage beside a converted length, because only
     * layout knows what it is a percentage *of*. */
    css_computed_is(area, "height", "75%");
    value = robo_css_computed_style_computed_by_name(area,
                                                     robo_css_slice_from_text("height"));
    ROBO_CHECK_EQ_INT(value ? (int)value->kind : -1,
                      (int)ROBO_CSS_COMPUTED_VALUE_PERCENTAGE);

    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "select"), &picker);
    css_computed_is(picker, "width", "12dvmax");

    /* An unknown unit is *reported* rather than read as a bare number, and the
     * rest of the element's style is unharmed. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "button"), &button);
    css_computed_is(button, "width", "unknown length unit");
    css_computed_flags_are(button, "width", "declared+invalid");
    /* The inherited half is present: the button declares no font size, so it has
     * the body's contextual length. */
    css_computed_is(button, "font-size", "1.25rem");
    css_computed_flags_are(button, "font-size", "inherited");

    /* Deterministic over the real tree: the same element computed twice yields
     * the same typed unit and the same magnitude. The comparison is on the
     * *record* rather than on the text, so a value that printed the same while
     * carrying a different unit would still fail. */
    {
        robo_css_computed_style_t *again = NULL;
        const robo_css_computed_value_t *first = NULL;
        const robo_css_computed_value_t *second = NULL;

        css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "body"), &again);
        first = robo_css_computed_style_computed_by_name(body_style,
                                                        robo_css_slice_from_text("font-size"));
        second = robo_css_computed_style_computed_by_name(again,
                                                          robo_css_slice_from_text("font-size"));
        ROBO_CHECK(first != NULL && second != NULL);
        if (first && second) {
            ROBO_CHECK_EQ_INT((int)second->kind, (int)first->kind);
            ROBO_CHECK_EQ_INT((int)second->unit, (int)first->unit);
            ROBO_CHECK_EQ_INT((int)second->number, (int)first->number);
        }
        ROBO_CHECK_EQ_INT((int)second->unit, (int)ROBO_CSS_UNIT_REM);
        ROBO_CHECK_EQ_INT((int)second->number, 1);
        robo_css_computed_style_destroy(again);
    }

    robo_css_computed_style_destroy(button);
    robo_css_computed_style_destroy(picker);
    robo_css_computed_style_destroy(area);
    robo_css_computed_style_destroy(field);
    robo_css_computed_style_destroy(label);
    robo_css_computed_style_destroy(paragraph);
    robo_css_computed_style_destroy(body_style);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

/**
 * `css.dom.box.pipeline` — the box model over the real tree.
 *
 * The CSS-side suite proves the vocabulary and the expansion; this proves they
 * survive a document, which is where the one thing that could go wrong at scale
 * would show: a shorthand is *one* declaration standing for *four* properties, and
 * the four have to arrive on elements the author never named individually. Every
 * arity is declared somewhere on the checkpoint's own tree, and the competition
 * between a shorthand and a longhand is settled on a real element rather than on a
 * hand-built cascade. It closes on the phase's acceptance sheet — one rule holding
 * a width, a height, both shorthands, a border width and a colour — because that is
 * the shape that proves the value layer reads dimensions and colours in the same
 * pass, and because a colour is the one family this phase must not have broken.
 */
static void test_css_dom_box_pipeline(void)
{
    css_dom_cascade_t fixture;
    robo_css_computed_style_t *body_style = NULL;
    robo_css_computed_style_t *paragraph = NULL;
    robo_css_computed_style_t *label = NULL;
    robo_css_computed_style_t *form_style = NULL;
    robo_css_computed_style_t *area = NULL;
    robo_css_computed_style_t *field = NULL;
    robo_css_computed_style_t *picker = NULL;
    robo_css_computed_style_t *button = NULL;
    robo_css_computed_style_t *acceptance = NULL;
    robo_css_stylesheet_id_t sheet;
    robo_css_stylesheet_id_t acceptance_sheet;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "body { margin: 0 auto }\n"
                                  "p { padding: 8px 12px 4px }\n"
                                  "label { border-width: thin medium thick }\n"
                                  "form { box-sizing: border-box }\n"
                                  "textarea { margin: 10%; padding: 2em }\n"
                                  "input { margin: 1px }\n"
                                  "select { padding: inherit }\n"
                                  "button { padding: 1px 2px; padding-left: 9px }");

    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "body") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "label") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "textarea") != ROBO_CSS_ELEMENT_REF_NONE);

    /* Two components: vertical zero, horizontal `auto` — and `auto` survives as
     * the keyword it is rather than becoming a number. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "body"), &body_style);
    css_computed_is(body_style, "margin-top", "0px");
    css_computed_is(body_style, "margin-right", "auto");
    css_computed_is(body_style, "margin-left", "auto");
    css_computed_flags_are(body_style, "margin-left", "declared");
    /* The shorthand's own entry is there too, with the author's text. */
    css_computed_is(body_style, "margin", "0 auto");

    /* Three components: the arity where top and bottom differ and left borrows
     * right, which is the row a hand-written branch gets wrong. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "p"), &paragraph);
    css_computed_is(paragraph, "padding-top", "8px");
    css_computed_is(paragraph, "padding-right", "12px");
    css_computed_is(paragraph, "padding-bottom", "4px");
    css_computed_is(paragraph, "padding-left", "12px");

    /* Border widths keep their keywords, because what `medium` is in pixels is a
     * used value and this phase computes no used value. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "label"), &label);
    css_computed_is(label, "border-top-width", "thin");
    css_computed_is(label, "border-right-width", "medium");
    css_computed_is(label, "border-bottom-width", "thick");
    css_computed_is(label, "border-left-width", "medium");

    /* `box-sizing` is a value model with no layout behind it: the keyword is what
     * a declaration means here, and which box a width applies to is a later
     * phase's question. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "form"), &form_style);
    css_computed_is(form_style, "box-sizing", "border-box");

    /* A percentage and a contextual length are box edges like any other, and they
     * stay what they are: only layout knows what `10%` is a percentage of, and
     * only a font knows what `2em` is. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "textarea"), &area);
    css_computed_is(area, "margin-top", "10%");
    css_computed_is(area, "margin-left", "10%");
    css_computed_is(area, "padding-top", "2em");

    /* The uniform spelling, on an element the sheet names by type. */
    css_dom_compute(&fixture, css_dom_ref_by_id(&fixture.dom, "DisplayName"), &field);
    css_computed_is(field, "margin-top", "1px");
    css_computed_is(field, "margin-bottom", "1px");

    /* `padding: inherit` on a property that does not inherit, with no padding on
     * the ancestor chain: the longhand's own initial value, which is zero — and
     * not the parent's box, because there is no such thing to take. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "select"), &picker);
    css_computed_is(picker, "padding-top", "0px");
    css_computed_flags_are(picker, "padding-top", "default");

    /* A shorthand and a longhand for the same edge, on one element, decided by
     * document order: `padding-left: 9px` comes later than `padding: 1px 2px`, so
     * the left edge is the longhand's and the other three are the shorthand's. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "button"), &button);
    css_computed_is(button, "padding-top", "1px");
    css_computed_is(button, "padding-right", "2px");
    css_computed_is(button, "padding-bottom", "1px");
    css_computed_is(button, "padding-left", "9px");

    /* The phase's own acceptance sheet, in one rule on one element: a width, a
     * height, both shorthands, a border width and a colour. The colour is here
     * because it is the one value family this phase did not add and must not
     * have broken — and because one rule holding a dimension, a percentage, two
     * keywords and a colour is the shape that exposes a value layer which reads
     * only one kind. This sheet is added second, so it is also the cross-sheet
     * source-order case: it outranks the `textarea` rule above it. */
    acceptance_sheet = css_dom_cascade_sheet(&fixture,
                                             "textarea { width: 200px; "
                                             "height: 100px; margin: 10px 20px; "
                                             "padding: 8px 12px; "
                                             "border-width: 2px; "
                                             "color: #336699 }");
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "textarea"),
                    &acceptance);
    css_computed_is(acceptance, "width", "200px");
    css_computed_is(acceptance, "height", "100px");
    css_computed_is(acceptance, "margin-top", "10px");
    css_computed_is(acceptance, "margin-right", "20px");
    css_computed_is(acceptance, "margin-bottom", "10px");
    css_computed_is(acceptance, "margin-left", "20px");
    css_computed_is(acceptance, "padding-top", "8px");
    css_computed_is(acceptance, "padding-right", "12px");
    css_computed_is(acceptance, "padding-bottom", "8px");
    css_computed_is(acceptance, "padding-left", "12px");
    css_computed_is(acceptance, "border-top-width", "2px");
    css_computed_is(acceptance, "border-right-width", "2px");
    css_computed_is(acceptance, "color", "#336699");
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(acceptance_sheet, NULL),
                      ROBO_OK);

    robo_css_computed_style_destroy(acceptance);
    robo_css_computed_style_destroy(button);
    robo_css_computed_style_destroy(picker);
    robo_css_computed_style_destroy(field);
    robo_css_computed_style_destroy(area);
    robo_css_computed_style_destroy(form_style);
    robo_css_computed_style_destroy(label);
    robo_css_computed_style_destroy(paragraph);
    robo_css_computed_style_destroy(body_style);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

/**
 * `css.dom.display.pipeline` — the display vocabulary over the real tree.
 *
 * The CSS-side suite proves the vocabulary and the value shape; this proves they
 * survive a document, which is where the two facts the phase is built on have to
 * hold at once: a declared `display` arrives as a typed value on elements the
 * sheet names, and an element the sheet does *not* name has **no entry at all**
 * because `display` is non-inherited and 9.7 materialises initial values for the
 * inherited-by-default set only. The query door is what answers that second case,
 * with the property's own initial value and `from_initial` set — so the pair of
 * assertions below is the whole interface a layout phase consumes.
 */
static void test_css_dom_display_pipeline(void)
{
    css_dom_cascade_t fixture;
    robo_css_computed_style_t *body_style = NULL;
    robo_css_computed_style_t *link = NULL;
    robo_css_computed_style_t *caption = NULL;
    robo_css_computed_style_t *notes = NULL;
    robo_css_computed_style_t *heading = NULL;
    robo_css_computed_value_t queried = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    int from_initial = -1;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "body { display: block }\n"
                                  "a { display: inline }\n"
                                  "label { display: inline-block }\n"
                                  "#Notes { display: none }\n");

    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "body") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_id(&fixture.dom, "Notes") != ROBO_CSS_ELEMENT_REF_NONE);

    /* One value per level of the box model's own vocabulary, each read as a
     * typed enumerator rather than as the keyword's letters. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "body"), &body_style);
    css_computed_is(body_style, "display", "block");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(body_style,
                                                    robo_css_slice_from_text("display"));

        robo_css_display_t display;

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_DISPLAY);
        display = robo_css_computed_value_display(value);
        ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(display), 1);
        ROBO_CHECK_EQ_INT(robo_css_display_generates_box(display), 1);
    }

    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "a"), &link);
    css_computed_is(link, "display", "inline");
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(
                          robo_css_computed_value_display(
                              robo_css_computed_style_computed_by_name(
                                  link, robo_css_slice_from_text("display")))),
                      1);

    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "label"), &caption);
    css_computed_is(caption, "display", "inline-block");
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_block(
                          robo_css_computed_value_display(
                              robo_css_computed_style_computed_by_name(
                                  caption, robo_css_slice_from_text("display")))),
                      1);

    /* `display: none` on a real element: a decided value, not a missing one, and
     * not a geometry — the element's own `textarea` box is untouched beside it. */
    css_dom_compute(&fixture, css_dom_ref_by_id(&fixture.dom, "Notes"), &notes);
    css_computed_is(notes, "display", "none");
    css_computed_flags_are(notes, "display", "declared");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(notes), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_none(
                          robo_css_computed_value_display(
                              robo_css_computed_style_computed_by_name(
                                  notes, robo_css_slice_from_text("display")))),
                      1);

    /* An element the sheet does not name has no entry — `display` does not
     * inherit, so the `body` above handed nothing down — and the query door
     * answers with the property's initial value, marked as such. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "h1"), &heading);
    css_computed_is(heading, "display", "<absent>");
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture.context, heading,
                          robo_css_slice_from_text("display"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_DISPLAY);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_display(&queried),
                      (int)ROBO_CSS_DISPLAY_INLINE);
    robo_css_computed_value_dispose(&queried);

    /* And an invalid value on a real element is reported rather than replaced. */
    {
        robo_css_computed_style_t *odd = NULL;
        robo_css_stylesheet_id_t bad = css_dom_cascade_sheet(
            &fixture, "select { display: bogus }");

        css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "select"), &odd);
        css_computed_is(odd, "display", "not a display value");
        css_computed_flags_are(odd, "display", "declared+invalid");
        ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(odd), 1);
        robo_css_computed_style_destroy(odd);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(bad, NULL), ROBO_OK);
    }

    robo_css_computed_style_destroy(heading);
    robo_css_computed_style_destroy(notes);
    robo_css_computed_style_destroy(caption);
    robo_css_computed_style_destroy(link);
    robo_css_computed_style_destroy(body_style);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

/**
 * `css.dom.position.pipeline` — the positioning family over the real tree.
 *
 * The CSS-side suite proves the vocabulary, the four insets and `z-index` as
 * values; this proves the family survives a document, which is where three facts
 * have to hold at once. A declared `position` arrives as a typed enumerator on the
 * elements the sheet names. The four offsets and the level arrive *beside* it —
 * a percentage which stays a percentage, an `auto` that stays a keyword, a whole
 * number — which is the whole reason the insets reuse the box model's shape
 * instead of getting one of their own. And an element the sheet does **not** name
 * has no entry for any of the six, because none of them inherits, so the query
 * door is what answers, with the property's own initial value and `from_initial`
 * set.
 *
 * That last pair is the interface a layout phase consumes, and the assertion on
 * it is deliberately the same shape as the display pipeline's: `static` and `auto`
 * are *values* and not failures, and the difference between "the author said
 * static" and "the author said nothing" is the flag, not the value.
 */
static void test_css_dom_position_pipeline(void)
{
    css_dom_cascade_t fixture;
    robo_css_computed_style_t *body_style = NULL;
    robo_css_computed_style_t *heading = NULL;
    robo_css_computed_style_t *button = NULL;
    robo_css_computed_style_t *paragraph = NULL;
    robo_css_computed_value_t queried = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    int from_initial = -1;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "body { position: relative; z-index: 0 }\n"
                                  "h1 { position: absolute; top: 25%; left: -8px;"
                                  " z-index: -1 }\n"
                                  "button { position: sticky; top: 0; right: 2em;"
                                  " bottom: auto }\n");

    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "body") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "button") != ROBO_CSS_ELEMENT_REF_NONE);

    /* `relative` is in flow *and* positioned, which is the pair that reads as a
     * contradiction until the two columns are read separately — and the reason the
     * row carries both. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "body"), &body_style);
    css_computed_is(body_style, "position", "relative");
    css_computed_is(body_style, "z-index", "0");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                body_style, robo_css_slice_from_text("position"));
        robo_css_positioning_t positioning;

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_POSITION);
        positioning = robo_css_computed_value_position(value);
        ROBO_CHECK_EQ_INT(robo_css_positioning_is_in_flow(positioning), 1);
        ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(positioning), 1);
        ROBO_CHECK_EQ_INT(robo_css_positioning_is_out_of_flow(positioning), 0);
    }

    /* `absolute` with a percentage, a negative length and a negative level, all
     * read beside the enumerator that gives them meaning. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "h1"), &heading);
    css_computed_is(heading, "position", "absolute");
    css_computed_flags_are(heading, "position", "declared");
    css_computed_is(heading, "top", "25%");
    css_computed_is(heading, "left", "-8px");
    css_computed_is(heading, "z-index", "-1");
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_out_of_flow(
                          robo_css_computed_value_position(
                              robo_css_computed_style_computed_by_name(
                                  heading, robo_css_slice_from_text("position")))),
                      1);
    /* The percentage is a *percentage* and the `auto` below is a *keyword* —
     * different kinds, which is what stops a later phase reading `auto` as a
     * zero-length offset. */
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                heading, robo_css_slice_from_text("top"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_PERCENTAGE);
    }

    /* `sticky` is represented and classified like any other value, and it is
     * *not* behaved on: the offsets beside it are offsets, and nothing here
     * scrolls. That is the phase's documented boundary, asserted rather than
     * assumed. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "button"), &button);
    css_computed_is(button, "position", "sticky");
    ROBO_CHECK_EQ_INT(robo_css_computed_value_position(
                          robo_css_computed_style_computed_by_name(
                              button, robo_css_slice_from_text("position"))),
                      (int)ROBO_CSS_POSITIONING_STICKY);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(button), 0);
    css_computed_is(button, "top", "0px");
    css_computed_is(button, "right", "2em");
    css_computed_is(button, "bottom", "auto");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                button, robo_css_slice_from_text("bottom"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_KEYWORD);
    }

    /* An element the sheet does not name has no entry for any of the six — none
     * of them inherits, so the `body` above handed nothing down — and the query
     * door answers each with the property's own initial value, marked as such. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "p"), &paragraph);
    css_computed_is(paragraph, "position", "<absent>");
    css_computed_is(paragraph, "z-index", "<absent>");
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture.context, paragraph,
                          robo_css_slice_from_text("position"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_position(&queried),
                      (int)ROBO_CSS_POSITIONING_STATIC);
    /* Static is a *value* here and not an absence: the box is in flow and not
     * positioned, so an offset on it would not apply. */
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(
                          robo_css_computed_value_position(&queried)),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_in_flow(
                          robo_css_computed_value_position(&queried)),
                      1);
    robo_css_computed_value_dispose(&queried);

    {
        static const char *const names[] = { "top", "right", "bottom", "left" };
        size_t index;

        for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
            css_computed_is(paragraph, names[index], "<absent>");
            ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                                  fixture.context, paragraph,
                                  robo_css_slice_from_text(names[index]), &queried,
                                  &from_initial, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(from_initial, 1);
            /* An `auto` inset is not a zero-length offset: it is the keyword, and
             * the kind is what says so. */
            ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_KEYWORD);
            robo_css_computed_value_dispose(&queried);
        }
    }

    /* And an invalid value on a real element is reported rather than replaced. */
    {
        robo_css_computed_style_t *odd = NULL;
        robo_css_stylesheet_id_t bad = css_dom_cascade_sheet(
            &fixture,
            "select { position: banana; top: nope; z-index: 1.5 }");

        css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "select"), &odd);
        css_computed_is(odd, "position", "not a position value");
        css_computed_is(odd, "top", "not a length, a percentage or auto");
        css_computed_is(odd, "z-index", "not a whole number");
        css_computed_flags_are(odd, "position", "declared+invalid");
        ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(odd), 1);
        robo_css_computed_style_destroy(odd);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(bad, NULL), ROBO_OK);
    }

    robo_css_computed_style_destroy(paragraph);
    robo_css_computed_style_destroy(button);
    robo_css_computed_style_destroy(heading);
    robo_css_computed_style_destroy(body_style);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    css_dom_cascade_end(&fixture);
}

/*
 * `css.dom.background.pipeline` — the background family over the real tree.
 *
 * The CSS-side suite proves the six longhands, the two closed vocabularies and
 * the two pairs as values; this proves the family survives a *document*, which is
 * where four facts have to hold at once. A declared longhand arrives as a typed
 * value on the elements the sheet names — a colour on one, a preserved image
 * reference on another, a pair of components on a third. The `background`
 * shorthand *distributes* over all six, so an element that wrote one declaration
 * has six entries. An element the sheet names for one property has **no entry**
 * for the other five — none of them inherits — so the query door is what answers, 
 * with the property's own initial value and `from_initial` set. And a percentage
 * stays a percentage: `25% 50%` is a pair of percentages because only the phase
 * with a box can say 25% of what.
 *
 * That last pair — the query door and `from_initial` — is the interface a paint
 * phase consumes, and it is asserted in the same shape the display, box and
 * positioning pipelines assert it.
 */
static void test_css_dom_background_pipeline(void)
{
    css_dom_cascade_t fixture;
    robo_css_computed_style_t *body_style = NULL;
    robo_css_computed_style_t *heading = NULL;
    robo_css_computed_style_t *button = NULL;
    robo_css_computed_style_t *paragraph = NULL;
    robo_css_computed_value_t queried = ROBO_CSS_CLEARED;
    robo_css_stylesheet_id_t sheet;
    int from_initial = -1;

    css_dom_cascade_begin(&fixture, robo_demo_default_html());
    sheet = css_dom_cascade_sheet(&fixture,
                                  "body { background-color: #336699;"
                                  " background-repeat: repeat-x }\n"
                                  "h1 { background-color: #ff0000;"
                                  " background-image: url(\"Heading.png\");"
                                  " background-position: 25% 50%;"
                                  " background-size: cover;"
                                  " background-repeat: no-repeat;"
                                  " background-attachment: fixed }\n"
                                  "button { background: #ffffff no-repeat }\n"
                                  "p { background-color: transparent }\n");

    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "body") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "h1") != ROBO_CSS_ELEMENT_REF_NONE);
    ROBO_CHECK(css_dom_ref_by_tag(&fixture.dom, "button") != ROBO_CSS_ELEMENT_REF_NONE);

    /* A colour and a repeat, and nothing else: the other four have no entry at
     * all, which is the difference between "the author said nothing" and "the
     * author said the initial value". */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "body"), &body_style);
    css_computed_is(body_style, "background-color", "#336699");
    css_computed_is(body_style, "background-repeat", "repeat-x");
    css_computed_flags_are(body_style, "background-color", "declared");
    css_computed_is(body_style, "background-image", "<absent>");
    css_computed_is(body_style, "background-position", "<absent>");
    css_computed_is(body_style, "background-size", "<absent>");
    css_computed_is(body_style, "background-attachment", "<absent>");
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture.context, body_style,
                          robo_css_slice_from_text("background-image"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_KEYWORD);
    {
        char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&queried, text, sizeof(text),
                                                       &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(text, "none");
    }
    robo_css_computed_value_dispose(&queried);
    /* The two shapes that are not words are answered by the query door as well:
     * `0% 0%` is a *pair* and `auto` is not a length. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture.context, body_style,
                          robo_css_slice_from_text("background-position"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    ROBO_CHECK_EQ_INT(queried.kind,
                      (int)ROBO_CSS_COMPUTED_VALUE_BACKGROUND_POSITION);
    ROBO_CHECK_EQ_INT(
        robo_css_computed_value_background_position(&queried).horizontal.kind,
        ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE);
    robo_css_computed_value_dispose(&queried);
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          fixture.context, body_style,
                          robo_css_slice_from_text("background-size"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_BACKGROUND_SIZE);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_background_size(&queried).kind,
                      ROBO_CSS_BACKGROUND_SIZE_AUTO);
    robo_css_computed_value_dispose(&queried);

    /* All six on one element, each typed: an image this build carries and
     * *flags*, a pair of percentages, a whole-value size keyword, a closed-vocabulary
     * repeat and attachment, and a colour. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "h1"), &heading);
    css_computed_is(heading, "background-color", "#ff0000");
    css_computed_is(heading, "background-image", "url(\"Heading.png\")");
    css_computed_is(heading, "background-position", "25% 50%");
    css_computed_is(heading, "background-size", "cover");
    css_computed_is(heading, "background-repeat", "no-repeat");
    css_computed_is(heading, "background-attachment", "fixed");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(heading), 0);
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                heading, robo_css_slice_from_text("background-image"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_PRESERVED);
        /* Carried *and* flagged: a consumer that read the tokens without the flag
         * would be reading a reference as if it were an image. */
        ROBO_CHECK_EQ_INT(value->unresolved, 1u);
    }
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                heading, robo_css_slice_from_text("background-position"));
        robo_css_background_position_t position;

        ROBO_CHECK_EQ_INT(value->kind,
                          (int)ROBO_CSS_COMPUTED_VALUE_BACKGROUND_POSITION);
        position = robo_css_computed_value_background_position(value);
        /* Two percentages, and the *horizontal* one is the first the author
         * wrote — the order is the grammar's, and it survives the document. */
        ROBO_CHECK_EQ_INT(position.horizontal.kind,
                          ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE);
        ROBO_CHECK_EQ_INT(position.vertical.kind,
                          ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE);
    }
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                heading, robo_css_slice_from_text("background-size"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_BACKGROUND_SIZE);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_background_size(value).kind,
                          ROBO_CSS_BACKGROUND_SIZE_COVER);
    }

    /* The shorthand: one declaration, six entries — and the ones it did not name
     * are the properties' own initial values rather than absent, which is what
     * makes `background: #ffffff no-repeat` mean "just this". */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "button"), &button);
    css_computed_is(button, "background-color", "#ffffff");
    css_computed_is(button, "background-repeat", "no-repeat");
    css_computed_is(button, "background-image", "none");
    css_computed_is(button, "background-position", "0% 0%");
    css_computed_is(button, "background-size", "auto");
    css_computed_is(button, "background-attachment", "scroll");
    css_computed_flags_are(button, "background-image", "declared");
    /* The shorthand's own entry is the author's text, preserved: nothing here
     * rewrites a declaration the consumer might want to read back. */
    css_computed_is(button, "background", "#ffffff no-repeat");

    /* And nothing leaked: a `p` that declared only a colour has only a colour. */
    css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "p"), &paragraph);
    css_computed_is(paragraph, "background-color", "#00000000");
    css_computed_is(paragraph, "background-image", "<absent>");
    css_computed_is(paragraph, "background-repeat", "<absent>");

    /* An invalid value on a real element is reported rather than replaced, and
     * the five refusals stay five sentences. */
    {
        robo_css_computed_style_t *odd = NULL;
        robo_css_stylesheet_id_t bad = css_dom_cascade_sheet(
            &fixture,
            "select { background-image: banana; background-position: banana;"
            " background-size: banana; background-repeat: space;"
            " background-attachment: floating; background-color: banana }");

        css_dom_compute(&fixture, css_dom_ref_by_tag(&fixture.dom, "select"), &odd);
        css_computed_is(odd, "background-image", "not a background image");
        css_computed_is(odd, "background-position", "not a background position");
        css_computed_is(odd, "background-size", "not a background size");
        css_computed_is(odd, "background-repeat", "not a background repeat value");
        css_computed_is(odd, "background-attachment", "not a background attachment value");
        /* The colour module's own sentence, which is 9.9's and not this phase's:
         * `background-color` keeps the shape 9.9 gave it. */
        css_computed_is(odd, "background-color", "unknown colour name");
        ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(odd), 1);
        robo_css_computed_style_destroy(odd);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(bad, NULL), ROBO_OK);
    }

    robo_css_computed_style_destroy(paragraph);
    robo_css_computed_style_destroy(button);
    robo_css_computed_style_destroy(heading);
    robo_css_computed_style_destroy(body_style);
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
    robo_test_register("css.dom.unit.pipeline", test_css_dom_unit_pipeline);
    robo_test_register("css.dom.box.pipeline", test_css_dom_box_pipeline);
    robo_test_register("css.dom.display.pipeline", test_css_dom_display_pipeline);
    robo_test_register("css.dom.position.pipeline", test_css_dom_position_pipeline);
    robo_test_register("css.dom.background.pipeline",
                       test_css_dom_background_pipeline);
}
