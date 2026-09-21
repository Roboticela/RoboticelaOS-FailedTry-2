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
 *   - HtmlElementCreate is the construction seam: the tree builder
 *     uses it now, the future DOM will use it for createElement. An element
 *     is always created *inside* a document, directly linked under a parent,
 *     so "an element with no document" cannot be expressed. There is no
 *     detached-element lifetime in this phase.
 *   - Destruction is the document's business alone (document destroy, or a
 *     tree clear). An element handed out and then retired resolves as stale.
 *
 * Naming policy (Roboticela)
 *   Element and attribute names are stored as lowercase ASCII. The
 *   tokenizer already lowercases parser input; HtmlElementCreate
 *   lowercases what it is given, so the invariant holds for the DOM seam
 *   too. HtmlElementNameNormalize is the strict policy helper for
 *   *application*-provided names (it validates as it lowercases); the parser
 *   path deliberately does not validate, because tolerating a malformed tag
 *   name is required behaviour, not an error.
 *
 * Attributes (Phase 8.6)
 *   The element's attributes are the real attribute model, not a temporary
 *   table: one `HtmlAttribute` per attribute, in a deterministic
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
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum copied element/attribute name length (excluding NUL). */
#define RoboHtmlNameMax 64u

/** Maximum copied attribute value length (excluding NUL) in the stub. */
#define RoboHtmlAttrValueMax 256u

/**
 * Future element registry reservation. Phase 8.1 defines only the shape
 * the element subsystem will register against; it is intentionally opaque.
 */
typedef struct HtmlElementBoundary {
    /** Reserved; must be 0. Guards against accidental use before the DOM phase. */
    uint32_t reserved;
} HtmlElementBoundary;

/** Reset the boundary reservation. Safe on NULL. */
void HtmlElementBoundaryInit(HtmlElementBoundary *b);

/**
 * Validate an element/attribute name at the boundary:
 *   - non-empty, ASCII letters/digits/'-'/'_' only,
 *   - length <= RoboHtmlNameMax.
 * This is a policy check for later phases, exposed so the contract is
 * testable now. No storage, no registry.
 * @return RoboOk; RoboInvalidArgument (NULL/empty); RoboParseError
 *         (bad character); RoboNotSupported (too long).
 */
Result HtmlElementNameValidate(const char *name,
                                              HtmlDiag *diag);

/**
 * ASCII-lowercase copy of @p name into @p out (capacity
 * RoboHtmlNameMax). Future tokenizer/tree-builder naming policy.
 * @return RoboOk; RoboInvalidArgument / RoboParseError per validate;
 *         RoboNotSupported when the copy would overflow.
 */
Result HtmlElementNameNormalize(const char *name,
                                               char *out,
                                               HtmlDiag *diag);

/*
 * Element roles are declared in RoboHTMLTypes.h alongside the node kinds and
 * the namespaces, and defined by the Phase 8.11 tag vocabulary. Read the role
 * question there; read the identity question — which tag is this, is it a
 * heading, is it void — in RoboHTMLTag.h.
 */

/* ------------------------------------------------------------------ */
/* Element state                                                       */
/* ------------------------------------------------------------------ */

/** Element state bits (see @c state in HtmlElementView). */
#define RoboHtmlElementStateNone 0u
/** The node was created by parsing (origin PARSER). */
#define RoboHtmlElementStateParserCreated 1u
/** The element is reachable from the document root. */
#define RoboHtmlElementStateConnected 2u
/** The source wrote "/>" for this element. */
#define RoboHtmlElementStateSelfClosing 4u

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
typedef struct HtmlAttribute {
    /** Borrowed name; lowercase ASCII, NUL-terminated. */
    const char *name;
    /** Bytes in @c name, excluding the terminator. */
    size_t nameLength;
    /**
     * Borrowed value; "" (with length 0) for a boolean-style attribute such
     * as `<input disabled>`. Character references are already decoded, and no
     * interpretation is applied — not CSS, not URL resolution, not script.
     */
    const char *value;
    /** Bytes in @c value, excluding the terminator. */
    size_t valueLength;
    /**
     * Namespace of the attribute *name*. Always NONE in this phase: HTML
     * attributes have no namespace, and the field exists because an
     * attribute's identity is (namespace, local name) the same way an
     * element's is — so the foreign-content phase fills this in rather than
     * changing the type.
     */
    HtmlNamespace namespaceId;
} HtmlAttribute;

/** Set @p attribute to the absent/empty state ("" name and value, NONE). */
void HtmlAttributeInit(HtmlAttribute *attribute);

