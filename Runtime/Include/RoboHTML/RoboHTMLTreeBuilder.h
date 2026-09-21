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
typedef enum HtmlInsertionMode {
    RoboHtmlModeInitial = 0,
    RoboHtmlModeBeforeHtml = 1,
    RoboHtmlModeBeforeHead = 2,
    RoboHtmlModeInHead = 3,
    RoboHtmlModeAfterHead = 4,
    RoboHtmlModeInBody = 5,
    RoboHtmlModeAfterBody = 6,
    RoboHtmlModeAfterAfterBody = 7,
    /** Inside an RCDATA/RAWTEXT/SCRIPT/PLAINTEXT element's content. */
    RoboHtmlModeText = 8
} HtmlInsertionMode;

/** Non-zero if @p mode is a defined insertion mode enumerator. */
int HtmlInsertionModeIsKnown(HtmlInsertionMode mode);

/** Human-readable insertion mode name; never NULL. */
const char *HtmlInsertionModeName(HtmlInsertionMode mode);

/**
 * Content family of the element currently being consumed in TEXT mode.
 *
 * RCDATA decodes character references (<title>, <textarea>); RAWTEXT and
 * SCRIPT do not (<style>, <script>, <xmp>, <iframe>, <noembed>); PLAINTEXT
 * never ends (<plaintext>).
 */
typedef enum HtmlTextMode {
    RoboHtmlTextModeNone = 0,
    RoboHtmlTextModeRcdata = 1,
    RoboHtmlTextModeRawtext = 2,
    RoboHtmlTextModeScript = 3,
    RoboHtmlTextModePlaintext = 4
} HtmlTextMode;

/** Human-readable text-mode name; never NULL. */
const char *HtmlTextModeName(HtmlTextMode mode);

/** Non-zero when @p mode is a defined text-mode enumerator. */
int HtmlTextModeIsKnown(HtmlTextMode mode);

/* ------------------------------------------------------------------ */
/* Build result                                                        */
/* ------------------------------------------------------------------ */

/** What one completed build produced (diagnostics + tests). */
typedef struct HtmlTreeResult {
    size_t nodeCount;    /* all live nodes, including the document node */
    size_t elementCount;
    size_t textCount;
    size_t commentCount;
    size_t doctypeCount;
    size_t attributeCount; /* attributes stored across all elements */
    size_t maxDepth;       /* deepest element nesting reached */
    /** Recoverable parse errors recorded (bounded; see MAX_DIAGNOSTICS). */
    size_t recoveredErrors;
    /** Non-zero when the node ceiling was hit before the input ended. */
    int truncated;
} HtmlTreeResult;

/** Upper bound on retained tree-builder diagnostics (bounded memory). */
#define RoboHtmlTreeBuilderMaxDiagnostics 64u

/* ------------------------------------------------------------------ */
/* Tree builder                                                        */
/* ------------------------------------------------------------------ */

/** Opaque tree builder (layout private to RoboHTML sources). */
typedef struct HtmlTreeBuilder HtmlTreeBuilder;

/**
 * Create a tree builder leased to @p documentId.
 *
 * The builder takes the document's exclusive parser lease, so a second
 * builder — or a live HtmlParser — on the same document is rejected
 * with RoboBusy. The document must accept content (CREATED, LOADING or
 * READY); exclusivity comes from the lease, not from the lifecycle, and
 * begin() settles a LOADING document before attaching its own source.
 *
 * @return RoboOk; RoboInvalidArgument; RoboInitError (module down);
 *         RoboNotFound (unknown document); RoboBusy (already leased);
 *         RoboOutOfMemory.
 */
Result HtmlTreeBuilderCreate(
    HtmlDocumentId documentId,
    HtmlTreeBuilder **out,
    HtmlDiag *diag);

/**
 * Attach @p src and prepare to build.
 *
 * A document that already holds a tree is cleared first (payload arena and
 * all nodes except a freshly registered root), so re-parsing is safe — but
 * every previously handed-out node handle becomes stale, which is why a
 * caller must do it deliberately.
 *
 * @return RoboOk; RoboInvalidArgument; RoboError (wrong lifecycle —
 *         the builder must be IDLE or COMPLETE); RoboOutOfMemory.
 */
Result HtmlTreeBuilderBegin(HtmlTreeBuilder *builder,
                                           const HtmlSource *src,
                                           HtmlDiag *diag);

/**
 * Pull one token from the attached tokenizer and consume it. Consuming the
 * EOF token finishes the build, so one call past the last real token closes
 * every open element and marks the build complete. Repeated calls after
 * completion are a no-op returning RoboOk.
 * @return RoboOk; RoboInvalidArgument (NULL builder / no source);
 *         RoboOutOfMemory (allocation failure, not the node ceiling —
 *         that truncates the tree and still reports RoboOk);
 *         RoboInitError when the builder was not begun.
 */
