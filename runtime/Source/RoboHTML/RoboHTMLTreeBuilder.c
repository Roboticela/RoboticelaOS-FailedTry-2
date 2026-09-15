/*
 * RoboHTML tree builder (Phase 8.3) — see RoboHTMLTreeBuilder.h.
 *
 * File shape:
 *   1. element classification (void / text-family / structural names)
 *   2. builder state, open-element stack, node/tree primitives
 *   3. insertion-mode handlers (the actual tree-construction rules)
 *   4. node creation, text merging, structural recovery
 *   5. public API, one-shot build, and the tree dump
 *
 * The rules follow the HTML tree-construction model in shape — insertion
 * modes, an open-element stack, implied structural elements, and
 * error-tolerant recovery — while deliberately implementing only the
 * subset this phase needs. Absent (with the seam kept open, see the header
 * for the list): active-formatting/adoption-agency, tables and foster
 * parenting, foreign content, fragment parsing, and DOCTYPE quirks modes.
 *
 * The builder never mutates the document lifecycle behind the caller's
 * back beyond the ordinary LOADING → READY transition a parse implies, and
 * every node it creates is owned by the document (it holds ids, never
 * pointers into the registry).
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

#define TREE_BUILDER_MAGIC 0x52425442u /* "RBTB" */

/** Initial open-element stack capacity (grows geometrically). */
#define OPEN_STACK_INITIAL 16u

/** Attribute slots staged per start tag (beyond this, extras are dropped). */
#define MAX_STAGED_ATTRS ROBO_HTML_MAX_ATTRS_PER_ELEMENT

/** Cap on mode re-dispatch per token; a guard, not a policy. */
#define REPROCESS_LIMIT 24u

/* ------------------------------------------------------------------ */
/* 1. Element classification                                           */
/* ------------------------------------------------------------------ */

/** Elements that never have children or content (inserted then popped). */
static const char *const k_void_elements[] = {
    "area",  "base",  "basefont", "bgsound", "br",     "col",   "embed",
    "frame", "hr",    "img",      "input",   "keygen", "link",  "meta",
    "param", "source", "track",   "wbr",
};

/** Content family of a text element; NONE when the element is ordinary. */
static robo_html_text_mode_t text_mode_for_element(const char *name)
{
    if (!name) {
        return ROBO_HTML_TEXT_MODE_NONE;
    }
    if (strcmp(name, "title") == 0 || strcmp(name, "textarea") == 0) {
        return ROBO_HTML_TEXT_MODE_RCDATA;
    }
    if (strcmp(name, "style") == 0 || strcmp(name, "xmp") == 0 ||
        strcmp(name, "iframe") == 0 || strcmp(name, "noembed") == 0 ||
        strcmp(name, "noframes") == 0) {
        return ROBO_HTML_TEXT_MODE_RAWTEXT;
    }
    if (strcmp(name, "script") == 0) {
        return ROBO_HTML_TEXT_MODE_SCRIPT;
    }
    if (strcmp(name, "plaintext") == 0) {
        return ROBO_HTML_TEXT_MODE_PLAINTEXT;
    }
    return ROBO_HTML_TEXT_MODE_NONE;
}

/** Tokenizer entry state for a text family (DATA when not a text family). */
static robo_html_tokenizer_state_t tokenizer_state_for_text_mode(
    robo_html_text_mode_t mode)
{
    switch (mode) {
    case ROBO_HTML_TEXT_MODE_RCDATA:
        return ROBO_HTML_TOKENIZER_STATE_RCDATA;
    case ROBO_HTML_TEXT_MODE_RAWTEXT:
        return ROBO_HTML_TOKENIZER_STATE_RAWTEXT;
    case ROBO_HTML_TEXT_MODE_SCRIPT:
        return ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA;
    case ROBO_HTML_TEXT_MODE_PLAINTEXT:
        return ROBO_HTML_TOKENIZER_STATE_PLAINTEXT;
    case ROBO_HTML_TEXT_MODE_NONE:
    default:
        return ROBO_HTML_TOKENIZER_STATE_DATA;
    }
}

