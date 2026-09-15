/*
 * RoboHTML tree builder (Phase 8.3).
 *
 * The tree builder is the first real implementation of the Phase 8.1 parser
 * boundary: it leases the document, owns a tokenizer, consumes the 8.2 token
 * stream, and constructs the document tree through document-scoped node
 * registration. Nothing above it needs to know how tokens map to nodes.
 *
 *   RoboHTML source (8.1)
 *        │
 *        ▼
 *   RoboHTML tokenizer (8.2)  ── tokens ──▶  tree builder (8.3)
 *        │                                        │
 *        │                                        ▼
 *        └───────────────────────────  document tree (nodes + payload arena)
 *
 * WHAT THIS PHASE IS
 *   - Real nodes: Document, DocumentType, Element, Text, Comment.
 *   - Real parent/child/sibling links with document ownership.
 *   - An explicit open-element stack and insertion-mode machine.
 *   - Bounded, non-fatal recovery for malformed markup.
 *   - RCDATA/RAWTEXT/SCRIPT/PLAINTEXT element content, driven through the
 *     tokenizer's raw-content entry point.
 *   - Tree dump for debugging and tests.
 *
 * WHAT THIS PHASE IS NOT
 *   - Not the HTML5 tree construction algorithm in full. Deliberately
 *     absent, with the seam kept open:
 *       * active formatting elements / adoption agency (no implied <b>/<i>
 *         reconstruction);
 *       * table insertion modes and foster parenting;
 *       * foreign content (SVG/MathML) integration points;
 *       * fragment parsing entry point (the design does not forbid it, but
 *         no fragment mode is implemented);
 *       * document-mode/quirks behaviour from DOCTYPE.
 *   - Not a DOM: no attribute mutation, no DOM events, no traversal APIs
 *     beyond the read accessors in RoboHTMLNode.h / RoboHTMLElement.h.
 *   - Not CSS, layout, paint or JavaScript.
 *
 * Recovery policy
 *   Malformed input is never fatal. Recoverable problems (unexpected end
 *   tag, duplicate structural tag, orphan content, unclosed element at EOF)
 *   are recorded as diagnostics and the build continues. Only genuine
 *   failures return an error code: NULL/invalid arguments, an uninitialized
 *   module, an unusable lifecycle, or allocation failure.
 *
 * Ownership
 *   - The builder owns its tokenizer, open-element stack and diagnostics.
 *   - The document owns every node and every byte of payload.
 *   - Destroying the builder after a successful build leaves the tree in
 *     the document, intact and usable.
 *
 * See docs/TreeBuilder.md.
 */

#ifndef ROBO_HTML_TREE_BUILDER_H
#define ROBO_HTML_TREE_BUILDER_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Insertion modes                                                     */
/* ------------------------------------------------------------------ */

/**
 * Tree-construction insertion modes (the subset this phase implements).
 *
 * The sequence for a well-formed document is
 *   INITIAL → BEFORE_HTML → BEFORE_HEAD → IN_HEAD → AFTER_HEAD → IN_BODY
 * with AFTER_BODY / AFTER_AFTER_BODY covering trailing content and TEXT
 * covering RCDATA/RAWTEXT/SCRIPT/PLAINTEXT element bodies.
 */
typedef enum robo_html_insertion_mode {
    ROBO_HTML_MODE_INITIAL = 0,
    ROBO_HTML_MODE_BEFORE_HTML = 1,
    ROBO_HTML_MODE_BEFORE_HEAD = 2,
    ROBO_HTML_MODE_IN_HEAD = 3,
    ROBO_HTML_MODE_AFTER_HEAD = 4,
    ROBO_HTML_MODE_IN_BODY = 5,
    ROBO_HTML_MODE_AFTER_BODY = 6,
    ROBO_HTML_MODE_AFTER_AFTER_BODY = 7,
    /** Inside an RCDATA/RAWTEXT/SCRIPT/PLAINTEXT element's content. */
    ROBO_HTML_MODE_TEXT = 8
} robo_html_insertion_mode_t;

/** Non-zero if @p mode is a defined insertion mode enumerator. */
int robo_html_insertion_mode_is_known(robo_html_insertion_mode_t mode);

/** Human-readable insertion mode name; never NULL. */
const char *robo_html_insertion_mode_name(robo_html_insertion_mode_t mode);

/**
 * Content family of the element currently being consumed in TEXT mode.
 *
 * RCDATA decodes character references (<title>, <textarea>); RAWTEXT and
 * SCRIPT do not (<style>, <script>, <xmp>, <iframe>, <noembed>); PLAINTEXT
 * never ends (<plaintext>).
 */
