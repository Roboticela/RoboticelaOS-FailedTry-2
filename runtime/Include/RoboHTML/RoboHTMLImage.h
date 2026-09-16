/*
 * RoboHTML images (Phase 8.12) — the resource behind `<img>`.
 *
 * Phase 8.11 gave `img` a tag identity and void behaviour, so the parser
 * already builds the element correctly. What was missing is everything after
 * that: an `<img>` is only interesting because of the *resource* its `src`
 * names, and this header is that resource's model.
 *
 *   <img src="..." alt="..." width="128" height="64">
 *        │
 *        ├── attributes        robo_html_element_image_attributes
 *        │     src     what to fetch
 *        │     alt     text, kept verbatim (never decoded, never shaped)
 *        │     width   / height: declared dimensions, parsed safely
 *        │
 *        └── resource          robo_html_image_t
 *              └── state       NONE │ PENDING │ DECODED │ FAILED │ UNSUPPORTED
 *                    └── pixels  robo_gfx_image_t (RoboGraphics, RGBA8)
 *
 * Bytes are fetched through a seam, not through this module
 *   RoboHTML never opens a file, never resolves a host name and never speaks
 *   a protocol — the 8.1 boundary said so and this phase keeps it. A caller
 *   installs a robo_html_image_resolver_t; RoboHTML hands it the reference
 *   and receives borrowed bytes for the duration of the call. That is the
 *   whole contract, and it is why the future network loader is an
 *   *implementation of a resolver* rather than a change to this model.
 *
 *   The one resolver this phase ships is an in-memory asset table
 *   (robo_html_image_assets_*): named byte blobs the application registered.
 *   It is the safe local/test mechanism — it cannot touch the filesystem, so
 *   there is no path to traverse and nothing to sandbox. A reference that
 *   names a scheme (`http:`, `file:`, `data:`, ...) is refused as
 *   UNSUPPORTED rather than reported as "not found", because the two mean
 *   different things: one is "this runtime will not fetch that", the other
 *   is "the bytes you asked for are not here".
 *
 * Failure is state, not an exception
 *   A missing asset, undecodable bytes and a refused scheme all leave the DOM
 *   exactly as it was and move the image's *state*. That is the only shape
 *   that keeps an `<img>` from being able to break a document — the same
 *   reasoning the decoder's safe refusals rest on, one level up.
 *
 * What this phase is NOT: layout, intrinsic sizing, CSS sizing, srcset or
 * <picture>, responsive selection, `load`/`error` events, JavaScript bindings,
 * an accessibility tree, image caching, decoding off the main thread, or any
 * form of network, file or data-scheme access.
 *
 * See html/docs/Images.md.
 */

#ifndef ROBO_HTML_IMAGE_H
#define ROBO_HTML_IMAGE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Resource state                                                      */
/* ------------------------------------------------------------------ */

/**
 * Where an image resource has got to.
 *
 * The states are deliberately about *the resource*, not about the element:
 * `<img>` with no `src` is an element whose resource is NONE, which is a
 * valid state and not an error.
 */
typedef enum robo_html_image_state {
    /** No usable `src`: the attribute is absent or empty. Nothing to fetch. */
    ROBO_HTML_IMAGE_STATE_NONE = 0,
    /** `src` names something, but no resolver has run for it yet. */
    ROBO_HTML_IMAGE_STATE_PENDING = 1,
    /** Bytes were fetched and decoded; pixels are available. */
    ROBO_HTML_IMAGE_STATE_DECODED = 2,
    /** Fetching or decoding failed: not found, malformed, truncated. */
    ROBO_HTML_IMAGE_STATE_FAILED = 3,
    /** Refused by policy or unimplemented: a scheme, or an unsupported format. */
    ROBO_HTML_IMAGE_STATE_UNSUPPORTED = 4
} robo_html_image_state_t;

/** Non-zero if @p state is a defined enumerator. */
int robo_html_image_state_is_known(robo_html_image_state_t state);