/** Content of <head> (or an element treated as head content) in body mode. */
static int is_head_content_element(const char *name)
{
    static const char *const k_head_elements[] = {
        "base", "basefont", "bgsound", "link", "meta", "title",
        "style", "script", "noscript", "template",
    };
    size_t i;

    if (!name) {
        return 0;
    }
    for (i = 0u; i < sizeof(k_head_elements) / sizeof(k_head_elements[0]); i++) {
        if (strcmp(name, k_head_elements[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/** Non-zero when @p name is one of the heading elements h1..h6. */
static int is_heading_name(const char *name)
{
    return name && name[0] == 'h' && name[1] >= '1' && name[1] <= '6' &&
           name[2] == '\0';
}

/** Non-zero for elements that are inserted and popped without children. */
static int is_void_element(const char *name)
{
    size_t i;

    if (!name) {
        return 0;
    }
    for (i = 0u; i < sizeof(k_void_elements) / sizeof(k_void_elements[0]); i++) {
        if (strcmp(name, k_void_elements[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/** Non-zero when @p name is a start tag that closes an open <p> in scope. */
static int is_p_closing_start_tag(const char *name)
{
    static const char *const k_p_closers[] = {
        "address", "article", "aside",  "blockquote", "details", "div",
        "dl",      "fieldset", "figcaption", "figure", "footer", "form",
        "h1",      "h2",    "h3",       "h4",         "h5",     "h6",
        "header",  "hgroup", "hr",      "main",       "menu",   "nav",
        "ol",      "p",     "pre",      "section",    "table",  "ul",
    };
    size_t i;

    if (!name) {
        return 0;
    }
    for (i = 0u; i < sizeof(k_p_closers) / sizeof(k_p_closers[0]); i++) {
        if (strcmp(name, k_p_closers[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 2. Builder state                                                    */
/* ------------------------------------------------------------------ */

struct robo_html_tree_builder {
    uint32_t magic;
    robo_html_document_id_t document_id;

    /** Exclusive 8.1 parser lease (the document records it; see Parser.h). */
    robo_html_parser_t *lease;
    robo_html_tokenizer_t *tokenizer;

    /** Copy of the attached source view (borrowed bytes; no heap). */
    robo_html_source_t source;
    int has_source;

    robo_html_insertion_mode_t mode;
    robo_html_insertion_mode_t text_return_mode;
    robo_html_text_mode_t text_mode;
    robo_html_parser_state_t state;

    /** Open-element stack (node ids, outermost first). */
    robo_html_node_id_t *open;
    size_t open_count;
    size_t open_capacity;

    /* Structural nodes (stable for the duration of one build). */
    robo_html_node_handle_t root;
    robo_html_node_handle_t html;
    robo_html_node_handle_t head;
    robo_html_node_handle_t body;

    /* Trailing text run, so adjacent character tokens merge into one node. */
    robo_html_node_id_t text_node;
    robo_html_node_id_t text_parent;

    int saw_doctype;
    int skip_next_newline;
    int complete;
    int stop;

    robo_html_tree_result_t result;
    robo_html_diag_list_t diagnostics;
    size_t dropped_diagnostics;
};

/* --- small predicates ---------------------------------------------- */

static int handle_is_live(const robo_html_node_handle_t *h)
{
    return robo_html_node_handle_is_wellformed(h) &&
           !robo_html_node_handle_is_stale(h);
}

static robo_html_document_slot_t *builder_slot(
    const robo_html_tree_builder_t *builder)
{
    if (!builder) {
        return NULL;
    }
    return robo_html_document_slot_find(builder->document_id);
}

/** Record a recoverable parse error (bounded list; never fatal). */
static void record_recovery(robo_html_tree_builder_t *builder,
                            robo_result_t code,
                            const char *message,
                            uint32_t line,
                            uint32_t column)
{
    if (!builder) {
        return;
    }
    if (builder->diagnostics.count >= ROBO_HTML_TREE_BUILDER_MAX_DIAGNOSTICS) {
        builder->dropped_diagnostics++;
        return;
    }
    robo_html_diagnostics_push(&builder->diagnostics, code,
                               ROBO_HTML_DOMAIN_PARSER, message, line, column);
    builder->result.recovered_errors++;
}

/** Record a recoverable parse error at @p tok's source position. */
static void record_token_recovery(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  const char *message)
{
    uint32_t line = tok ? tok->pos.line : 0u;
    uint32_t column = tok ? tok->pos.column : 0u;

    record_recovery(builder, ROBO_PARSE_ERROR, message, line, column);
}

/** Mark the build truncated: the node ceiling was reached. */
static void mark_truncated(robo_html_tree_builder_t *builder)
{
    if (!builder || builder->result.truncated) {
        return;
    }
    builder->result.truncated = 1;
    builder->stop = 1;
    robo_html_diagnostics_push(&builder->diagnostics, ROBO_OUT_OF_MEMORY,
                               ROBO_HTML_DOMAIN_PARSER,
                               "node ceiling reached; tree truncated", 0u, 0u);
    robo_log_warn("robo_html: doc=%llu tree truncated at the node ceiling",
                  (unsigned long long)builder->document_id);
}

/* --- open-element stack -------------------------------------------- */

static robo_result_t stack_reserve(robo_html_tree_builder_t *builder,
                                   size_t extra,
                                   robo_html_diag_t *diag)
{
    size_t need;
    size_t next;
    robo_html_node_id_t *grown;

    if (!builder) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (extra > SIZE_MAX - builder->open_count) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_OUT_OF_MEMORY,
                              "open-element stack size overflow");
    }
    need = builder->open_count + extra;
    if (need <= builder->open_capacity) {
        return ROBO_OK;
    }
    next = (builder->open_capacity == 0u) ? OPEN_STACK_INITIAL
                                          : builder->open_capacity;
    while (next < need) {
        if (next > (SIZE_MAX / 2u)) {
            next = need;
            break;
        }
        next *= 2u;
    }
    grown = (robo_html_node_id_t *)robo_realloc(builder->open,
                                                next * sizeof(*grown));
    if (!grown) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_OUT_OF_MEMORY,
                              "open-element stack allocation failed");
    }
    builder->open = grown;
    builder->open_capacity = next;
    return ROBO_OK;
}

static robo_result_t stack_push(robo_html_tree_builder_t *builder,
                                robo_html_node_id_t id,
                                robo_html_diag_t *diag)
{
    /*
     * No separate depth ceiling: the node registry already bounds how many
     * elements can exist, so this heap stack cannot grow without limit. The
     * only depth-sensitive consumer is the dump, which caps its recursion.
     */
    if (stack_reserve(builder, 1u, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_OUT_OF_MEMORY;
    }
    builder->open[builder->open_count++] = id;
    if (builder->open_count > builder->result.max_depth) {
        builder->result.max_depth = builder->open_count;
    }
    return ROBO_OK;
}

static void stack_pop(robo_html_tree_builder_t *builder)
{
    if (builder && builder->open_count > 0u) {
        builder->open_count--;
    }
}

static robo_html_node_id_t stack_top(const robo_html_tree_builder_t *builder)
{
    if (!builder || builder->open_count == 0u) {
        return ROBO_HTML_NODE_ID_NONE;
    }
    return builder->open[builder->open_count - 1u];
}

/** Index of the nearest open element named @p name; -1 when absent. */
static long stack_find(const robo_html_tree_builder_t *builder,
                       robo_html_document_slot_t *slot,
                       const char *name)
{
    size_t i;

    if (!builder || !slot || !name) {
        return -1;
    }
    for (i = builder->open_count; i > 0u; i--) {
        const robo_html_node_entry_t *e =
            robo_html_node_entry_get(builder->document_id, builder->open[i - 1u]);

        if (!e) {
            continue;
        }
        if (strcmp(robo_html_payload_element_name(slot, e, NULL), name) == 0) {
            return (long)(i - 1u);
        }
    }
    return -1;
}

/** Index of the nearest open element with kind/text family @p mode. */
static long stack_find_text_family(const robo_html_tree_builder_t *builder,
                                   robo_html_document_slot_t *slot)
{
    size_t i;

    if (!builder || !slot) {
        return -1;
    }
    for (i = builder->open_count; i > 0u; i--) {
        const robo_html_node_entry_t *e =
            robo_html_node_entry_get(builder->document_id, builder->open[i - 1u]);

        if (!e) {
            continue;
        }
        if (text_mode_for_element(
                robo_html_payload_element_name(slot, e, NULL)) !=
            ROBO_HTML_TEXT_MODE_NONE) {
            return (long)(i - 1u);
        }
    }
    return -1;
}

/** Popping @p node_id would leave the document (structural safety). */
static int is_structural(const robo_html_tree_builder_t *builder,
                         robo_html_node_id_t node_id)
{
    return node_id == builder->html.node_id || node_id == builder->head.node_id ||
           node_id == builder->body.node_id;
}

/* ------------------------------------------------------------------ */
/* 3. Node and text creation                                           */
/* ------------------------------------------------------------------ */

static robo_result_t register_node(robo_html_tree_builder_t *builder,
                                   robo_html_node_kind_t kind,
                                   robo_html_node_handle_t *out,
                                   robo_html_diag_t *diag)
{
    robo_result_t r = robo_html_document_register_node(
        builder->document_id, kind, ROBO_HTML_NODE_ORIGIN_PARSER, out, diag);

    if (r == ROBO_OUT_OF_MEMORY) {
        mark_truncated(builder);
    }
    return r;
}

/**
 * Create an element, record its payload, and link it under @p parent_id.
 * Pushing onto the open-element stack is the caller's decision (void and
 * self-closing elements are never pushed).
 */
static robo_result_t create_element(robo_html_tree_builder_t *builder,
                                    robo_html_node_id_t parent_id,
                                    const char *name,
                                    size_t name_length,
                                    const robo_html_attr_view_t *attrs,
                                    size_t attr_count,
                                    int self_closing,
                                    robo_html_node_handle_t *out_handle,
                                    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    robo_html_node_handle_t h;
    robo_html_node_entry_t *entry;
    robo_result_t r;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "document was destroyed during the build");
    }
    r = register_node(builder, ROBO_HTML_NODE_KIND_ELEMENT, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    entry = robo_html_node_entry_mut(builder->document_id, h.node_id);
    r = robo_html_payload_write_element(slot, entry, name, name_length, attrs,
                                        attr_count, self_closing, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_node_link_child(builder->document_id, parent_id, h.node_id,
                                  diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (out_handle) {
        *out_handle = h;
    }
    return ROBO_OK;
}

/** Create a text or comment node carrying @p bytes. */
static robo_result_t create_leaf(robo_html_tree_builder_t *builder,
                                 robo_html_node_kind_t kind,
                                 robo_html_node_id_t parent_id,
                                 const char *bytes,
                                 size_t length,
                                 robo_html_node_id_t *out_id,
                                 robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    robo_html_node_handle_t h;
    robo_html_node_entry_t *entry;
    robo_result_t r;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "document was destroyed during the build");
    }
    r = register_node(builder, kind, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    entry = robo_html_node_entry_mut(builder->document_id, h.node_id);
    r = robo_html_payload_write_text(slot, entry, bytes, length, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_node_link_child(builder->document_id, parent_id, h.node_id,
                                  diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (out_id) {
        *out_id = h.node_id;
    }
    return ROBO_OK;
}

/**
 * Insert characters under @p parent_id, merging into the previous text node
 * when that node's payload still ends the arena (the common case for
 * split-up character tokens).
 */
static robo_result_t insert_text(robo_html_tree_builder_t *builder,
                                 robo_html_node_id_t parent_id,
                                 const char *bytes,
                                 size_t length,
                                 robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    robo_html_node_id_t created = ROBO_HTML_NODE_ID_NONE;
    robo_result_t r;

    if (length == 0u || !bytes) {
        return ROBO_OK;
    }
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "document was destroyed during the build");
    }
    if (builder->text_parent == parent_id &&
        builder->text_node != ROBO_HTML_NODE_ID_NONE) {
        robo_html_node_entry_t *entry =
            robo_html_node_entry_mut(builder->document_id, builder->text_node);

        if (entry && entry->kind == ROBO_HTML_NODE_KIND_TEXT) {
            /* NULL diagnostics: "not the arena tail" is an expected miss. */
            if (robo_html_payload_append_text(slot, entry, bytes, length, NULL) ==
                ROBO_OK) {
                return ROBO_OK;
            }
        }
    }
    r = create_leaf(builder, ROBO_HTML_NODE_KIND_TEXT, parent_id, bytes, length,
                    &created, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->text_node = created;
    builder->text_parent = parent_id;
    return ROBO_OK;
}

/** Text insertion into the current node (top of the open-element stack). */
static robo_result_t insert_text_current(robo_html_tree_builder_t *builder,
                                         const char *bytes,
                                         size_t length,
                                         robo_html_diag_t *diag)
{
    robo_html_node_id_t current = stack_top(builder);

    if (current == ROBO_HTML_NODE_ID_NONE) {
        current = builder->root.node_id;
    }
    return insert_text(builder, current, bytes, length, diag);
}

/* --- structural elements ------------------------------------------- */

static robo_result_t stage_attrs(const robo_html_token_t *tok,
                                 robo_html_attr_view_t *out,
                                 size_t *out_count,
                                 robo_html_tree_builder_t *builder)
{
    size_t count = tok->u.tag.attr_count;
    size_t i;

    if (count > MAX_STAGED_ATTRS) {
        record_token_recovery(builder, tok,
                             "too many attributes; extras dropped");
        count = MAX_STAGED_ATTRS;
    }
    for (i = 0u; i < count; i++) {
        out[i].name = tok->u.tag.attrs[i].name ? tok->u.tag.attrs[i].name : "";
        out[i].value =
            tok->u.tag.attrs[i].value ? tok->u.tag.attrs[i].value : "";
    }
    *out_count = count;
    return ROBO_OK;
}

/**
 * Insert @p name under the current node, pushing it unless it is void or
 * self-closing. @p out_id receives the new node id.
 */
static robo_result_t insert_element(robo_html_tree_builder_t *builder,
                                    const char *name,
                                    const robo_html_attr_view_t *attrs,
                                    size_t attr_count,
                                    int self_closing,
                                    robo_html_node_id_t *out_id,
                                    robo_html_diag_t *diag)
{
    robo_html_node_handle_t h;
    robo_html_node_id_t parent = stack_top(builder);
    robo_result_t r;

    if (parent == ROBO_HTML_NODE_ID_NONE) {
        parent = builder->root.node_id;
    }
    r = create_element(builder, parent, name, strlen(name), attrs, attr_count,
                       self_closing, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (out_id) {
        *out_id = h.node_id;
    }
    /* A structural element never becomes the implicit text-merge target. */
    builder->text_node = ROBO_HTML_NODE_ID_NONE;
    builder->text_parent = ROBO_HTML_NODE_ID_NONE;
    if (self_closing || is_void_element(name)) {
        return ROBO_OK;
    }
    return stack_push(builder, h.node_id, diag);
}

/** Create <html> as the document's root element (implied or explicit). */
static robo_result_t ensure_html(robo_html_tree_builder_t *builder,
                                 const robo_html_attr_view_t *attrs,
                                 size_t attr_count,
                                 robo_html_diag_t *diag)
{
    robo_html_node_handle_t h;
    robo_result_t r;

    if (handle_is_live(&builder->html)) {
        return ROBO_OK;
    }
    r = create_element(builder, builder->root.node_id, "html", 4u, attrs,
                       attr_count, 0, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->html = h;
    return stack_push(builder, h.node_id, diag);
}

static robo_result_t ensure_head(robo_html_tree_builder_t *builder,
                                 robo_html_diag_t *diag)
{
    robo_html_node_handle_t h;
    robo_result_t r;
    robo_html_node_id_t parent;

    if (handle_is_live(&builder->head)) {
        return ROBO_OK;
    }
    r = ensure_html(builder, NULL, 0u, diag);
    if (r != ROBO_OK) {
        return r;
    }
    parent = builder->html.node_id;
    r = create_element(builder, parent, "head", 4u, NULL, 0u, 0, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->head = h;
    return stack_push(builder, h.node_id, diag);
}

static robo_result_t ensure_body(robo_html_tree_builder_t *builder,
                                 robo_html_diag_t *diag)
{
    robo_html_node_handle_t h;
    robo_result_t r;

    if (handle_is_live(&builder->body)) {
        return ROBO_OK;
    }
    r = ensure_html(builder, NULL, 0u, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = create_element(builder, builder->html.node_id, "body", 4u, NULL, 0u, 0,
                       &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->body = h;
    return stack_push(builder, h.node_id, diag);
}

/* ------------------------------------------------------------------ */
/* 4. Insertion-mode helpers                                           */
/* ------------------------------------------------------------------ */

/** Close an open <p> when @p name is a start tag that may not nest in one. */
static void close_p_if_open(robo_html_tree_builder_t *builder,
                            robo_html_document_slot_t *slot,
                            const char *name)
{
    long index;

    if (!is_p_closing_start_tag(name)) {
        return;
    }
    index = stack_find(builder, slot, "p");
    if (index < 0) {
        return;
    }
    while ((long)builder->open_count > index) {
        robo_html_node_id_t top = stack_top(builder);

        if (is_structural(builder, top)) {
            break;
        }
        stack_pop(builder);
    }
    if (builder->open_count > 0u) {
        stack_pop(builder); /* the <p> itself */
    }
    builder->text_node = ROBO_HTML_NODE_ID_NONE;
    builder->text_parent = ROBO_HTML_NODE_ID_NONE;
}

/** Close open elements up to (and including) the nearest @p name. */
static int close_to_element(robo_html_tree_builder_t *builder,
                            robo_html_document_slot_t *slot,
                            const char *name)
{
    long index = stack_find(builder, slot, name);

    if (index < 0) {
        return 0;
    }
    if (is_structural(builder, builder->open[index])) {
        return 0;
    }
    while ((long)(builder->open_count - 1u) >= index) {
        stack_pop(builder);
    }
    builder->text_node = ROBO_HTML_NODE_ID_NONE;
    builder->text_parent = ROBO_HTML_NODE_ID_NONE;
    return 1;
}

/** Switch to a text family: push nothing, remember the return mode. */
static robo_result_t enter_text_mode(robo_html_tree_builder_t *builder,
                                     robo_html_text_mode_t mode,
                                     robo_html_diag_t *diag)
{
    robo_result_t r;

    builder->text_return_mode = builder->mode;
    builder->text_mode = mode;
    builder->mode = ROBO_HTML_MODE_TEXT;
    /* PLAINTEXT is documented by the tokenizer as never ending. */
    r = robo_html_tokenizer_switch_state(builder->tokenizer,
                                         tokenizer_state_for_text_mode(mode),
                                         diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->state = ROBO_HTML_PARSER_STATE_FEEDING;
    return ROBO_OK;
}

static robo_result_t leave_text_mode(robo_html_tree_builder_t *builder,
                                     robo_html_diag_t *diag)
{
    robo_result_t r;

    builder->text_mode = ROBO_HTML_TEXT_MODE_NONE;
    builder->mode = builder->text_return_mode;
    r = robo_html_tokenizer_switch_state(builder->tokenizer,
                                         ROBO_HTML_TOKENIZER_STATE_DATA, diag);
    return r;
}

/* ------------------------------------------------------------------ */
/* 5. Insertion modes                                                  */
/* ------------------------------------------------------------------ */

static robo_result_t mode_initial(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag);
static robo_result_t mode_before_html(robo_html_tree_builder_t *builder,
                                      const robo_html_token_t *tok,
                                      int *reprocess,
                                      robo_html_diag_t *diag);
static robo_result_t mode_before_head(robo_html_tree_builder_t *builder,
                                      const robo_html_token_t *tok,
                                      int *reprocess,
                                      robo_html_diag_t *diag);
static robo_result_t mode_in_head(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag);
static robo_result_t mode_after_head(robo_html_tree_builder_t *builder,
                                     const robo_html_token_t *tok,
                                     int *reprocess,
                                     robo_html_diag_t *diag);
static robo_result_t mode_in_body(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag);
static robo_result_t mode_after_body(robo_html_tree_builder_t *builder,
                                     const robo_html_token_t *tok,
                                     int *reprocess,
                                     robo_html_diag_t *diag);
static robo_result_t mode_after_after_body(robo_html_tree_builder_t *builder,
                                           const robo_html_token_t *tok,
                                           int *reprocess,
                                           robo_html_diag_t *diag);
static robo_result_t mode_text(robo_html_tree_builder_t *builder,
                               const robo_html_token_t *tok,
                               int *reprocess,
                               robo_html_diag_t *diag);

/** Whitespace-only character token (the one test every early mode needs). */
static int token_is_all_whitespace(const robo_html_token_t *tok)
{
    size_t i;

    if (tok->kind != ROBO_HTML_TOKEN_CHARACTER) {
        return 0;
    }
    for (i = 0u; i < tok->u.characters.length; i++) {
        char c = tok->u.characters.data[i];

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return 0;
        }
    }
    return 1;
}

static robo_result_t append_comment(robo_html_tree_builder_t *builder,
                                    robo_html_node_id_t parent_id,
                                    const robo_html_token_t *tok,
                                    robo_html_diag_t *diag)
{
    robo_html_node_id_t id = ROBO_HTML_NODE_ID_NONE;
    robo_result_t r;

    /* A comment breaks an adjacent-character merge run. */
    builder->text_node = ROBO_HTML_NODE_ID_NONE;
    builder->text_parent = ROBO_HTML_NODE_ID_NONE;
    r = create_leaf(builder, ROBO_HTML_NODE_KIND_COMMENT, parent_id,
                    tok->u.comment.data, tok->u.comment.length, &id, diag);
    return r;
}

static robo_result_t create_doctype_node(robo_html_tree_builder_t *builder,
                                         const robo_html_token_t *tok,
                                         robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    robo_html_node_handle_t h;
    robo_html_node_entry_t *entry;
    robo_result_t r;

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE, ROBO_NOT_FOUND,
                              "document was destroyed during the build");
    }
    r = register_node(builder, ROBO_HTML_NODE_KIND_DOCUMENT_TYPE, &h, diag);
    if (r != ROBO_OK) {
        return r;
    }
    entry = robo_html_node_entry_mut(builder->document_id, h.node_id);
    r = robo_html_payload_write_doctype(slot, entry, tok->u.doctype.name,
                                        tok->u.doctype.public_id,
                                        tok->u.doctype.system_id, diag);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_html_node_link_child(builder->document_id, builder->root.node_id,
                                     h.node_id, diag);
}

/* --- INITIAL -------------------------------------------------------- */

static robo_result_t mode_initial(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag)
{
    (void)diag;
    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        if (token_is_all_whitespace(tok)) {
            return ROBO_OK; /* ignored before <html> */
        }
        builder->mode = ROBO_HTML_MODE_BEFORE_HTML;
        *reprocess = 1;
        return ROBO_OK;
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->root.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        builder->saw_doctype = 1;
        if (create_doctype_node(builder, tok, diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_OUT_OF_MEMORY;
        }
        builder->mode = ROBO_HTML_MODE_BEFORE_HTML;
        return ROBO_OK;
    case ROBO_HTML_TOKEN_END_TAG:
        record_token_recovery(builder, tok, "end tag before <html>; ignored");
        builder->mode = ROBO_HTML_MODE_BEFORE_HTML;
        *reprocess = 1;
        return ROBO_OK;
    case ROBO_HTML_TOKEN_START_TAG:
    default:
        builder->mode = ROBO_HTML_MODE_BEFORE_HTML;
        *reprocess = 1;
        return ROBO_OK;
    }
}

/* --- BEFORE_HTML ---------------------------------------------------- */

static robo_result_t mode_before_html(robo_html_tree_builder_t *builder,
                                      const robo_html_token_t *tok,
                                      int *reprocess,
                                      robo_html_diag_t *diag)
{
    robo_result_t r;

    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        if (token_is_all_whitespace(tok)) {
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->root.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "duplicate DOCTYPE; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_END_TAG:
        if (strcmp(tok->u.tag.name, "head") != 0 &&
            strcmp(tok->u.tag.name, "body") != 0 &&
            strcmp(tok->u.tag.name, "html") != 0 &&
            strcmp(tok->u.tag.name, "br") != 0) {
            record_token_recovery(builder, tok,
                                 "unexpected end tag before <html>; ignored");
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_START_TAG:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            robo_html_attr_view_t attrs[MAX_STAGED_ATTRS];
            size_t attr_count = 0u;

            stage_attrs(tok, attrs, &attr_count, builder);
            r = ensure_html(builder, attrs, attr_count, diag);
            if (r != ROBO_OK) {
                return r;
            }
            builder->mode = ROBO_HTML_MODE_BEFORE_HEAD;
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_EOF:
    default:
        break;
    }

    r = ensure_html(builder, NULL, 0u, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->mode = ROBO_HTML_MODE_BEFORE_HEAD;
    *reprocess = 1;
    return ROBO_OK;
}

/* --- BEFORE_HEAD ---------------------------------------------------- */

static robo_result_t mode_before_head(robo_html_tree_builder_t *builder,
                                      const robo_html_token_t *tok,
                                      int *reprocess,
                                      robo_html_diag_t *diag)
{
    robo_result_t r;

    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        if (token_is_all_whitespace(tok)) {
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->html.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "DOCTYPE after <html>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_END_TAG:
        if (strcmp(tok->u.tag.name, "head") != 0 &&
            strcmp(tok->u.tag.name, "body") != 0 &&
            strcmp(tok->u.tag.name, "html") != 0 &&
            strcmp(tok->u.tag.name, "br") != 0) {
            record_token_recovery(builder, tok, "unexpected end tag; ignored");
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_START_TAG:
        if (strcmp(tok->u.tag.name, "head") == 0) {
            robo_html_attr_view_t attrs[MAX_STAGED_ATTRS];
            size_t attr_count = 0u;
            robo_html_node_handle_t h;

            stage_attrs(tok, attrs, &attr_count, builder);
            r = create_element(builder, builder->html.node_id, "head", 4u, attrs,
                               attr_count, 0, &h, diag);
            if (r != ROBO_OK) {
                return r;
            }
            builder->head = h;
            r = stack_push(builder, h.node_id, diag);
            if (r != ROBO_OK) {
                return r;
            }
            builder->mode = ROBO_HTML_MODE_IN_HEAD;
            return ROBO_OK;
        }
        if (strcmp(tok->u.tag.name, "html") == 0) {
            record_token_recovery(builder, tok,
                                 "duplicate <html> start tag; ignored");
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_EOF:
    default:
        break;
    }

    /* Anything else: the head is implied, then the token is reprocessed. */
    r = ensure_head(builder, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->mode = ROBO_HTML_MODE_IN_HEAD;
    *reprocess = 1;
    return ROBO_OK;
}

/* --- IN_HEAD -------------------------------------------------------- */

static robo_result_t mode_in_head(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag)
{
    robo_result_t r;

    (void)reprocess;
    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        /* Only whitespace is head content; anything else implies </head>. */
        if (token_is_all_whitespace(tok)) {
            return insert_text_current(builder, tok->u.characters.data,
                                       tok->u.characters.length, diag);
        }
        break;
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->head.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "DOCTYPE inside <head>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_END_TAG:
        if (strcmp(tok->u.tag.name, "head") == 0) {
            if (!close_to_element(builder, builder_slot(builder), "head")) {
                stack_pop(builder);
            }
            builder->mode = ROBO_HTML_MODE_AFTER_HEAD;
            return ROBO_OK;
        }
        if (strcmp(tok->u.tag.name, "body") == 0 ||
            strcmp(tok->u.tag.name, "html") == 0 ||
            strcmp(tok->u.tag.name, "br") == 0) {
            break; /* "anything else": close head and reprocess */
        }
        record_token_recovery(builder, tok,
                             "end tag inside <head>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_START_TAG:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            record_token_recovery(builder, tok,
                                 "duplicate <html> start tag; ignored");
            return ROBO_OK;
        }
        {
            robo_html_text_mode_t family =
                text_mode_for_element(tok->u.tag.name);

            if (family != ROBO_HTML_TEXT_MODE_NONE) {
                robo_html_attr_view_t attrs[MAX_STAGED_ATTRS];
                size_t attr_count = 0u;
                robo_html_node_id_t id = ROBO_HTML_NODE_ID_NONE;

                stage_attrs(tok, attrs, &attr_count, builder);
                r = insert_element(builder, tok->u.tag.name, attrs, attr_count,
                                   0, &id, diag);
                if (r != ROBO_OK) {
                    return r;
                }
                return enter_text_mode(builder, family, diag);
            }
            if (is_head_content_element(tok->u.tag.name)) {
                robo_html_attr_view_t attrs[MAX_STAGED_ATTRS];
                size_t attr_count = 0u;
                robo_html_node_id_t id = ROBO_HTML_NODE_ID_NONE;

                stage_attrs(tok, attrs, &attr_count, builder);
                return insert_element(builder, tok->u.tag.name, attrs,
                                      attr_count, tok->u.tag.self_closing, &id,
                                      diag);
            }
            if (strcmp(tok->u.tag.name, "head") == 0) {
                record_token_recovery(builder, tok,
                                     "duplicate <head> start tag; ignored");
                return ROBO_OK;
            }
        }
        break;
    case ROBO_HTML_TOKEN_EOF:
    default:
        break;
    }

    if (!close_to_element(builder, builder_slot(builder), "head")) {
        stack_pop(builder);
    }
    builder->mode = ROBO_HTML_MODE_AFTER_HEAD;
    *reprocess = 1;
    return ROBO_OK;
}

/* --- AFTER_HEAD ----------------------------------------------------- */

static robo_result_t mode_after_head(robo_html_tree_builder_t *builder,
                                     const robo_html_token_t *tok,
                                     int *reprocess,
                                     robo_html_diag_t *diag)
{
    robo_result_t r;

    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        if (token_is_all_whitespace(tok)) {
            builder->text_node = ROBO_HTML_NODE_ID_NONE;
            return insert_text(builder, builder->html.node_id,
                               tok->u.characters.data, tok->u.characters.length,
                               diag);
        }
        break;
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->html.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "DOCTYPE after <head>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_END_TAG:
        if (strcmp(tok->u.tag.name, "body") != 0 &&
            strcmp(tok->u.tag.name, "html") != 0 &&
            strcmp(tok->u.tag.name, "br") != 0) {
            record_token_recovery(builder, tok, "unexpected end tag; ignored");
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_START_TAG:
        if (strcmp(tok->u.tag.name, "body") == 0) {
            robo_html_attr_view_t attrs[MAX_STAGED_ATTRS];
            size_t attr_count = 0u;
            robo_html_node_handle_t h;

            stage_attrs(tok, attrs, &attr_count, builder);
            r = create_element(builder, builder->html.node_id, "body", 4u, attrs,
                               attr_count, 0, &h, diag);
            if (r != ROBO_OK) {
                return r;
            }
            builder->body = h;
            r = stack_push(builder, h.node_id, diag);
            if (r != ROBO_OK) {
                return r;
            }
            builder->mode = ROBO_HTML_MODE_IN_BODY;
            return ROBO_OK;
        }
        if (strcmp(tok->u.tag.name, "html") == 0 ||
            strcmp(tok->u.tag.name, "head") == 0) {
            record_token_recovery(builder, tok,
                                 "start tag after <head>; ignored");
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_EOF:
    default:
        break;
    }

    r = ensure_body(builder, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder->mode = ROBO_HTML_MODE_IN_BODY;
    *reprocess = 1;
    return ROBO_OK;
}

/* --- IN_BODY -------------------------------------------------------- */

static robo_result_t body_start_tag(robo_html_tree_builder_t *builder,
                                    const robo_html_token_t *tok,
                                    int *reprocess,
                                    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    robo_html_attr_view_t attrs[MAX_STAGED_ATTRS];
    size_t attr_count = 0u;
    robo_html_node_id_t id = ROBO_HTML_NODE_ID_NONE;

    (void)reprocess; /* every path in this handler resolves the token itself */
    {
        robo_html_text_mode_t family = text_mode_for_element(tok->u.tag.name);

        if (strcmp(tok->u.tag.name, "html") == 0) {
            record_token_recovery(builder, tok,
                                 "duplicate <html> start tag; ignored");
            return ROBO_OK;
        }
        if (strcmp(tok->u.tag.name, "body") == 0 ||
            strcmp(tok->u.tag.name, "head") == 0) {
            record_token_recovery(builder, tok,
                                 "duplicate structural start tag; ignored");
            return ROBO_OK;
        }
        if (strcmp(tok->u.tag.name, "button") == 0 &&
            stack_find(builder, slot, "button") >= 0) {
            record_token_recovery(builder, tok,
                                 "nested <button>; closing the outer one");
            close_to_element(builder, slot, "button");
        }
        if (is_heading_name(tok->u.tag.name)) {
            long index;

            /* A heading closes an open heading and an open paragraph. */
            for (index = (long)builder->open_count - 1; index >= 0; index--) {
                const robo_html_node_entry_t *e = robo_html_node_entry_get(
                    builder->document_id, builder->open[index]);

                if (e && is_heading_name(robo_html_payload_element_name(slot, e,
                                                                       NULL))) {
                    record_token_recovery(builder, tok,
                                         "nested heading; closing the outer one");
                    while ((long)builder->open_count > index) {
                        stack_pop(builder);
                    }
                    break;
                }
            }
        }
        close_p_if_open(builder, slot, tok->u.tag.name);

        stage_attrs(tok, attrs, &attr_count, builder);
        if (insert_element(builder, tok->u.tag.name, attrs, attr_count,
                           tok->u.tag.self_closing, &id, diag) != ROBO_OK) {
            return diag ? diag->code : ROBO_OUT_OF_MEMORY;
        }
        if (family != ROBO_HTML_TEXT_MODE_NONE) {
            return enter_text_mode(builder, family, diag);
        }
        /* <pre>/<listing>/<textarea> swallow one immediately following LF. */
        if (strcmp(tok->u.tag.name, "pre") == 0 ||
            strcmp(tok->u.tag.name, "listing") == 0 ||
            strcmp(tok->u.tag.name, "textarea") == 0) {
            builder->skip_next_newline = 1;
        }
        return ROBO_OK;
    }
}

static robo_result_t body_end_tag(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    const char *name = tok->u.tag.name;

    (void)diag;
    if (strcmp(name, "body") == 0) {
        if (!handle_is_live(&builder->body)) {
            record_token_recovery(builder, tok, "end tag for unopened <body>");
            return ROBO_OK;
        }
        builder->mode = ROBO_HTML_MODE_AFTER_BODY;
        return ROBO_OK;
    }
    if (strcmp(name, "html") == 0) {
        builder->mode = ROBO_HTML_MODE_AFTER_BODY;
        *reprocess = 1;
        return ROBO_OK;
    }
    if (strcmp(name, "br") == 0) {
        /* Legacy <br> mismatch: the spec re-runs it as a start tag. */
        record_token_recovery(builder, tok,
                             "</br> treated as <br> per the HTML model");
        {
            robo_html_node_id_t id = ROBO_HTML_NODE_ID_NONE;

            return insert_element(builder, "br", NULL, 0u, 0, &id, diag);
        }
    }
    if (is_heading_name(name)) {
        if (stack_find(builder, slot, name) < 0) {
            record_token_recovery(builder, tok, "end tag for unopened heading");
            return ROBO_OK;
        }
        close_to_element(builder, slot, name);
        return ROBO_OK;
    }
    if (close_to_element(builder, slot, name)) {
        return ROBO_OK;
    }
    record_token_recovery(builder, tok,
                         "end tag with no matching open element; ignored");
    return ROBO_OK;
}

static robo_result_t mode_in_body(robo_html_tree_builder_t *builder,
                                  const robo_html_token_t *tok,
                                  int *reprocess,
                                  robo_html_diag_t *diag)
{
    (void)reprocess;
    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        return insert_text_current(builder, tok->u.characters.data,
                                   tok->u.characters.length, diag);
    case ROBO_HTML_TOKEN_COMMENT: {
        robo_html_node_id_t current = stack_top(builder);

        if (current == ROBO_HTML_NODE_ID_NONE) {
            current = builder->root.node_id;
        }
        return append_comment(builder, current, tok, diag);
    }
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "DOCTYPE inside <body>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_START_TAG:
        return body_start_tag(builder, tok, reprocess, diag);
    case ROBO_HTML_TOKEN_END_TAG:
        return body_end_tag(builder, tok, reprocess, diag);
    case ROBO_HTML_TOKEN_EOF:
    default:
        return ROBO_OK;
    }
}

/* --- AFTER_BODY ----------------------------------------------------- */

static robo_result_t mode_after_body(robo_html_tree_builder_t *builder,
                                     const robo_html_token_t *tok,
                                     int *reprocess,
                                     robo_html_diag_t *diag)
{
    switch (tok->kind) {
    case ROBO_HTML_TOKEN_CHARACTER:
        if (token_is_all_whitespace(tok)) {
            builder->text_node = ROBO_HTML_NODE_ID_NONE;
            return insert_text(builder, builder->body.node_id,
                               tok->u.characters.data, tok->u.characters.length,
                               diag);
        }
        break;
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->html.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "DOCTYPE after </body>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_START_TAG:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            record_token_recovery(builder, tok,
                                 "start tag after </body>; ignored");
            return ROBO_OK;
        }
        break;
    case ROBO_HTML_TOKEN_END_TAG:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            builder->mode = ROBO_HTML_MODE_AFTER_AFTER_BODY;
            return ROBO_OK;
        }
        record_token_recovery(builder, tok, "end tag after </body>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_EOF:
    default:
        return ROBO_OK;
    }

    record_token_recovery(builder, tok,
                         "content after </body> processed in body mode");
    builder->mode = ROBO_HTML_MODE_IN_BODY;
    *reprocess = 1;
    return ROBO_OK;
}

/* --- AFTER_AFTER_BODY ----------------------------------------------- */

static robo_result_t mode_after_after_body(robo_html_tree_builder_t *builder,
                                           const robo_html_token_t *tok,
                                           int *reprocess,
                                           robo_html_diag_t *diag)
{
    switch (tok->kind) {
    case ROBO_HTML_TOKEN_COMMENT:
        return append_comment(builder, builder->root.node_id, tok, diag);
    case ROBO_HTML_TOKEN_DOCTYPE:
        record_token_recovery(builder, tok, "DOCTYPE after </html>; ignored");
        return ROBO_OK;
    case ROBO_HTML_TOKEN_EOF:
        return ROBO_OK;
    case ROBO_HTML_TOKEN_CHARACTER:
        if (token_is_all_whitespace(tok)) {
            return insert_text_current(builder, tok->u.characters.data,
                                       tok->u.characters.length, diag);
        }
        break;
    default:
        break;
    }
    record_token_recovery(builder, tok,
                         "content after </html> processed in body mode");
    builder->mode = ROBO_HTML_MODE_IN_BODY;
    *reprocess = 1;
    return ROBO_OK;
}

/* --- TEXT (RCDATA / RAWTEXT / SCRIPT / PLAINTEXT) -------------------- */

static robo_result_t mode_text(robo_html_tree_builder_t *builder,
                               const robo_html_token_t *tok,
                               int *reprocess,
                               robo_html_diag_t *diag)
{
    (void)reprocess;
    if (tok->kind == ROBO_HTML_TOKEN_CHARACTER) {
        if (builder->skip_next_newline) {
            const char *data = tok->u.characters.data;
            size_t length = tok->u.characters.length;

            if (length > 0u && data[0] == '\n') {
                data++;
                length--;
            }
            builder->skip_next_newline = 0;
            return insert_text_current(builder, data, length, diag);
        }
        return insert_text_current(builder, tok->u.characters.data,
                                   tok->u.characters.length, diag);
    }
    if (tok->kind == ROBO_HTML_TOKEN_END_TAG) {
        robo_html_document_slot_t *slot = builder_slot(builder);
        long index = stack_find_text_family(builder, slot);

        if (index >= 0) {
            while (builder->open_count > (size_t)index) {
                stack_pop(builder);
            }
        } else {
            stack_pop(builder);
        }
        return leave_text_mode(builder, diag);
    }
    if (tok->kind == ROBO_HTML_TOKEN_EOF) {
        builder->text_mode = ROBO_HTML_TEXT_MODE_NONE;
        builder->mode = builder->text_return_mode;
        return ROBO_OK;
    }
    /* The tokenizer should not emit these inside raw content, but a
     * malformed source must never be fatal. */
    record_token_recovery(builder, tok, "unexpected token inside text content");
    return ROBO_OK;
}

/* --- dispatch ------------------------------------------------------- */

static robo_result_t process_token(robo_html_tree_builder_t *builder,
                                   const robo_html_token_t *tok,
                                   robo_html_diag_t *diag)
{
    unsigned int guard = 0u;

    if (tok->kind == ROBO_HTML_TOKEN_START_TAG && builder->skip_next_newline &&
        builder->mode != ROBO_HTML_MODE_TEXT) {
        builder->skip_next_newline = 0;
    }
    for (guard = 0u; guard < REPROCESS_LIMIT; guard++) {
        int reprocess = 0;
        robo_result_t r;

        switch (builder->mode) {
        case ROBO_HTML_MODE_INITIAL:
            r = mode_initial(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_BEFORE_HTML:
            r = mode_before_html(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_BEFORE_HEAD:
            r = mode_before_head(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_IN_HEAD:
            r = mode_in_head(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_AFTER_HEAD:
            r = mode_after_head(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_IN_BODY:
            r = mode_in_body(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_AFTER_BODY:
            r = mode_after_body(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_AFTER_AFTER_BODY:
            r = mode_after_after_body(builder, tok, &reprocess, diag);
            break;
        case ROBO_HTML_MODE_TEXT:
            r = mode_text(builder, tok, &reprocess, diag);
            break;
        default:
            record_token_recovery(builder, tok,
                                 "unknown insertion mode; build stopped");
            builder->stop = 1;
            return ROBO_OK;
        }
        if (r != ROBO_OK) {
            return r;
        }
        if (!reprocess) {
            return ROBO_OK;
        }
    }
    record_token_recovery(builder, tok, "mode re-dispatch limit reached");
    builder->stop = 1;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* 6. Result accounting and finalization                               */
/* ------------------------------------------------------------------ */

static void recount(robo_html_tree_builder_t *builder)
{
    robo_html_document_slot_t *slot = builder_slot(builder);
    size_t i;

    if (!slot) {
        return;
    }
    builder->result.node_count = 0u;
    builder->result.element_count = 0u;
    builder->result.text_count = 0u;
    builder->result.comment_count = 0u;
    builder->result.doctype_count = 0u;
    builder->result.attribute_count = 0u;
    for (i = 0u; i < ROBO_HTML_MAX_NODES_PER_DOCUMENT; i++) {
        const robo_html_node_entry_t *e = &slot->nodes.slots[i];

        if (!e->in_use) {
            continue;
        }
        builder->result.node_count++;
        switch (e->kind) {
        case ROBO_HTML_NODE_KIND_ELEMENT:
            builder->result.element_count++;
            builder->result.attribute_count +=
                robo_html_payload_attr_count(slot, e);
            break;
        case ROBO_HTML_NODE_KIND_TEXT:
            builder->result.text_count++;
            break;
        case ROBO_HTML_NODE_KIND_COMMENT:
            builder->result.comment_count++;
            break;
        case ROBO_HTML_NODE_KIND_DOCUMENT_TYPE:
            builder->result.doctype_count++;
            break;
        case ROBO_HTML_NODE_KIND_DOCUMENT:
        case ROBO_HTML_NODE_KIND_FRAGMENT:
        default:
            break;
        }
    }
}

/** Close everything still open, recording one diagnostic per element. */
static void close_all_open(robo_html_tree_builder_t *builder,
                           robo_html_document_slot_t *slot)
{
    while (builder->open_count > 0u) {
        robo_html_node_id_t id = stack_top(builder);
        const robo_html_node_entry_t *e =
            robo_html_node_entry_get(builder->document_id, id);

        if (e && !is_structural(builder, id)) {
            record_recovery(builder, ROBO_PARSE_ERROR,
                            "element left open at end of input", 0u, 0u);
        }
        stack_pop(builder);
    }
    (void)slot;
}

/* ------------------------------------------------------------------ */
/* 7. Public API                                                       */
/* ------------------------------------------------------------------ */

int robo_html_insertion_mode_is_known(robo_html_insertion_mode_t mode)
{
    return mode >= ROBO_HTML_MODE_INITIAL && mode <= ROBO_HTML_MODE_TEXT;
}

const char *robo_html_insertion_mode_name(robo_html_insertion_mode_t mode)
{
    switch (mode) {
    case ROBO_HTML_MODE_INITIAL:
        return "initial";
    case ROBO_HTML_MODE_BEFORE_HTML:
        return "before-html";
    case ROBO_HTML_MODE_BEFORE_HEAD:
        return "before-head";
    case ROBO_HTML_MODE_IN_HEAD:
        return "in-head";
    case ROBO_HTML_MODE_AFTER_HEAD:
        return "after-head";
    case ROBO_HTML_MODE_IN_BODY:
        return "in-body";
    case ROBO_HTML_MODE_AFTER_BODY:
        return "after-body";
    case ROBO_HTML_MODE_AFTER_AFTER_BODY:
        return "after-after-body";
    case ROBO_HTML_MODE_TEXT:
        return "text";
    default:
        return "unknown";
    }
}

const char *robo_html_text_mode_name(robo_html_text_mode_t mode)
{
    switch (mode) {
    case ROBO_HTML_TEXT_MODE_NONE:
        return "none";
    case ROBO_HTML_TEXT_MODE_RCDATA:
        return "rcdata";
    case ROBO_HTML_TEXT_MODE_RAWTEXT:
        return "rawtext";
    case ROBO_HTML_TEXT_MODE_SCRIPT:
        return "script";
    case ROBO_HTML_TEXT_MODE_PLAINTEXT:
        return "plaintext";
    default:
        return "unknown";
    }
}

int robo_html_text_mode_is_known(robo_html_text_mode_t mode)
{
    return mode >= ROBO_HTML_TEXT_MODE_NONE &&
           mode <= ROBO_HTML_TEXT_MODE_PLAINTEXT;
}

robo_result_t robo_html_tree_builder_create(robo_html_document_id_t document_id,
                                            robo_html_tree_builder_t **out,
                                            robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_tree_builder_t *builder;
    robo_html_parser_t *lease = NULL;
    robo_result_t r;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    *out = NULL;
    if (!robo_html_is_initialized()) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_INIT_ERROR,
                              "RoboHTML module not initialized");
    }
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    /*
     * Any lifecycle that accepts content can host a build, including LOADING:
     * a document whose source was attached with attach_source (so it sits in
     * LOADING, waiting to be parsed) is exactly the case a builder exists
     * for. Exclusivity is the parser lease below, not the lifecycle — begin()
     * settles a LOADING document before it attaches its own source.
     */
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_BUSY,
                              "document is not idle");
    }
    /* Taking the 8.1 parser lease keeps a second builder out (ROBO_BUSY). */
    r = robo_html_parser_create(document_id, &lease, diag);
    if (r != ROBO_OK) {
        return r;
    }
    builder = (robo_html_tree_builder_t *)robo_calloc(
        1u, sizeof(robo_html_tree_builder_t));
    if (!builder) {
        robo_html_parser_destroy(lease);
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_OUT_OF_MEMORY, "tree builder allocation");
    }
    builder->magic = TREE_BUILDER_MAGIC;
    builder->document_id = document_id;
    builder->lease = lease;
    builder->mode = ROBO_HTML_MODE_INITIAL;
    builder->text_return_mode = ROBO_HTML_MODE_INITIAL;
    builder->text_mode = ROBO_HTML_TEXT_MODE_NONE;
    builder->state = ROBO_HTML_PARSER_STATE_IDLE;
    builder->root = slot->root;
    builder->text_node = ROBO_HTML_NODE_ID_NONE;
    builder->text_parent = ROBO_HTML_NODE_ID_NONE;
    robo_html_node_handle_clear(&builder->html);
    robo_html_node_handle_clear(&builder->head);
    robo_html_node_handle_clear(&builder->body);
    robo_html_diagnostics_init(&builder->diagnostics);
    robo_html_source_init(&builder->source);
    *out = builder;
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_begin(robo_html_tree_builder_t *builder,
                                           const robo_html_source_t *src,
                                           robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_result_t r;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "builder is NULL");
    }
    if (!src || !robo_html_source_kind_is_known(src->kind) ||
        src->kind == ROBO_HTML_SOURCE_KIND_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is not usable");
    }
    if (builder->state != ROBO_HTML_PARSER_STATE_IDLE &&
        builder->state != ROBO_HTML_PARSER_STATE_COMPLETE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_ERROR,
                              "build already in progress");
    }
    slot = builder_slot(builder);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "document was destroyed");
    }
    /* A previous build leaves the document LOADING: settle it first. */
    if (slot->lifecycle == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        r = robo_html_document_detach_source(builder->document_id, diag);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_ERROR,
                              "lifecycle does not allow building");
    }
    /*
     * Re-parsing clears the previous tree; old node handles go stale while
     * the document's own root survives. A finalized document is cleared too,
     * because clearing is what reopens construction after a completed
     * build closed it.
     */
    if (slot->nodes.used > 1u || slot->finalized) {
        r = robo_html_document_clear_tree(slot, diag);
        if (r != ROBO_OK) {
            return r;
        }
    }
    builder->root = slot->root;
    robo_html_node_handle_clear(&builder->html);
    robo_html_node_handle_clear(&builder->head);
    robo_html_node_handle_clear(&builder->body);

    if (builder->tokenizer) {
        robo_html_tokenizer_destroy(builder->tokenizer);
        builder->tokenizer = NULL;
    }
    r = robo_html_tokenizer_create(src, &builder->tokenizer, diag);
    if (r != ROBO_OK) {
        builder->state = ROBO_HTML_PARSER_STATE_FAILED;
        return r;
    }
    builder->source = *src;
    builder->has_source = 1;

    r = robo_html_document_attach_source(builder->document_id, src, diag);
    if (r != ROBO_OK) {
        builder->state = ROBO_HTML_PARSER_STATE_FAILED;
        return r;
    }

    builder->mode = ROBO_HTML_MODE_INITIAL;
    builder->text_return_mode = ROBO_HTML_MODE_INITIAL;
    builder->text_mode = ROBO_HTML_TEXT_MODE_NONE;
    builder->state = ROBO_HTML_PARSER_STATE_FEEDING;
    builder->open_count = 0u;
    builder->text_node = ROBO_HTML_NODE_ID_NONE;
    builder->text_parent = ROBO_HTML_NODE_ID_NONE;
    builder->saw_doctype = 0;
    builder->skip_next_newline = 0;
    builder->complete = 0;
    builder->stop = 0;
    memset(&builder->result, 0, sizeof(builder->result));
    robo_html_diagnostics_clear(&builder->diagnostics);
    builder->dropped_diagnostics = 0u;
    if (builder->lease) {
        builder->lease->state = ROBO_HTML_PARSER_STATE_FEEDING;
        builder->lease->source_kind = src->kind;
        builder->lease->source_length = robo_html_source_length(src);
    }
    robo_log_debug("robo_html: doc=%llu tree build started (%s, %llu bytes)",
                   (unsigned long long)builder->document_id,
                   robo_html_source_kind_name(src->kind),
                   (unsigned long long)robo_html_source_length(src));
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_consume(
    robo_html_tree_builder_t *builder,
    const robo_html_token_t *token,
    robo_html_diag_t *diag)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !token) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "builder or token is NULL");
    }
    if (!robo_html_token_kind_is_known(token->kind) ||
        token->kind == ROBO_HTML_TOKEN_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "unknown token kind");
    }
    if (builder->state != ROBO_HTML_PARSER_STATE_FEEDING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_INIT_ERROR,
                              "builder is not begun");
    }
    if (builder->complete) {
        return ROBO_OK;
    }
    {
        robo_result_t r = process_token(builder, token, diag);

        /* Hitting the node ceiling truncates rather than failing the build. */
        if (r != ROBO_OK && builder->stop) {
            return robo_html_tree_builder_finish(builder, diag);
        }
        return r;
    }
}

robo_result_t robo_html_tree_builder_next(robo_html_tree_builder_t *builder,
                                          robo_html_diag_t *diag)
{
    robo_html_token_t token;
    robo_result_t r;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "builder is NULL");
    }
    if (!builder->has_source || !builder->tokenizer) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "no source attached");
    }
    if (builder->complete) {
        return ROBO_OK; /* idempotent once finished */
    }
    if (builder->stop) {
        /* Truncated: settle the tree instead of consuming further input. */
        return robo_html_tree_builder_finish(builder, diag);
    }
    r = robo_html_tokenizer_next(builder->tokenizer, &token, diag);
    if (r != ROBO_OK) {
        builder->state = ROBO_HTML_PARSER_STATE_FAILED;
        return r;
    }
    if (token.kind == ROBO_HTML_TOKEN_EOF) {
        /* EOF still runs through the modes so pending content settles. */
        r = process_token(builder, &token, diag);
        if (r != ROBO_OK && !builder->stop) {
            return r;
        }
        return robo_html_tree_builder_finish(builder, diag);
    }
    r = process_token(builder, &token, diag);
    if (r != ROBO_OK && builder->stop) {
        return robo_html_tree_builder_finish(builder, diag);
    }
    return r;
}

robo_result_t robo_html_tree_builder_finish(robo_html_tree_builder_t *builder,
                                            robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "builder is NULL");
    }
    if (builder->complete) {
        return ROBO_OK;
    }
    slot = builder_slot(builder);
    if (builder->state == ROBO_HTML_PARSER_STATE_FEEDING) {
        close_all_open(builder, slot);
    }
    if (slot) {
        recount(builder);
    }
    builder->complete = 1;
    builder->state = ROBO_HTML_PARSER_STATE_COMPLETE;
    if (builder->lease) {
        builder->lease->state = ROBO_HTML_PARSER_STATE_COMPLETE;
    }
    /* The document reaches READY once its tree exists. */
    if (slot && slot->lifecycle == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
        if (robo_html_document_mark_ready(builder->document_id, NULL) !=
            ROBO_OK) {
            record_recovery(builder, ROBO_ERROR,
                            "document could not transition to ready", 0u, 0u);
        }
    }
    /*
     * A finished build closes tree construction: the document model owns the
     * tree from here, and further register/link calls would be a caller
     * mistake rather than part of a parse. Re-parsing goes through begin()
     * (which clears the tree and reopens construction) or reset().
     */
    if (slot) {
        if (robo_html_document_finalize(builder->document_id, NULL) !=
            ROBO_OK) {
            record_recovery(builder, ROBO_ERROR,
                            "document could not be finalized", 0u, 0u);
        }
    }
    robo_log_debug(
        "robo_html: doc=%llu tree built: nodes=%llu elements=%llu text=%llu "
        "comments=%llu doctypes=%llu attrs=%llu depth=%llu recovered=%llu",
        (unsigned long long)builder->document_id,
        (unsigned long long)builder->result.node_count,
        (unsigned long long)builder->result.element_count,
        (unsigned long long)builder->result.text_count,
        (unsigned long long)builder->result.comment_count,
        (unsigned long long)builder->result.doctype_count,
        (unsigned long long)builder->result.attribute_count,
        (unsigned long long)builder->result.max_depth,
        (unsigned long long)builder->result.recovered_errors);
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_build(robo_html_tree_builder_t *builder,
                                           const robo_html_source_t *src,
                                           robo_html_diag_t *diag)
{
    robo_result_t r;

    r = robo_html_tree_builder_begin(builder, src, diag);
    if (r != ROBO_OK) {
        return r;
    }
    while (!builder->complete && !builder->stop) {
        r = robo_html_tree_builder_next(builder, diag);
        if (r != ROBO_OK) {
            return r;
        }
    }
    return robo_html_tree_builder_finish(builder, diag);
}

robo_result_t robo_html_build_tree(robo_html_document_id_t document_id,
                                   const robo_html_source_t *src,
                                   robo_html_tree_result_t *out_result,
                                   robo_html_diag_t *diag)
{
    robo_html_tree_builder_t *builder = NULL;
    robo_result_t r;

    r = robo_html_tree_builder_create(document_id, &builder, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_html_tree_builder_build(builder, src, diag);
    if (r == ROBO_OK && out_result) {
        *out_result = builder->result;
    }
    robo_html_tree_builder_destroy(builder);
    return r;
}

robo_result_t robo_html_tree_builder_get_state(
    const robo_html_tree_builder_t *builder,
    robo_html_parser_state_t *out_state)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out_state) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_state = builder->state;
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_get_mode(
    const robo_html_tree_builder_t *builder,
    robo_html_insertion_mode_t *out_mode)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out_mode) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_mode = builder->mode;
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_get_text_mode(
    const robo_html_tree_builder_t *builder,
    robo_html_text_mode_t *out_mode)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out_mode) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_mode = builder->text_mode;
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_set_text_mode(
    robo_html_tree_builder_t *builder,
    robo_html_text_mode_t mode,
    robo_html_diag_t *diag)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC ||
        !robo_html_text_mode_is_known(mode)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT,
                              "builder or text mode is invalid");
    }
    if (builder->state != ROBO_HTML_PARSER_STATE_FEEDING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_INIT_ERROR,
                              "builder is not begun");
    }
    if (mode == ROBO_HTML_TEXT_MODE_NONE) {
        builder->text_mode = ROBO_HTML_TEXT_MODE_NONE;
        return ROBO_OK;
    }
    return enter_text_mode(builder, mode, diag);
}

