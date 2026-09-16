/*
 * RoboHTML text-node model (Phase 8.7).
 *
 * Text IS a node. Its identity is the node handle from RoboHTMLNode.h, its
 * owner is the document, and its position — parent, siblings, child order —
 * is the tree link set already in place. This header adds no second node
 * hierarchy and no second traversal: it adds the text *view* over text-kind
 * nodes, exactly as RoboHTMLElement.h adds the element view over
 * element-kind nodes.
 *
 *   Document (8.4)
 *     └── Node (8.1/8.3)
 *           ├── Element (8.5)  ← name, namespace, classification, attributes
 *           └── Text    (8.7)  ← character data, length, mutation
 *
 * Character data
 *   - A text node's content is the real parsed character data. It lives in
 *     the owning document's payload arena as one byte run, never as a
 *     separately owned allocation and never as a string hanging off its
 *     parent element. <div>Hello <strong>Roboticela</strong> Runtime</div>
 *     is three children of the div — Text, Element, Text — not a div that
 *     remembers some text.
 *   - Bytes are preserved exactly as the tokenizer produced them, after
 *     character-reference decoding. Nothing here normalizes, trims,
 *     collapses, resamples, case-folds or re-encodes: <div>a  \n\tb</div>
 *     keeps its two spaces, newline and tab, and <p>مرحبا 😀</p> keeps its
 *     UTF-8 bytes. Whitespace collapsing, shaping and line breaking are
 *     later phases operating on this data, not something this phase may do
 *     to it — collapsing here would destroy information no later layer
 *     could recover.
 *
 * Identity and lifetime
 *   - robo_html_text_create is the construction seam, the text-side twin of
 *     robo_html_element_create. Text is always created *inside* a document,
 *     already linked under a parent, so "a text node with no document"
 *     cannot be expressed.
 *   - Destruction is the document's business alone (document destroy or a
 *     tree clear). A handle handed out and then retired resolves as stale,
 *     and every accessor here reports that as "not text" rather than
 *     dereferencing anything.
 *   - Handles are inert tokens carrying no ownership, so no text-node
 *     reference can outlive the document that issued it.
 *
 * Borrowed-data contract
 *   - Content pointers point INTO the payload arena and stay valid until the
 *     next mutation on that document or until the document is destroyed —
 *     the same contract node views and attribute values already have.
 *     Callers that must keep the bytes copy them.
 *
 * Empty text (the policy, stated once)
 *   - The model PERMITS an empty text node: a zero-length run is a legal
 *     value, and the future DOM's createTextNode("") must be expressible
 *     through the same seam as any other creation.
 *   - The PARSER never produces one, because a zero-length character run
 *     carries no information: the tree builder returns early on a zero-length
 *     run instead of registering a node. That is not a loss — there is no
 *     character there to lose — and it keeps a whitespace-heavy document
 *     from accumulating empty nodes.
 *   - Neither policy is inferred from the value at read time: a node either
 *     exists (and its length may be 0) or it does not. robo_html_text_is_empty
 *     answers the first question, and "is this a text node at all" is
 *     robo_html_text_is_text.
 *
 * Mutation
 *   - set replaces the whole run; append extends it. Both are DOM-shaped
 *     verbs rather than parser-shaped ones: they exist so the DOM phase and
 *     later editing have one place to go, not because the parser needs them.
 *     A mutation invalidates borrowed content pointers taken earlier — the
 *     arena moved — while node identity, parent, siblings and child order are
 *     untouched.
 *   - append always appends. When the run already ends the arena it extends
 *     in place with no copy and no allocation; otherwise it copies the run
 *     plus the addition into a fresh payload. The two are the same operation
 *     with the same result, which is what lets the tree builder's character
 *     merge and a future DOM appendText share one implementation.
 *
 * What this phase is NOT: the CharacterData API (substringData / insertData /
 * deleteData / replaceData), splitText, normalize, the Range and Selection
 * APIs, contenteditable, mutation observers, JavaScript bindings, CDATA and
 * foreign-content text, whitespace processing, text shaping, bidi, line
 * breaking, font selection and layout measurement. Text nodes carry bytes;
 * every one of those consumers reads those bytes later.
 */

#ifndef ROBO_HTML_TEXT_H
#define ROBO_HTML_TEXT_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Text state                                                          */
/* ------------------------------------------------------------------ */

/** Text state bits (see @c state in robo_html_text_view_t). */
#define ROBO_HTML_TEXT_STATE_NONE 0u
/** The node was created by parsing (origin PARSER). */
#define ROBO_HTML_TEXT_STATE_PARSER_CREATED 1u
/** The node is reachable from the document root. */
#define ROBO_HTML_TEXT_STATE_CONNECTED 2u

/* ------------------------------------------------------------------ */
/* Text view                                                           */
/* ------------------------------------------------------------------ */

/**
 * Snapshot of one text node: identity, parent, content and state.
 *
 * @c content is borrowed and byte-exact, with @c length excluding the NUL
 * terminator. Nothing here is owned by the caller and nothing keeps the node
 * alive.
 *
 * Siblings and children are deliberately NOT duplicated here. A text node is
 * a leaf, and where it sits among its siblings is a question the tree
 * answering machinery in RoboHTMLNode.h already answers for every node kind
 * (robo_html_node_get_view / child_at / next_sibling). Repeating the links
 * here would create a second traversal surface to keep in step.
 */
