/*
 * RoboHTML image load lifecycle (Phase 17.5).
 *
 * Phase 8.12 built the image *model*: one resource with a state (none,
 * pending, decoded, failed, unsupported) over a RoboGraphics bitmap, the
 * `src`/`alt`/declared-dimension readers, the fetch seam, the safe local asset
 * resolver, and the per-element attachment. What 8.12 did not answer is what a
 * document layer asks *around* it — is this reference one this runtime will
 * even try, how big is the box now, did that change the layout, and what
 * happened to a load that was still outstanding when the element went away.
 * This header is that layer, and it adds no second mechanism to any of it.
 *
 *   source reference
 *       ↓  HtmlImageReferenceClassify          (policy, no bytes)
 *   class                              local | absolute | external |
 *                                      unsupported | invalid | none
 *       ↓  HtmlElementImageLoad                (the one lifecycle call)
 *   fetch      Phase 8.12's HtmlImageResolver (borrowed bytes, no I/O here)
 *       ↓
 *   decode     Phase 8.12's HtmlImageCreateDecoded
 *       ↓
 *   intrinsic  Phase 8.12's HtmlImageIntrinsicSize
 *       ↓
 *   used size  declared `width`/`height`, else intrinsic
 *       ↓
 *   outcome    HtmlImageLoadOutcome — state, class, sizes, invalidation
 *
 * ## The outcome is the notification
 *
 * RoboHTML has no DOM event system yet, so there is no `onload`/`onerror` to
 * fire and this header does not pretend otherwise: `loadEvents` is 0 in the
 * capability table. The equivalent, and what a caller uses today, is the
 * returned `HtmlImageLoadOutcome` — `state` is DECODED for a load, FAILED or
 * UNSUPPORTED for an error, PENDING while a fetch is outstanding — so "the
 * image loaded" and "the image broke" are both readable facts rather than
 * silence. A later event phase routes its `load`/`error` dispatch through the
 * same outcome instead of inventing a second one.
 *
 * ## Failure is a state, and the only errors are structural
 *
 * `HtmlElementImageLoad` returns RoboOk for every *resource* outcome,
 * including FAILED, UNSUPPORTED, INVALID (a reference policy refused) and
 * PENDING. It returns an error only when the call itself could not be made:
 * RoboInvalidArgument, RoboNotFound (not a live element), RoboNotSupported
 * (not an `<img>`), RoboOutOfMemory. The seam's own Result is preserved in
 * `HtmlImageLoadOutcome::fetchResult`, so "why was there nothing" is never
 * lost — and a resolver that refuses is reported as a refusal, not as a
 * missing image.
 *
 * ## Reference policy is Phase 8.13's, not a second one
 *
 * The classification below is a view over `HtmlNavigationTargetCreate` and
 * `HtmlNavigationSchemeIsPermitted`: `javascript:`, `data:` and `file:` are
 * refused by the same policy that refuses them for a link, before the fetch
 * seam is consulted, so an `<img>` cannot be used to reach a subsystem a
 * document was not granted. This header adds no scheme list of its own.
 *
 * ## What this is NOT
 *
 * No I/O, no networking, no asynchronous loader and no scheduler — the fetch
 * seam is synchronous and a caller that wants a threaded loader owns that
 * thread. No `srcset`/`sizes`/`<picture>` selection (`srcset` is 0 in the
 * capability table), no lazy loading, no decode thread, no image cache beyond
 * the per-element attachment, no `naturalWidth`/`complete` accessors on a
 * node object, no `load`/`error` events, and no layout: the sizes here are the
 * *content* box's intrinsic and declared dimensions, which a layout phase
 * consumes rather than this one computing.
 */

#ifndef ROBO_HTML_IMAGE_LOAD_H
#define ROBO_HTML_IMAGE_LOAD_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLNode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Reference classification                                            */
/* ------------------------------------------------------------------ */

/**
 * What a `src` reference turned out to be, before anything is fetched.
 *
 * This is the *policy* view, and it is deliberately separate from
 * `HtmlImageState`: a reference can be perfectly well-formed and still be one
 * this runtime will not fetch, and that difference is what a caller needs to
 * report. `NONE` is a state too, not an error — `<img alt="...">` with no
 * `src` is a legal element.
 */