robo_result_t robo_html_tree_builder_current_node(
    const robo_html_tree_builder_t *builder,
    robo_html_node_handle_t *out)
{
    robo_html_node_id_t id;
    const robo_html_node_entry_t *e;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    id = stack_top(builder);
    if (id == ROBO_HTML_NODE_ID_NONE) {
        id = builder->root.node_id;
    }
    e = robo_html_node_entry_get(builder->document_id, id);
    if (!e) {
        return ROBO_NOT_FOUND;
    }
    *out = robo_html_node_handle_make(builder->document_id, e);
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_open_element_count(
    const robo_html_tree_builder_t *builder,
    size_t *out_count)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = builder->open_count;
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_open_element(
    const robo_html_tree_builder_t *builder,
    size_t index,
    robo_html_node_handle_t *out)
{
    const robo_html_node_entry_t *e;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= builder->open_count) {
        return ROBO_NOT_FOUND;
    }
    e = robo_html_node_entry_get(builder->document_id, builder->open[index]);
    if (!e) {
        return ROBO_NOT_FOUND;
    }
    *out = robo_html_node_handle_make(builder->document_id, e);
    return ROBO_OK;
}

robo_result_t robo_html_tree_builder_result(
    const robo_html_tree_builder_t *builder,
    robo_html_tree_result_t *out)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = builder->result;
    return ROBO_OK;
}

