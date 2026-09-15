/*
 * RoboHTML element model (Phase 8.5).
 *
 * An element IS a node: its identity is the node handle from
 * RoboHTMLNode.h, its owner is the document, and its parent/child/sibling
 * links are the tree links already in place. This header adds no second
 * ownership model — it adds the element *view* over element-kind nodes:
 * tag name, namespace, classification, state, and the attribute boundary.
 *
 *   Document (8.4)
 *     └── Node (8.1/8.3)
 *           └── Element (8.5)  ← namespace, classification, state,
 *                                attribute access, creation seam
 *
 * Identity and lifetime
 *   - robo_html_element_create is the construction seam: the tree builder
 *     uses it now, the future DOM will use it for createElement. An element
 *     is always created *inside* a document, directly linked under a parent,
 *     so "an element with no document" cannot be expressed. There is no
 *     detached-element lifetime in this phase.
 *   - Destruction is the document's business alone (document destroy, or a
 *     tree clear). An element handed out and then retired resolves as stale.
 *
 * Naming policy (Roboticela)
 *   Element and attribute names are stored as lowercase ASCII. The
 *   tokenizer already lowercases parser input; robo_html_element_create
 *   lowercases what it is given, so the invariant holds for the DOM seam
 *   too. robo_html_element_name_normalize is the strict policy helper for
 *   *application*-provided names (it validates as it lowercases); the parser
 *   path deliberately does not validate, because tolerating a malformed tag
 *   name is required behaviour, not an error.
 *
 * Attributes (Phase 8.6)
 *   The element's attributes are the real attribute model, not a temporary
 *   table: one `robo_html_attribute_t` per attribute, in a deterministic
 *   order, with add / set / remove / lookup / presence / iteration. The
 *   tokenizer's staged values are a short-lived parser view of the same type,
 *   so there is exactly one attribute representation in the runtime.
 *
 *   ORDER      attributes keep the order they were given in. Parsing
 *              preserves source order; `set` keeps the first match's
 *              position; `add` appends; `remove` preserves the relative
 *              order of everything left.
 *
 *   DUPLICATES Parsed duplicates are preserved exactly as the tokenizer
 *              emitted them (an HTML parser does not fold them and this
 *              model does not invent a different answer). Lookup returns the
 *              first match. `set` collapses matches to one, at the first
 *              match's position; `remove` drops every match; `add` is the
 *              only operation that can introduce a duplicate.
 *
 *   CASE       Names are stored lowercase ASCII, and lookup is ASCII
 *              case-insensitive. Storage and lookup semantics are therefore
 *              separate concerns: the stored form is cheap to compare, and
 *              callers may pass any case. Preserving original case for
 *              foreign content is a later concern — the parser never
 *              switches namespace yet, so the tokenizer's lowercasing is
 *              lossless here.
 *
 *   PRESENCE   An attribute is present because a record exists for it, which
 *              is what makes `<input disabled>` and `<input disabled="">`
 *              both "present with an empty value" and distinguishable from
 *              "absent". Presence is never inferred from the value.
 *
 * What this phase is NOT: CSS/selectors/cascade, layout, paint, JavaScript,
 * events, custom elements, foreign-content parsing, `NamedNodeMap`, the
 * `dataset` API, and the rest of the Element DOM surface
 * (getElementById / querySelector / classList).
 */

#ifndef ROBO_HTML_ELEMENT_H
#define ROBO_HTML_ELEMENT_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum copied element/attribute name length (excluding NUL). */
#define ROBO_HTML_NAME_MAX 64u

/** Maximum copied attribute value length (excluding NUL) in the stub. */
#define ROBO_HTML_ATTR_VALUE_MAX 256u

/**
 * Future element registry reservation. Phase 8.1 defines only the shape
 * the element subsystem will register against; it is intentionally opaque.
 */
