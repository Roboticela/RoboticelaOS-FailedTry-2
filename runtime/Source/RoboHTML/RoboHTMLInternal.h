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
typedef struct robo_html_document_slot {
    int in_use;
    robo_html_document_id_t id;

    robo_html_document_lifecycle_t lifecycle;
    robo_html_document_lifecycle_t previous_lifecycle;

    /** Failure record for FAILED documents (diagnostics, no heap). */
    robo_html_diagnostic_domain_t fail_domain;
    const char *fail_message;

    /** Attached source metadata (kind; bytes stay application-owned). */
    robo_html_source_kind_t source_kind;
    size_t source_length;
    char source_name[ROBO_HTML_URL_MAX];

    robo_html_encoding_state_t encoding;

    robo_html_node_registry_t nodes;
    robo_html_node_handle_t root;

    /**
     * Document finalization (Phase 8.4). While 0 the tree-construction
     * APIs (robo_html_document_register_node / robo_html_node_link_child)
     * may still change the tree; once 1 the tree is closed for
     * construction and only read/traversal stays available. Cleared by
     * robo_html_document_clear_tree, so re-parsing a document is explicit.
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
    uint32_t tree_version;

    /**
     * Node payload arena (Phase 8.3). Every element name, attribute pair,
     * text run, comment and doctype string lives here; a node entry stores
     * only a byte range. Bump-allocated, append-only, freed as a unit at
     * document teardown — which is why a half-built tree cannot leak and
     * why lookups never chase a freed per-node allocation.
     */
    char *payload;
    size_t payload_used;
    size_t payload_capacity;

    /** Parser lease (exclusive; NULL when unleased). */
    struct robo_html_parser *leased_parser;

    /** Client/render hooks (8.1: lifecycle + invalidation). */
    robo_html_client_hooks_t client;
    void *client_userdata;

    /** Mutation hook foundation (8.1: node registration + lifecycle). */
    robo_html_mutation_hook_fn mutation_hook;
    void *mutation_userdata;

    /** Resource boundary slot (acquisition delegated; see RoboHTMLResource.h). */
    robo_html_load_phase_t load_phase;
    robo_html_loader_t *loader;

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
    struct robo_html_image_attachment *images;
    size_t image_count;
    size_t image_capacity;

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
    struct robo_html_control_state *controls;
    size_t control_count;
    size_t control_capacity;
} robo_html_document_slot_t;

/** One image resource attached to one node of one document. */
typedef struct robo_html_image_attachment {
    robo_html_node_handle_t node;
    struct robo_html_image *image;
} robo_html_image_attachment_t;

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
typedef struct robo_html_control_state {
    robo_html_node_handle_t node;
    /** Owned current value, or NULL when the value still tracks the attribute. */
    char *value;
    size_t value_length;
    /** Non-zero when a checkedness override exists, in @c checked. */
    int checked_set;
    int checked;
} robo_html_control_state_t;

/**
 * Release every control-state entry and the table itself (Phase 8.14).
 * Safe on a NULL slot and on a slot that never held one; clears the fields.
 * Called from both teardown paths — robo_html_document_clear_tree and
 * robo_html_document_destroy — because in both cases the nodes the table
 * refers to are gone and a recycled id must not inherit their state.
 */
void robo_html_document_controls_free(robo_html_document_slot_t *slot);

/**
 * Release every attached image resource and the table itself (Phase 8.12).
 * Safe on a NULL slot and on a slot that never held an image; clears the
 * fields. Called from both teardown paths — robo_html_document_clear_tree and
 * robo_html_document_destroy — because in both cases the nodes the table
 * refers to are gone.
 */
void robo_html_document_images_free(robo_html_document_slot_t *slot);

/** Parser object (heap-allocated; layout private to RoboHTML sources). */
struct robo_html_parser {
    uint32_t magic; /* ROBO_HTML_PARSER_MAGIC; guards use-after-destroy */
    robo_html_document_id_t document_id;
    robo_html_parser_state_t state;
    robo_html_parser_mode_t mode;
    robo_html_source_kind_t source_kind;
    size_t source_length;
};

#define ROBO_HTML_PARSER_MAGIC 0x524F4255u /* "ROBU" */