typedef enum HtmlImageReferenceClass {
    /** No usable reference: absent, empty, or whitespace only. */
    RoboHtmlImageReferenceNone = 0,
    /** A relative internal path, resolved against the document's reference. */
    RoboHtmlImageReferenceLocal = 1,
    /** A root-relative internal path (`/Assets/Logo.png`). */
    RoboHtmlImageReferenceAbsolute = 2,
    /** An absolute URI whose scheme this runtime will hand to the seam. */
    RoboHtmlImageReferenceExternal = 3,
    /** Known-but-refused or unrecognised scheme, or a fragment (no bytes). */
    RoboHtmlImageReferenceUnsupported = 4,
    /** A reference that cannot be made into a target at all. */
    RoboHtmlImageReferenceInvalid = 5
} HtmlImageReferenceClass;

/** Non-zero if @p value is a defined enumerator. */
int HtmlImageReferenceClassIsKnown(HtmlImageReferenceClass value);

/** Stable lowercase name of @p value; never NULL, never allocated. */
const char *HtmlImageReferenceClassName(HtmlImageReferenceClass value);

/**
 * Classify @p reference against @p base (the document's reference; may be NULL
 * or empty) without touching any bytes.
 *
 * A fragment (`#id`) is INVALID rather than unsupported: it names a position
 * in this document, not bytes, so it is not an image reference at all.
 *
 * @return RoboOk with @c *out set; RoboInvalidArgument for NULL @p out, or
 *         @p reference NULL with non-zero @p referenceLength; RoboOutOfMemory.
 */
Result HtmlImageReferenceClassify(
    const char *reference,
    size_t referenceLength,
    const char *base,
    size_t baseLength,
    HtmlImageReferenceClass *out,
    HtmlDiag *diag);

/**
 * Classify @p element's `src` against its own document's reference.
 *
 * @return RoboOk with the class (NONE for a missing/empty `src`);
 *         RoboInvalidArgument; RoboNotFound (not a live element, or its
 *         document is gone); RoboNotSupported (not an `<img>`).
 */
