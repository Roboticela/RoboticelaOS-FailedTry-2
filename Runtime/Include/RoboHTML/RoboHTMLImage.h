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
 *        ├── attributes        HtmlElementImageAttributes
 *        │     src     what to fetch
 *        │     alt     text, kept verbatim (never decoded, never shaped)
 *        │     width   / height: declared dimensions, parsed safely
 *        │
 *        └── resource          HtmlImage
 *              └── state       NONE │ PENDING │ DECODED │ FAILED │ UNSUPPORTED
 *                    └── pixels  GfxImage (RoboGraphics, RGBA8)
 *
 * Bytes are fetched through a seam, not through this module
 *   RoboHTML never opens a file, never resolves a host name and never speaks
 *   a protocol — the 8.1 boundary said so and this phase keeps it. A caller
 *   installs a HtmlImageResolver; RoboHTML hands it the reference
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
typedef enum HtmlImageState {
    /** No usable `src`: the attribute is absent or empty. Nothing to fetch. */
    RoboHtmlImageStateNone = 0,
    /** `src` names something, but no resolver has run for it yet. */
    RoboHtmlImageStatePending = 1,
    /** Bytes were fetched and decoded; pixels are available. */
    RoboHtmlImageStateDecoded = 2,
    /** Fetching or decoding failed: not found, malformed, truncated. */
    RoboHtmlImageStateFailed = 3,
    /** Refused by policy or unimplemented: a scheme, or an unsupported format. */
    RoboHtmlImageStateUnsupported = 4
} HtmlImageState;

/** Non-zero if @p state is a defined enumerator. */
int HtmlImageStateIsKnown(HtmlImageState state);

/** Stable lowercase name of @p state; never NULL, never allocated. */
const char *HtmlImageStateName(HtmlImageState state);

/** Largest declared `width`/`height` this model accepts (a value above it is
 *  treated as invalid rather than clamped, so a typo cannot silently resize). */
#define RoboHtmlImageMaxDeclaredDimension 1000000u

/* ------------------------------------------------------------------ */
/* Attribute reading (no fetching, no decoding)                        */
/* ------------------------------------------------------------------ */

/**
 * The image-relevant attributes of one element, read verbatim.
 *
 * Every string is borrowed from the element and stays valid only while the
 * element's attributes are unchanged — the same rule as
 * HtmlElementAttributeValue, which this is a typed view over.
 *
 * `widthValid` / `heightValid` are meaningful only when the corresponding
 * `_set` flag is 1: `set` means "the attribute is present and non-empty",
 * `valid` means "and it parsed as a dimension this phase accepts". That split
 * is what lets a caller tell `<img width>` from `<img width="abc">` from
 * `<img>` without guessing, and it is why an invalid dimension is reported
 * rather than quietly dropped.
 */
typedef struct HtmlImageAttributes {
    /** Non-zero when a `src` attribute is present at all. */
    int sourcePresent;
    /** Borrowed `src` bytes; NULL when absent. */
    const char *source;
    size_t sourceLength;
    /** Non-zero when the reference names something to fetch (non-empty). */
    int sourceUsable;

    /** Borrowed `alt` bytes; NULL when absent. Never trimmed, never decoded. */
    const char *alt;
    size_t altLength;

    int widthSet;
    int widthValid;
    uint32_t width;
    int heightSet;
    int heightValid;
    uint32_t height;
} HtmlImageAttributes;

/**
 * Read the image attributes of @p element.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when the element is not an `<img>`.
 */
Result HtmlElementImageAttributes(
    const HtmlNodeHandle *element,
    HtmlImageAttributes *out,
    HtmlDiag *diag);

/**
 * Parse one declared dimension exactly as the HTML attribute rules do, minus
 * percentages: optional surrounding ASCII whitespace, then digits, and
 * nothing else. No `px` suffix, no signs, no exponent, no overflow.
 *
 * @return RoboOk with @c *outValid / @c *outValue set (a NULL or empty
 *         @p value is RoboOk with @c *outValid == 0);
 *         RoboInvalidArgument for NULL out-params.
 */