/**
 * One element's identity, shape and state.
 *
 * @c name is borrowed and lowercase (ASCII lowercasing is applied once,
 * when the element is created). @c classification is derived from
 * (namespace, name) at read time. @c state is a mask of
 * ROBO_HTML_ELEMENT_STATE_*. No pointer, id or field here is owned by the
 * caller, and nothing in the struct keeps the element alive.
 */
typedef struct HtmlElementView {
    const char *name;
    size_t nameLength;
    size_t attributeCount;
    /** Non-zero when the source wrote "/>" for this element. */
    int selfClosing;
    /** The element's namespace (always HTML in this phase). */
    HtmlNamespace namespaceId;
    /**
     * The element's type (Phase 8.11); UNKNOWN for a name this runtime does
     * not recognise. Derived from (namespace, name) exactly as
     * @c classification is, so reading a view costs no more than it did.
     */
    HtmlTag tag;
    /** Derived role; GENERIC for anything unrecognised. */
    HtmlElementClass classification;
    /** Mask of ROBO_HTML_ELEMENT_STATE_*. */
    unsigned int state;
} HtmlElementView;

/* ------------------------------------------------------------------ */
/* Element creation (the construction seam)                            */
/* ------------------------------------------------------------------ */

/**
 * Everything needed to create one element.
 *
 * A descriptor rather than nine positional arguments, because this is the
 * seam Phase 8.6's attribute model and the DOM's createElement both extend.
 * @c namespaceId and @c localName are required; @c localName is
 * lowercased on the way in.
 */
typedef struct HtmlElementCreateInfo {
    /** Parent the element is appended under as its last child. */
    HtmlNodeHandle parent;
    /** Creation site (see RoboHTMLDom.h). */
    HtmlNodeOrigin origin;
    /** Platform namespace; NONE and unknown values are rejected. */
    HtmlNamespace namespaceId;
    /** Tag name; must be non-NULL and non-empty. */
    const char *localName;
    /** Attributes in source order; NULL is allowed when count is 0. */
    const HtmlAttribute *attributes;
    size_t attributeCount;
    /** Non-zero to record "/>" as the element's self-closing flag. */
    int selfClosing;
} HtmlElementCreateInfo;

/** Zero @p info to an empty descriptor (namespace NONE, no name). */
void HtmlElementCreateInfoInit(HtmlElementCreateInfo *info);

/**
 * Create an element in @p documentId and link it as the last child of
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
 * @return RoboOk; RoboInvalidArgument (NULL out, NULL/empty name,
 *         unknown namespace or origin, ill-formed parent handle);
 *         RoboNotFound (unknown document or parent); RoboError
 *         (lifecycle/finalization refuses construction); RoboOutOfMemory
 *         (node registry or payload arena exhausted).
 */