/** Stable lowercase name of @p state; never NULL, never allocated. */
const char *robo_html_image_state_name(robo_html_image_state_t state);

/** Largest declared `width`/`height` this model accepts (a value above it is
 *  treated as invalid rather than clamped, so a typo cannot silently resize). */
#define ROBO_HTML_IMAGE_MAX_DECLARED_DIMENSION 1000000u

/* ------------------------------------------------------------------ */
/* Attribute reading (no fetching, no decoding)                        */
/* ------------------------------------------------------------------ */

/**
 * The image-relevant attributes of one element, read verbatim.
 *
 * Every string is borrowed from the element and stays valid only while the
 * element's attributes are unchanged — the same rule as
 * robo_html_element_attribute_value, which this is a typed view over.
 *
 * `width_valid` / `height_valid` are meaningful only when the corresponding
 * `_set` flag is 1: `set` means "the attribute is present and non-empty",
 * `valid` means "and it parsed as a dimension this phase accepts". That split
 * is what lets a caller tell `<img width>` from `<img width="abc">` from
 * `<img>` without guessing, and it is why an invalid dimension is reported
 * rather than quietly dropped.
 */
typedef struct robo_html_image_attributes {
    /** Non-zero when a `src` attribute is present at all. */
    int source_present;
    /** Borrowed `src` bytes; NULL when absent. */
    const char *source;
    size_t source_length;
    /** Non-zero when the reference names something to fetch (non-empty). */
    int source_usable;

    /** Borrowed `alt` bytes; NULL when absent. Never trimmed, never decoded. */
    const char *alt;
    size_t alt_length;

    int width_set;
    int width_valid;
    uint32_t width;
    int height_set;
    int height_valid;
    uint32_t height;
} robo_html_image_attributes_t;

/**
 * Read the image attributes of @p element.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p element / @p out;
 *         ROBO_NOT_FOUND when @p element is not a live element;
 *         ROBO_NOT_SUPPORTED when the element is not an `<img>`.
 */
robo_result_t robo_html_element_image_attributes(
    const robo_html_node_handle_t *element,
    robo_html_image_attributes_t *out,
    robo_html_diag_t *diag);

/**
 * Parse one declared dimension exactly as the HTML attribute rules do, minus
 * percentages: optional surrounding ASCII whitespace, then digits, and
 * nothing else. No `px` suffix, no signs, no exponent, no overflow.
 *
 * @return ROBO_OK with @c *out_valid / @c *out_value set (a NULL or empty
 *         @p value is ROBO_OK with @c *out_valid == 0);
 *         ROBO_INVALID_ARGUMENT for NULL out-params.
 */
robo_result_t robo_html_image_parse_dimension(const char *value,
                                              size_t length,
                                              int *out_valid,
                                              uint32_t *out_value);

/* ------------------------------------------------------------------ */
/* The resource                                                        */
/* ------------------------------------------------------------------ */

typedef struct robo_html_image robo_html_image_t;

/**
 * Create a resource by decoding @p bytes, with no element behind it.
 *
 * Failure to decode is *not* a failure to create: `ROBO_OK` with a state of
 * FAILED or UNSUPPORTED is the normal answer for bad or unsupported bytes,
 * and the object then exists precisely so the caller has somewhere to read
 * "why" from. ROBO_INVALID_ARGUMENT / ROBO_OUT_OF_MEMORY mean the call could
 * not be attempted or completed at all.
 *
 * @param source_reference Optional `src` text to record (copied; may be NULL).
 */
robo_result_t robo_html_image_create_decoded(const void *bytes,
                                            size_t length,
                                            const char *source_reference,
                                            robo_html_image_t **out,
                                            robo_html_diag_t *diag);

/** Release @p image and everything it owns. Safe on NULL. */
void robo_html_image_destroy(robo_html_image_t *image);

void robo_html_image_retain(robo_html_image_t *image);
void robo_html_image_release(robo_html_image_t *image);

/** Current state; NONE for a NULL image. */
robo_html_image_state_t robo_html_image_state(const robo_html_image_t *image);