typedef enum robo_html_text_mode {
    ROBO_HTML_TEXT_MODE_NONE = 0,
    ROBO_HTML_TEXT_MODE_RCDATA = 1,
    ROBO_HTML_TEXT_MODE_RAWTEXT = 2,
    ROBO_HTML_TEXT_MODE_SCRIPT = 3,
    ROBO_HTML_TEXT_MODE_PLAINTEXT = 4
} robo_html_text_mode_t;

/** Human-readable text-mode name; never NULL. */
const char *robo_html_text_mode_name(robo_html_text_mode_t mode);

/** Non-zero when @p mode is a defined text-mode enumerator. */
int robo_html_text_mode_is_known(robo_html_text_mode_t mode);

/* ------------------------------------------------------------------ */
/* Build result                                                        */
/* ------------------------------------------------------------------ */

/** What one completed build produced (diagnostics + tests). */
typedef struct robo_html_tree_result {
    size_t node_count;    /* all live nodes, including the document node */
    size_t element_count;
    size_t text_count;
    size_t comment_count;
    size_t doctype_count;
    size_t attribute_count; /* attributes stored across all elements */
    size_t max_depth;       /* deepest element nesting reached */
    /** Recoverable parse errors recorded (bounded; see MAX_DIAGNOSTICS). */
    size_t recovered_errors;
    /** Non-zero when the node ceiling was hit before the input ended. */
    int truncated;
} robo_html_tree_result_t;

/** Upper bound on retained tree-builder diagnostics (bounded memory). */
#define ROBO_HTML_TREE_BUILDER_MAX_DIAGNOSTICS 64u

/* ------------------------------------------------------------------ */
/* Tree builder                                                        */
/* ------------------------------------------------------------------ */

/** Opaque tree builder (layout private to RoboHTML sources). */
typedef struct robo_html_tree_builder robo_html_tree_builder_t;

/**
 * Create a tree builder leased to @p document_id.
 *
 * The builder takes the document's exclusive parser lease, so a second
 * builder — or a live robo_html_parser_t — on the same document is rejected
 * with ROBO_BUSY. The document must accept content (CREATED, LOADING or
 * READY); exclusivity comes from the lease, not from the lifecycle, and
 * begin() settles a LOADING document before attaching its own source.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT; ROBO_INIT_ERROR (module down);
 *         ROBO_NOT_FOUND (unknown document); ROBO_BUSY (already leased);
 *         ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_tree_builder_create(
    robo_html_document_id_t document_id,
    robo_html_tree_builder_t **out,
    robo_html_diag_t *diag);

/**
 * Attach @p src and prepare to build.
 *
 * A document that already holds a tree is cleared first (payload arena and
 * all nodes except a freshly registered root), so re-parsing is safe — but
 * every previously handed-out node handle becomes stale, which is why a
 * caller must do it deliberately.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT; ROBO_ERROR (wrong lifecycle —
 *         the builder must be IDLE or COMPLETE); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_tree_builder_begin(robo_html_tree_builder_t *builder,
                                           const robo_html_source_t *src,
                                           robo_html_diag_t *diag);

/**
 * Pull one token from the attached tokenizer and consume it. Consuming the
 * EOF token finishes the build, so one call past the last real token closes
 * every open element and marks the build complete. Repeated calls after
 * completion are a no-op returning ROBO_OK.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL builder / no source);
 *         ROBO_OUT_OF_MEMORY (allocation failure, not the node ceiling —
 *         that truncates the tree and still reports ROBO_OK);
 *         ROBO_INIT_ERROR when the builder was not begun.
 */
robo_result_t robo_html_tree_builder_next(robo_html_tree_builder_t *builder,
                                          robo_html_diag_t *diag);

/**
 * Consume one externally supplied token.
 * The token is only read; tokenizer ownership of its buffers is unchanged.
 * Malformed content inside the token is tolerated.
 */
robo_result_t robo_html_tree_builder_consume(
    robo_html_tree_builder_t *builder,
    const robo_html_token_t *token,
    robo_html_diag_t *diag);

/**
 * Finalize the tree: close every still-open element and record the
 * "unclosed element" diagnostics, then mark the build complete.
 * Idempotent while COMPLETE. The document is left valid either way.
 */
robo_result_t robo_html_tree_builder_finish(robo_html_tree_builder_t *builder,
                                            robo_html_diag_t *diag);

/**
 * Convenience: begin + drive to EOF + finish.
 * Equivalent to the three calls above in sequence.
 */