Result HtmlImageParseDimension(const char *value,
                                              size_t length,
                                              int *outValid,
                                              uint32_t *outValue);

/* ------------------------------------------------------------------ */
/* The resource                                                        */
/* ------------------------------------------------------------------ */

typedef struct HtmlImage HtmlImage;

/**
 * Create a resource by decoding @p bytes, with no element behind it.
 *
 * Failure to decode is *not* a failure to create: `RoboOk` with a state of
 * FAILED or UNSUPPORTED is the normal answer for bad or unsupported bytes,
 * and the object then exists precisely so the caller has somewhere to read
 * "why" from. RoboInvalidArgument / RoboOutOfMemory mean the call could
 * not be attempted or completed at all.
 *
 * @param sourceReference Optional `src` text to record (copied; may be NULL).
 */
Result HtmlImageCreateDecoded(const void *bytes,
                                            size_t length,
                                            const char *sourceReference,
                                            HtmlImage **out,
                                            HtmlDiag *diag);

/** Release @p image and everything it owns. Safe on NULL. */
void HtmlImageDestroy(HtmlImage *image);

void HtmlImageRetain(HtmlImage *image);
void HtmlImageRelease(HtmlImage *image);

/** Current state; NONE for a NULL image. */
HtmlImageState MakeHtmlImageState(const HtmlImage *image);

/** Borrowed `src` bytes with no terminator, or NULL when none was recorded. */
const char *HtmlImageSource(const HtmlImage *image,
                                   size_t *outLength);

/** Borrowed decoded pixels, or NULL unless the state is DECODED. */
GfxImage *HtmlImageGraphics(const HtmlImage *image);

/**
 * Intrinsic size in pixels, i.e. the decoded bitmap's own dimensions.
 * @return 1 and fills the out-params when the state is DECODED; 0 otherwise
 *         (out-params untouched).
 */
int HtmlImageIntrinsicSize(const HtmlImage *image,
                                   uint32_t *outWidth,
                                   uint32_t *outHeight);

/** The error that produced FAILED/UNSUPPORTED; RoboOk for other states. */
Result HtmlImageFailure(const HtmlImage *image);

/**
 * Render the resource as one debug line, e.g.
 *   @c img src="assets/Logo.png" alt="Roboticela" declared=128x64
 *   @c state=decoded intrinsic=128x64
 * Returns the length written (excluding the terminator); truncates rather
 * than overruns and always terminates when @p capacity is non-zero.
 */
size_t HtmlImageDescribe(const HtmlImage *image, char *out,
                                size_t capacity);

/* ------------------------------------------------------------------ */
/* The fetch seam                                                      */
/* ------------------------------------------------------------------ */

/** What a resolver is told about the reference it is being asked for. */
typedef struct HtmlImageRequest {
    /** Borrowed `src` bytes exactly as the document wrote them. */
    const char *reference;
    size_t referenceLength;
    /** Borrowed `alt` bytes, or NULL. */
    const char *alt;
    size_t altLength;
    /** Borrowed document source name/URL, or "" when the document has none. */
    const char *documentName;
} HtmlImageRequest;

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
 * @return RoboOk with a non-NULL @p outBytes and non-zero @p outLength;
 *         RoboNotFound when the reference is unknown (→ state FAILED);
 *         RoboNotSupported when the reference names something this runtime
 *         will not fetch (→ state UNSUPPORTED);
 *         RoboBusy when a fetch is outstanding (→ state stays PENDING).
 */
typedef Result (*HtmlImageFetchFn)(
    void *userdata,
    const HtmlImageRequest *request,
    const uint8_t **outBytes,
    size_t *outLength,
    HtmlDiag *diag);

/** A resolver is a function and its opaque state; no ownership is implied. */
typedef struct HtmlImageResolver {
    HtmlImageFetchFn fetch;
    void *userdata;
} HtmlImageResolver;

/** Set up an empty resolver (fetch NULL). */
void HtmlImageResolverInit(HtmlImageResolver *resolver);