size_t robo_html_tree_builder_diagnostic_count(
    const robo_html_tree_builder_t *builder)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return 0u;
    }
    return builder->diagnostics.count;
}

robo_result_t robo_html_tree_builder_get_diagnostic(
    const robo_html_tree_builder_t *builder,
    size_t index,
    robo_html_diag_t *out_diag)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out_diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= builder->diagnostics.count) {
        return ROBO_NOT_FOUND;
    }
    *out_diag = builder->diagnostics.items[index];
    return ROBO_OK;
}

void robo_html_tree_builder_destroy(robo_html_tree_builder_t *builder)
{
    if (!builder) {
        return;
    }
    if (builder->magic != TREE_BUILDER_MAGIC) {
        return; /* already destroyed or never created */
    }
    builder->magic = 0u;
    if (builder->tokenizer) {
        robo_html_tokenizer_destroy(builder->tokenizer);
        builder->tokenizer = NULL;
    }
    if (builder->open) {
        robo_free(builder->open);
        builder->open = NULL;
    }
    /* Releasing the lease also detaches the document if it is still LOADING. */
    if (builder->lease) {
        robo_html_parser_destroy(builder->lease);
        builder->lease = NULL;
    }
    robo_html_diagnostics_clear(&builder->diagnostics);
    robo_free(builder);
}