Result HtmlElementCreate(
    HtmlDocumentId documentId,
    const HtmlElementCreateInfo *info,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Element access (read side)                                          */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p h resolves to a live element in its document.
 * False for NULL, ill-formed, stale and non-element handles alike, so a
 * caller may test a handle before treating it as an element.
 */
int HtmlElementIsElement(const HtmlNodeHandle *h);

/**
 * Read an element's name/namespace/classification/state/attribute summary.
 * @return RoboOk; RoboInvalidArgument (NULL args / ill-formed handle);
 *         RoboNotFound (unknown document, unknown/stale id, or the node
 *         is not an element — use HtmlElementIsElement to tell the
 *         difference between a missing node and a non-element one).
 */
Result HtmlElementViewGet(
    const HtmlNodeHandle *element,
    HtmlElementView *out,
    HtmlDiag *diag);

/**
 * Borrowed lowercase tag name; "" when @p element is not a live element.
 * @p outLength is optional. Cheaper than a full view when only the name is
 * needed, and the pointer has the arena's lifetime, not the call's.
 */
const char *HtmlElementName(const HtmlNodeHandle *element,
                                   size_t *outLength);

/** Namespace of @p element; NONE when it is not a live element. */
HtmlNamespace HtmlElementNamespace(
    const HtmlNodeHandle *element);

/** Classification of @p element; GENERIC when it is not a live element. */
HtmlElementClass HtmlElementClassification(
    const HtmlNodeHandle *element);

/**
 * Non-zero when @p element is a live element whose tag name is @p name.
 * ASCII case-insensitive, so callers may pass "DIV" or "div" — the element
 * model owns the one comparison rule so every caller agrees.
 *
 * This compares *names*, not types: it answers "does this element's name
 * read as `name`?" and so it matches a custom element too. Callers asking
 * "is this a paragraph?" want the symbolic form
 * @c HtmlElementIsTag below, which cannot be satisfied by a name
 * that this runtime does not recognise.
 */
int HtmlElementIsNamed(const HtmlNodeHandle *element,
                               const char *name);

/**
 * Element type (Phase 8.11): which tag is @p element?
 *
 * @c RoboHtmlTagUnknown when @p element is not a live element, and also
 * for a live element whose name this runtime does not recognise — a custom
 * element, an SVG element, or an element from a future version of HTML. In
 * that case the element still works: it has a name, attributes, children and
 * the full DOM surface, it simply has no recognised type.
 */
HtmlTag HtmlElementTag(const HtmlNodeHandle *element);

/**
 * Non-zero when @p element is a live element whose tag is @p tag.
 *
 * This is the question "is this a div?" without a string compare, and it is
 * the reason the vocabulary lives in one place: the caller names a symbol
 * and the runtime owns the spelling.
 *
 * UNKNOWN is answerable and means "this element has no recognised identity" —
 * so a custom element satisfies `is_tag(element, UNKNOWN)`, and no two custom
 * elements are thereby claimed to be the same element. UNKNOWN is the absence
 * of identity, never a shared identity. A non-element handle (a text node, a
 * stale id, NULL) satisfies no tag at all, including UNKNOWN, because "no
 * recognised identity" is a statement about an element.
 */
int HtmlElementIsTag(const HtmlNodeHandle *element,
                             HtmlTag tag);

/**
 * Parent handle of @p element. The document element's parent is the document
 * node, so this returns that node rather than failing — the tree has no
 * childless-root special case. RoboNotFound when there is no parent.
 */
Result HtmlElementParent(const HtmlNodeHandle *element,
                                       HtmlNodeHandle *out,
                                       HtmlDiag *diag);

/**
 * Read attribute @p index in stored order (parsed duplicates preserved
 * exactly as the tokenizer emitted them — no last-wins folding in this
 * phase). Out of range → RoboNotFound, @p out is zeroed.
 *
 * @p out borrows the payload arena; copy the bytes to keep them past a
 * mutation or a rebuild on that document.
 */
Result HtmlElementAttributeAt(
    const HtmlNodeHandle *element,
    size_t index,
    HtmlAttribute *out,
    HtmlDiag *diag);

/**
 * First attribute named @p name (ASCII case-insensitive, since stored names
 * are lowercase); RoboNotFound when absent. When a parsed document carries
 * duplicate names this is the first one — the later ones stay reachable by
 * index, because folding them here would hide what the source said.
 */
Result HtmlElementFindAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    HtmlAttribute *out,
    HtmlDiag *diag);

/**
 * Attribute count of @p element; RoboNotFound when it is not a live
 * element. Equivalent to @c attributeCount in the view, for callers that
 * want the count without the rest of the snapshot.
 */
Result HtmlElementAttributeCount(
    const HtmlNodeHandle *element,
    size_t *outCount,
    HtmlDiag *diag);

/**
 * Non-zero when @p element carries an attribute named @p name (ASCII
 * case-insensitive). @p name must be non-NULL. This is the presence test
 * (e.g. `<input disabled>`), separate from the value lookup, because "no
 * such attribute" and "present but empty" are different answers.
 */
int HtmlElementHasAttribute(const HtmlNodeHandle *element,
                                    const char *name);

/**
 * Value of attribute @p name: borrowed bytes, or NULL when the attribute is
 * absent. @p outLength (optional) receives the byte count.
 *
 * NULL-versus-"" is the whole point of this shape: NULL means "no such
 * attribute", "" with a length of 0 means "present with an empty value" —
 * the distinction `<input>` / `<input disabled>` / `<input disabled="">`
 * depends on. A convenience over HtmlElementFindAttribute for the
 * common "give me the string" case.
 */
const char *HtmlElementAttributeValue(
    const HtmlNodeHandle *element,
    const char *name,
    size_t *outLength);

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
 * @return RoboOk; RoboInvalidArgument (NULL args, empty or unusable
 *         name); RoboNotFound (not a live element); RoboNotSupported
 *         (attribute limit); RoboOutOfMemory.
 */
Result HtmlElementSetAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    const char *value,
    HtmlDiag *diag);

/**
 * Append @p name = @p value unconditionally, without collapsing a record
 * that is already there. The parser's behaviour, exposed so the parser and
 * the model share one encoder; an application adding attributes should use
 * HtmlElementSetAttribute.
 */
Result HtmlElementAddAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    const char *value,
    HtmlDiag *diag);

/**
 * Remove every record named @p name. Absent is a successful no-op (DOM
 * removeAttribute does not fail), so this cannot be used as a presence
 * test — HtmlElementHasAttribute is that.
 */
Result HtmlElementRemoveAttribute(
    const HtmlNodeHandle *element,
    const char *name,
    HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_ELEMENT_H */
