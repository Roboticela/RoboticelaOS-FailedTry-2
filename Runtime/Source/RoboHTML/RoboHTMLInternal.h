/*
 * Internal RoboHTML glue (Phase 8.1) — not a public API.
 * Mirrors RoboWindowInternal.h conventions: shared internal layout for
 * the document table, parser leases, and hook slots.
 */
#ifndef ROBO_HTML_INTERNAL_H
#define ROBO_HTML_INTERNAL_H

#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLEncoding.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

/** Per-document state slot (fixed table, module-owned). */
typedef struct HtmlDocumentSlot {
    int inUse;
    HtmlDocumentId id;

    HtmlDocumentLifecycle lifecycle;
    HtmlDocumentLifecycle previousLifecycle;

    /** Failure record for FAILED documents (diagnostics, no heap). */
    HtmlDiagnosticDomain failDomain;
    const char *failMessage;

    /** Attached source metadata (kind; bytes stay application-owned). */
    HtmlSourceKind sourceKind;
    size_t sourceLength;
    char sourceName[RoboHtmlUrlMax];

    HtmlEncodingState encoding;

    HtmlNodeRegistry nodes;
    HtmlNodeHandle root;

    /**
     * Document finalization (Phase 8.4). While 0 the tree-construction
     * APIs (HtmlDocumentRegisterNode / HtmlNodeLinkChild)
     * may still change the tree; once 1 the tree is closed for
     * construction and only read/traversal stays available. Cleared by
     * HtmlDocumentClearTree, so re-parsing a document is explicit.
     */
    int finalized;

    /**
     * Structural-change counter (Phase 8.9). Bumped once per successful
     * tree mutation — a parser link, a DOM append/insert/replace, a DOM
     * removal, a bulk clear. It is the document's "shape" generation: a
     * traversal that snapshots it can tell that the tree it was walking is
     * no longer the tree it started on, which is the only reason an
     * iteration can be declared invalid rather than silently wrong.
     *
     * Monotonic for the slot's lifetime and never reset to 0, because
     * "version 0" has to keep meaning "no document".
     */
    uint32_t treeVersion;

    /**
     * Node payload arena (Phase 8.3). Every element name, attribute pair,
     * text run, comment and doctype string lives here; a node entry stores
     * only a byte range. Bump-allocated, append-only, freed as a unit at
     * document teardown — which is why a half-built tree cannot leak and
     * why lookups never chase a freed per-node allocation.
     */
    char *payload;
    size_t payloadUsed;
    size_t payloadCapacity;

    /** Parser lease (exclusive; NULL when unleased). */
    struct HtmlParser *leasedParser;

    /** Client/render hooks (8.1: lifecycle + invalidation). */
    HtmlClientHooks client;
    void *clientUserdata;

    /** Mutation hook foundation (8.1: node registration + lifecycle). */
    HtmlMutationHookFn MutationHook;
    void *mutationUserdata;

    /** Resource boundary slot (acquisition delegated; see RoboHTMLResource.h). */
    HtmlLoadPhase loadPhase;
    HtmlLoader *loader;

    /**
     * Image resources (Phase 8.12), one per `<img>` that has been resolved.
     *
     * The table is keyed by *node identity* — document, node id and
     * generation — not by element pointer, so a moved element keeps its
     * picture while a recycled node id cannot inherit one: a fresh node gets
     * a new generation, which is exactly what generations are for.
     *
     * It lives here rather than in the node payload because the payload
     * arena is freed and re-bumped as a unit, while a decoded image is a
     * separately owned RoboGraphics object with its own lifetime. Releasing
     * the array at teardown releases the document's claims on them; the
     * images themselves follow their own refcount, so a caller that retained
     * one keeps working after the document is gone.
     */
    struct HtmlImageAttachment *images;
    size_t imageCount;
    size_t imageCapacity;

    /**
     * Form-control state (Phase 8.14), one entry per control whose *current*
     * value or checkedness has been set explicitly.
     *
     * Keyed by node identity for the same reason the image table is: a moved
     * control keeps its state, and a recycled node id cannot inherit one
     * because a fresh node gets a new generation.
     *
     * It is deliberately NOT stored in the element's payload. The payload is
     * the attribute record, and it is rewritten wholesale on every attribute
     * mutation — so control state living there would be clobbered by the very
     * operation the model has to survive (`<input value="a">` then
     * `setAttribute("value", "b")` must not resurrect a cleared value), and
     * the attribute payload would have to grow a concept that only form
     * controls have. A separate table keeps the attribute model and the
     * element-type model independent, which is what lets an attribute be read
     * by code that has never heard of forms.
     *
     * An entry exists only while a control is *not* tracking its attribute:
     * no entry means "current value is the declared value", so the common
     * case costs no memory and a document full of untouched inputs allocates
     * nothing. That is also why `present` is not a field — the entry's
     * existence is the fact.
     */
    struct HtmlControlState *controls;
    size_t controlCount;
    size_t controlCapacity;

    /**
     * DOM selector matcher (Phase 17.1): the function that answers
     * `querySelector`/`querySelectorAll` for this document, or NULL when none
     * is installed. It lives by document rather than module-wide because the
     * document is the only thing the query doors are given, and because it
     * carries no subscription state a replacement would lose (unlike the
     * mutation hook).
     */
    HtmlSelectorMatchFn selectorMatch;
    void *selectorUserdata;
} HtmlDocumentSlot;

