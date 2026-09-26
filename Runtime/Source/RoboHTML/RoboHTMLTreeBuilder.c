/*
 * RoboHTML tree builder (Phase 8.3) — see RoboHTMLTreeBuilder.h.
 *
 * File shape:
 *   1. parser tables (text family, head content, <p>-closing start tags)
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
 * back beyond the ordinary LOADING → INTERACTIVE → COMPLETE settling a parse
 * implies (Phase 8.10), and every node it creates is owned by the document
 * (it holds ids, never pointers into the registry).
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTML/RoboHTMLCustomElements.h"
#include "RoboHTML/RoboHTMLTemplate.h"

#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

#define TREE_BUILDER_MAGIC 0x52425442u /* "RBTB" */

/** Initial open-element stack capacity (grows geometrically). */
#define OPEN_STACK_INITIAL 16u

/** Attribute slots staged per start tag (beyond this, extras are dropped). */
#define MAX_STAGED_ATTRS RoboHtmlMaxAttrsPerElement

/** Cap on mode re-dispatch per token; a guard, not a policy. */
#define REPROCESS_LIMIT 24u

/* ------------------------------------------------------------------ */
/* 1. Parser tables                                                    */
/* ------------------------------------------------------------------ */

/*
 * The void-element list and the heading test that used to live here are gone:
 * both were element-type facts, and the type vocabulary in RoboHTMLTag.c now
 * owns them (Phase 8.11). What remains are the tables that are genuinely
 * parser policy rather than identity.
 */

/** Content family of a text element; NONE when the element is ordinary. */
static HtmlTextMode TextModeForElement(const char *name)
{
    if (!name) {
        return RoboHtmlTextModeNone;
    }
    if (strcmp(name, "title") == 0 || strcmp(name, "textarea") == 0) {
        return RoboHtmlTextModeRcdata;
    }
    if (strcmp(name, "style") == 0 || strcmp(name, "xmp") == 0 ||
        strcmp(name, "iframe") == 0 || strcmp(name, "noembed") == 0 ||
        strcmp(name, "noframes") == 0) {
        return RoboHtmlTextModeRawtext;
    }
    if (strcmp(name, "script") == 0) {
        return RoboHtmlTextModeScript;
    }
    if (strcmp(name, "plaintext") == 0) {
        return RoboHtmlTextModePlaintext;
    }
    return RoboHtmlTextModeNone;
}

/** Tokenizer entry state for a text family (DATA when not a text family). */
static HtmlTokenizerState TokenizerStateForTextMode(
    HtmlTextMode mode)
{
    switch (mode) {
    case RoboHtmlTextModeRcdata:
        return RoboHtmlTokenizerStateRcdata;
    case RoboHtmlTextModeRawtext:
        return RoboHtmlTokenizerStateRawtext;
    case RoboHtmlTextModeScript:
        return RoboHtmlTokenizerStateScriptData;
    case RoboHtmlTextModePlaintext:
        return RoboHtmlTokenizerStatePlaintext;
    case RoboHtmlTextModeNone:
    default:
        return RoboHtmlTokenizerStateData;
    }
}

/** Content of <head> (or an element treated as head content) in body mode. */
static int IsHeadContentElement(const char *name)
{
    static const char *const kHeadElements[] = {
        "base", "basefont", "bgsound", "link", "meta", "title",
        "style", "script", "noscript", "template",
    };
    size_t i;

    if (!name) {
        return 0;
    }
    for (i = 0u; i < sizeof(kHeadElements) / sizeof(kHeadElements[0]); i++) {
        if (strcmp(name, kHeadElements[i]) == 0) {
            return 1;
        }
    }
    return 0;
}/**
 * Non-zero when @p name is one of the heading elements h1..h6.
 *
 * "Heading" is a fact about the element type, so the tag vocabulary owns it
 * (Phase 8.11) and this reads it rather than re-deriving h1..h6 from the
 * spelling. The parser's own tables stay below: those are parse *policy* with
 * contextual entries — which start tags close an open <p>, which elements may
 * sit in <head> — and identity is not the right place for parser policy.
 */
static int IsHeadingName(const char *name)
{
    return HtmlTagIsHeading(HtmlTagFromName(
        RoboHtmlNamespaceHtml, name));
}

/**
 * Non-zero for elements that are inserted and popped without children.
 *
 * Void-ness is an intrinsic property of the element type, so it is defined in
 * the tag vocabulary and read from there. This used to be a private list of
 * eighteen names, which meant "is <br> void?" had two answers in the runtime
 * — the parser's copy and any consumer's — that could drift apart. There is
 * now one answer, and it is the same one the DOM and its tests use.
 */
static int IsVoidElement(const char *name)
{
    return HtmlTagIsVoid(
        HtmlTagFromName(RoboHtmlNamespaceHtml, name));
}