/* ------------------------------------------------------------------ *
 * Tokenizer internals (Phase 8.2)                                    *
 * ------------------------------------------------------------------ */

#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"

/** Growable byte buffer for token text (tag names, data, comments, ...). */
typedef struct robo_html_token_buffer {
    char *data;
    size_t length;
    size_t capacity; /* includes the NUL slot */
} robo_html_token_buffer_t;

void robo_html_token_buffer_init(robo_html_token_buffer_t *buf);
void robo_html_token_buffer_free(robo_html_token_buffer_t *buf);
robo_result_t robo_html_token_buffer_reserve(robo_html_token_buffer_t *buf,
                                             size_t extra);
void robo_html_token_buffer_clear(robo_html_token_buffer_t *buf);
robo_result_t robo_html_token_buffer_append_byte(
    robo_html_token_buffer_t *buf, char byte);
robo_result_t robo_html_token_buffer_append_utf8(
    robo_html_token_buffer_t *buf, uint32_t cp);

/** One pending attribute (name + value scratch buffers). */
typedef struct robo_html_token_attr_entry {
    robo_html_token_buffer_t name;
    robo_html_token_buffer_t value;
} robo_html_token_attr_entry_t;

/**
 * Attribute pool reused across tokens. robo_html_token_pool_next hands out
 * an entry that has already been reset, so a reused slot can never carry a
 * previous token's name or value into the next one.
 */
typedef struct robo_html_token_pool {
    robo_html_token_attr_entry_t *entries;
    size_t count;
    size_t capacity;
} robo_html_token_pool_t;

void robo_html_token_pool_init(robo_html_token_pool_t *pool);
void robo_html_token_pool_free(robo_html_token_pool_t *pool);
void robo_html_token_pool_clear(robo_html_token_pool_t *pool);
robo_result_t robo_html_token_pool_next(robo_html_token_pool_t *pool,
                                        robo_html_token_attr_entry_t **out);

/** Bounded in-tokenizer diagnostic list (survives until reset). */
typedef struct robo_html_diag_list {
    robo_html_diag_t items[ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS];
    size_t count;
} robo_html_diag_list_t;

void robo_html_diagnostics_init(robo_html_diag_list_t *list);
void robo_html_diagnostics_push(robo_html_diag_list_t *list,
                                robo_result_t code,
                                robo_html_diagnostic_domain_t domain,
                                const char *message,
                                uint32_t line,
                                uint32_t column);
void robo_html_diagnostics_clear(robo_html_diag_list_t *list);

/** Document table access (RoboHTMLDocument.c). */
robo_html_document_slot_t *robo_html_document_slot_find(
    robo_html_document_id_t id);
robo_html_document_slot_t *robo_html_document_slot_alloc(void);

/* ------------------------------------------------------------------ *
 * Node payload arena (Phase 8.3, RoboHTMLDocument.c)                   *
 * ------------------------------------------------------------------ */

/**
 * Node payload header. Every payload starts with this header followed by
 * kind-specific bytes:
 *
 *   ELEMENT       name '\0'  then per attribute
 *                 [u32 name_len][u32 value_len] name '\0' value '\0'
 *   TEXT/COMMENT  bytes '\0'
 *   DOCUMENT_TYPE name '\0' public_id '\0' system_id '\0'
 *   DOCUMENT      no payload
 */
typedef struct robo_html_payload_header {
    uint32_t length;     /* payload bytes after this header */
    uint32_t attr_count; /* ELEMENT only */
    uint32_t flags;      /* ELEMENT only; see ROBO_HTML_PAYLOAD_FLAG_* */
    /**
     * ELEMENT only: the element's namespace (a robo_html_namespace_t).
     * Namespaces belong to the element, not to the name string, so they are
     * stored once per element rather than being folded into the tag name.
     */
    uint32_t ns;
} robo_html_payload_header_t;

/** Element payload flag: the source had "/>". */
#define ROBO_HTML_PAYLOAD_FLAG_SELF_CLOSING 1u

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
#define ROBO_HTML_PAYLOAD_ALIGNMENT 4u