/**
 * Register a node without consulting the finalize gate (Phase 17.1).
 *
 * `HtmlDocumentRegisterNode` closes when a build finalizes the tree; the DOM's
 * `createElement` family must keep producing nodes for a live document, so it
 * uses this door instead. The lifecycle gate still applies, so a failed or
 * destroyed document accepts nothing.
 */
Result HtmlDocumentRegisterNodeLive(
    HtmlDocumentId documentId,
    HtmlNodeKind kind,
    HtmlNodeOrigin origin,
    HtmlNodeHandle *outHandle,
    HtmlDiag *diag);

/** One image resource attached to one node of one document. */
typedef struct HtmlImageAttachment {
    HtmlNodeHandle node;
    struct HtmlImage *image;
} HtmlImageAttachment;

/**
 * Explicit state of one form control (Phase 8.14).
 *
 * `value` is owned by the table (NUL-terminated as well as counted, so a C
 * caller can pass it on without copying) and is NULL when only the checkedness
 * was set explicitly — the two overrides are independent, exactly as they are
 * in HTML, where setting `.value` does not touch `.checked`.
 *
 * There is no `value_dirty` flag beside a nullable pointer: a NULL pointer
 * *is* "not overridden", and a flag next to it could only disagree.
 */
typedef struct HtmlControlState {
    HtmlNodeHandle node;
    /** Owned current value, or NULL when the value still tracks the attribute. */
    char *value;
    size_t valueLength;
    /** Non-zero when a checkedness override exists, in @c checked. */
    int checkedSet;
    int checked;
} HtmlControlState;

/**
 * Release every control-state entry and the table itself (Phase 8.14).
 * Safe on a NULL slot and on a slot that never held one; clears the fields.
 * Called from both teardown paths — HtmlDocumentClearTree and
 * HtmlDocumentDestroy — because in both cases the nodes the table
 * refers to are gone and a recycled id must not inherit their state.
 */
void HtmlDocumentControlsFree(HtmlDocumentSlot *slot);

/**
 * Release every attached image resource and the table itself (Phase 8.12).
 * Safe on a NULL slot and on a slot that never held an image; clears the
 * fields. Called from both teardown paths — HtmlDocumentClearTree and
 * HtmlDocumentDestroy — because in both cases the nodes the table
 * refers to are gone.
 */
void HtmlDocumentImagesFree(HtmlDocumentSlot *slot);

/** Parser object (heap-allocated; layout private to RoboHTML sources). */
struct HtmlParser {
    uint32_t magic; /* RoboHtmlParserMagic; guards use-after-destroy */
    HtmlDocumentId documentId;
    HtmlParserState state;
    HtmlParserMode mode;
    HtmlSourceKind sourceKind;
    size_t sourceLength;
};

#define RoboHtmlParserMagic 0x524F4255u /* "ROBU" */

/* ------------------------------------------------------------------ *
 * Tokenizer internals (Phase 8.2)                                    *
 * ------------------------------------------------------------------ */

#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"

/** Growable byte buffer for token text (tag names, data, comments, ...). */
typedef struct HtmlTokenBuffer {
    char *data;
    size_t length;
    size_t capacity; /* includes the NUL slot */
} HtmlTokenBuffer;

void HtmlTokenBufferInit(HtmlTokenBuffer *buf);
void HtmlTokenBufferFree(HtmlTokenBuffer *buf);
Result HtmlTokenBufferReserve(HtmlTokenBuffer *buf,
                                             size_t extra);