/** Non-zero when @p name is a start tag that closes an open <p> in scope. */
static int IsPClosingStartTag(const char *name)
{
    static const char *const kPClosers[] = {
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
    for (i = 0u; i < sizeof(kPClosers) / sizeof(kPClosers[0]); i++) {
        if (strcmp(name, kPClosers[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 2. Builder state                                                    */
/* ------------------------------------------------------------------ */

struct HtmlTreeBuilder {
    uint32_t magic;
    HtmlDocumentId documentId;

    /** Exclusive 8.1 parser lease (the document records it; see Parser.h). */
    HtmlParser *lease;
    HtmlTokenizer *tokenizer;

    /** Copy of the attached source view (borrowed bytes; no heap). */
    HtmlSource source;
    int hasSource;

    HtmlInsertionMode mode;
    HtmlInsertionMode textReturnMode;
    HtmlTextMode textMode;
    HtmlParserState state;

    /** Open-element stack (node ids, outermost first). */
    HtmlNodeId *open;
    size_t openCount;
    size_t openCapacity;

    /* Structural nodes (stable for the duration of one build). */
    HtmlNodeHandle root;
    HtmlNodeHandle html;
    HtmlNodeHandle head;
    HtmlNodeHandle body;

    /* Trailing text run, so adjacent character tokens merge into one node. */
    HtmlNodeId textNode;
    HtmlNodeId textParent;

    int sawDoctype;
    int skipNextNewline;
    int complete;
    int stop;

    HtmlTreeResult result;
    HtmlDiagList diagnostics;
    size_t droppedDiagnostics;
};

/* --- small predicates ---------------------------------------------- */

static int HandleIsLive(const HtmlNodeHandle *h)
{
    return HtmlNodeHandleIsWellformed(h) &&
           !HtmlNodeHandleIsStale(h);
}

static HtmlDocumentSlot *BuilderSlot(
    const HtmlTreeBuilder *builder)
{
    if (!builder) {
        return NULL;
    }
    return HtmlDocumentSlotFind(builder->documentId);
}

/** Record a recoverable parse error (bounded list; never fatal). */
static void RecordRecovery(HtmlTreeBuilder *builder,
                            Result code,
                            const char *message,
                            uint32_t line,
                            uint32_t column)
{
    if (!builder) {
        return;
    }
    if (builder->diagnostics.count >= RoboHtmlTreeBuilderMaxDiagnostics) {
        builder->droppedDiagnostics++;
        return;
    }
    HtmlDiagnosticsPush(&builder->diagnostics, code,
                               RoboHtmlDomainParser, message, line, column);
    builder->result.recoveredErrors++;
}

/** Record a recoverable parse error at @p tok's source position. */
static void RecordTokenRecovery(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  const char *message)
{
    uint32_t line = tok ? tok->pos.line : 0u;
    uint32_t column = tok ? tok->pos.column : 0u;

    RecordRecovery(builder, RoboParseError, message, line, column);
}

/** Mark the build truncated: the node ceiling was reached. */
static void MarkTruncated(HtmlTreeBuilder *builder)
{
    if (!builder || builder->result.truncated) {
        return;
    }
    builder->result.truncated = 1;
    builder->stop = 1;
    HtmlDiagnosticsPush(&builder->diagnostics, RoboOutOfMemory,
                               RoboHtmlDomainParser,
                               "node ceiling reached; tree truncated", 0u, 0u);
    LogWarn("robo_html: doc=%llu tree truncated at the node ceiling",
                  (unsigned long long)builder->documentId);
}

/* --- open-element stack -------------------------------------------- */

static Result StackReserve(HtmlTreeBuilder *builder,
                                   size_t extra,
                                   HtmlDiag *diag)
{
    size_t need;
    size_t next;
    HtmlNodeId *grown;

    if (!builder) {
        return RoboInvalidArgument;
    }
    if (extra > SIZE_MAX - builder->openCount) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboOutOfMemory,
                              "open-element stack size overflow");
    }
    need = builder->openCount + extra;
    if (need <= builder->openCapacity) {
        return RoboOk;
    }
    next = (builder->openCapacity == 0u) ? OPEN_STACK_INITIAL
                                          : builder->openCapacity;
    while (next < need) {
        if (next > (SIZE_MAX / 2u)) {
            next = need;
            break;
        }
        next *= 2u;
    }
    grown = (HtmlNodeId *)Realloc(builder->open,
                                                next * sizeof(*grown));
    if (!grown) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboOutOfMemory,
                              "open-element stack allocation failed");
    }
    builder->open = grown;
    builder->openCapacity = next;
    return RoboOk;
}

static Result StackPush(HtmlTreeBuilder *builder,
                                HtmlNodeId id,
                                HtmlDiag *diag)
{
    /*
     * No separate depth ceiling: the node registry already bounds how many
     * elements can exist, so this heap stack cannot grow without limit. The
     * only depth-sensitive consumer is the dump, which caps its recursion.
     */
    if (StackReserve(builder, 1u, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }
    builder->open[builder->openCount++] = id;
    if (builder->openCount > builder->result.maxDepth) {
        builder->result.maxDepth = builder->openCount;
    }
    return RoboOk;
}

static void StackPop(HtmlTreeBuilder *builder)
{
    if (builder && builder->openCount > 0u) {
        builder->openCount--;
    }
}

static HtmlNodeId StackTop(const HtmlTreeBuilder *builder)
{
    if (!builder || builder->openCount == 0u) {
        return RoboHtmlNodeIdNone;
    }
    return builder->open[builder->openCount - 1u];
}

/** Index of the nearest open element named @p name; -1 when absent. */
static long StackFind(const HtmlTreeBuilder *builder,
                       HtmlDocumentSlot *slot,
                       const char *name)
{
    size_t i;

    if (!builder || !slot || !name) {
        return -1;
    }
    for (i = builder->openCount; i > 0u; i--) {
        const HtmlNodeEntry *e =
            HtmlNodeEntryGet(builder->documentId, builder->open[i - 1u]);

        if (!e) {
            continue;
        }
        if (strcmp(HtmlPayloadElementName(slot, e, NULL), name) == 0) {
            return (long)(i - 1u);
        }
    }
    return -1;
}

/** Index of the nearest open element with kind/text family @p mode. */
static long StackFindTextFamily(const HtmlTreeBuilder *builder,
                                   HtmlDocumentSlot *slot)
{
    size_t i;

    if (!builder || !slot) {
        return -1;
    }
    for (i = builder->openCount; i > 0u; i--) {
        const HtmlNodeEntry *e =
            HtmlNodeEntryGet(builder->documentId, builder->open[i - 1u]);

        if (!e) {
            continue;
        }
        if (TextModeForElement(
                HtmlPayloadElementName(slot, e, NULL)) !=
            RoboHtmlTextModeNone) {
            return (long)(i - 1u);
        }
    }
    return -1;
}

/** Popping @p nodeId would leave the document (structural safety). */
static int IsStructural(const HtmlTreeBuilder *builder,
                         HtmlNodeId nodeId)
{
    return nodeId == builder->html.nodeId || nodeId == builder->head.nodeId ||
           nodeId == builder->body.nodeId;
}

/* ------------------------------------------------------------------ */
/* 3. Node and text creation                                           */
/* ------------------------------------------------------------------ */

static Result RegisterNode(HtmlTreeBuilder *builder,
                                   HtmlNodeKind kind,
                                   HtmlNodeHandle *out,
                                   HtmlDiag *diag)
{
    Result r = HtmlDocumentRegisterNode(
        builder->documentId, kind, RoboHtmlNodeOriginParser, out, diag);

    if (r == RoboOutOfMemory) {
        MarkTruncated(builder);
    }
    return r;
}

/**
 * Create an element as the last child of @p parentId.
 *
 * Phase 8.5 moved element creation behind HtmlElementCreate, so the
 * builder no longer registers, encodes and links by hand: it describes the
 * element and the element model enforces ownership, lifecycle, namespace
 * and the lowercase-name invariant in one place. Pushing onto the
 * open-element stack is still the caller's decision (void and self-closing
 * elements are never pushed).
 */
static Result CreateElement(HtmlTreeBuilder *builder,
                                    HtmlNodeId parentId,
                                    const char *name,
                                    const HtmlAttribute *attrs,
                                    size_t attrCount,
                                    int selfClosing,
                                    HtmlNodeHandle *outHandle,
                                    HtmlDiag *diag)
{
    const HtmlNodeEntry *parentEntry;
    HtmlElementCreateInfo info;
    Result r;

    parentEntry = HtmlNodeEntryGet(builder->documentId, parentId);
    if (!parentEntry) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "parent node does not resolve");
    }
    HtmlElementCreateInfoInit(&info);
    info.parent = HtmlNodeHandleMake(builder->documentId,
                                             parentEntry);
    info.origin = RoboHtmlNodeOriginParser;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = name;
    info.attributes = attrs;
    info.attributeCount = attrCount;
    info.selfClosing = selfClosing;

    r = HtmlElementCreate(builder->documentId, &info, outHandle, diag);
    if (r == RoboOutOfMemory) {
        MarkTruncated(builder);
    }
    return r;
}

/** Handle of @p parentId in the builder's document; 0 when it is not live. */
static int BuilderParentHandle(HtmlTreeBuilder *builder,
                                 HtmlNodeId parentId,
                                 HtmlNodeHandle *out)
{
    const HtmlNodeEntry *parentEntry =
        HtmlNodeEntryGet(builder->documentId, parentId);

    if (!parentEntry) {
        return 0;
    }
    *out = HtmlNodeHandleMake(builder->documentId, parentEntry);
    return 1;
}

/**
 * Create a text node carrying @p bytes as the last child of @p parentId.
 *
 * Phase 8.7 moved text creation behind HtmlTextCreate, so the builder
 * no longer registers, encodes and links a text node by hand: it describes
 * the run and the text model enforces document ownership, the
 * cross-document rule and the lifecycle gate in one place — the same shape
 * CreateElement already has for elements. Character data is copied into the
 * document's payload arena, so the tokenizer's buffer stays the tokenizer's.
 */
static Result CreateTextNode(HtmlTreeBuilder *builder,
                                      HtmlNodeId parentId,
                                      const char *bytes,
                                      size_t length,
                                      HtmlNodeHandle *outHandle,
                                      HtmlDiag *diag)
{
    HtmlTextCreateInfo info;
    HtmlNodeHandle h;
    Result r;

    HtmlTextCreateInfoInit(&info);
    if (!BuilderParentHandle(builder, parentId, &info.parent)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "text parent node does not resolve");
    }
    info.origin = RoboHtmlNodeOriginParser;
    info.content = bytes;
    info.length = length;

    r = HtmlTextCreate(builder->documentId, &info, &h, diag);
    if (r == RoboOutOfMemory) {
        MarkTruncated(builder);
    }
    if (r != RoboOk) {
        return r;
    }
    if (outHandle) {
        *outHandle = h;
    }
    return RoboOk;
}

/** Create a comment node carrying @p bytes (comments are not text nodes). */
static Result CreateCommentLeaf(HtmlTreeBuilder *builder,
                                         HtmlNodeId parentId,
                                         const char *bytes,
                                         size_t length,
                                         HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = BuilderSlot(builder);
    HtmlNodeHandle h;
    HtmlNodeEntry *entry;
    Result r;

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "document was destroyed during the build");
    }
    r = RegisterNode(builder, RoboHtmlNodeKindComment, &h, diag);
    if (r != RoboOk) {
        return r;
    }
    entry = HtmlNodeEntryMut(builder->documentId, h.nodeId);
    r = HtmlPayloadWriteText(slot, entry, bytes, length, diag);
    if (r != RoboOk) {
        return r;
    }
    return HtmlNodeLinkChild(builder->documentId, parentId, h.nodeId,
                                     diag);
}

/**
 * Insert characters under @p parentId, merging into the open text run.
 *
 * The tokenizer is free to hand character data over in as many pieces as it
 * likes (buffer boundaries, character references, split-up tokens), so the
 * builder — not the tokenizer — owns the decision of how many Text nodes a
 * run becomes. A run is open while textNode/textParent still name it; an
 * element, a comment, a mode change or a stack pop closes it, which is what
 * makes `<p>Hello <b>world</b>!</p>` the three children it should be rather
 * than one node per token.
 *
 * Adjacent characters within one run therefore always end up in the same
 * node. HtmlTextAppend guarantees that: it grows the run in place
 * while it still ends the arena (the parser's normal case, no copy) and
 * otherwise writes the extended run into a fresh payload.
 */
static Result InsertText(HtmlTreeBuilder *builder,
                                 HtmlNodeId parentId,
                                 const char *bytes,
                                 size_t length,
                                 HtmlDiag *diag)
{
    HtmlNodeHandle created;
    Result r;

    if (length == 0u || !bytes) {
        /* A zero-length run carries nothing, so no node is warranted. */
        return RoboOk;
    }
    if (!BuilderSlot(builder)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "document was destroyed during the build");
    }
    if (builder->textParent == parentId &&
        builder->textNode != RoboHtmlNodeIdNone) {
        const HtmlNodeEntry *entry =
            HtmlNodeEntryGet(builder->documentId, builder->textNode);

        if (entry && entry->kind == RoboHtmlNodeKindText) {
            HtmlNodeHandle run =
                HtmlNodeHandleMake(builder->documentId, entry);

            /* NULL diagnostics: a miss here is expected, not a failure. */
            if (HtmlTextAppend(&run, bytes, length, NULL) == RoboOk) {
                return RoboOk;
            }
        }
    }
    r = CreateTextNode(builder, parentId, bytes, length, &created, diag);
    if (r != RoboOk) {
        return r;
    }
    builder->textNode = created.nodeId;
    builder->textParent = parentId;
    return RoboOk;
}

/** Text insertion into the current node (top of the open-element stack). */
static Result InsertTextCurrent(HtmlTreeBuilder *builder,
                                         const char *bytes,
                                         size_t length,
                                         HtmlDiag *diag)
{
    HtmlNodeId current = StackTop(builder);

    if (current == RoboHtmlNodeIdNone) {
        current = builder->root.nodeId;
    }
    return InsertText(builder, current, bytes, length, diag);
}

/* --- structural elements ------------------------------------------- */

/**
 * Stage a token's attributes as the model's type (Phase 8.6).
 *
 * The staged values are the same `HtmlAttribute` an element stores,
 * differing only in lifetime: the tokenizer's buffers live until the next
 * token, the stored copy lives in the payload arena. One representation, so
 * nothing has to be translated between the parser and the tree.
 *
 * Source order and duplicates are passed through untouched: an HTML parser
 * does not fold repeated names, and the model does not invent a different
 * answer than the tokenizer gave.
 */
static Result StageAttrs(const HtmlToken *tok,
                                 HtmlAttribute *out,
                                 size_t *outCount,
                                 HtmlTreeBuilder *builder)
{
    size_t count = tok->u.tag.attrCount;
    size_t i;

    if (count > MAX_STAGED_ATTRS) {
        RecordTokenRecovery(builder, tok,
                             "too many attributes; extras dropped");
        count = MAX_STAGED_ATTRS;
    }
    for (i = 0u; i < count; i++) {
        const char *name = tok->u.tag.attrs[i].name;
        const char *value = tok->u.tag.attrs[i].value;

        out[i].name = name ? name : "";
        out[i].nameLength = strlen(out[i].name);
        out[i].value = value ? value : "";
        out[i].valueLength = strlen(out[i].value);
        out[i].namespaceId = RoboHtmlNamespaceNone;
    }
    *outCount = count;
    return RoboOk;
}