/* ------------------------------------------------------------------ */
/* 8. Tree dump                                                        */
/* ------------------------------------------------------------------ */

#define DUMP_INDENT_MAX 24
#define DUMP_TEXT_BUDGET 72
/** Recursion bound: the dump is the one depth-sensitive consumer. */
#define DUMP_DEPTH_MAX 64u

typedef struct dump_writer {
    robo_html_tree_sink_fn sink;
    void *user;
    size_t emitted;
} dump_writer_t;

static robo_result_t dump_write(dump_writer_t *w, const char *text, size_t length)
{
    if (length == 0u) {
        return ROBO_OK;
    }
    return w->sink(text, length, w->user);
}

static robo_result_t dump_str(dump_writer_t *w, const char *text)
{
    return dump_write(w, text, text ? strlen(text) : 0u);
}

static robo_result_t dump_indent(dump_writer_t *w, size_t depth)
{
    char spaces[DUMP_INDENT_MAX * 2u + 1u];
    size_t count = depth * 2u;
    robo_result_t r;

    if (count > DUMP_INDENT_MAX * 2u) {
        count = DUMP_INDENT_MAX * 2u;
    }
    memset(spaces, ' ', count);
    r = dump_write(w, spaces, count);
    if (r != ROBO_OK) {
        return r;
    }
    return ROBO_OK;
}