void HtmlTokenBufferClear(HtmlTokenBuffer *buf);
Result HtmlTokenBufferAppendByte(
    HtmlTokenBuffer *buf, char byte);
Result HtmlTokenBufferAppendUtf8(
    HtmlTokenBuffer *buf, uint32_t cp);

/** One pending attribute (name + value scratch buffers). */
typedef struct HtmlTokenAttrEntry {
    HtmlTokenBuffer name;
    HtmlTokenBuffer value;
} HtmlTokenAttrEntry;

/**
 * Attribute pool reused across tokens. HtmlTokenPoolNext hands out
 * an entry that has already been reset, so a reused slot can never carry a
 * previous token's name or value into the next one.
 */
typedef struct HtmlTokenPool {
    HtmlTokenAttrEntry *entries;
    size_t count;
    size_t capacity;
} HtmlTokenPool;

void HtmlTokenPoolInit(HtmlTokenPool *pool);
void HtmlTokenPoolFree(HtmlTokenPool *pool);
void HtmlTokenPoolClear(HtmlTokenPool *pool);
Result HtmlTokenPoolNext(HtmlTokenPool *pool,
                                        HtmlTokenAttrEntry **out);

/** Bounded in-tokenizer diagnostic list (survives until reset). */
typedef struct HtmlDiagList {
    HtmlDiag items[RoboHtmlTokenizerMaxDiagnostics];
    size_t count;
} HtmlDiagList;

void HtmlDiagnosticsInit(HtmlDiagList *list);
void HtmlDiagnosticsPush(HtmlDiagList *list,
                                Result code,
                                HtmlDiagnosticDomain domain,
                                const char *message,
                                uint32_t line,
                                uint32_t column);
void HtmlDiagnosticsClear(HtmlDiagList *list);

/** Document table access (RoboHTMLDocument.c). */
HtmlDocumentSlot *HtmlDocumentSlotFind(
    HtmlDocumentId id);
HtmlDocumentSlot *HtmlDocumentSlotAlloc(void);

/* ------------------------------------------------------------------ *
 * Node payload arena (Phase 8.3, RoboHTMLDocument.c)                   *
 * ------------------------------------------------------------------ */

/**
 * Node payload header. Every payload starts with this header followed by
 * kind-specific bytes:
 *
 *   ELEMENT       name '\0'  then per attribute
 *                 [u32 nameLen][u32 valueLen] name '\0' value '\0'
 *   TEXT/COMMENT  bytes '\0'
 *   DOCUMENT_TYPE name '\0' publicId '\0' systemId '\0'
 *   DOCUMENT      no payload
 */
typedef struct HtmlPayloadHeader {
    uint32_t length;     /* payload bytes after this header */
    uint32_t attrCount; /* ELEMENT only */
    uint32_t flags;      /* ELEMENT only; see ROBO_HTML_PAYLOAD_FLAG_* */
    /**
     * ELEMENT only: the element's namespace (a HtmlNamespace).
     * Namespaces belong to the element, not to the name string, so they are
     * stored once per element rather than being folded into the tag name.
     */
    uint32_t ns;
} HtmlPayloadHeader;

/** Element payload flag: the source had "/>". */
#define RoboHtmlPayloadFlagSelfClosing 1u

/**
 * Alignment, in bytes, guaranteed for the start of every arena reservation.
 *
 * The arena is a byte cursor, so without this a name of an odd length would
 * place the *next* reservation at an odd offset — and a payload header is
 * four `uint32_t`s, which is undefined behaviour to access unaligned. The
 * value is the header's own required alignment rather than a word size,
 * because the header is the only type the arena is ever reinterpreted as;
 * string payloads live *inside* a reservation and need no alignment of their
 * own.
 */
#define RoboHtmlPayloadAlignment 4u

/**
 * Reserve @p size bytes at the arena tail.
 *
 * The returned offset is aligned to @c RoboHtmlPayloadAlignment (padding,
 * when needed, is skipped and never read), and the reservation still ends
 * exactly at the new @c payloadUsed, so a payload that ends the arena can
 * still be grown in place by HtmlDocumentArenaExtendTail.
 *
 * @p outPtr is borrowed until the next arena call (growth may move it).
 */
Result HtmlDocumentArenaAlloc(HtmlDocumentSlot *slot,
                                             size_t size,
                                             uint32_t *outOffset,
                                             void **outPtr,
                                             HtmlDiag *diag);