/**
 * Insert @p name under the current node, pushing it unless it is void or
 * self-closing. @p outId receives the new node id.
 */
static Result InsertElement(HtmlTreeBuilder *builder,
                                    const char *name,
                                    const HtmlAttribute *attrs,
                                    size_t attrCount,
                                    int selfClosing,
                                    HtmlNodeId *outId,
                                    HtmlDiag *diag)
{
    HtmlNodeHandle h;
    HtmlNodeId parent = StackTop(builder);
    Result r;

    if (parent == RoboHtmlNodeIdNone) {
        parent = builder->root.nodeId;
    }
    r = CreateElement(builder, parent, name, attrs, attrCount, selfClosing,
                       &h, diag);
    if (r != RoboOk) {
        return r;
    }
    if (outId) {
        *outId = h.nodeId;
    }
    /* A structural element never becomes the implicit text-merge target. */
    builder->textNode = RoboHtmlNodeIdNone;
    builder->textParent = RoboHtmlNodeIdNone;
    if (selfClosing || IsVoidElement(name)) {
        return RoboOk;
    }
    return StackPush(builder, h.nodeId, diag);
}

/** Create <html> as the document's root element (implied or explicit). */
static Result EnsureHtml(HtmlTreeBuilder *builder,
                                 const HtmlAttribute *attrs,
                                 size_t attrCount,
                                 HtmlDiag *diag)
{
    HtmlNodeHandle h;
    Result r;

    if (HandleIsLive(&builder->html)) {
        return RoboOk;
    }
    r = CreateElement(builder, builder->root.nodeId, "html", attrs,
                       attrCount, 0, &h, diag);
    if (r != RoboOk) {
        return r;
    }
    builder->html = h;
    return StackPush(builder, h.nodeId, diag);
}

static Result EnsureHead(HtmlTreeBuilder *builder,
                                 HtmlDiag *diag)
{
    HtmlNodeHandle h;
    Result r;
    HtmlNodeId parent;

    if (HandleIsLive(&builder->head)) {
        return RoboOk;
    }
    r = EnsureHtml(builder, NULL, 0u, diag);
    if (r != RoboOk) {
        return r;
    }
    parent = builder->html.nodeId;
    r = CreateElement(builder, parent, "head", NULL, 0u, 0, &h, diag);
    if (r != RoboOk) {
        return r;
    }
    builder->head = h;
    return StackPush(builder, h.nodeId, diag);
}

static Result EnsureBody(HtmlTreeBuilder *builder,
                                 HtmlDiag *diag)
{
    HtmlNodeHandle h;
    Result r;

    if (HandleIsLive(&builder->body)) {
        return RoboOk;
    }
    r = EnsureHtml(builder, NULL, 0u, diag);
    if (r != RoboOk) {
        return r;
    }
    r = CreateElement(builder, builder->html.nodeId, "body", NULL, 0u, 0, &h,
                       diag);
    if (r != RoboOk) {
        return r;
    }
    builder->body = h;
    return StackPush(builder, h.nodeId, diag);
}

/* ------------------------------------------------------------------ */
/* 4. Insertion-mode helpers                                           */
/* ------------------------------------------------------------------ */

/** Close an open <p> when @p name is a start tag that may not nest in one. */
static void ClosePIfOpen(HtmlTreeBuilder *builder,
                            HtmlDocumentSlot *slot,
                            const char *name)
{
    long index;

    if (!IsPClosingStartTag(name)) {
        return;
    }
    index = StackFind(builder, slot, "p");
    if (index < 0) {
        return;
    }
    while ((long)builder->openCount > index) {
        HtmlNodeId top = StackTop(builder);

        if (IsStructural(builder, top)) {
            break;
        }
        StackPop(builder);
    }
    if (builder->openCount > 0u) {
        StackPop(builder); /* the <p> itself */
    }
    builder->textNode = RoboHtmlNodeIdNone;
    builder->textParent = RoboHtmlNodeIdNone;
}

/** Close open elements up to (and including) the nearest @p name. */
static int CloseToElement(HtmlTreeBuilder *builder,
                            HtmlDocumentSlot *slot,
                            const char *name)
{
    long index = StackFind(builder, slot, name);

    if (index < 0) {
        return 0;
    }
    if (IsStructural(builder, builder->open[index])) {
        return 0;
    }
    while ((long)(builder->openCount - 1u) >= index) {
        StackPop(builder);
    }
    builder->textNode = RoboHtmlNodeIdNone;
    builder->textParent = RoboHtmlNodeIdNone;
    return 1;
}

/** Switch to a text family: push nothing, remember the return mode. */
static Result EnterTextMode(HtmlTreeBuilder *builder,
                                     HtmlTextMode mode,
                                     HtmlDiag *diag)
{
    Result r;

    builder->textReturnMode = builder->mode;
    builder->textMode = mode;
    builder->mode = RoboHtmlModeText;
    /* PLAINTEXT is documented by the tokenizer as never ending. */
    r = HtmlTokenizerSwitchState(builder->tokenizer,
                                         TokenizerStateForTextMode(mode),
                                         diag);
    if (r != RoboOk) {
        return r;
    }
    builder->state = RoboHtmlParserStateFeeding;
    return RoboOk;
}

static Result LeaveTextMode(HtmlTreeBuilder *builder,
                                     HtmlDiag *diag)
{
    Result r;

    builder->textMode = RoboHtmlTextModeNone;
    builder->mode = builder->textReturnMode;
    r = HtmlTokenizerSwitchState(builder->tokenizer,
                                         RoboHtmlTokenizerStateData, diag);
    return r;
}

/* ------------------------------------------------------------------ */
/* 5. Insertion modes                                                  */
/* ------------------------------------------------------------------ */

static Result ModeInitial(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag);
static Result ModeBeforeHtml(HtmlTreeBuilder *builder,
                                      const HtmlToken *tok,
                                      int *reprocess,
                                      HtmlDiag *diag);
static Result ModeBeforeHead(HtmlTreeBuilder *builder,
                                      const HtmlToken *tok,
                                      int *reprocess,
                                      HtmlDiag *diag);
static Result ModeInHead(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag);
static Result ModeAfterHead(HtmlTreeBuilder *builder,
                                     const HtmlToken *tok,
                                     int *reprocess,
                                     HtmlDiag *diag);
static Result ModeInBody(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag);
static Result ModeAfterBody(HtmlTreeBuilder *builder,
                                     const HtmlToken *tok,
                                     int *reprocess,
                                     HtmlDiag *diag);
static Result ModeAfterAfterBody(HtmlTreeBuilder *builder,
                                           const HtmlToken *tok,
                                           int *reprocess,
                                           HtmlDiag *diag);
static Result ModeText(HtmlTreeBuilder *builder,
                               const HtmlToken *tok,
                               int *reprocess,
                               HtmlDiag *diag);

/** Whitespace-only character token (the one test every early mode needs). */
static int TokenIsAllWhitespace(const HtmlToken *tok)
{
    size_t i;

    if (tok->kind != RoboHtmlTokenCharacter) {
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

static Result AppendComment(HtmlTreeBuilder *builder,
                                    HtmlNodeId parentId,
                                    const HtmlToken *tok,
                                    HtmlDiag *diag)
{
    Result r;

    /* A comment breaks an adjacent-character merge run. */
    builder->textNode = RoboHtmlNodeIdNone;
    builder->textParent = RoboHtmlNodeIdNone;
    r = CreateCommentLeaf(builder, parentId, tok->u.comment.data,
                            tok->u.comment.length, diag);
    return r;
}

static Result CreateDoctypeNode(HtmlTreeBuilder *builder,
                                         const HtmlToken *tok,
                                         HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = BuilderSlot(builder);
    HtmlNodeHandle h;
    HtmlNodeEntry *entry;
    Result r;

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode, RoboNotFound,
                              "document was destroyed during the build");
    }
    r = RegisterNode(builder, RoboHtmlNodeKindDocumentType, &h, diag);
    if (r != RoboOk) {
        return r;
    }
    entry = HtmlNodeEntryMut(builder->documentId, h.nodeId);
    r = HtmlPayloadWriteDoctype(slot, entry, tok->u.doctype.name,
                                        tok->u.doctype.publicId,
                                        tok->u.doctype.systemId, diag);
    if (r != RoboOk) {
        return r;
    }
    return HtmlNodeLinkChild(builder->documentId, builder->root.nodeId,
                                     h.nodeId, diag);
}

/* --- INITIAL -------------------------------------------------------- */

static Result ModeInitial(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag)
{
    (void)diag;
    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        if (TokenIsAllWhitespace(tok)) {
            return RoboOk; /* ignored before <html> */
        }
        builder->mode = RoboHtmlModeBeforeHtml;
        *reprocess = 1;
        return RoboOk;
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->root.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        builder->sawDoctype = 1;
        if (CreateDoctypeNode(builder, tok, diag) != RoboOk) {
            return diag ? diag->code : RoboOutOfMemory;
        }
        builder->mode = RoboHtmlModeBeforeHtml;
        return RoboOk;
    case RoboHtmlTokenEndTag:
        RecordTokenRecovery(builder, tok, "end tag before <html>; ignored");
        builder->mode = RoboHtmlModeBeforeHtml;
        *reprocess = 1;
        return RoboOk;
    case RoboHtmlTokenStartTag:
    default:
        builder->mode = RoboHtmlModeBeforeHtml;
        *reprocess = 1;
        return RoboOk;
    }
}

/* --- BEFORE_HTML ---------------------------------------------------- */