/**
 * Reserve @p size bytes at the arena tail.
 *
 * The returned offset is aligned to @c ROBO_HTML_PAYLOAD_ALIGNMENT (padding,
 * when needed, is skipped and never read), and the reservation still ends
 * exactly at the new @c payload_used, so a payload that ends the arena can
 * still be grown in place by robo_html_document_arena_extend_tail.
 *
 * @p out_ptr is borrowed until the next arena call (growth may move it).
 */
robo_result_t robo_html_document_arena_alloc(robo_html_document_slot_t *slot,
                                             size_t size,
                                             uint32_t *out_offset,
                                             void **out_ptr,
                                             robo_html_diag_t *diag);

/**
 * Grow the allocation that currently ends the arena by @p extra bytes.
 * Used to append adjacent text into one node instead of allocating another.
 * Fails with ROBO_NOT_SUPPORTED when @p offset is not the arena tail.
 */
robo_result_t robo_html_document_arena_extend_tail(
    robo_html_document_slot_t *slot,
    uint32_t offset,
    size_t old_size,
    size_t extra,
    void **out_ptr,
    robo_html_diag_t *diag);

/** Borrowed pointer to an existing payload range; NULL when out of bounds. */
const void *robo_html_document_arena_at(const robo_html_document_slot_t *slot,
                                        uint32_t offset,
                                        uint32_t length);

/** Free the arena and reset the cursor (document teardown). */
void robo_html_document_arena_free(robo_html_document_slot_t *slot);

/**
 * Drop the whole tree: the payload arena and every node except the
 * document node, whose identity is preserved (fresh generation, so handles
 * to the discarded tree are stale). Node ids restart dense and
 * construction reopens.
 */
robo_result_t robo_html_document_clear_tree(robo_html_document_slot_t *slot,
                                            robo_html_diag_t *diag);

/* ------------------------------------------------------------------ *
 * Internal tree mutation (Phase 8.3)                                  *
 * ------------------------------------------------------------------ */

/** Mutable registry entry for @p node_id; NULL when absent. */
robo_html_node_entry_t *robo_html_node_entry_mut(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id);

/** Read-only registry entry for @p node_id; NULL when absent. */
const robo_html_node_entry_t *robo_html_node_entry_get(
    robo_html_document_id_t document_id,
    robo_html_node_id_t node_id);

/** Build an inert handle from a registry entry. */
robo_html_node_handle_t robo_html_node_handle_make(
    robo_html_document_id_t document_id,
    const robo_html_node_entry_t *entry);

/**
 * Append @p child_id as the last child of @p parent_id.
 * Rejects self-parenting, non-live ids, and a child that already has a
 * parent. Never touches payload or ordering beyond the sibling links.
 *
 * This is the *construction* path: it refuses once the document is
 * finalized, which is what closes a parser's tree. The DOM mutation API
 * (RoboHTMLMutation.h) deliberately does not use it — see
 * robo_html_node_insert_relative.
 */
robo_result_t robo_html_node_link_child(robo_html_document_id_t document_id,
                                        robo_html_node_id_t parent_id,
                                        robo_html_node_id_t child_id,
                                        robo_html_diag_t *diag);

/**
 * The one place a child link is made.
 *
 * Inserts @p child_id into @p parent_id's child list immediately before
 * @p reference_id, or as the last child when @p reference_id is
 * ROBO_HTML_NODE_ID_NONE. Applies every structural rule in one function so
 * a parser link and a DOM link cannot disagree about them:
 *
 *   - @p parent_id and @p child_id must be live nodes of this document;
 *   - the parent's kind must be able to hold children;
 *   - the document node can never become a child;
 *   - @p child_id must be detached (a node has at most one parent — this
 *     primitive never moves a node implicitly; the caller must unlink
 *     first, which is what makes an ownership transfer an explicit act);
 *   - @p child_id must not be @p parent_id or an ancestor of it, so the
 *     result is always acyclic;
 *   - @p reference_id, when given, must be a live child of @p parent_id.
 *
 * Bumps the document's tree version exactly once on success and never
 * partially links: every rejection happens before the first write.
 *
 * Unlike robo_html_node_link_child this does NOT consult the construction
 * gate. A finalized document is still mutable by the DOM; finalization
 * closes the parser's construction, not the tree's editability (see
 * RoboHTMLDocument.h).
 */