/** Borrowed `src` bytes with no terminator, or NULL when none was recorded. */
const char *robo_html_image_source(const robo_html_image_t *image,
                                   size_t *out_length);

/** Borrowed decoded pixels, or NULL unless the state is DECODED. */
robo_gfx_image_t *robo_html_image_graphics(const robo_html_image_t *image);

/**
 * Intrinsic size in pixels, i.e. the decoded bitmap's own dimensions.
 * @return 1 and fills the out-params when the state is DECODED; 0 otherwise
 *         (out-params untouched).
 */
int robo_html_image_intrinsic_size(const robo_html_image_t *image,
                                   uint32_t *out_width,
                                   uint32_t *out_height);

/** The error that produced FAILED/UNSUPPORTED; ROBO_OK for other states. */
robo_result_t robo_html_image_failure(const robo_html_image_t *image);

/**
 * Render the resource as one debug line, e.g.
 *   @c img src="assets/Logo.png" alt="Roboticela" declared=128x64
 *   @c state=decoded intrinsic=128x64
 * Returns the length written (excluding the terminator); truncates rather
 * than overruns and always terminates when @p capacity is non-zero.
 */
size_t robo_html_image_describe(const robo_html_image_t *image, char *out,
                                size_t capacity);

/* ------------------------------------------------------------------ */
/* The fetch seam                                                      */
/* ------------------------------------------------------------------ */

/** What a resolver is told about the reference it is being asked for. */
typedef struct robo_html_image_request {
    /** Borrowed `src` bytes exactly as the document wrote them. */
    const char *reference;
    size_t reference_length;
    /** Borrowed `alt` bytes, or NULL. */
    const char *alt;
    size_t alt_length;
    /** Borrowed document source name/URL, or "" when the document has none. */
    const char *document_name;
} robo_html_image_request_t;

/**
 * Fetch bytes for @p request.
 *
 * The returned bytes are **borrowed for the duration of the call**: RoboHTML
 * decodes them immediately and keeps no pointer, so a resolver may serve
 * them from static storage, a stack buffer, an arena or an mmap without any
 * lifetime contract. Ownership of bytes never crosses this boundary — which
 * is the property that keeps the future network loader from having to know
 * anything about RoboHTML's memory model.
 *
 * @return ROBO_OK with a non-NULL @p out_bytes and non-zero @p out_length;
 *         ROBO_NOT_FOUND when the reference is unknown (→ state FAILED);
 *         ROBO_NOT_SUPPORTED when the reference names something this runtime
 *         will not fetch (→ state UNSUPPORTED);
 *         ROBO_BUSY when a fetch is outstanding (→ state stays PENDING).
 */
typedef robo_result_t (*robo_html_image_fetch_fn)(
    void *userdata,
    const robo_html_image_request_t *request,
    const uint8_t **out_bytes,
    size_t *out_length,
    robo_html_diag_t *diag);

/** A resolver is a function and its opaque state; no ownership is implied. */
typedef struct robo_html_image_resolver {
    robo_html_image_fetch_fn fetch;
    void *userdata;
} robo_html_image_resolver_t;

/** Set up an empty resolver (fetch NULL). */
void robo_html_image_resolver_init(robo_html_image_resolver_t *resolver);

/** Non-zero when @p resolver has a fetch function and can be used. */
int robo_html_image_resolver_is_usable(const robo_html_image_resolver_t *resolver);

/**
 * Decode @p bytes and return the state they produced, in the same
 * vocabulary an element reports. Exposed so a caller can answer "is this a
 * format this runtime handles?" without building an element to find out.
 */
robo_result_t robo_html_image_bytes_state(const void *bytes, size_t length,
                                          robo_html_image_state_t *out_state);

/* ------------------------------------------------------------------ */
/* Local asset resolver (the safe local/test mechanism)                */
/* ------------------------------------------------------------------ */

/** One named byte blob. @p bytes is borrowed and must outlive the table. */
typedef struct robo_html_image_asset {
    const char *name;
    const uint8_t *bytes;
    size_t length;
} robo_html_image_asset_t;