static Result ModeBeforeHtml(HtmlTreeBuilder *builder,
                                      const HtmlToken *tok,
                                      int *reprocess,
                                      HtmlDiag *diag)
{
    Result r;

    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        if (TokenIsAllWhitespace(tok)) {
            return RoboOk;
        }
        break;
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->root.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "duplicate DOCTYPE; ignored");
        return RoboOk;
    case RoboHtmlTokenEndTag:
        if (strcmp(tok->u.tag.name, "head") != 0 &&
            strcmp(tok->u.tag.name, "body") != 0 &&
            strcmp(tok->u.tag.name, "html") != 0 &&
            strcmp(tok->u.tag.name, "br") != 0) {
            RecordTokenRecovery(builder, tok,
                                 "unexpected end tag before <html>; ignored");
            return RoboOk;
        }
        break;
    case RoboHtmlTokenStartTag:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            HtmlAttribute attrs[MAX_STAGED_ATTRS];
            size_t attrCount = 0u;

            StageAttrs(tok, attrs, &attrCount, builder);
            r = EnsureHtml(builder, attrs, attrCount, diag);
            if (r != RoboOk) {
                return r;
            }
            builder->mode = RoboHtmlModeBeforeHead;
            return RoboOk;
        }
        break;
    case RoboHtmlTokenEof:
    default:
        break;
    }

    r = EnsureHtml(builder, NULL, 0u, diag);
    if (r != RoboOk) {
        return r;
    }
    builder->mode = RoboHtmlModeBeforeHead;
    *reprocess = 1;
    return RoboOk;
}

/* --- BEFORE_HEAD ---------------------------------------------------- */

static Result ModeBeforeHead(HtmlTreeBuilder *builder,
                                      const HtmlToken *tok,
                                      int *reprocess,
                                      HtmlDiag *diag)
{
    Result r;

    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        if (TokenIsAllWhitespace(tok)) {
            return RoboOk;
        }
        break;
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->html.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "DOCTYPE after <html>; ignored");
        return RoboOk;
    case RoboHtmlTokenEndTag:
        if (strcmp(tok->u.tag.name, "head") != 0 &&
            strcmp(tok->u.tag.name, "body") != 0 &&
            strcmp(tok->u.tag.name, "html") != 0 &&
            strcmp(tok->u.tag.name, "br") != 0) {
            RecordTokenRecovery(builder, tok, "unexpected end tag; ignored");
            return RoboOk;
        }
        break;
    case RoboHtmlTokenStartTag:
        if (strcmp(tok->u.tag.name, "head") == 0) {
            HtmlAttribute attrs[MAX_STAGED_ATTRS];
            size_t attrCount = 0u;
            HtmlNodeHandle h;

            StageAttrs(tok, attrs, &attrCount, builder);
            r = CreateElement(builder, builder->html.nodeId, "head", attrs,
                               attrCount, 0, &h, diag);
            if (r != RoboOk) {
                return r;
            }
            builder->head = h;
            r = StackPush(builder, h.nodeId, diag);
            if (r != RoboOk) {
                return r;
            }
            builder->mode = RoboHtmlModeInHead;
            return RoboOk;
        }
        if (strcmp(tok->u.tag.name, "html") == 0) {
            RecordTokenRecovery(builder, tok,
                                 "duplicate <html> start tag; ignored");
            return RoboOk;
        }
        break;
    case RoboHtmlTokenEof:
    default:
        break;
    }

    /* Anything else: the head is implied, then the token is reprocessed. */
    r = EnsureHead(builder, diag);
    if (r != RoboOk) {
        return r;
    }
    builder->mode = RoboHtmlModeInHead;
    *reprocess = 1;
    return RoboOk;
}

/* --- IN_HEAD -------------------------------------------------------- */

static Result ModeInHead(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag)
{
    Result r;

    (void)reprocess;
    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        /* Only whitespace is head content; anything else implies </head>. */
        if (TokenIsAllWhitespace(tok)) {
            return InsertTextCurrent(builder, tok->u.characters.data,
                                       tok->u.characters.length, diag);
        }
        break;
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->head.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "DOCTYPE inside <head>; ignored");
        return RoboOk;
    case RoboHtmlTokenEndTag:
        if (strcmp(tok->u.tag.name, "head") == 0) {
            if (!CloseToElement(builder, BuilderSlot(builder), "head")) {
                StackPop(builder);
            }
            builder->mode = RoboHtmlModeAfterHead;
            return RoboOk;
        }
        if (strcmp(tok->u.tag.name, "body") == 0 ||
            strcmp(tok->u.tag.name, "html") == 0 ||
            strcmp(tok->u.tag.name, "br") == 0) {
            break; /* "anything else": close head and reprocess */
        }
        RecordTokenRecovery(builder, tok,
                             "end tag inside <head>; ignored");
        return RoboOk;
    case RoboHtmlTokenStartTag:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            RecordTokenRecovery(builder, tok,
                                 "duplicate <html> start tag; ignored");
            return RoboOk;
        }
        {
            HtmlTextMode family =
                TextModeForElement(tok->u.tag.name);

            if (family != RoboHtmlTextModeNone) {
                HtmlAttribute attrs[MAX_STAGED_ATTRS];
                size_t attrCount = 0u;
                HtmlNodeId id = RoboHtmlNodeIdNone;

                StageAttrs(tok, attrs, &attrCount, builder);
                r = InsertElement(builder, tok->u.tag.name, attrs, attrCount,
                                   0, &id, diag);
                if (r != RoboOk) {
                    return r;
                }
                return EnterTextMode(builder, family, diag);
            }
            if (IsHeadContentElement(tok->u.tag.name)) {
                HtmlAttribute attrs[MAX_STAGED_ATTRS];
                size_t attrCount = 0u;
                HtmlNodeId id = RoboHtmlNodeIdNone;

                StageAttrs(tok, attrs, &attrCount, builder);
                return InsertElement(builder, tok->u.tag.name, attrs,
                                      attrCount, tok->u.tag.selfClosing, &id,
                                      diag);
            }
            if (strcmp(tok->u.tag.name, "head") == 0) {
                RecordTokenRecovery(builder, tok,
                                     "duplicate <head> start tag; ignored");
                return RoboOk;
            }
        }
        break;
    case RoboHtmlTokenEof:
    default:
        break;
    }

    if (!CloseToElement(builder, BuilderSlot(builder), "head")) {
        StackPop(builder);
    }
    builder->mode = RoboHtmlModeAfterHead;
    *reprocess = 1;
    return RoboOk;
}

/* --- AFTER_HEAD ----------------------------------------------------- */

static Result ModeAfterHead(HtmlTreeBuilder *builder,
                                     const HtmlToken *tok,
                                     int *reprocess,
                                     HtmlDiag *diag)
{
    Result r;

    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        if (TokenIsAllWhitespace(tok)) {
            builder->textNode = RoboHtmlNodeIdNone;
            return InsertText(builder, builder->html.nodeId,
                               tok->u.characters.data, tok->u.characters.length,
                               diag);
        }
        break;
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->html.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "DOCTYPE after <head>; ignored");
        return RoboOk;
    case RoboHtmlTokenEndTag:
        if (strcmp(tok->u.tag.name, "body") != 0 &&
            strcmp(tok->u.tag.name, "html") != 0 &&
            strcmp(tok->u.tag.name, "br") != 0) {
            RecordTokenRecovery(builder, tok, "unexpected end tag; ignored");
            return RoboOk;
        }
        break;
    case RoboHtmlTokenStartTag:
        if (strcmp(tok->u.tag.name, "body") == 0) {
            HtmlAttribute attrs[MAX_STAGED_ATTRS];
            size_t attrCount = 0u;
            HtmlNodeHandle h;

            StageAttrs(tok, attrs, &attrCount, builder);
            r = CreateElement(builder, builder->html.nodeId, "body", attrs,
                               attrCount, 0, &h, diag);
            if (r != RoboOk) {
                return r;
            }
            builder->body = h;
            r = StackPush(builder, h.nodeId, diag);
            if (r != RoboOk) {
                return r;
            }
            builder->mode = RoboHtmlModeInBody;
            return RoboOk;
        }
        if (strcmp(tok->u.tag.name, "html") == 0 ||
            strcmp(tok->u.tag.name, "head") == 0) {
            RecordTokenRecovery(builder, tok,
                                 "start tag after <head>; ignored");
            return RoboOk;
        }
        break;
    case RoboHtmlTokenEof:
    default:
        break;
    }

    r = EnsureBody(builder, diag);
    if (r != RoboOk) {
        return r;
    }
    builder->mode = RoboHtmlModeInBody;
    *reprocess = 1;
    return RoboOk;
}

/* --- IN_BODY -------------------------------------------------------- */

static Result BodyStartTag(HtmlTreeBuilder *builder,
                                    const HtmlToken *tok,
                                    int *reprocess,
                                    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = BuilderSlot(builder);
    HtmlAttribute attrs[MAX_STAGED_ATTRS];
    size_t attrCount = 0u;
    HtmlNodeId id = RoboHtmlNodeIdNone;

    (void)reprocess; /* every path in this handler resolves the token itself */
    {
        HtmlTextMode family = TextModeForElement(tok->u.tag.name);

        if (strcmp(tok->u.tag.name, "html") == 0) {
            RecordTokenRecovery(builder, tok,
                                 "duplicate <html> start tag; ignored");
            return RoboOk;
        }
        if (strcmp(tok->u.tag.name, "body") == 0 ||
            strcmp(tok->u.tag.name, "head") == 0) {
            RecordTokenRecovery(builder, tok,
                                 "duplicate structural start tag; ignored");
            return RoboOk;
        }
        if (strcmp(tok->u.tag.name, "button") == 0 &&
            StackFind(builder, slot, "button") >= 0) {
            RecordTokenRecovery(builder, tok,
                                 "nested <button>; closing the outer one");
            CloseToElement(builder, slot, "button");
        }
        if (IsHeadingName(tok->u.tag.name)) {
            long index;

            /* A heading closes an open heading and an open paragraph. */
            for (index = (long)builder->openCount - 1; index >= 0; index--) {
                const HtmlNodeEntry *e = HtmlNodeEntryGet(
                    builder->documentId, builder->open[index]);

                if (e && IsHeadingName(HtmlPayloadElementName(slot, e,
                                                                       NULL))) {
                    RecordTokenRecovery(builder, tok,
                                         "nested heading; closing the outer one");
                    while ((long)builder->openCount > index) {
                        StackPop(builder);
                    }
                    break;
                }
            }
        }
        ClosePIfOpen(builder, slot, tok->u.tag.name);

        StageAttrs(tok, attrs, &attrCount, builder);
        if (InsertElement(builder, tok->u.tag.name, attrs, attrCount,
                           tok->u.tag.selfClosing, &id, diag) != RoboOk) {
            return diag ? diag->code : RoboOutOfMemory;
        }
        if (family != RoboHtmlTextModeNone) {
            return EnterTextMode(builder, family, diag);
        }
        /* <pre>/<listing>/<textarea> swallow one immediately following LF. */
        if (strcmp(tok->u.tag.name, "pre") == 0 ||
            strcmp(tok->u.tag.name, "listing") == 0 ||
            strcmp(tok->u.tag.name, "textarea") == 0) {
            builder->skipNextNewline = 1;
        }
        return RoboOk;
    }
}