/**
 * Grow the allocation that currently ends the arena by @p extra bytes.
 * Used to append adjacent text into one node instead of allocating another.
 * Fails with RoboNotSupported when @p offset is not the arena tail.
 */
Result HtmlDocumentArenaExtendTail(
    HtmlDocumentSlot *slot,
    uint32_t offset,
    size_t oldSize,
    size_t extra,
    void **outPtr,
    HtmlDiag *diag);

/** Borrowed pointer to an existing payload range; NULL when out of bounds. */
const void *HtmlDocumentArenaAt(const HtmlDocumentSlot *slot,
                                        uint32_t offset,
                                        uint32_t length);

/** Free the arena and reset the cursor (document teardown). */
void HtmlDocumentArenaFree(HtmlDocumentSlot *slot);

/**
 * Drop the whole tree: the payload arena and every node except the
 * document node, whose identity is preserved (fresh generation, so handles
 * to the discarded tree are stale). Node ids restart dense and
 * construction reopens.
 */
Result HtmlDocumentClearTree(HtmlDocumentSlot *slot,
                                            HtmlDiag *diag);

/* ------------------------------------------------------------------ *
 * Internal tree mutation (Phase 8.3)                                  *
 * ------------------------------------------------------------------ */

/** Mutable registry entry for @p nodeId; NULL when absent. */
HtmlNodeEntry *HtmlNodeEntryMut(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId);

/** Read-only registry entry for @p nodeId; NULL when absent. */
const HtmlNodeEntry *HtmlNodeEntryGet(
    HtmlDocumentId documentId,
    HtmlNodeId nodeId);

/** Build an inert handle from a registry entry. */
HtmlNodeHandle HtmlNodeHandleMake(
    HtmlDocumentId documentId,
    const HtmlNodeEntry *entry);

/**
 * Append @p childId as the last child of @p parentId.
 * Rejects self-parenting, non-live ids, and a child that already has a
 * parent. Never touches payload or ordering beyond the sibling links.
 *
 * This is the *construction* path: it refuses once the document is
 * finalized, which is what closes a parser's tree. The DOM mutation API
 * (RoboHTMLMutation.h) deliberately does not use it — see
 * HtmlNodeInsertRelative.
 */
Result HtmlNodeLinkChild(HtmlDocumentId documentId,
                                        HtmlNodeId parentId,
                                        HtmlNodeId childId,
                                        HtmlDiag *diag);

/**
 * The one place a child link is made.
 *
 * Inserts @p childId into @p parentId's child list immediately before
 * @p referenceId, or as the last child when @p referenceId is
 * RoboHtmlNodeIdNone. Applies every structural rule in one function so
 * a parser link and a DOM link cannot disagree about them:
 *
 *   - @p parentId and @p childId must be live nodes of this document;
 *   - the parent's kind must be able to hold children;
 *   - the document node can never become a child;
 *   - @p childId must be detached (a node has at most one parent — this
 *     primitive never moves a node implicitly; the caller must unlink
 *     first, which is what makes an ownership transfer an explicit act);
 *   - @p childId must not be @p parentId or an ancestor of it, so the
 *     result is always acyclic;
 *   - @p referenceId, when given, must be a live child of @p parentId.
 *
 * Bumps the document's tree version exactly once on success and never
 * partially links: every rejection happens before the first write.
 *
 * Unlike HtmlNodeLinkChild this does NOT consult the construction
 * gate. A finalized document is still mutable by the DOM; finalization
 * closes the parser's construction, not the tree's editability (see
 * RoboHTMLDocument.h).
 */
Result HtmlNodeInsertRelative(HtmlDocumentId documentId,
                                             HtmlNodeId parentId,
                                             HtmlNodeId childId,
                                             HtmlNodeId referenceId,
                                             HtmlDiag *diag);

/**
 * Non-zero when @p kind is a node that can hold children (document,
 * element, fragment). The one answer to "can this be a parent?", shared by
 * the parser link, the DOM mutation API and the linking primitive so the
 * three can never disagree about the shape of a tree.
 */
int HtmlNodeKindCanHoldChildren(HtmlNodeKind kind);

/**
 * Non-zero when @p ancestorId is @p nodeId itself or reachable from
 * @p nodeId by following parent links. Bounded by the node ceiling, so a
 * corrupted parent chain terminates as 0 instead of hanging. False for
 * unknown or non-live ids.
 */