/** Quoted, escaped payload with a byte budget so one node cannot flood. */
static robo_result_t dump_quoted(dump_writer_t *w, const char *bytes,
                                 size_t length)
{
    size_t i;
    size_t shown = (length > DUMP_TEXT_BUDGET) ? DUMP_TEXT_BUDGET : length;
    robo_result_t r;

    r = dump_write(w, "\"", 1u);
    if (r != ROBO_OK) {
        return r;
    }
    for (i = 0u; i < shown; i++) {
        char c = bytes[i];

        if (c == '"' || c == '\\') {
            char pair[2];
            pair[0] = '\\';
            pair[1] = c;
            r = dump_write(w, pair, 2u);
        } else if (c == '\n') {
            r = dump_str(w, "\\n");
        } else if (c == '\r') {
            r = dump_str(w, "\\r");
        } else if (c == '\t') {
            r = dump_str(w, "\\t");
        } else {
            r = dump_write(w, &c, 1u);
        }
        if (r != ROBO_OK) {
            return r;
        }
    }
    r = dump_write(w, "\"", 1u);
    if (r != ROBO_OK) {
        return r;
    }
    if (shown < length) {
        return dump_str(w, "...");
    }
    return ROBO_OK;
}

static robo_result_t dump_one(robo_html_document_slot_t *slot,
                              robo_html_document_id_t document_id,
                              robo_html_node_id_t id,
                              size_t depth,
                              dump_writer_t *w);