static Result BodyEndTag(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = BuilderSlot(builder);
    const char *name = tok->u.tag.name;

    (void)diag;
    if (strcmp(name, "body") == 0) {
        if (!HandleIsLive(&builder->body)) {
            RecordTokenRecovery(builder, tok, "end tag for unopened <body>");
            return RoboOk;
        }
        builder->mode = RoboHtmlModeAfterBody;
        return RoboOk;
    }
    if (strcmp(name, "html") == 0) {
        builder->mode = RoboHtmlModeAfterBody;
        *reprocess = 1;
        return RoboOk;
    }
    if (strcmp(name, "br") == 0) {
        /* Legacy <br> mismatch: the spec re-runs it as a start tag. */
        RecordTokenRecovery(builder, tok,
                             "</br> treated as <br> per the HTML model");
        {
            HtmlNodeId id = RoboHtmlNodeIdNone;

            return InsertElement(builder, "br", NULL, 0u, 0, &id, diag);
        }
    }
    if (IsHeadingName(name)) {
        if (StackFind(builder, slot, name) < 0) {
            RecordTokenRecovery(builder, tok, "end tag for unopened heading");
            return RoboOk;
        }
        CloseToElement(builder, slot, name);
        return RoboOk;
    }
    if (CloseToElement(builder, slot, name)) {
        return RoboOk;
    }
    RecordTokenRecovery(builder, tok,
                         "end tag with no matching open element; ignored");
    return RoboOk;
}

static Result ModeInBody(HtmlTreeBuilder *builder,
                                  const HtmlToken *tok,
                                  int *reprocess,
                                  HtmlDiag *diag)
{
    (void)reprocess;
    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        return InsertTextCurrent(builder, tok->u.characters.data,
                                   tok->u.characters.length, diag);
    case RoboHtmlTokenComment: {
        HtmlNodeId current = StackTop(builder);

        if (current == RoboHtmlNodeIdNone) {
            current = builder->root.nodeId;
        }
        return AppendComment(builder, current, tok, diag);
    }
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "DOCTYPE inside <body>; ignored");
        return RoboOk;
    case RoboHtmlTokenStartTag:
        return BodyStartTag(builder, tok, reprocess, diag);
    case RoboHtmlTokenEndTag:
        return BodyEndTag(builder, tok, reprocess, diag);
    case RoboHtmlTokenEof:
    default:
        return RoboOk;
    }
}

/* --- AFTER_BODY ----------------------------------------------------- */

static Result ModeAfterBody(HtmlTreeBuilder *builder,
                                     const HtmlToken *tok,
                                     int *reprocess,
                                     HtmlDiag *diag)
{
    switch (tok->kind) {
    case RoboHtmlTokenCharacter:
        if (TokenIsAllWhitespace(tok)) {
            builder->textNode = RoboHtmlNodeIdNone;
            return InsertText(builder, builder->body.nodeId,
                               tok->u.characters.data, tok->u.characters.length,
                               diag);
        }
        break;
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->html.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "DOCTYPE after </body>; ignored");
        return RoboOk;
    case RoboHtmlTokenStartTag:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            RecordTokenRecovery(builder, tok,
                                 "start tag after </body>; ignored");
            return RoboOk;
        }
        break;
    case RoboHtmlTokenEndTag:
        if (strcmp(tok->u.tag.name, "html") == 0) {
            builder->mode = RoboHtmlModeAfterAfterBody;
            return RoboOk;
        }
        RecordTokenRecovery(builder, tok, "end tag after </body>; ignored");
        return RoboOk;
    case RoboHtmlTokenEof:
    default:
        return RoboOk;
    }

    RecordTokenRecovery(builder, tok,
                         "content after </body> processed in body mode");
    builder->mode = RoboHtmlModeInBody;
    *reprocess = 1;
    return RoboOk;
}

/* --- AFTER_AFTER_BODY ----------------------------------------------- */

static Result ModeAfterAfterBody(HtmlTreeBuilder *builder,
                                           const HtmlToken *tok,
                                           int *reprocess,
                                           HtmlDiag *diag)
{
    switch (tok->kind) {
    case RoboHtmlTokenComment:
        return AppendComment(builder, builder->root.nodeId, tok, diag);
    case RoboHtmlTokenDoctype:
        RecordTokenRecovery(builder, tok, "DOCTYPE after </html>; ignored");
        return RoboOk;
    case RoboHtmlTokenEof:
        return RoboOk;
    case RoboHtmlTokenCharacter:
        if (TokenIsAllWhitespace(tok)) {
            return InsertTextCurrent(builder, tok->u.characters.data,
                                       tok->u.characters.length, diag);
        }
        break;
    default:
        break;
    }
    RecordTokenRecovery(builder, tok,
                         "content after </html> processed in body mode");
    builder->mode = RoboHtmlModeInBody;
    *reprocess = 1;
    return RoboOk;
}

/* --- TEXT (RCDATA / RAWTEXT / SCRIPT / PLAINTEXT) -------------------- */

static Result ModeText(HtmlTreeBuilder *builder,
                               const HtmlToken *tok,
                               int *reprocess,
                               HtmlDiag *diag)
{
    (void)reprocess;
    if (tok->kind == RoboHtmlTokenCharacter) {
        if (builder->skipNextNewline) {
            const char *data = tok->u.characters.data;
            size_t length = tok->u.characters.length;

            if (length > 0u && data[0] == '\n') {
                data++;
                length--;
            }
            builder->skipNextNewline = 0;
            return InsertTextCurrent(builder, data, length, diag);
        }
        return InsertTextCurrent(builder, tok->u.characters.data,
                                   tok->u.characters.length, diag);
    }
    if (tok->kind == RoboHtmlTokenEndTag) {
        HtmlDocumentSlot *slot = BuilderSlot(builder);
        long index = StackFindTextFamily(builder, slot);

        if (index >= 0) {
            while (builder->openCount > (size_t)index) {
                StackPop(builder);
            }
        } else {
            StackPop(builder);
        }
        return LeaveTextMode(builder, diag);
    }
    if (tok->kind == RoboHtmlTokenEof) {
        builder->textMode = RoboHtmlTextModeNone;
        builder->mode = builder->textReturnMode;
        return RoboOk;
    }
    /* The tokenizer should not emit these inside raw content, but a
     * malformed source must never be fatal. */
    RecordTokenRecovery(builder, tok, "unexpected token inside text content");
    return RoboOk;
}

/* --- dispatch ------------------------------------------------------- */