Result HtmlTreeBuilderNext(HtmlTreeBuilder *builder,
                                          HtmlDiag *diag);

/**
 * Consume one externally supplied token.
 * The token is only read; tokenizer ownership of its buffers is unchanged.
 * Malformed content inside the token is tolerated.
 */
Result HtmlTreeBuilderConsume(
    HtmlTreeBuilder *builder,
    const HtmlToken *token,
    HtmlDiag *diag);

/**
 * Finalize the tree: close every still-open element and record the
 * "unclosed element" diagnostics, then mark the build complete.
 * Idempotent while COMPLETE. The document is left valid either way.
 */
Result HtmlTreeBuilderFinish(HtmlTreeBuilder *builder,
                                            HtmlDiag *diag);

/**
 * Convenience: begin + drive to EOF + finish.
 * Equivalent to the three calls above in sequence.
 */
Result HtmlTreeBuilderBuild(HtmlTreeBuilder *builder,
                                           const HtmlSource *src,
                                           HtmlDiag *diag);

/**
 * One-shot: build @p documentId's tree from @p src with a temporary
 * builder. On success the document owns the tree and the builder is gone.
 * @p outResult optional; it is not written on failure.
 */
Result HtmlBuildTree(HtmlDocumentId documentId,
                                   const HtmlSource *src,
                                   HtmlTreeResult *outResult,
                                   HtmlDiag *diag);

/** Copy the parser-boundary state of the build (see RoboHTMLParser.h). */
Result HtmlTreeBuilderGetState(
    const HtmlTreeBuilder *builder,
    HtmlParserState *outState);

/** Copy the current insertion mode. */
Result HtmlTreeBuilderGetMode(
    const HtmlTreeBuilder *builder,
    HtmlInsertionMode *outMode);

/** Copy the current TEXT-mode content family (NONE outside TEXT mode). */
Result HtmlTreeBuilderGetTextMode(
    const HtmlTreeBuilder *builder,
    HtmlTextMode *outMode);

/**
 * Enter a text family explicitly (the `switch` hook later phases need).
 * Valid while building. Sets the builder's text mode; the caller is
 * responsible for having switched the tokenizer correspondingly.
 */
Result HtmlTreeBuilderSetTextMode(
    HtmlTreeBuilder *builder,
    HtmlTextMode mode,
    HtmlDiag *diag);

/** Handle of the current node (top of the open-element stack). */
Result HtmlTreeBuilderCurrentNode(
    const HtmlTreeBuilder *builder,
    HtmlNodeHandle *out);

/** Number of entries on the open-element stack. */
Result HtmlTreeBuilderOpenElementCount(
    const HtmlTreeBuilder *builder,
    size_t *outCount);

/**
 * Handle of the open element at @p index, where index 0 is the outermost
 * open element (document order) and count - 1 is the current node.
 * Out of range → RoboNotFound.
 */
Result HtmlTreeBuilderOpenElement(
    const HtmlTreeBuilder *builder,
    size_t index,
    HtmlNodeHandle *out);

/** Copy the build counters/last result. */
Result HtmlTreeBuilderResult(
    const HtmlTreeBuilder *builder,
    HtmlTreeResult *out);

/** Recoverable parse errors recorded so far (bounded). */
size_t HtmlTreeBuilderDiagnosticCount(
    const HtmlTreeBuilder *builder);

/** Copy diagnostic @p index; out of range → RoboNotFound. */
Result HtmlTreeBuilderGetDiagnostic(
    const HtmlTreeBuilder *builder,
    size_t index,
    HtmlDiag *outDiag);

/**
 * Destroy the builder: releases the tokenizer, the open-element stack and
 * the lease. The tree in the document is untouched.
 */
void HtmlTreeBuilderDestroy(HtmlTreeBuilder *builder);

/* ------------------------------------------------------------------ */
/* Tree dump (debugging)                                               */
/* ------------------------------------------------------------------ */

/**
 * Sink for dump text. Returning anything but RoboOk aborts the dump and
 * that code becomes the return value of the dump call.
 */
typedef Result (*HtmlTreeSinkFn)(const char *text,
                                                size_t length,
                                                void *user);

/**
 * Write an indented dump of @p documentId's tree to @p sink:
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
 *
 * Element names are printed bare in the HTML namespace and prefixed
 * (`svg:`, `mathml:`) otherwise — same local name, different element.
 */
Result HtmlTreeDump(HtmlDocumentId documentId,
                                  HtmlTreeSinkFn sink,
                                  void *user,
                                  HtmlDiag *diag);

/**
 * Same dump into a caller buffer (NUL-terminated, safely truncated).
 * @p outLength receives the untruncated length so callers can detect
 * truncation. @p outLength optional.
 */
Result HtmlTreeDumpToBuffer(HtmlDocumentId documentId,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLength,
                                            HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TREE_BUILDER_H */