robo_result_t robo_html_node_insert_relative(robo_html_document_id_t document_id,
                                             robo_html_node_id_t parent_id,
                                             robo_html_node_id_t child_id,
                                             robo_html_node_id_t reference_id,
                                             robo_html_diag_t *diag);

/**
 * Non-zero when @p kind is a node that can hold children (document,
 * element, fragment). The one answer to "can this be a parent?", shared by
 * the parser link, the DOM mutation API and the linking primitive so the
 * three can never disagree about the shape of a tree.
 */
int robo_html_node_kind_can_hold_children(robo_html_node_kind_t kind);

/**
 * Non-zero when @p ancestor_id is @p node_id itself or reachable from
 * @p node_id by following parent links. Bounded by the node ceiling, so a
 * corrupted parent chain terminates as 0 instead of hanging. False for
 * unknown or non-live ids.
 */
int robo_html_node_is_ancestor_of(robo_html_document_id_t document_id,
                                  robo_html_node_id_t ancestor_id,
                                  robo_html_node_id_t node_id);

/** Bump @p slot's structural-change counter (never wraps to 0). */
void robo_html_document_note_tree_change(robo_html_document_slot_t *slot);

/**
 * Current structural-change counter of @p document_id, or 0 when the
 * document is unknown/destroyed. 0 is never a live version, so a caller
 * that stores it can use it as "no document" too.
 */
uint32_t robo_html_document_tree_version(
    robo_html_document_id_t document_id);

/**
 * Detach @p node_id from its parent and siblings. The node keeps its own
 * subtree and payload; it becomes a detached root. No-op (ROBO_OK) when it
 * is already detached. Bumps the document's tree version when it actually
 * moves the node, because a detach is a structural change like any other.
 */
robo_result_t robo_html_node_unlink(robo_html_document_id_t document_id,
                                    robo_html_node_id_t node_id,
                                    robo_html_diag_t *diag);