static Result ProcessToken(HtmlTreeBuilder *builder,
                                   const HtmlToken *tok,
                                   HtmlDiag *diag)
{
    unsigned int guard = 0u;

    if (tok->kind == RoboHtmlTokenStartTag && builder->skipNextNewline &&
        builder->mode != RoboHtmlModeText) {
        builder->skipNextNewline = 0;
    }
    for (guard = 0u; guard < REPROCESS_LIMIT; guard++) {
        int reprocess = 0;
        Result r;

        switch (builder->mode) {
        case RoboHtmlModeInitial:
            r = ModeInitial(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeBeforeHtml:
            r = ModeBeforeHtml(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeBeforeHead:
            r = ModeBeforeHead(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeInHead:
            r = ModeInHead(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeAfterHead:
            r = ModeAfterHead(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeInBody:
            r = ModeInBody(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeAfterBody:
            r = ModeAfterBody(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeAfterAfterBody:
            r = ModeAfterAfterBody(builder, tok, &reprocess, diag);
            break;
        case RoboHtmlModeText:
            r = ModeText(builder, tok, &reprocess, diag);
            break;
        default:
            RecordTokenRecovery(builder, tok,
                                 "unknown insertion mode; build stopped");
            builder->stop = 1;
            return RoboOk;
        }
        if (r != RoboOk) {
            return r;
        }
        if (!reprocess) {
            return RoboOk;
        }
    }
    RecordTokenRecovery(builder, tok, "mode re-dispatch limit reached");
    builder->stop = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 6. Result accounting and finalization                               */
/* ------------------------------------------------------------------ */

static void recount(HtmlTreeBuilder *builder)
{
    HtmlDocumentSlot *slot = BuilderSlot(builder);
    size_t i;

    if (!slot) {
        return;
    }
    builder->result.nodeCount = 0u;
    builder->result.elementCount = 0u;
    builder->result.textCount = 0u;
    builder->result.commentCount = 0u;
    builder->result.doctypeCount = 0u;
    builder->result.attributeCount = 0u;
    for (i = 0u; i < RoboHtmlMaxNodesPerDocument; i++) {
        const HtmlNodeEntry *e = &slot->nodes.slots[i];

        if (!e->inUse) {
            continue;
        }
        builder->result.nodeCount++;
        switch (e->kind) {
        case RoboHtmlNodeKindElement:
            builder->result.elementCount++;
            builder->result.attributeCount +=
                HtmlPayloadAttrCount(slot, e);
            break;
        case RoboHtmlNodeKindText:
            builder->result.textCount++;
            break;
        case RoboHtmlNodeKindComment:
            builder->result.commentCount++;
            break;
        case RoboHtmlNodeKindDocumentType:
            builder->result.doctypeCount++;
            break;
        case RoboHtmlNodeKindDocument:
        case RoboHtmlNodeKindFragment:
        default:
            break;
        }
    }
}

/** Close everything still open, recording one diagnostic per element. */
static void CloseAllOpen(HtmlTreeBuilder *builder,
                           HtmlDocumentSlot *slot)
{
    while (builder->openCount > 0u) {
        HtmlNodeId id = StackTop(builder);
        const HtmlNodeEntry *e =
            HtmlNodeEntryGet(builder->documentId, id);

        if (e && !IsStructural(builder, id)) {
            RecordRecovery(builder, RoboParseError,
                            "element left open at end of input", 0u, 0u);
        }
        StackPop(builder);
    }
    (void)slot;
}

/* ------------------------------------------------------------------ */
/* 7. Public API                                                       */
/* ------------------------------------------------------------------ */

int HtmlInsertionModeIsKnown(HtmlInsertionMode mode)
{
    return mode >= RoboHtmlModeInitial && mode <= RoboHtmlModeText;
}

const char *HtmlInsertionModeName(HtmlInsertionMode mode)
{
    switch (mode) {
    case RoboHtmlModeInitial:
        return "initial";
    case RoboHtmlModeBeforeHtml:
        return "before-html";
    case RoboHtmlModeBeforeHead:
        return "before-head";
    case RoboHtmlModeInHead:
        return "in-head";
    case RoboHtmlModeAfterHead:
        return "after-head";
    case RoboHtmlModeInBody:
        return "in-body";
    case RoboHtmlModeAfterBody:
        return "after-body";
    case RoboHtmlModeAfterAfterBody:
        return "after-after-body";
    case RoboHtmlModeText:
        return "text";
    default:
        return "unknown";
    }
}

const char *HtmlTextModeName(HtmlTextMode mode)
{
    switch (mode) {
    case RoboHtmlTextModeNone:
        return "none";
    case RoboHtmlTextModeRcdata:
        return "rcdata";
    case RoboHtmlTextModeRawtext:
        return "rawtext";
    case RoboHtmlTextModeScript:
        return "script";
    case RoboHtmlTextModePlaintext:
        return "plaintext";
    default:
        return "unknown";
    }
}

int HtmlTextModeIsKnown(HtmlTextMode mode)
{
    return mode >= RoboHtmlTextModeNone &&
           mode <= RoboHtmlTextModePlaintext;
}

Result HtmlTreeBuilderCreate(HtmlDocumentId documentId,
                                            HtmlTreeBuilder **out,
                                            HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlTreeBuilder *builder;
    HtmlParser *lease = NULL;
    Result r;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "out is NULL");
    }
    *out = NULL;
    if (!HtmlIsInitialized()) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboInitError,
                              "RoboHTML module not initialized");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboNotFound,
                              "unknown document id");
    }
    /*
     * Any lifecycle that accepts content can host a build, including LOADING:
     * a document whose source was attached with attach_source (so it sits in
     * LOADING, waiting to be parsed) is exactly the case a builder exists
     * for. Exclusivity is the parser lease below, not the lifecycle — begin()
     * settles a LOADING document before it attaches its own source.
     */
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboBusy,
                              "document is not idle");
    }
    /* Taking the 8.1 parser lease keeps a second builder out (RoboBusy). */
    r = HtmlParserCreate(documentId, &lease, diag);
    if (r != RoboOk) {
        return r;
    }
    builder = (HtmlTreeBuilder *)RoboCalloc(
        1u, sizeof(HtmlTreeBuilder));
    if (!builder) {
        HtmlParserDestroy(lease);
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboOutOfMemory, "tree builder allocation");
    }
    builder->magic = TREE_BUILDER_MAGIC;
    builder->documentId = documentId;
    builder->lease = lease;
    builder->mode = RoboHtmlModeInitial;
    builder->textReturnMode = RoboHtmlModeInitial;
    builder->textMode = RoboHtmlTextModeNone;
    builder->state = RoboHtmlParserStateIdle;
    builder->root = slot->root;
    builder->textNode = RoboHtmlNodeIdNone;
    builder->textParent = RoboHtmlNodeIdNone;
    HtmlNodeHandleClear(&builder->html);
    HtmlNodeHandleClear(&builder->head);
    HtmlNodeHandleClear(&builder->body);
    HtmlDiagnosticsInit(&builder->diagnostics);
    HtmlSourceInit(&builder->source);
    *out = builder;
    return RoboOk;
}

Result HtmlTreeBuilderBegin(HtmlTreeBuilder *builder,
                                           const HtmlSource *src,
                                           HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    Result r;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "builder is NULL");
    }
    if (!src || !HtmlSourceKindIsKnown(src->kind) ||
        src->kind == RoboHtmlSourceKindNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is not usable");
    }
    if (builder->state != RoboHtmlParserStateIdle &&
        builder->state != RoboHtmlParserStateComplete) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboError,
                              "build already in progress");
    }
    slot = BuilderSlot(builder);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "document was destroyed");
    }
    /* A previous build leaves the document LOADING: settle it first. */
    if (slot->lifecycle == RoboHtmlDocumentLifecycleLoading) {
        r = HtmlDocumentDetachSource(builder->documentId, diag);
        if (r != RoboOk) {
            return r;
        }
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboError,
                              "lifecycle does not allow building");
    }
    /*
     * Re-parsing clears the previous tree; old node handles go stale while
     * the document's own root survives. A finalized document is cleared too,
     * because clearing is what reopens construction after a completed
     * build closed it.
     */
    if (slot->nodes.used > 1u || slot->finalized) {
        r = HtmlDocumentClearTree(slot, diag);
        if (r != RoboOk) {
            return r;
        }
    }
    builder->root = slot->root;
    HtmlNodeHandleClear(&builder->html);
    HtmlNodeHandleClear(&builder->head);
    HtmlNodeHandleClear(&builder->body);

    if (builder->tokenizer) {
        HtmlTokenizerDestroy(builder->tokenizer);
        builder->tokenizer = NULL;
    }
    r = HtmlTokenizerCreate(src, &builder->tokenizer, diag);
    if (r != RoboOk) {
        builder->state = RoboHtmlParserStateFailed;
        return r;
    }
    builder->source = *src;
    builder->hasSource = 1;

    r = HtmlDocumentAttachSource(builder->documentId, src, diag);
    if (r != RoboOk) {
        builder->state = RoboHtmlParserStateFailed;
        return r;
    }

    builder->mode = RoboHtmlModeInitial;
    builder->textReturnMode = RoboHtmlModeInitial;
    builder->textMode = RoboHtmlTextModeNone;
    builder->state = RoboHtmlParserStateFeeding;
    builder->openCount = 0u;
    builder->textNode = RoboHtmlNodeIdNone;
    builder->textParent = RoboHtmlNodeIdNone;
    builder->sawDoctype = 0;
    builder->skipNextNewline = 0;
    builder->complete = 0;
    builder->stop = 0;
    memset(&builder->result, 0, sizeof(builder->result));
    HtmlDiagnosticsClear(&builder->diagnostics);
    builder->droppedDiagnostics = 0u;
    if (builder->lease) {
        builder->lease->state = RoboHtmlParserStateFeeding;
        builder->lease->sourceKind = src->kind;
        builder->lease->sourceLength = HtmlSourceLength(src);
    }
    LogDebug("robo_html: doc=%llu tree build started (%s, %llu bytes)",
                   (unsigned long long)builder->documentId,
                   HtmlSourceKindName(src->kind),
                   (unsigned long long)HtmlSourceLength(src));
    return RoboOk;
}

Result HtmlTreeBuilderConsume(
    HtmlTreeBuilder *builder,
    const HtmlToken *token,
    HtmlDiag *diag)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !token) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "builder or token is NULL");
    }
    if (!HtmlTokenKindIsKnown(token->kind) ||
        token->kind == RoboHtmlTokenNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "unknown token kind");
    }
    if (builder->state != RoboHtmlParserStateFeeding) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboInitError,
                              "builder is not begun");
    }
    if (builder->complete) {
        return RoboOk;
    }
    {
        Result r = ProcessToken(builder, token, diag);

        /* Hitting the node ceiling truncates rather than failing the build. */
        if (r != RoboOk && builder->stop) {
            return HtmlTreeBuilderFinish(builder, diag);
        }
        return r;
    }
}

Result HtmlTreeBuilderNext(HtmlTreeBuilder *builder,
                                          HtmlDiag *diag)
{
    HtmlToken token;
    Result r;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "builder is NULL");
    }
    if (!builder->hasSource || !builder->tokenizer) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "no source attached");
    }
    if (builder->complete) {
        return RoboOk; /* idempotent once finished */
    }
    if (builder->stop) {
        /* Truncated: settle the tree instead of consuming further input. */
        return HtmlTreeBuilderFinish(builder, diag);
    }
    r = HtmlTokenizerNext(builder->tokenizer, &token, diag);
    if (r != RoboOk) {
        builder->state = RoboHtmlParserStateFailed;
        return r;
    }
    if (token.kind == RoboHtmlTokenEof) {
        /* EOF still runs through the modes so pending content settles. */
        r = ProcessToken(builder, &token, diag);
        if (r != RoboOk && !builder->stop) {
            return r;
        }
        return HtmlTreeBuilderFinish(builder, diag);
    }
    r = ProcessToken(builder, &token, diag);
    if (r != RoboOk && builder->stop) {
        return HtmlTreeBuilderFinish(builder, diag);
    }
    return r;
}