robo_result_t robo_html_tree_builder_build(robo_html_tree_builder_t *builder,
                                           const robo_html_source_t *src,
                                           robo_html_diag_t *diag);

/**
 * One-shot: build @p document_id's tree from @p src with a temporary
 * builder. On success the document owns the tree and the builder is gone.
 * @p out_result optional; it is not written on failure.
 */
robo_result_t robo_html_build_tree(robo_html_document_id_t document_id,
                                   const robo_html_source_t *src,
                                   robo_html_tree_result_t *out_result,
                                   robo_html_diag_t *diag);

/** Copy the parser-boundary state of the build (see RoboHTMLParser.h). */
robo_result_t robo_html_tree_builder_get_state(
    const robo_html_tree_builder_t *builder,
    robo_html_parser_state_t *out_state);

/** Copy the current insertion mode. */
robo_result_t robo_html_tree_builder_get_mode(
    const robo_html_tree_builder_t *builder,
    robo_html_insertion_mode_t *out_mode);

/** Copy the current TEXT-mode content family (NONE outside TEXT mode). */
robo_result_t robo_html_tree_builder_get_text_mode(
    const robo_html_tree_builder_t *builder,
    robo_html_text_mode_t *out_mode);

/**
 * Enter a text family explicitly (the `switch` hook later phases need).
 * Valid while building. Sets the builder's text mode; the caller is
 * responsible for having switched the tokenizer correspondingly.
 */
robo_result_t robo_html_tree_builder_set_text_mode(
    robo_html_tree_builder_t *builder,
    robo_html_text_mode_t mode,
    robo_html_diag_t *diag);

/** Handle of the current node (top of the open-element stack). */
robo_result_t robo_html_tree_builder_current_node(
    const robo_html_tree_builder_t *builder,
    robo_html_node_handle_t *out);

/** Number of entries on the open-element stack. */
robo_result_t robo_html_tree_builder_open_element_count(
    const robo_html_tree_builder_t *builder,
    size_t *out_count);

/**
 * Handle of the open element at @p index, where index 0 is the outermost
 * open element (document order) and count - 1 is the current node.
 * Out of range → ROBO_NOT_FOUND.
 */
robo_result_t robo_html_tree_builder_open_element(
    const robo_html_tree_builder_t *builder,
    size_t index,
    robo_html_node_handle_t *out);

/** Copy the build counters/last result. */
robo_result_t robo_html_tree_builder_result(
    const robo_html_tree_builder_t *builder,
    robo_html_tree_result_t *out);

/** Recoverable parse errors recorded so far (bounded). */
size_t robo_html_tree_builder_diagnostic_count(
    const robo_html_tree_builder_t *builder);

/** Copy diagnostic @p index; out of range → ROBO_NOT_FOUND. */
robo_result_t robo_html_tree_builder_get_diagnostic(
    const robo_html_tree_builder_t *builder,
    size_t index,
    robo_html_diag_t *out_diag);

/**
 * Destroy the builder: releases the tokenizer, the open-element stack and
 * the lease. The tree in the document is untouched.
 */
void robo_html_tree_builder_destroy(robo_html_tree_builder_t *builder);

/* ------------------------------------------------------------------ */
/* Tree dump (debugging)                                               */
/* ------------------------------------------------------------------ */

/**
 * Sink for dump text. Returning anything but ROBO_OK aborts the dump and
 * that code becomes the return value of the dump call.
 */
typedef robo_result_t (*robo_html_tree_sink_fn)(const char *text,
                                                size_t length,
                                                void *user);

/**
 * Write an indented dump of @p document_id's tree to @p sink:
 *
 *   Document
 *     doctype html
 *     html
 *       head
 *         title
 *           "RoboticelaOS Runtime"
 *       body
 *         h1
 *           "..."
 *
 * Attributes are printed after the element name; text and comment payloads
 * are quoted and truncated with `...` past a fixed budget so a single huge
 * text node cannot flood the sink. Bounded iterations: a corrupted link
 * cycle terminates instead of hanging.
 */
robo_result_t robo_html_tree_dump(robo_html_document_id_t document_id,
                                  robo_html_tree_sink_fn sink,
                                  void *user,
                                  robo_html_diag_t *diag);

/**
 * Same dump into a caller buffer (NUL-terminated, safely truncated).
 * @p out_length receives the untruncated length so callers can detect
 * truncation. @p out_length optional.
 */
robo_result_t robo_html_tree_dump_to_buffer(robo_html_document_id_t document_id,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_length,
                                            robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TREE_BUILDER_H */