typedef struct robo_html_text_view {
    robo_html_node_handle_t handle;
    /** Invalid when detached (only possible after a future DOM removal). */
    robo_html_node_handle_t parent;
    /** Borrowed character data; never NULL; "" when the run is empty. */
    const char *content;
    /** Bytes in @c content, excluding the terminator. */
    size_t length;
    /** Mask of ROBO_HTML_TEXT_STATE_*. */
    unsigned int state;
} robo_html_text_view_t;

/* ------------------------------------------------------------------ */
/* Text creation (the construction seam)                               */
/* ------------------------------------------------------------------ */

/**
 * Everything needed to create one text node.
 *
 * @c content may be NULL only when @c length is 0, which is how an empty
 * text node is requested.
 */
typedef struct robo_html_text_create_info {
    /** Parent the text node is appended under as its last child. */
    robo_html_node_handle_t parent;
    /** Creation site (see RoboHTMLDom.h). */
    robo_html_node_origin_t origin;
    /** Character data to copy; NULL allowed when @c length is 0. */
    const char *content;
    /** Bytes in @c content. */
    size_t length;
} robo_html_text_create_info_t;

/** Zero @p info to an empty descriptor (no parent, no content). */
void robo_html_text_create_info_init(robo_html_text_create_info_t *info);

/**
 * Create a text node in @p document_id and link it as the last child of
 * info->parent.
 *
 * The one place a text node comes into existence, so the ownership rules are
 * enforced here rather than at each call site — the same shape as
 * robo_html_element_create: the document must exist and accept construction,
 * the parent must be a live node of the *same* document (a handle claiming
 * another document is rejected before anything is resolved, so a
 * cross-document mistake cannot silently alias a node), and the origin must
 * be a defined one.
 *
 * The content bytes are copied into the document's payload arena, so the
 * caller keeps its buffer. Content is never interpreted: no trimming, no
 * entity decoding (that already happened in the tokenizer), no validation.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL out/info, NULL content with
 *         non-zero length, unknown origin, ill-formed parent handle);
 *         ROBO_NOT_FOUND (unknown document or parent); ROBO_ERROR
 *         (lifecycle/finalization refuses construction); ROBO_OUT_OF_MEMORY
 *         (node registry or payload arena exhausted); ROBO_NOT_SUPPORTED
 *         (run exceeds one payload).
 */
robo_result_t robo_html_text_create(
    robo_html_document_id_t document_id,
    const robo_html_text_create_info_t *info,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Text access (read side)                                             */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p h resolves to a live text node in its document.
 * False for NULL, ill-formed, stale, and non-text handles alike, so a caller
 * may test a handle before treating it as text — and this is the kind test
 * the "is this Document, Element or Text?" question resolves through.
 */
int robo_html_text_is_text(const robo_html_node_handle_t *h);

/**
 * Read a text node's parent, content, length and state.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL args); ROBO_NOT_FOUND
 *         (unknown document, unknown/stale id, a cleared handle, or a live
 *         node that is not text — use robo_html_text_is_text to tell a
 *         missing node from a non-text one). Nothing here treats a stale
 *         handle as an argument error: the handle is well-formed, it simply
 *         names nothing live any more.
 */
robo_result_t robo_html_text_view_get(const robo_html_node_handle_t *text,
                                      robo_html_text_view_t *out,
                                      robo_html_diag_t *diag);

/**
 * Borrowed character data of @p text, or NULL when @p text is not a live
 * text node. @p out_length (optional) receives the byte count.
 *
 * NULL-versus-"" is the distinction that matters: NULL means "not a text
 * node" and "" with length 0 means "a text node whose run is empty". It is
 * the same shape robo_html_element_attribute_value uses for absent versus
 * present-and-empty.
 */
const char *robo_html_text_content(const robo_html_node_handle_t *text,
                                   size_t *out_length);

/**
 * Byte length of @p text's content; 0 when it is not a live text node.
 * The value alone cannot separate "empty run" from "not text", so pair it
 * with robo_html_text_is_text when that matters.
 */
size_t robo_html_text_length(const robo_html_node_handle_t *text);

/**
 * Non-zero when @p text is a live text node whose content is empty; 0 for a
 * non-empty run and for anything that is not a live text node.
 */
int robo_html_text_is_empty(const robo_html_node_handle_t *text);

/**
 * Parent of @p text. ROBO_NOT_FOUND when the node has no parent (or is not a
 * live text node); @p out is cleared in that case.
 */
robo_result_t robo_html_text_parent(const robo_html_node_handle_t *text,
                                    robo_html_node_handle_t *out,
                                    robo_html_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Text mutation                                                       */
/* ------------------------------------------------------------------ */

/**
 * Replace @p text's whole run with @p bytes (which may be NULL when
 * @p length is 0, making the node empty). The DOM's textContent-style write.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL handle or NULL bytes with a
 *         non-zero length); ROBO_NOT_FOUND (not a live text node);
 *         ROBO_OUT_OF_MEMORY; ROBO_NOT_SUPPORTED (run exceeds one payload).
 */
robo_result_t robo_html_text_set(const robo_html_node_handle_t *text,
                                 const char *bytes,
                                 size_t length,
                                 robo_html_diag_t *diag);

/**
 * Extend @p text's run with @p bytes. Appends in place when the run ends the
 * arena and copies into a fresh payload otherwise — same result either way.
 * A zero-length append succeeds and changes nothing.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT; ROBO_NOT_FOUND (not a live text
 *         node); ROBO_OUT_OF_MEMORY; ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_html_text_append(const robo_html_node_handle_t *text,
                                    const char *bytes,
                                    size_t length,
                                    robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TEXT_H */