/**
 * Did this build consume its source to the end?
 *
 * COMPLETE means "no further input can arrive", so it is a fact about the
 * input rather than about the tree: two things can still be outstanding
 * after a finished build, and either one settles the document at
 * INTERACTIVE instead —
 *   - the source is a stream, so more bytes may still be delivered;
 *   - the build was truncated at the node ceiling, so the source was
 *     abandoned rather than consumed.
 *
 * Note that no public constructor produces a STREAM source yet, so today
 * this reduces to the truncation test. The condition is stated in full
 * anyway because it is the rule, and the parser already switches on the same
 * enum to choose its mode.
 */
static int BuildConsumedWholeSource(
    const HtmlTreeBuilder *builder)
{
    return !builder->stop &&
           builder->source.kind != RoboHtmlSourceKindStream;
}

Result HtmlTreeBuilderFinish(HtmlTreeBuilder *builder,
                                            HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "builder is NULL");
    }
    if (builder->complete) {
        return RoboOk;
    }
    slot = BuilderSlot(builder);
    if (builder->state == RoboHtmlParserStateFeeding) {
        CloseAllOpen(builder, slot);
    }
    if (slot) {
        recount(builder);
    }
    builder->complete = 1;
    builder->state = RoboHtmlParserStateComplete;
    if (builder->lease) {
        builder->lease->state = RoboHtmlParserStateComplete;
    }
    /*
     * The document reaches INTERACTIVE once its tree exists: parsing has
     * finished and the DOM is live and complete enough to read, traverse and
     * mutate. Whether it also reaches COMPLETE is a question about *input*,
     * not about the tree — see BuildConsumedWholeSource.
     */
    if (slot && slot->lifecycle == RoboHtmlDocumentLifecycleLoading) {
        if (HtmlDocumentMarkInteractive(builder->documentId, NULL) !=
            RoboOk) {
            RecordRecovery(builder, RoboError,
                            "document could not transition to interactive",
                            0u, 0u);
        } else if (BuildConsumedWholeSource(builder)) {
            if (HtmlDocumentMarkComplete(builder->documentId,
                                                 NULL) != RoboOk) {
                RecordRecovery(builder, RoboError,
                                "document could not transition to complete",
                                0u, 0u);
            }
        }
    }
    /*
     * A finished build closes tree construction: the document model owns the
     * tree from here, and further register/link calls would be a caller
     * mistake rather than part of a parse. Re-parsing goes through begin()
     * (which clears the tree and reopens construction) or reset().
     */
    if (slot) {
        if (HtmlDocumentFinalize(builder->documentId, NULL) !=
            RoboOk) {
            RecordRecovery(builder, RoboError,
                            "document could not be finalized", 0u, 0u);
        }
    }
    LogDebug(
        "robo_html: doc=%llu tree built: nodes=%llu elements=%llu text=%llu "
        "comments=%llu doctypes=%llu attrs=%llu depth=%llu recovered=%llu",
        (unsigned long long)builder->documentId,
        (unsigned long long)builder->result.nodeCount,
        (unsigned long long)builder->result.elementCount,
        (unsigned long long)builder->result.textCount,
        (unsigned long long)builder->result.commentCount,
        (unsigned long long)builder->result.doctypeCount,
        (unsigned long long)builder->result.attributeCount,
        (unsigned long long)builder->result.maxDepth,
        (unsigned long long)builder->result.recoveredErrors);
    return RoboOk;
}

Result HtmlTreeBuilderBuild(HtmlTreeBuilder *builder,
                                           const HtmlSource *src,
                                           HtmlDiag *diag)
{
    Result r;

    r = HtmlTreeBuilderBegin(builder, src, diag);
    if (r != RoboOk) {
        return r;
    }
    while (!builder->complete && !builder->stop) {
        r = HtmlTreeBuilderNext(builder, diag);
        if (r != RoboOk) {
            return r;
        }
    }
    return HtmlTreeBuilderFinish(builder, diag);
}

Result HtmlBuildTree(HtmlDocumentId documentId,
                                   const HtmlSource *src,
                                   HtmlTreeResult *outResult,
                                   HtmlDiag *diag)
{
    HtmlTreeBuilder *builder = NULL;
    Result r;

    r = HtmlTreeBuilderCreate(documentId, &builder, diag);
    if (r != RoboOk) {
        return r;
    }
    r = HtmlTreeBuilderBuild(builder, src, diag);
    if (r == RoboOk) {
        /*
         * Phase 17.7: a parsed `<template>`'s children belong in
         * `template.content`, not in the light tree. This builder nests them
         * under the element as it parses (it has no "in template" insertion
         * mode); the adoption pass afterwards is what makes the finished tree
         * the standard shape, and it is deliberately *after* the build so no
         * insertion-mode behaviour changes. The count is not read here: the
         * pass reports what it did through its own door for callers that want
         * it, and a parse's job is the tree it produced.
         */
        Result adopt = HtmlDocumentAdoptTemplates(documentId, NULL, diag);

        if (adopt != RoboOk) {
            r = adopt;
        }
        /*
         * Phase 17.9: parsed markup that names a defined custom element is
         * already custom, and the tree builder links nodes through the
         * construction path rather than the mutation one — so the lifecycle
         * notifications a DOM insert would raise never happen during a parse.
         * One upgrade pass at the end is the same result: an
         * `attributeChanged` for each observed attribute the source gave the
         * element, then `connected` because the parse put it in the document.
         */
        if (r == RoboOk) {
            Result upgraded =
                HtmlDocumentUpgradeCustomElements(documentId, NULL, diag);

            if (upgraded != RoboOk) {
                r = upgraded;
            }
        }
    }
    if (r == RoboOk && outResult) {
        *outResult = builder->result;
    }
    HtmlTreeBuilderDestroy(builder);
    return r;
}

Result HtmlTreeBuilderGetState(
    const HtmlTreeBuilder *builder,
    HtmlParserState *outState)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !outState) {
        return RoboInvalidArgument;
    }
    *outState = builder->state;
    return RoboOk;
}

Result HtmlTreeBuilderGetMode(
    const HtmlTreeBuilder *builder,
    HtmlInsertionMode *outMode)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !outMode) {
        return RoboInvalidArgument;
    }
    *outMode = builder->mode;
    return RoboOk;
}

Result HtmlTreeBuilderGetTextMode(
    const HtmlTreeBuilder *builder,
    HtmlTextMode *outMode)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !outMode) {
        return RoboInvalidArgument;
    }
    *outMode = builder->textMode;
    return RoboOk;
}

Result HtmlTreeBuilderSetTextMode(
    HtmlTreeBuilder *builder,
    HtmlTextMode mode,
    HtmlDiag *diag)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC ||
        !HtmlTextModeIsKnown(mode)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument,
                              "builder or text mode is invalid");
    }
    if (builder->state != RoboHtmlParserStateFeeding) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboInitError,
                              "builder is not begun");
    }
    if (mode == RoboHtmlTextModeNone) {
        builder->textMode = RoboHtmlTextModeNone;
        return RoboOk;
    }
    return EnterTextMode(builder, mode, diag);
}

Result HtmlTreeBuilderCurrentNode(
    const HtmlTreeBuilder *builder,
    HtmlNodeHandle *out)
{
    HtmlNodeId id;
    const HtmlNodeEntry *e;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out) {
        return RoboInvalidArgument;
    }
    id = StackTop(builder);
    if (id == RoboHtmlNodeIdNone) {
        id = builder->root.nodeId;
    }
    e = HtmlNodeEntryGet(builder->documentId, id);
    if (!e) {
        return RoboNotFound;
    }
    *out = HtmlNodeHandleMake(builder->documentId, e);
    return RoboOk;
}

Result HtmlTreeBuilderOpenElementCount(
    const HtmlTreeBuilder *builder,
    size_t *outCount)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !outCount) {
        return RoboInvalidArgument;
    }
    *outCount = builder->openCount;
    return RoboOk;
}

Result HtmlTreeBuilderOpenElement(
    const HtmlTreeBuilder *builder,
    size_t index,
    HtmlNodeHandle *out)
{
    const HtmlNodeEntry *e;

    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out) {
        return RoboInvalidArgument;
    }
    if (index >= builder->openCount) {
        return RoboNotFound;
    }
    e = HtmlNodeEntryGet(builder->documentId, builder->open[index]);
    if (!e) {
        return RoboNotFound;
    }
    *out = HtmlNodeHandleMake(builder->documentId, e);
    return RoboOk;
}

Result HtmlTreeBuilderResult(
    const HtmlTreeBuilder *builder,
    HtmlTreeResult *out)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !out) {
        return RoboInvalidArgument;
    }
    *out = builder->result;
    return RoboOk;
}

size_t HtmlTreeBuilderDiagnosticCount(
    const HtmlTreeBuilder *builder)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC) {
        return 0u;
    }
    return builder->diagnostics.count;
}

Result HtmlTreeBuilderGetDiagnostic(
    const HtmlTreeBuilder *builder,
    size_t index,
    HtmlDiag *outDiag)
{
    if (!builder || builder->magic != TREE_BUILDER_MAGIC || !outDiag) {
        return RoboInvalidArgument;
    }
    if (index >= builder->diagnostics.count) {
        return RoboNotFound;
    }
    *outDiag = builder->diagnostics.items[index];
    return RoboOk;
}

void HtmlTreeBuilderDestroy(HtmlTreeBuilder *builder)
{
    if (!builder) {
        return;
    }
    if (builder->magic != TREE_BUILDER_MAGIC) {
        return; /* already destroyed or never created */
    }
    builder->magic = 0u;
    if (builder->tokenizer) {
        HtmlTokenizerDestroy(builder->tokenizer);
        builder->tokenizer = NULL;
    }
    if (builder->open) {
        RoboFree(builder->open);
        builder->open = NULL;
    }
    /* Releasing the lease also detaches the document if it is still LOADING. */
    if (builder->lease) {
        HtmlParserDestroy(builder->lease);
        builder->lease = NULL;
    }
    HtmlDiagnosticsClear(&builder->diagnostics);
    RoboFree(builder);
}

/* ------------------------------------------------------------------ */
/* 8. Tree dump                                                        */
/* ------------------------------------------------------------------ */