int HtmlNodeIsAncestorOf(HtmlDocumentId documentId,
                                  HtmlNodeId ancestorId,
                                  HtmlNodeId nodeId);

/** Bump @p slot's structural-change counter (never wraps to 0). */
void HtmlDocumentNoteTreeChange(HtmlDocumentSlot *slot);

/**
 * Current structural-change counter of @p documentId, or 0 when the
 * document is unknown/destroyed. 0 is never a live version, so a caller
 * that stores it can use it as "no document" too.
 */
uint32_t HtmlDocumentTreeVersion(
    HtmlDocumentId documentId);

/**
 * Detach @p nodeId from its parent and siblings. The node keeps its own
 * subtree and payload; it becomes a detached root. No-op (RoboOk) when it
 * is already detached. Bumps the document's tree version when it actually
 * moves the node, because a detach is a structural change like any other.
 */
Result HtmlNodeUnlink(HtmlDocumentId documentId,
                                    HtmlNodeId nodeId,
                                    HtmlDiag *diag);

/** Record @p entry's payload range (kind-specific bytes already written). */
void HtmlNodeSetPayload(HtmlNodeEntry *entry,
                                uint32_t offset,
                                uint32_t length);

/* ------------------------------------------------------------------ *
 * Node payload layout (Phase 8.3, RoboHTMLPayload.c)                   *
 * ------------------------------------------------------------------ *
 * Encode/decode live in one translation unit so the byte layout has a
 * single definition: writers never hand-roll offsets and readers never
 * guess at them.
 */

/** Refuse to store more attributes than this on one element (bounded memory). */
#define RoboHtmlMaxAttrsPerElement 64u

/**
 * ASCII case-insensitive name equality — the runtime's one name
 * comparison, in the one translation unit that also defines the byte
 * layout, so element-name and attribute-name lookups cannot disagree.
 * NULL operands are never equal; both must be NUL-terminated.
 */
int HtmlNameEqual(const char *a, const char *b);

/**
 * Non-zero when @p text holds something once surrounding ASCII whitespace is
 * ignored: present, and not blank.
 *
 * This is the "did the author actually name something?" test, and it is the
 * same rule everywhere it is asked — `<a href="  ">` names nothing, and so
 * does `<form action="  ">` or a control's `form="  "`. Callers with an
 * extra policy of their own compose it with their own filter rather than
 * re-implementing this one (the anchor module also refuses control bytes, so
 * its usable test is this rule plus that check).
 */
int HtmlSpanIsUsable(const char *text, size_t length);

/** Same, with @p a given by length (a payload name is length-prefixed). */
int HtmlNameEqualSpan(const char *a, size_t aLength, const char *b);

/** Bytes an element payload needs for @p name and @p attrs. */
size_t HtmlPayloadElementSize(const char *name,
                                      size_t nameLength,
                                      const HtmlAttribute *attrs,
                                      size_t attrCount);

/** Write an element payload; on success records the range on @p entry. */
Result HtmlPayloadWriteElement(
    HtmlDocumentSlot *slot,
    HtmlNodeEntry *entry,
    const char *name,
    size_t nameLength,
    const HtmlAttribute *attrs,
    size_t attrCount,
    int selfClosing,
    HtmlNamespace ns,
    HtmlDiag *diag);

/** How one attribute mutation changes the stored table (Phase 8.6). */
typedef enum HtmlAttrMutation {
    /** Append @p name unconditionally; the only way to duplicate a name. */
    RoboHtmlAttrMutationAdd = 0,
    /** Collapse every @p name to one record at the first match's position;
     *  appends when no match, matching DOM setAttribute. */
    RoboHtmlAttrMutationSet = 1,
    /** Drop every @p name; absent is a no-op. */
    RoboHtmlAttrMutationRemove = 2
} HtmlAttrMutation;

/**
 * Apply one attribute mutation to a live element payload.
 *
 * Implemented as a payload *rewrite*, because an element payload is written
 * once and never edited in place: the new table is built in a scratch block
 * measured from the old one plus the change, then appended to the arena and
 * adopted by the entry. The superseded bytes stay in the arena as dead
 * space until document teardown — the arena is append-only and freed as a
 * unit, which is what keeps an element's payload range describable by two
 * integers however many times it is rewritten.
 *
 * @p name must be non-empty. @p name and @p value are borrowed for the
 * duration of the call only; aliasing the arena (a value read from another
 * attribute, say) is safe, because every byte is copied into the scratch
 * block before the arena is touched.
 *
 * @return RoboOk (including REMOVE of an absent name, which changes
 *         nothing); RoboInvalidArgument (NULL/empty name); RoboNotFound
 *         (entry is not a live element payload); RoboNotSupported (the
 *         result would exceed RoboHtmlMaxAttrsPerElement);
 *         RoboOutOfMemory.
 */