static robo_result_t dump_children(robo_html_document_slot_t *slot,
                                   robo_html_document_id_t document_id,
                                   robo_html_node_id_t first,
                                   size_t depth,
                                   dump_writer_t *w)
{
    robo_html_node_id_t child = first;
    size_t guard = 0u;

    if (depth >= DUMP_DEPTH_MAX && first != ROBO_HTML_NODE_ID_NONE) {
        /* Deeper subtrees are elided: the dump stays bounded, never fatal. */
        robo_result_t r = dump_indent(w, depth);

        if (r != ROBO_OK) {
            return r;
        }
        return dump_str(w, "... (deeper nodes elided)\n");
    }
    while (child != ROBO_HTML_NODE_ID_NONE &&
           guard < ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        const robo_html_node_entry_t *e =
            robo_html_node_entry_get(document_id, child);
        robo_html_node_id_t next;

        if (!e) {
            return ROBO_OK; /* corrupted link: stop rather than spin */
        }
        next = e->next_sibling;
        {
            robo_result_t r = dump_one(slot, document_id, child, depth, w);

            if (r != ROBO_OK) {
                return r;
            }
        }
        child = next;
        guard++;
    }
    if (guard >= ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        return dump_str(w, "  (sibling cycle truncated)\n");
    }
    return ROBO_OK;
}