Result HtmlElementImageReferenceClass(
    const HtmlNodeHandle *element,
    HtmlImageReferenceClass *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* The load outcome                                                    */
/* ------------------------------------------------------------------ */

/**
 * Everything one load attempt decided, in one record.
 *
 * The two size pairs are separate on purpose. `intrinsic*` is what the decoded
 * bitmap says and is only meaningful when `hasIntrinsic` is 1; `declared*` is
 * what the `width`/`height` attributes said and is only meaningful when the
 * matching `*FromDeclaration` flag is 1. `used*` is the answer a layout
 * consumes: the declaration when there is one, the intrinsic size otherwise,
 * and `hasUsedSize` is 0 when neither exists (a pending or broken image with
 * no declared box, which is exactly the case a renderer must not guess at).
 */
typedef struct HtmlImageLoadOutcome {
    /** Where the element's resource ended up. */
    HtmlImageState state;
    /** What the reference was, before any attempt. */
    HtmlImageReferenceClass reference;

    /** Non-zero when the fetch seam was consulted at all. */
    int requested;
    /** Non-zero when a resource is attached to the element. */
    int resolved;
    /** Non-zero when the state is DECODED. */
    int loaded;
    /** Non-zero when the state is FAILED or UNSUPPORTED. */
    int failed;
    /** Non-zero when the state is PENDING (a fetch was outstanding). */
    int pending;
    /** Non-zero when this load replaced a resource that was already there. */
    int replaced;
    /** Non-zero when the `src` was changed by this call. */
    int sourceChanged;

    /** Non-zero when the decoded bitmap has a size; gates `intrinsic*`. */
    int hasIntrinsic;
    uint32_t intrinsicWidth;
    uint32_t intrinsicHeight;

    /** Gated by the matching `*FromDeclaration` flag. */
    uint32_t declaredWidth;
    uint32_t declaredHeight;
    int widthFromDeclaration;
    int heightFromDeclaration;

    /** The size a layout should use. `hasUsedSize` gates both. */
    uint32_t usedWidth;
    uint32_t usedHeight;
    int hasUsedSize;

    /** The resource content changed (attached, replaced, or released). */
    int contentChanged;
    /** The used size changed (including appearing or disappearing). */
    int sizeChanged;
    /** The change is layout-affecting: `contentChanged || sizeChanged`. */
    int layoutAffected;

    /**
     * The fetch seam's own Result (RoboOk when it was not consulted), or
     * RoboBusy when a fetch was already outstanding. Preserved so a refusal
     * is distinguishable from a missing asset.
     */
    Result fetchResult;
} HtmlImageLoadOutcome;

/** Reset @p outcome to the empty record (no resource, RoboOk). Safe on NULL. */
void HtmlImageLoadOutcomeInit(HtmlImageLoadOutcome *outcome);

/**
 * Render @p outcome as one line, e.g.
 *   @c class=local state=failed fetch=not-found used=-
 *
 * Returns the length written excluding the terminator; truncates rather than
 * overruns, and always terminates when @p capacity is non-zero.
 */
size_t HtmlImageLoadOutcomeDescribe(
    const HtmlImageLoadOutcome *outcome,
    char *out,
    size_t capacity);

/* ------------------------------------------------------------------ */
/* The lifecycle calls                                                 */
/* ------------------------------------------------------------------ */

/**
 * Run the whole lifecycle for @p element: classify `src`, fetch through
 * @p resolver, decode, record the resource, and report the outcome —
 * including the used size and whether the change is layout-affecting.
 *
 * A `src` that is absent or unusable releases any earlier resource, which is
 * how a mutated/removed `src` becomes observable without teaching this call
 * about attribute mutation. A reference policy refuses (UNSUPPORTED or
 * INVALID) is reported *without* consulting @p resolver.
 *
 * @return RoboOk for every resource outcome; RoboInvalidArgument for a NULL
 *         @p element / @p out / unusable @p resolver; RoboNotFound; 
 *         RoboNotSupported (not an `<img>`, or a NAME `img` handle whose
 *         document is gone); RoboOutOfMemory.
 */
Result HtmlElementImageLoad(
    const HtmlNodeHandle *element,
    const HtmlImageResolver *resolver,
    HtmlImageLoadOutcome *out,
    HtmlDiag *diag);

/**
 * Replace @p element's `src` with @p src and immediately load it — the
 * "image replacement" path, as one call so a caller cannot set the attribute
 * and forget to re-resolve.
 *
 * A NULL or empty @p src removes the attribute and releases the resource
 * rather than trying to fetch an empty name.
 *
 * @return as HtmlElementImageLoad.
 */
Result HtmlElementImageReplaceSource(
    const HtmlNodeHandle *element,
    const char *src,
    size_t srcLength,
    const HtmlImageResolver *resolver,
    HtmlImageLoadOutcome *out,
    HtmlDiag *diag);

/**
 * The size a layout should use for @p element: the declared `width`/`height`
 * attribute when it parsed, the decoded intrinsic size otherwise, and
 * @c *outHasSize = 0 when neither exists. Answerable with no resolver, so a
 * renderer handed a document it did not load can still ask.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound; RoboNotSupported.
 */
Result HtmlElementImageSize(
    const HtmlNodeHandle *element,
    uint32_t *outWidth,
    uint32_t *outHeight,
    int *outHasSize,
    HtmlDiag *diag);

/**
 * Release @p element's resource and report whether a load was outstanding —
 * the clean answer for an element removed from the tree mid-load, which is the
 * one case where a resource would otherwise outlive its reason to exist.
 *
 * This is Phase 8.12's detach plus the pending fact, so a caller can tell
 * "this element has a source and no resource" from "this element had a
 * resource". The pending fact is **derived** from the element's state — a
 * usable `src` with nothing attached reads PENDING — so it is not a latch and
 * calling this twice reports the same thing twice. That is the honest answer
 * rather than a defect: 8.12's model records a resource and why it failed, not
 * *when* an attempt was made, and inventing a latch here would be a second
 * piece of state about a load that this layer does not own. What the call
 * guarantees is the release: after it, the element holds no resource.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p outWasPending;
 *         RoboNotFound; RoboNotSupported.
 */
Result HtmlElementImageAbandon(
    const HtmlNodeHandle *element,
    int *outWasPending,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Capabilities                                                       */
/* ------------------------------------------------------------------ */

/**
 * What this surface actually does, field by field, so a caller reads an
 * absence as an absence. The three zeros at the end are the phase's stated
 * non-goals, not oversights.
 */
typedef struct HtmlImageLoadCapabilities {
    int fetchSeam;         /* a resolver serves bytes; RoboHTML does no I/O */
    int intrinsicSize;     /* decoded bitmap dimensions */
    int declaredSize;      /* `width`/`height` attributes */
    int referencePolicy;   /* scheme/fragment classification */
    int layoutInvalidation;/* content/size/layout-affecting flags */
    int replacement;       /* src replaced and reloaded as one call */
    int removalAbandon;    /* release an outstanding load */
    int srcset;            /* 0: no `srcset`/`sizes`/`<picture>` selection */
    int responsiveImages;  /* 0: no media/density selection */
    int loadEvents;        /* 0: no DOM events; the outcome is the report */
    int asyncLoading;      /* 0: the fetch seam is synchronous */
    int networkAccess;     /* 0: this module never opens a socket or file */
    const char *version;   /* RoboHTML's version string */
} HtmlImageLoadCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlImageLoadGetCapabilities(HtmlImageLoadCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_IMAGE_LOAD_H */