typedef struct robo_html_image_assets robo_html_image_assets_t;

/**
 * Copy @p entries (the array, not the bytes) into an owned table.
 * Names must be non-empty and unique ASCII; a duplicate or empty name is
 * ROBO_INVALID_ARGUMENT, because silent shadowing would make a missing image
 * look like a present one.
 */
robo_result_t robo_html_image_assets_create(const robo_html_image_asset_t *entries,
                                            size_t count,
                                            robo_html_image_assets_t **out);

/** Add one asset after creation. Duplicate name → ROBO_ALREADY_EXISTS. */
robo_result_t robo_html_image_assets_add(robo_html_image_assets_t *assets,
                                         const char *name,
                                         const void *bytes,
                                         size_t length,
                                         robo_html_diag_t *diag);

/** Borrowed asset bytes, or NULL when the name is not registered. */
const uint8_t *robo_html_image_assets_lookup(const robo_html_image_assets_t *assets,
                                             const char *name,
                                             size_t *out_length);

/** Number of registered assets. */
size_t robo_html_image_assets_count(const robo_html_image_assets_t *assets);

void robo_html_image_assets_destroy(robo_html_image_assets_t *assets);

/**
 * A resolver over @p assets. @p assets is borrowed and must outlive every
 * use of the returned resolver; the resolver itself owns nothing, so it may
 * be built on the stack.
 */
robo_html_image_resolver_t robo_html_image_assets_resolver(
    const robo_html_image_assets_t *assets);

/* ------------------------------------------------------------------ */
/* Element-side resource attachment                                    */
/* ------------------------------------------------------------------ */

/**
 * Fetch and decode the resource for @p element, attaching the result to it.
 *
 * Reads the attributes, asks @p resolver for bytes, decodes them, and records
 * the outcome as the element's image state. Re-resolving replaces any earlier
 * resource, so a mutated `src` is honoured by calling this again.
 *
 * @return ROBO_OK for every resource outcome — including FAILED and
 *         UNSUPPORTED, which are states and not errors; ROBO_INVALID_ARGUMENT
 *         for NULL arguments or an unusable resolver; ROBO_NOT_FOUND when
 *         @p element is not a live element; ROBO_NOT_SUPPORTED when it is not
 *         an `<img>`; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_html_element_image_resolve(
    const robo_html_node_handle_t *element,
    const robo_html_image_resolver_t *resolver,
    robo_html_diag_t *diag);

/**
 * The resource attached to @p element, borrowed (retain it to keep it).
 *
 * @return ROBO_OK and @c *out = NULL when nothing is attached — a `src` that
 *         has not been resolved yet is not an error;
 *         ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         @p element is not a live element; ROBO_NOT_SUPPORTED when it is not
 *         an `<img>`.
 */
robo_result_t robo_html_element_image_get(const robo_html_node_handle_t *element,
                                          robo_html_image_t **out,
                                          robo_html_diag_t *diag);

/**
 * Detach and release @p element's resource, leaving the element otherwise
 * untouched (attributes and children are not modified). Idempotent.
 * @return ROBO_OK, or the same structural errors as the getter.
 */
robo_result_t robo_html_element_image_detach(const robo_html_node_handle_t *element,
                                             robo_html_diag_t *diag);

/**
 * State of @p element's resource, derived from the element plus whatever is
 * attached: NONE when there is no usable `src`, PENDING when a usable `src`
 * has no resource yet, otherwise the attached resource's state. This is
 * answerable without a resolver, which is what makes it usable from a
 * renderer that was handed a document it did not load.
 *
 * Unknown/non-element handles report ROBO_HTML_IMAGE_STATE_NONE.
 */
robo_html_image_state_t robo_html_element_image_state(
    const robo_html_node_handle_t *element);

/**
 * Count of image resources currently attached to @p document_id.
 * Unknown/destroyed document → 0.
 */
size_t robo_html_document_image_count(robo_html_document_id_t document_id);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_IMAGE_H */