/** Record @p entry's payload range (kind-specific bytes already written). */
void robo_html_node_set_payload(robo_html_node_entry_t *entry,
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
#define ROBO_HTML_MAX_ATTRS_PER_ELEMENT 64u

/**
 * ASCII case-insensitive name equality — the runtime's one name
 * comparison, in the one translation unit that also defines the byte
 * layout, so element-name and attribute-name lookups cannot disagree.
 * NULL operands are never equal; both must be NUL-terminated.
 */
int robo_html_name_equal(const char *a, const char *b);

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
int robo_html_span_is_usable(const char *text, size_t length);

/** Same, with @p a given by length (a payload name is length-prefixed). */
int robo_html_name_equal_span(const char *a, size_t a_length, const char *b);

/** Bytes an element payload needs for @p name and @p attrs. */
size_t robo_html_payload_element_size(const char *name,
                                      size_t name_length,
                                      const robo_html_attribute_t *attrs,
                                      size_t attr_count);

/** Write an element payload; on success records the range on @p entry. */
robo_result_t robo_html_payload_write_element(
    robo_html_document_slot_t *slot,
    robo_html_node_entry_t *entry,
    const char *name,
    size_t name_length,
    const robo_html_attribute_t *attrs,
    size_t attr_count,
    int self_closing,
    robo_html_namespace_t ns,
    robo_html_diag_t *diag);

/** How one attribute mutation changes the stored table (Phase 8.6). */
typedef enum robo_html_attr_mutation {
    /** Append @p name unconditionally; the only way to duplicate a name. */
    ROBO_HTML_ATTR_MUTATION_ADD = 0,
    /** Collapse every @p name to one record at the first match's position;
     *  appends when no match, matching DOM setAttribute. */
    ROBO_HTML_ATTR_MUTATION_SET = 1,
    /** Drop every @p name; absent is a no-op. */
    ROBO_HTML_ATTR_MUTATION_REMOVE = 2
} robo_html_attr_mutation_t;

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
 * @return ROBO_OK (including REMOVE of an absent name, which changes
 *         nothing); ROBO_INVALID_ARGUMENT (NULL/empty name); ROBO_NOT_FOUND
 *         (entry is not a live element payload); ROBO_NOT_SUPPORTED (the
 *         result would exceed ROBO_HTML_MAX_ATTRS_PER_ELEMENT);
 *         ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_payload_mutate_attributes(
    robo_html_document_slot_t *slot,
    robo_html_node_entry_t *entry,
    robo_html_attr_mutation_t mutation,
    const char *name,
    const char *value,
    robo_html_diag_t *diag);

/** Write a text/comment payload; on success records the range on @p entry. */
robo_result_t robo_html_payload_write_text(robo_html_document_slot_t *slot,
                                           robo_html_node_entry_t *entry,
                                           const char *bytes,
                                           size_t length,
                                           robo_html_diag_t *diag);

/**
 * Append @p length bytes to a text payload that currently ends the arena.
 * Returns ROBO_NOT_SUPPORTED when the payload is not the arena tail (the
 * caller then creates a sibling text node instead of merging).
 */
robo_result_t robo_html_payload_append_text(robo_html_document_slot_t *slot,
                                            robo_html_node_entry_t *entry,
                                            const char *bytes,
                                            size_t length,
                                            robo_html_diag_t *diag);

/** Write a DOCTYPE payload (absent identifiers become empty strings). */
robo_result_t robo_html_payload_write_doctype(robo_html_document_slot_t *slot,
                                              robo_html_node_entry_t *entry,
                                              const char *name,
                                              const char *public_id,
                                              const char *system_id,
                                              robo_html_diag_t *diag);

/** Borrowed element name ("" when the node is not an element). */
const char *robo_html_payload_element_name(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *entry,
    size_t *out_length);

/** Stored attribute count (0 when the node is not an element). */
size_t robo_html_payload_attr_count(const robo_html_document_slot_t *slot,
                                    const robo_html_node_entry_t *entry);

/** Borrowed attribute @p index in stored order; ROBO_NOT_FOUND when out of
 *  range. The out-struct's strings point into the payload arena. */
robo_result_t robo_html_payload_attr_at(const robo_html_document_slot_t *slot,
                                        const robo_html_node_entry_t *entry,
                                        size_t index,
                                        robo_html_attribute_t *out);

/**
 * First attribute named @p name (ASCII case-insensitive), ROBO_NOT_FOUND
 * when absent. First-match is the model's lookup rule: parsed duplicates are
 * preserved, so a later duplicate is still reachable by index.
 */
robo_result_t robo_html_payload_attr_find(const robo_html_document_slot_t *slot,
                                          const robo_html_node_entry_t *entry,
                                          const char *name,
                                          robo_html_attribute_t *out);

/** Borrowed text/comment bytes ("" when not applicable); 0 when absent. */
const char *robo_html_payload_text(const robo_html_document_slot_t *slot,
                                   const robo_html_node_entry_t *entry,
                                   size_t *out_length);

/** Non-zero when the element payload recorded "/>" in the source. */
int robo_html_payload_self_closing(const robo_html_document_slot_t *slot,
                                   const robo_html_node_entry_t *entry);

/** Stored element namespace; NONE when the node is not an element. */
robo_html_namespace_t robo_html_payload_namespace(
    const robo_html_document_slot_t *slot,
    const robo_html_node_entry_t *entry);

/** Borrowed DOCTYPE parts (absent parts are ""). */
void robo_html_payload_doctype(const robo_html_document_slot_t *slot,
                               const robo_html_node_entry_t *entry,
                               const char **out_name,
                               const char **out_public_id,
                               const char **out_system_id);

/** Mutation hook emission (RoboHTMLDom.c). */
void robo_html_emit_mutation(const robo_html_mutation_event_t *event,
                             void *userdata);

/** Client hook emission (RoboHTMLClient.c). */
void robo_html_emit_lifecycle(robo_html_document_slot_t *slot,
                              robo_html_document_lifecycle_t from,
                              robo_html_document_lifecycle_t to);
void robo_html_emit_invalidate(robo_html_document_slot_t *slot,
                               robo_html_invalidation_reason_t reason);

/** Lifecycle transition worker shared by document/parser/resource paths. */
robo_result_t robo_html_document_apply_transition(
    robo_html_document_slot_t *slot,
    robo_html_document_lifecycle_t to);

#endif /* ROBO_HTML_INTERNAL_H */