#define DUMP_INDENT_MAX 24
#define DUMP_TEXT_BUDGET 72
/** Recursion bound: the dump is the one depth-sensitive consumer. */
#define DUMP_DEPTH_MAX 64u

typedef struct dumpWriter {
    HtmlTreeSinkFn sink;
    void *user;
    size_t emitted;
    /* Set once the depth cut has been reported, so it prints exactly once. */
    int elided;
} DumpWriter;

static Result DumpWrite(DumpWriter *w, const char *text, size_t length)
{
    if (length == 0u) {
        return RoboOk;
    }
    return w->sink(text, length, w->user);
}

static Result DumpStr(DumpWriter *w, const char *text)
{
    return DumpWrite(w, text, text ? strlen(text) : 0u);
}

static Result DumpIndent(DumpWriter *w, size_t depth)
{
    char spaces[DUMP_INDENT_MAX * 2u + 1u];
    size_t count = depth * 2u;
    Result r;

    if (count > DUMP_INDENT_MAX * 2u) {
        count = DUMP_INDENT_MAX * 2u;
    }
    memset(spaces, ' ', count);
    r = DumpWrite(w, spaces, count);
    if (r != RoboOk) {
        return r;
    }
    return RoboOk;
}

/** Quoted, escaped payload with a byte budget so one node cannot flood. */
static Result DumpQuoted(DumpWriter *w, const char *bytes,
                                 size_t length)
{
    size_t i;
    size_t shown = (length > DUMP_TEXT_BUDGET) ? DUMP_TEXT_BUDGET : length;
    Result r;

    r = DumpWrite(w, "\"", 1u);
    if (r != RoboOk) {
        return r;
    }
    for (i = 0u; i < shown; i++) {
        char c = bytes[i];

        if (c == '"' || c == '\\') {
            char pair[2];
            pair[0] = '\\';
            pair[1] = c;
            r = DumpWrite(w, pair, 2u);
        } else if (c == '\n') {
            r = DumpStr(w, "\\n");
        } else if (c == '\r') {
            r = DumpStr(w, "\\r");
        } else if (c == '\t') {
            r = DumpStr(w, "\\t");
        } else {
            r = DumpWrite(w, &c, 1u);
        }
        if (r != RoboOk) {
            return r;
        }
    }
    r = DumpWrite(w, "\"", 1u);
    if (r != RoboOk) {
        return r;
    }
    if (shown < length) {
        return DumpStr(w, "...");
    }
    return RoboOk;
}

/**
 * Visitor for the traversal walk that renders the dump.
 *
 * Depth arrives from the walker instead of being carried by a recursive
 * call, and the pre-order order, the cycle bound and the no-recursion rule
 * are the ones every other consumer uses — this used to be the one place
 * that walked the tree with the C stack. It is also the one consumer that
 * cannot print an arbitrarily deep tree, so the depth cut below is a dump
 * policy rather than a traversal limit.
 */
static Result DumpVisit(const HtmlNodeView *view,
                                size_t depth,
                                void *user)
{
    DumpWriter *w = (DumpWriter *)user;
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(view->handle.documentId);
    const HtmlNodeEntry *e =
        HtmlNodeEntryGet(view->handle.documentId,
                                 view->handle.nodeId);
    Result r;

    if (!slot || !e) {
        return RoboNotFound; /* the document went away mid-dump */
    }
    /*
     * Deeper subtrees are elided: the dump stays bounded, never fatal, and
     * one line marks where it was cut.
     */
    if (depth >= DUMP_DEPTH_MAX) {
        if (w->elided) {
            return RoboOk;
        }
        w->elided = 1;
        r = DumpIndent(w, depth);
        if (r != RoboOk) {
            return r;
        }
        return DumpStr(w, "... (deeper nodes elided)\n");
    }
    if (w->emitted++ >= RoboHtmlMaxNodesPerDocument) {
        return RoboOk;
    }
    r = DumpIndent(w, depth);
    if (r != RoboOk) {
        return r;
    }
    switch (e->kind) {
    case RoboHtmlNodeKindDocument:
        /*
         * The document node is the tree root, so its state belongs on its
         * own line rather than as a child: a lifecycle line indented under
         * it would claim a node that does not exist. The lifecycle is read
         * from the document, never assumed, so a dump taken mid-load says
         * so.
         */
        r = DumpStr(w, "Document lifecycle=");
        if (r == RoboOk) {
            r = DumpStr(w, HtmlDocumentLifecycleName(
                                slot->lifecycle));
        }
        break;
    case RoboHtmlNodeKindDocumentType: {
        const char *name = "";
        const char *pub = "";
        const char *sys = "";

        HtmlPayloadDoctype(slot, e, &name, &pub, &sys);
        r = DumpStr(w, "doctype ");
        if (r == RoboOk) {
            r = DumpStr(w, name);
        }
        if (r == RoboOk && pub[0] != '\0') {
            r = DumpStr(w, " public ");
            if (r == RoboOk) {
                r = DumpQuoted(w, pub, strlen(pub));
            }
        }
        if (r == RoboOk && sys[0] != '\0') {
            r = DumpStr(w, " system ");
            if (r == RoboOk) {
                r = DumpQuoted(w, sys, strlen(sys));
            }
        }
        break;
    }
    case RoboHtmlNodeKindElement: {
        HtmlNamespace ns = HtmlPayloadNamespace(slot, e);
        size_t attrs = HtmlPayloadAttrCount(slot, e);
        size_t i;

        /*
         * An element from another namespace prints its namespace, because
         * the same local name in another namespace is a different element.
         * HTML names stay unprefixed, so the common dump is unchanged.
         */
        if (ns != RoboHtmlNamespaceHtml && ns != RoboHtmlNamespaceNone) {
            r = DumpStr(w, HtmlNamespaceName(ns));
            if (r == RoboOk) {
                r = DumpStr(w, ":");
            }
        } else {
            r = RoboOk;
        }
        if (r == RoboOk) {
            r = DumpStr(w, view->name);
        }
        /*
         * Attributes are read through the model, not the raw payload, so
         * the dump shows exactly what an attribute lookup would return —
         * including `<input disabled>` and `<input disabled="">`, which
         * both print as `disabled=""` because presence is the record, not
         * the value. Order is stored order, which for parsed input is source
         * order.
         */
        for (i = 0u; r == RoboOk && i < attrs; i++) {
            HtmlAttribute attr;

            if (HtmlPayloadAttrAt(slot, e, i, &attr) != RoboOk) {
                break;
            }
            r = DumpStr(w, " ");
            if (r == RoboOk) {
                r = DumpStr(w, attr.name);
            }
            if (r == RoboOk) {
                r = DumpStr(w, "=");
            }
            if (r == RoboOk) {
                r = DumpQuoted(w, attr.value, attr.valueLength);
            }
        }
        if (r == RoboOk && HtmlPayloadSelfClosing(slot, e)) {
            r = DumpStr(w, " /");
        }
        break;
    }
    case RoboHtmlNodeKindText: {
        /*
         * Read through the text model, so the dump shows exactly what the
         * content accessor returns rather than a second decoding of the same
         * payload that could drift from it. The label is what makes a
         * whitespace-only run legible (Text: "\n  ") and separates a text
         * node from the comment line right below it.
         */
        r = DumpStr(w, "Text: ");
        if (r == RoboOk) {
            r = DumpQuoted(w, view->text, view->textLength);
        }
        break;
    }
    case RoboHtmlNodeKindComment: {
        r = DumpStr(w, "#comment ");
        if (r == RoboOk) {
            r = DumpQuoted(w, view->text, view->textLength);
        }
        break;
    }
    case RoboHtmlNodeKindFragment:
    default:
        r = DumpStr(w, "fragment");
        break;
    }
    if (r != RoboOk) {
        return r;
    }
    if (r != RoboOk) {
        return r;
    }
    return DumpStr(w, "\n");
}

Result HtmlTreeDump(HtmlDocumentId documentId,
                                  HtmlTreeSinkFn sink,
                                  void *user,
                                  HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    DumpWriter w;

    if (!sink) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument, "sink is NULL");
    }
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown document id");
    }
    w.sink = sink;
    w.user = user;
    w.emitted = 0u;
    w.elided = 0;
    /*
     * The dump is a traversal now: order, depth, the cycle bound and the
     * no-recursion rule come from the walker rather than from a second
     * subtree walk that would have to be kept in step with it.
     */
    return HtmlNodeWalkPreOrder(&slot->root, RoboHtmlNodeFilterAll,
                                         DumpVisit, &w, diag);
}

/* --- buffer sink ---------------------------------------------------- */

typedef struct bufferSink {
    char *out;
    size_t cap;
    size_t used;
} BufferSink;

static Result BufferSinkWrite(const char *text,
                                       size_t length,
                                       void *user)
{
    BufferSink *b = (BufferSink *)user;

    if (!b || length == 0u) {
        return RoboOk;
    }
    if (b->used + length < b->cap) {
        memcpy(b->out + b->used, text, length);
    } else if (b->used < b->cap) {
        memcpy(b->out + b->used, text, b->cap - b->used - 1u);
    }
    b->used += length;
    return RoboOk;
}

Result HtmlTreeDumpToBuffer(HtmlDocumentId documentId,
                                            char *out,
                                            size_t outCap,
                                            size_t *outLength,
                                            HtmlDiag *diag)
{
    BufferSink b;
    Result r;

    if (!out || outCap == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainNode,
                              RoboInvalidArgument,
                              "output buffer is NULL or empty");
    }
    out[0] = '\0';
    b.out = out;
    b.cap = outCap;
    b.used = 0u;
    r = HtmlTreeDump(documentId, BufferSinkWrite, &b, diag);
    if (outLength) {
        *outLength = b.used;
    }
    if (r != RoboOk) {
        return r;
    }
    if (b.used < outCap) {
        out[b.used] = '\0';
    } else {
        out[outCap - 1u] = '\0';
    }
    return RoboOk;
}