static robo_result_t dump_one(robo_html_document_slot_t *slot,
                              robo_html_document_id_t document_id,
                              robo_html_node_id_t id,
                              size_t depth,
                              dump_writer_t *w)
{
    const robo_html_node_entry_t *e =
        robo_html_node_entry_get(document_id, id);
    robo_result_t r;

    if (!e) {
        return ROBO_NOT_FOUND;
    }
    if (w->emitted++ >= ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        return ROBO_OK;
    }
    r = dump_indent(w, depth);
    if (r != ROBO_OK) {
        return r;
    }
    switch (e->kind) {
    case ROBO_HTML_NODE_KIND_DOCUMENT:
        r = dump_str(w, "Document");
        break;
    case ROBO_HTML_NODE_KIND_DOCUMENT_TYPE: {
        const char *name = "";
        const char *pub = "";
        const char *sys = "";

        robo_html_payload_doctype(slot, e, &name, &pub, &sys);
        r = dump_str(w, "doctype ");
        if (r == ROBO_OK) {
            r = dump_str(w, name);
        }
        if (r == ROBO_OK && pub[0] != '\0') {
            r = dump_str(w, " public ");
            if (r == ROBO_OK) {
                r = dump_quoted(w, pub, strlen(pub));
            }
        }
        if (r == ROBO_OK && sys[0] != '\0') {
            r = dump_str(w, " system ");
            if (r == ROBO_OK) {
                r = dump_quoted(w, sys, strlen(sys));
            }
        }
        break;
    }
    case ROBO_HTML_NODE_KIND_ELEMENT: {
        size_t name_length = 0u;
        const char *name = robo_html_payload_element_name(slot, e,
                                                          &name_length);
        size_t attrs = robo_html_payload_attr_count(slot, e);
        size_t i;

        r = dump_write(w, name, name_length);
        for (i = 0u; r == ROBO_OK && i < attrs; i++) {
            const char *an = "";
            const char *av = "";

            if (robo_html_payload_attr_at(slot, e, i, &an, &av) != ROBO_OK) {
                break;
            }
            r = dump_str(w, " ");
            if (r == ROBO_OK) {
                r = dump_str(w, an);
            }
            if (r == ROBO_OK) {
                r = dump_str(w, "=");
            }
            if (r == ROBO_OK) {
                r = dump_quoted(w, av, strlen(av));
            }
        }
        if (r == ROBO_OK && robo_html_payload_self_closing(slot, e)) {
            r = dump_str(w, " /");
        }
        break;
    }
    case ROBO_HTML_NODE_KIND_TEXT: {
        size_t length = 0u;
        const char *text = robo_html_payload_text(slot, e, &length);

        r = dump_quoted(w, text, length);
        break;
    }
    case ROBO_HTML_NODE_KIND_COMMENT: {
        size_t length = 0u;
        const char *text = robo_html_payload_text(slot, e, &length);

        r = dump_str(w, "#comment ");
        if (r == ROBO_OK) {
            r = dump_quoted(w, text, length);
        }
        break;
    }
    case ROBO_HTML_NODE_KIND_FRAGMENT:
    default:
        r = dump_str(w, "fragment");
        break;
    }
    if (r != ROBO_OK) {
        return r;
    }
    r = dump_str(w, "\n");
    if (r != ROBO_OK) {
        return r;
    }
    return dump_children(slot, document_id, e->first_child, depth + 1u, w);
}

robo_result_t robo_html_tree_dump(robo_html_document_id_t document_id,
                                  robo_html_tree_sink_fn sink,
                                  void *user,
                                  robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);
    dump_writer_t w;

    if (!sink) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT, "sink is NULL");
    }
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    w.sink = sink;
    w.user = user;
    w.emitted = 0u;
    return dump_one(slot, document_id, slot->root.node_id, 0u, &w);
}

/* --- buffer sink ---------------------------------------------------- */

typedef struct buffer_sink {
    char *out;
    size_t cap;
    size_t used;
} buffer_sink_t;

static robo_result_t buffer_sink_write(const char *text,
                                       size_t length,
                                       void *user)
{
    buffer_sink_t *b = (buffer_sink_t *)user;

    if (!b || length == 0u) {
        return ROBO_OK;
    }
    if (b->used + length < b->cap) {
        memcpy(b->out + b->used, text, length);
    } else if (b->used < b->cap) {
        memcpy(b->out + b->used, text, b->cap - b->used - 1u);
    }
    b->used += length;
    return ROBO_OK;
}

robo_result_t robo_html_tree_dump_to_buffer(robo_html_document_id_t document_id,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_length,
                                            robo_html_diag_t *diag)
{
    buffer_sink_t b;
    robo_result_t r;

    if (!out || out_cap == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_NODE,
                              ROBO_INVALID_ARGUMENT,
                              "output buffer is NULL or empty");
    }
    out[0] = '\0';
    b.out = out;
    b.cap = out_cap;
    b.used = 0u;
    r = robo_html_tree_dump(document_id, buffer_sink_write, &b, diag);
    if (out_length) {
        *out_length = b.used;
    }
    if (r != ROBO_OK) {
        return r;
    }
    if (b.used < out_cap) {
        out[b.used] = '\0';
    } else {
        out[out_cap - 1u] = '\0';
    }
    return ROBO_OK;
}