typedef struct robo_html_element_boundary {
    /** Reserved; must be 0. Guards against accidental use before the DOM phase. */
    uint32_t reserved;
} robo_html_element_boundary_t;

/** Reset the boundary reservation. Safe on NULL. */
void robo_html_element_boundary_init(robo_html_element_boundary_t *b);

/**
 * Validate an element/attribute name at the boundary:
 *   - non-empty, ASCII letters/digits/'-'/'_' only,
 *   - length <= ROBO_HTML_NAME_MAX.
 * This is a policy check for later phases, exposed so the contract is
 * testable now. No storage, no registry.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL/empty); ROBO_PARSE_ERROR
 *         (bad character); ROBO_NOT_SUPPORTED (too long).
 */
robo_result_t robo_html_element_name_validate(const char *name,
                                              robo_html_diag_t *diag);

/**
 * ASCII-lowercase copy of @p name into @p out (capacity
 * ROBO_HTML_NAME_MAX). Future tokenizer/tree-builder naming policy.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT / ROBO_PARSE_ERROR per validate;
 *         ROBO_NOT_SUPPORTED when the copy would overflow.
 */
robo_result_t robo_html_element_name_normalize(const char *name,
                                               char *out,
                                               robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Element classification                                              */
/* ------------------------------------------------------------------ */

/**
 * Coarse role an element plays, derived from (namespace, tag name).
 *
 * This is deliberately NOT the HTML content-category specification: it is
 * the small vocabulary a renderer or a later phase needs to make one
 * decision without a string table of its own. It is a pure function of the
 * element's identity, so classifying costs no storage and no lookup, and an
 * unrecognised element is @c GENERIC rather than an error — which is what
 * makes custom elements, SVG and future elements safe to classify.
 */
typedef enum robo_html_element_class {
    /** Not a known element name in this namespace. */
    ROBO_HTML_ELEMENT_CLASS_GENERIC = 0,
    /** The document element, <html>. */
    ROBO_HTML_ELEMENT_CLASS_ROOT = 1,
    /** Document metadata: <head>, <title>, <meta>, <link>, <script>, ... */
    ROBO_HTML_ELEMENT_CLASS_METADATA = 2,
    /** Block containers: <body>, <div>, <section>, <li>, ... */
    ROBO_HTML_ELEMENT_CLASS_SECTION = 3,
    /** <h1> … <h6>. */
    ROBO_HTML_ELEMENT_CLASS_HEADING = 4,
    /** <p>. */
    ROBO_HTML_ELEMENT_CLASS_PARAGRAPH = 5,
    /** Text-level elements: <span>, <a>, <em>, <code>, <br>, ... */
    ROBO_HTML_ELEMENT_CLASS_TEXT = 6,
    /** Replaced/embedded content: <img>, <video>, <canvas>, <iframe>, ... */
    ROBO_HTML_ELEMENT_CLASS_REPLACED = 7,
    /** Form controls: <button>, <input>, <select>, <form>, ... */
    ROBO_HTML_ELEMENT_CLASS_CONTROL = 8,
    /** Number of defined classes. */
    ROBO_HTML_ELEMENT_CLASS_COUNT = 9
} robo_html_element_class_t;

/** Non-zero if @p cls is a defined element class enumerator. */
int robo_html_element_class_is_known(robo_html_element_class_t cls);

/** Human-readable class name; never NULL; "unknown" when undefined. */
const char *robo_html_element_class_name(robo_html_element_class_t cls);

/**
 * Classify @p local_name in @p ns. @p local_name must already be lowercase
 * (stored names are), but the comparison is ASCII case-insensitive so a
 * caller holding a name from elsewhere still gets the right answer.
 * Anything unrecognised — including every name in a non-HTML namespace —
 * is @c GENERIC, never an error.
 */
robo_html_element_class_t robo_html_element_classify(
    robo_html_namespace_t ns,
    const char *local_name);

/* ------------------------------------------------------------------ */
/* Element state                                                       */
/* ------------------------------------------------------------------ */

/** Element state bits (see @c state in robo_html_element_view_t). */
#define ROBO_HTML_ELEMENT_STATE_NONE 0u
/** The node was created by parsing (origin PARSER). */
#define ROBO_HTML_ELEMENT_STATE_PARSER_CREATED 1u
/** The element is reachable from the document root. */
#define ROBO_HTML_ELEMENT_STATE_CONNECTED 2u
/** The source wrote "/>" for this element. */
#define ROBO_HTML_ELEMENT_STATE_SELF_CLOSING 4u

/* ------------------------------------------------------------------ */
/* Element view                                                        */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Attribute model (Phase 8.6)                                         */
/* ------------------------------------------------------------------ */

/**
 * One attribute: name plus value, borrowed.
 *
 * This is the single attribute representation in the runtime. The tokenizer
 * stages the same type when it hands a start tag to the tree builder, and an
 * element stores it; only the lifetime differs, and the lifetime is stated
 * by whoever hands the value out.
 *
 * Borrowed-string contract: @c name and @c value point into the owning
 * document's payload arena and are valid until the next mutation or build on
 * that document, or until the document is destroyed. There are no attribute
 * handles and no attribute references to go stale — an attribute is a
 * value-in/out-out snapshot, so a captured one simply stops being meaningful
 * when the arena moves, exactly as a node view does. Callers that must keep
 * an attribute copy the bytes.
 */
typedef struct robo_html_attribute {
    /** Borrowed name; lowercase ASCII, NUL-terminated. */
    const char *name;
    /** Bytes in @c name, excluding the terminator. */
    size_t name_length;
    /**
     * Borrowed value; "" (with length 0) for a boolean-style attribute such
     * as `<input disabled>`. Character references are already decoded, and no
     * interpretation is applied — not CSS, not URL resolution, not script.
     */
    const char *value;
    /** Bytes in @c value, excluding the terminator. */
    size_t value_length;
    /**
     * Namespace of the attribute *name*. Always NONE in this phase: HTML
     * attributes have no namespace, and the field exists because an
     * attribute's identity is (namespace, local name) the same way an
     * element's is — so the foreign-content phase fills this in rather than
     * changing the type.
     */
    robo_html_namespace_t namespace_id;
} robo_html_attribute_t;

/** Set @p attribute to the absent/empty state ("" name and value, NONE). */
void robo_html_attribute_init(robo_html_attribute_t *attribute);

/**
 * One element's identity, shape and state.
 *
 * @c name is borrowed and lowercase (ASCII lowercasing is applied once,
 * when the element is created). @c classification is derived from
 * (namespace, name) at read time. @c state is a mask of
 * ROBO_HTML_ELEMENT_STATE_*. No pointer, id or field here is owned by the
 * caller, and nothing in the struct keeps the element alive.
 */
typedef struct robo_html_element_view {
    const char *name;
    size_t name_length;
    size_t attribute_count;
    /** Non-zero when the source wrote "/>" for this element. */
    int self_closing;
    /** The element's namespace (always HTML in this phase). */
    robo_html_namespace_t namespace_id;
    /** Derived role; GENERIC for anything unrecognised. */
    robo_html_element_class_t classification;
    /** Mask of ROBO_HTML_ELEMENT_STATE_*. */
    unsigned int state;
} robo_html_element_view_t;

/* ------------------------------------------------------------------ */
/* Element creation (the construction seam)                            */
/* ------------------------------------------------------------------ */

/**
 * Everything needed to create one element.
 *
 * A descriptor rather than nine positional arguments, because this is the
 * seam Phase 8.6's attribute model and the DOM's createElement both extend.
 * @c namespace_id and @c local_name are required; @c local_name is
 * lowercased on the way in.
 */
typedef struct robo_html_element_create_info {
    /** Parent the element is appended under as its last child. */
    robo_html_node_handle_t parent;
    /** Creation site (see RoboHTMLDom.h). */
    robo_html_node_origin_t origin;
    /** Platform namespace; NONE and unknown values are rejected. */
    robo_html_namespace_t namespace_id;
    /** Tag name; must be non-NULL and non-empty. */
    const char *local_name;
    /** Attributes in source order; NULL is allowed when count is 0. */
    const robo_html_attribute_t *attributes;
    size_t attribute_count;
    /** Non-zero to record "/>" as the element's self-closing flag. */
    int self_closing;
} robo_html_element_create_info_t;

/** Zero @p info to an empty descriptor (namespace NONE, no name). */
void robo_html_element_create_info_init(robo_html_element_create_info_t *info);

/**
 * Create an element in @p document_id and link it as the last child of
 * info->parent.
 *
 * This is the one place an element comes into existence, so the ownership
 * and lifecycle rules are enforced here rather than at each call site: the
 * document must exist and accept construction (not finalized, lifecycle not
 * FAILED/DESTROYED), the parent must be a live node of the *same* document,
 * and the name must be usable. Element identity is allocated by the
 * document, so a handle is valid only while the element lives in it.
 *
 * The element is created already attached: there is no detached state in
 * this phase, which is what keeps "every element has exactly one document"
 * true by construction rather than by convention.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL out, NULL/empty name,
 *         unknown namespace or origin, ill-formed parent handle);
 *         ROBO_NOT_FOUND (unknown document or parent); ROBO_ERROR
 *         (lifecycle/finalization refuses construction); ROBO_OUT_OF_MEMORY
 *         (node registry or payload arena exhausted).
 */
robo_result_t robo_html_element_create(
    robo_html_document_id_t document_id,
    const robo_html_element_create_info_t *info,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Element access (read side)                                          */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p h resolves to a live element in its document.
 * False for NULL, ill-formed, stale and non-element handles alike, so a
 * caller may test a handle before treating it as an element.
 */
int robo_html_element_is_element(const robo_html_node_handle_t *h);

/**
 * Read an element's name/namespace/classification/state/attribute summary.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL args / ill-formed handle);
 *         ROBO_NOT_FOUND (unknown document, unknown/stale id, or the node
 *         is not an element — use robo_html_element_is_element to tell the
 *         difference between a missing node and a non-element one).
 */
robo_result_t robo_html_element_view_get(
    const robo_html_node_handle_t *element,
    robo_html_element_view_t *out,
    robo_html_diag_t *diag);

/**
 * Borrowed lowercase tag name; "" when @p element is not a live element.
 * @p out_length is optional. Cheaper than a full view when only the name is
 * needed, and the pointer has the arena's lifetime, not the call's.
 */
const char *robo_html_element_name(const robo_html_node_handle_t *element,
                                   size_t *out_length);

/** Namespace of @p element; NONE when it is not a live element. */
robo_html_namespace_t robo_html_element_namespace(
    const robo_html_node_handle_t *element);

/** Classification of @p element; GENERIC when it is not a live element. */
robo_html_element_class_t robo_html_element_classification(
    const robo_html_node_handle_t *element);

/**
 * Non-zero when @p element is a live element whose tag name is @p name.
 * ASCII case-insensitive, so callers may pass "DIV" or "div" — the element
 * model owns the one comparison rule so every caller agrees.
 */
int robo_html_element_is_named(const robo_html_node_handle_t *element,
                               const char *name);

/**
 * Parent handle of @p element. The document element's parent is the document
 * node, so this returns that node rather than failing — the tree has no
 * childless-root special case. ROBO_NOT_FOUND when there is no parent.
 */
robo_result_t robo_html_element_parent(const robo_html_node_handle_t *element,
                                       robo_html_node_handle_t *out,
                                       robo_html_diag_t *diag);

/**
 * Read attribute @p index in stored order (parsed duplicates preserved
 * exactly as the tokenizer emitted them — no last-wins folding in this
 * phase). Out of range → ROBO_NOT_FOUND, @p out is zeroed.
 *
 * @p out borrows the payload arena; copy the bytes to keep them past a
 * mutation or a rebuild on that document.
 */
robo_result_t robo_html_element_attribute_at(
    const robo_html_node_handle_t *element,
    size_t index,
    robo_html_attribute_t *out,
    robo_html_diag_t *diag);

/**
 * First attribute named @p name (ASCII case-insensitive, since stored names
 * are lowercase); ROBO_NOT_FOUND when absent. When a parsed document carries
 * duplicate names this is the first one — the later ones stay reachable by
 * index, because folding them here would hide what the source said.
 */
robo_result_t robo_html_element_find_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    robo_html_attribute_t *out,
    robo_html_diag_t *diag);

/**
 * Attribute count of @p element; ROBO_NOT_FOUND when it is not a live
 * element. Equivalent to @c attribute_count in the view, for callers that
 * want the count without the rest of the snapshot.
 */
robo_result_t robo_html_element_attribute_count(
    const robo_html_node_handle_t *element,
    size_t *out_count,
    robo_html_diag_t *diag);

/**
 * Non-zero when @p element carries an attribute named @p name (ASCII
 * case-insensitive). @p name must be non-NULL. This is the presence test
 * (e.g. `<input disabled>`), separate from the value lookup, because "no
 * such attribute" and "present but empty" are different answers.
 */
int robo_html_element_has_attribute(const robo_html_node_handle_t *element,
                                    const char *name);

/**
 * Value of attribute @p name: borrowed bytes, or NULL when the attribute is
 * absent. @p out_length (optional) receives the byte count.
 *
 * NULL-versus-"" is the whole point of this shape: NULL means "no such
 * attribute", "" with a length of 0 means "present with an empty value" —
 * the distinction `<input>` / `<input disabled>` / `<input disabled="">`
 * depends on. A convenience over robo_html_element_find_attribute for the
 * common "give me the string" case.
 */
const char *robo_html_element_attribute_value(
    const robo_html_node_handle_t *element,
    const char *name,
    size_t *out_length);

/* ------------------------------------------------------------------ */
/* Attribute mutation (DOM seam)                                       */
/* ------------------------------------------------------------------ */

/*
 * These are the model's write operations, and they are deliberately the
 * four DOM-shaped verbs rather than anything parser-shaped. Names are
 * validated and ASCII-lowercased on the way in (the same policy the element
 * name uses), so storage and lookup agree; the parser path does not go
 * through here and keeps its "tolerate what the source said" behaviour.
 *
 * Mutating rewrites the element's payload, which invalidates any borrowed
 * attribute bytes taken before the call — the arena moved. Node handles,
 * children and identity are untouched.
 */

/**
 * Set @p name to @p value: every existing record with that name collapses to
 * one, at the first match's position; appended when absent. This is DOM
 * setAttribute.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL args, empty or unusable
 *         name); ROBO_NOT_FOUND (not a live element); ROBO_NOT_SUPPORTED
 *         (attribute limit); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_element_set_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    const char *value,
    robo_html_diag_t *diag);

/**
 * Append @p name = @p value unconditionally, without collapsing a record
 * that is already there. The parser's behaviour, exposed so the parser and
 * the model share one encoder; an application adding attributes should use
 * robo_html_element_set_attribute.
 */
robo_result_t robo_html_element_add_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    const char *value,
    robo_html_diag_t *diag);

/**
 * Remove every record named @p name. Absent is a successful no-op (DOM
 * removeAttribute does not fail), so this cannot be used as a presence
 * test — robo_html_element_has_attribute is that.
 */
robo_result_t robo_html_element_remove_attribute(
    const robo_html_node_handle_t *element,
    const char *name,
    robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ELEMENT_H */