Result HtmlPayloadMutateAttributes(
    HtmlDocumentSlot *slot,
    HtmlNodeEntry *entry,
    HtmlAttrMutation mutation,
    const char *name,
    const char *value,
    HtmlDiag *diag);

/** Write a text/comment payload; on success records the range on @p entry. */
Result HtmlPayloadWriteText(HtmlDocumentSlot *slot,
                                           HtmlNodeEntry *entry,
                                           const char *bytes,
                                           size_t length,
                                           HtmlDiag *diag);

/**
 * Append @p length bytes to a text payload that currently ends the arena.
 * Returns RoboNotSupported when the payload is not the arena tail (the
 * caller then creates a sibling text node instead of merging).
 */
Result HtmlPayloadAppendText(HtmlDocumentSlot *slot,
                                            HtmlNodeEntry *entry,
                                            const char *bytes,
                                            size_t length,
                                            HtmlDiag *diag);

/** Write a DOCTYPE payload (absent identifiers become empty strings). */
Result HtmlPayloadWriteDoctype(HtmlDocumentSlot *slot,
                                              HtmlNodeEntry *entry,
                                              const char *name,
                                              const char *publicId,
                                              const char *systemId,
                                              HtmlDiag *diag);

/** Borrowed element name ("" when the node is not an element). */
const char *HtmlPayloadElementName(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *entry,
    size_t *outLength);

/** Stored attribute count (0 when the node is not an element). */
size_t HtmlPayloadAttrCount(const HtmlDocumentSlot *slot,
                                    const HtmlNodeEntry *entry);

/** Borrowed attribute @p index in stored order; RoboNotFound when out of
 *  range. The out-struct's strings point into the payload arena. */
Result HtmlPayloadAttrAt(const HtmlDocumentSlot *slot,
                                        const HtmlNodeEntry *entry,
                                        size_t index,
                                        HtmlAttribute *out);

/**
 * First attribute named @p name (ASCII case-insensitive), RoboNotFound
 * when absent. First-match is the model's lookup rule: parsed duplicates are
 * preserved, so a later duplicate is still reachable by index.
 */
Result HtmlPayloadAttrFind(const HtmlDocumentSlot *slot,
                                          const HtmlNodeEntry *entry,
                                          const char *name,
                                          HtmlAttribute *out);

/** Borrowed text/comment bytes ("" when not applicable); 0 when absent. */
const char *HtmlPayloadText(const HtmlDocumentSlot *slot,
                                   const HtmlNodeEntry *entry,
                                   size_t *outLength);

/** Non-zero when the element payload recorded "/>" in the source. */
int HtmlPayloadSelfClosing(const HtmlDocumentSlot *slot,
                                   const HtmlNodeEntry *entry);

/** Stored element namespace; NONE when the node is not an element. */
HtmlNamespace HtmlPayloadNamespace(
    const HtmlDocumentSlot *slot,
    const HtmlNodeEntry *entry);

/** Borrowed DOCTYPE parts (absent parts are ""). */
void HtmlPayloadDoctype(const HtmlDocumentSlot *slot,
                               const HtmlNodeEntry *entry,
                               const char **outName,
                               const char **outPublicId,
                               const char **outSystemId);

/** Mutation hook emission (RoboHTMLDom.c). */
void HtmlEmitMutation(const HtmlMutationEvent *event,
                             void *userdata);

/** Client hook emission (RoboHTMLClient.c). */
void HtmlEmitLifecycle(HtmlDocumentSlot *slot,
                              HtmlDocumentLifecycle from,
                              HtmlDocumentLifecycle to);
void HtmlEmitInvalidate(HtmlDocumentSlot *slot,
                               HtmlInvalidationReason reason);

/** Lifecycle transition worker shared by document/parser/resource paths. */
Result HtmlDocumentApplyTransition(
    HtmlDocumentSlot *slot,
    HtmlDocumentLifecycle to);

#endif /* ROBO_HTML_INTERNAL_H */