/** Non-zero when @p resolver has a fetch function and can be used. */
int HtmlImageResolverIsUsable(const HtmlImageResolver *resolver);

/**
 * Decode @p bytes and return the state they produced, in the same
 * vocabulary an element reports. Exposed so a caller can answer "is this a
 * format this runtime handles?" without building an element to find out.
 */
Result HtmlImageBytesState(const void *bytes, size_t length,
                                          HtmlImageState *outState);

/* ------------------------------------------------------------------ */
/* Local asset resolver (the safe local/test mechanism)                */
/* ------------------------------------------------------------------ */

/** One named byte blob. @p bytes is borrowed and must outlive the table. */
typedef struct HtmlImageAsset {
    const char *name;
    const uint8_t *bytes;
    size_t length;
} HtmlImageAsset;

typedef struct HtmlImageAssets HtmlImageAssets;

/**
 * Copy @p entries (the array, not the bytes) into an owned table.
 * Names must be non-empty and unique ASCII; a duplicate or empty name is
 * RoboInvalidArgument, because silent shadowing would make a missing image
 * look like a present one.
 */
Result HtmlImageAssetsCreate(const HtmlImageAsset *entries,
                                            size_t count,
                                            HtmlImageAssets **out);

/** Add one asset after creation. Duplicate name → RoboAlreadyExists. */
Result HtmlImageAssetsAdd(HtmlImageAssets *assets,
                                         const char *name,
                                         const void *bytes,
                                         size_t length,
                                         HtmlDiag *diag);

/** Borrowed asset bytes, or NULL when the name is not registered. */
const uint8_t *HtmlImageAssetsLookup(const HtmlImageAssets *assets,
                                             const char *name,
                                             size_t *outLength);

/** Number of registered assets. */
size_t HtmlImageAssetsCount(const HtmlImageAssets *assets);

void HtmlImageAssetsDestroy(HtmlImageAssets *assets);

/**
 * A resolver over @p assets. @p assets is borrowed and must outlive every
 * use of the returned resolver; the resolver itself owns nothing, so it may
 * be built on the stack.
 */
HtmlImageResolver HtmlImageAssetsResolver(
    const HtmlImageAssets *assets);

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
 * @return RoboOk for every resource outcome — including FAILED and
 *         UNSUPPORTED, which are states and not errors; RoboInvalidArgument
 *         for NULL arguments or an unusable resolver; RoboNotFound when
 *         @p element is not a live element; RoboNotSupported when it is not
 *         an `<img>`; RoboOutOfMemory.
 */
Result HtmlElementImageResolve(
    const HtmlNodeHandle *element,
    const HtmlImageResolver *resolver,
    HtmlDiag *diag);

/**
 * The resource attached to @p element, borrowed (retain it to keep it).
 *
 * @return RoboOk and @c *out = NULL when nothing is attached — a `src` that
 *         has not been resolved yet is not an error;
 *         RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         @p element is not a live element; RoboNotSupported when it is not
 *         an `<img>`.
 */
Result HtmlElementImageGet(const HtmlNodeHandle *element,
                                          HtmlImage **out,
                                          HtmlDiag *diag);

/**
 * Detach and release @p element's resource, leaving the element otherwise
 * untouched (attributes and children are not modified). Idempotent.
 * @return RoboOk, or the same structural errors as the getter.
 */
Result HtmlElementImageDetach(const HtmlNodeHandle *element,
                                             HtmlDiag *diag);

/**
 * State of @p element's resource, derived from the element plus whatever is
 * attached: NONE when there is no usable `src`, PENDING when a usable `src`
 * has no resource yet, otherwise the attached resource's state. This is
 * answerable without a resolver, which is what makes it usable from a
 * renderer that was handed a document it did not load.
 *
 * Unknown/non-element handles report RoboHtmlImageStateNone.
 */
HtmlImageState HtmlElementImageState(
    const HtmlNodeHandle *element);

/**
 * Count of image resources currently attached to @p documentId.
 * Unknown/destroyed document → 0.
 */
size_t HtmlDocumentImageCount(HtmlDocumentId documentId);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_IMAGE_H */
