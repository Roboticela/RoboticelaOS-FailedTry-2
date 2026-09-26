/*
 * RoboHTML Phase 17.5 — the image load lifecycle.
 *
 * This file answers the four questions a document layer asks *around* Phase
 * 8.12's resource model, and it answers each one by calling 8.12 rather than
 * by reimplementing it:
 *
 *   will this runtime even try?   HtmlNavigationTargetCreate + the 8.13 scheme
 *                                 policy, so an `<img>` and an `<a>` refuse the
 *                                 same schemes for the same reason
 *   how big is the box?           declared attributes, else the decoded
 *                                 intrinsic size, else "unknown"
 *   did that change the layout?   a before/after reading of the used size and
 *                                 the attached resource
 *   what happened to a load that  8.12's detach plus the pending state it was
 *   was in flight?                in when the element went away
 *
 * There is no second fetch, no second decode and no second state vocabulary:
 * every outcome this file reports is read off `HtmlImageState` and
 * `HtmlImageIntrinsicSize`.
 */

#include "RoboHTML/RoboHTMLImageLoad.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Reference classification                                            */
/* ------------------------------------------------------------------ */

int HtmlImageReferenceClassIsKnown(HtmlImageReferenceClass value)
{
    return value >= RoboHtmlImageReferenceNone &&
           value <= RoboHtmlImageReferenceInvalid;
}

const char *HtmlImageReferenceClassName(HtmlImageReferenceClass value)
{
    switch (value) {
    case RoboHtmlImageReferenceNone:        return "none";
    case RoboHtmlImageReferenceLocal:       return "local";
    case RoboHtmlImageReferenceAbsolute:    return "absolute";
    case RoboHtmlImageReferenceExternal:    return "external";
    case RoboHtmlImageReferenceUnsupported: return "unsupported";
    case RoboHtmlImageReferenceInvalid:     return "invalid";
    default:                                return "unknown";
    }
}

/*
 * Map a resolved navigation target onto the image policy view.
 *
 * A fragment is INVALID rather than unsupported, because `src="#x"` names a
 * position in this document and not bytes: there is no resource to refuse.
 */
static HtmlImageReferenceClass ClassForTarget(const HtmlNavigationTarget *target)
{
    HtmlTargetKind kind;
    const char *scheme = NULL;
    size_t schemeLength = 0u;
    const char *resolved = NULL;
    size_t resolvedLength = 0u;

    kind = HtmlNavigationTargetKind(target);
    switch (kind) {
    case RoboHtmlTargetNone:
        return RoboHtmlImageReferenceNone;
    case RoboHtmlTargetFragment:
        return RoboHtmlImageReferenceInvalid;
    case RoboHtmlTargetInvalid:
        return RoboHtmlImageReferenceInvalid;
    case RoboHtmlTargetExternal:
        scheme = HtmlNavigationTargetScheme(target, &schemeLength);
        if (!scheme) {
            return RoboHtmlImageReferenceUnsupported;
        }
        return HtmlNavigationSchemeIsPermitted(scheme, schemeLength)
                   ? RoboHtmlImageReferenceExternal
                   : RoboHtmlImageReferenceUnsupported;
    case RoboHtmlTargetRoute:
        resolved = HtmlNavigationTargetResolved(target, &resolvedLength);
        if (resolved && resolvedLength > 0u && resolved[0] == '/') {
            return RoboHtmlImageReferenceAbsolute;
        }
        return RoboHtmlImageReferenceLocal;
    default:
        return RoboHtmlImageReferenceInvalid;
    }
}

Result HtmlImageReferenceClassify(const char *reference,
                                  size_t referenceLength,
                                  const char *base,
                                  size_t baseLength,
                                  HtmlImageReferenceClass *out,
                                  HtmlDiag *diag)
{
    HtmlNavigationTarget *target = NULL;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null out");
    }
    *out = RoboHtmlImageReferenceNone;
    if (!reference && referenceLength != 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null reference with non-zero length");
    }

    res = HtmlNavigationTargetCreate(reference ? reference : "",
                                     reference ? referenceLength : 0u,
                                     base ? base : "",
                                     base ? baseLength : 0u,
                                     &target, diag);
    if (res != RoboOk) {
        return res;
    }
    *out = ClassForTarget(target);
    HtmlNavigationTargetDestroy(target);
    return RoboOk;
}

Result HtmlElementImageReferenceClass(const HtmlNodeHandle *element,
                                      HtmlImageReferenceClass *out,
                                      HtmlDiag *diag)
{
    HtmlImageAttributes attributes;
    const char *base;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null out");
    }
    *out = RoboHtmlImageReferenceNone;
    res = HtmlElementImageAttributes(element, &attributes, diag);
    if (res != RoboOk) {
        return res;
    }
    if (!attributes.sourceUsable) {
        return RoboOk;
    }
    base = element ? HtmlDocumentSourceName(element->documentId) : NULL;
    return HtmlImageReferenceClassify(attributes.source,
                                      attributes.sourceLength,
                                      base, base ? strlen(base) : 0u,
                                      out, diag);
}

/* ------------------------------------------------------------------ */
/* The gate                                                            */
/* ------------------------------------------------------------------ */

/*
 * Liveness is checked *before* the tag, so a handle whose document is gone
 * reports NotFound rather than "not an img". Reads like this are why the
 * ordering matters: a stale handle's tag is not a fact anyone can trust, and
 * answering NotSupported for it would tell a caller that a destroyed document
 * holds an element of another kind.
 */
static Result ImageLoadGate(const HtmlNodeHandle *element, HtmlDiag *diag)
{
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null element");
    }
    if (!HtmlIsInitialized() || !HtmlDocumentIsLive(element->documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotFound,
                            "document does not resolve");
    }
    if (HtmlElementTag(element) != RoboHtmlTagImg) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotSupported,
                            "element is not an img");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Outcome helpers                                                     */
/* ------------------------------------------------------------------ */

void HtmlImageLoadOutcomeInit(HtmlImageLoadOutcome *outcome)
{
    if (!outcome) {
        return;
    }
    memset(outcome, 0, sizeof(*outcome));
    outcome->state = RoboHtmlImageStateNone;
    outcome->reference = RoboHtmlImageReferenceNone;
}

/* A tiny truncating text builder, the same shape Phase 8.12's Describe
 * helpers use: every append is bounded and the terminator is written once. */
typedef struct LoadText {
    char *out;
    size_t capacity;
    size_t used;
} LoadText;

static void LoadAppendBytes(LoadText *b, const char *bytes, size_t length)
{
    size_t i;

    if (!bytes) {
        return;
    }
    for (i = 0u; i < length; i++) {
        if (b->out && b->used + 1u < b->capacity) {
            b->out[b->used] = bytes[i];
        }
        b->used++;
    }
}

static void LoadAppendText(LoadText *b, const char *text)
{
    size_t length = 0u;

    if (!text) {
        return;
    }
    while (text[length] != '\0') {
        length++;
    }
    LoadAppendBytes(b, text, length);
}

static void LoadAppendUint(LoadText *b, uint32_t value)
{
    char digits[16];
    int at = 0;

    if (value == 0u) {
        LoadAppendText(b, "0");
        return;
    }
    while (value > 0u && at < (int)sizeof(digits)) {
        digits[at++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (at > 0) {
        char one[2];

        one[0] = digits[--at];
        one[1] = '\0';
        LoadAppendText(b, one);
    }
}

size_t HtmlImageLoadOutcomeDescribe(const HtmlImageLoadOutcome *outcome,
                                    char *out, size_t capacity)
{
    LoadText b;
    uint32_t fetchValue;

    b.out = out;
    b.capacity = capacity;
    b.used = 0u;
    if (!outcome) {
        if (out && capacity > 0u) {
            out[0] = '\0';
        }
        return 0u;
    }

    LoadAppendText(&b, "class=");
    LoadAppendText(&b, HtmlImageReferenceClassName(outcome->reference));
    LoadAppendText(&b, " state=");
    LoadAppendText(&b, HtmlImageStateName(outcome->state));
    LoadAppendText(&b, " fetch=");
    fetchValue = (outcome->fetchResult == RoboOk)
                     ? 0u
                     : (uint32_t)outcome->fetchResult;
    LoadAppendUint(&b, fetchValue);
    LoadAppendText(&b, " used=");
    if (outcome->hasUsedSize) {
        LoadAppendUint(&b, outcome->usedWidth);
        LoadAppendText(&b, "x");
        LoadAppendUint(&b, outcome->usedHeight);
    } else {
        LoadAppendText(&b, "-");
    }
    LoadAppendText(&b, " intrinsic=");
    if (outcome->hasIntrinsic) {
        LoadAppendUint(&b, outcome->intrinsicWidth);
        LoadAppendText(&b, "x");
        LoadAppendUint(&b, outcome->intrinsicHeight);
    } else {
        LoadAppendText(&b, "-");
    }
    LoadAppendText(&b, " declared=");
    if (outcome->widthFromDeclaration) {
        LoadAppendUint(&b, outcome->declaredWidth);
    } else {
        LoadAppendText(&b, "-");
    }
    LoadAppendText(&b, "/");
    if (outcome->heightFromDeclaration) {
        LoadAppendUint(&b, outcome->declaredHeight);
    } else {
        LoadAppendText(&b, "-");
    }
    LoadAppendText(&b, " layout=");
    LoadAppendUint(&b, outcome->layoutAffected ? 1u : 0u);

    if (out && capacity > 0u) {
        out[(b.used < capacity) ? b.used : capacity - 1u] = '\0';
    }
    return b.used;
}

/* ------------------------------------------------------------------ */
/* Size reading                                                        */
/* ------------------------------------------------------------------ */

static Result ReadUsedSize(const HtmlNodeHandle *element,
                           uint32_t *outWidth,
                           uint32_t *outHeight,
                           int *outHasSize,
                           int *outWidthDeclared,
                           int *outHeightDeclared,
                           uint32_t *outDeclaredWidth,
                           uint32_t *outDeclaredHeight,
                           int *outHasIntrinsic,
                           uint32_t *outIntrinsicWidth,
                           uint32_t *outIntrinsicHeight,
                           HtmlDiag *diag)
{
    HtmlImageAttributes attributes;
    HtmlImage *image = NULL;
    uint32_t intrinsicWidth = 0u;
    uint32_t intrinsicHeight = 0u;
    int hasIntrinsic = 0;
    uint32_t width = 0u;
    uint32_t height = 0u;
    int hasSize = 0;
    Result res;

    res = HtmlElementImageAttributes(element, &attributes, diag);
    if (res != RoboOk) {
        return res;
    }

    res = HtmlElementImageGet(element, &image, diag);
    if (res != RoboOk) {
        return res;
    }
    if (image) {
        /* Borrowed from the element's attachment: the table owns the only
         * reference, so releasing here would free the resource out from under
         * it — the pointer must simply not be kept. */
        hasIntrinsic = HtmlImageIntrinsicSize(image, &intrinsicWidth,
                                              &intrinsicHeight);
    }

    /* Declared wins; the other axis falls back to the intrinsic size, which
     * is what preserves the aspect ratio at natural scale. A declared width
     * with no intrinsic height answers "no usable size" rather than inventing
     * one — a layout must not have to guess. */
    if (attributes.widthValid) {
        width = attributes.width;
    } else if (hasIntrinsic) {
        width = intrinsicWidth;
    }
    if (attributes.heightValid) {
        height = attributes.height;
    } else if (hasIntrinsic) {
        height = intrinsicHeight;
    }
    hasSize = (width != 0u || height != 0u) &&
              (attributes.widthValid || hasIntrinsic) &&
              (attributes.heightValid || hasIntrinsic);

    if (outWidth) { *outWidth = width; }
    if (outHeight) { *outHeight = height; }
    if (outHasSize) { *outHasSize = hasSize; }
    if (outWidthDeclared) { *outWidthDeclared = attributes.widthValid; }
    if (outHeightDeclared) { *outHeightDeclared = attributes.heightValid; }
    if (outDeclaredWidth) { *outDeclaredWidth = attributes.width; }
    if (outDeclaredHeight) { *outDeclaredHeight = attributes.height; }
    if (outHasIntrinsic) { *outHasIntrinsic = hasIntrinsic; }
    if (outIntrinsicWidth) { *outIntrinsicWidth = intrinsicWidth; }
    if (outIntrinsicHeight) { *outIntrinsicHeight = intrinsicHeight; }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The lifecycle                                                       */
/* ------------------------------------------------------------------ */

Result HtmlElementImageLoad(const HtmlNodeHandle *element,
                            const HtmlImageResolver *resolver,
                            HtmlImageLoadOutcome *out,
                            HtmlDiag *diag)
{
    HtmlImageAttributes attributes;
    HtmlImage *previous = NULL;
    int beforeHasSize = 0;
    uint32_t beforeWidth = 0u;
    uint32_t beforeHeight = 0u;
    int hadPrevious = 0;
    HtmlImageReferenceClass reference = RoboHtmlImageReferenceNone;
    Result res;
    Result fetchResult = RoboOk;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null out");
    }
    HtmlImageLoadOutcomeInit(out);

    /* The gate first, so "not an img" is a refusal rather than a NONE class. */
    res = ImageLoadGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    /* Phase 17.10: asking a resolver for bytes is the one capability that
     * would touch the outside world, so it is the one this phase gates even
     * though the seam itself never performs I/O. */
    res = HtmlSecurityRequireCapability(
        element->documentId, RoboHtmlCapabilityResourceLoading,
        "the document was not granted resource loading", diag);
    if (res != RoboOk) {
        return res;
    }
    res = HtmlElementImageAttributes(element, &attributes, diag);
    if (res != RoboOk) {
        return res;
    }

    /* Read the before-state so replacement and size change are facts, not
     * guesses about what a caller did earlier. */
    res = ReadUsedSize(element, &beforeWidth, &beforeHeight, &beforeHasSize,
                       NULL, NULL, NULL, NULL, NULL, NULL, NULL, diag);
    if (res != RoboOk) {
        return res;
    }
    res = HtmlElementImageGet(element, &previous, diag);
    if (res != RoboOk) {
        return res;
    }
    if (previous) {
        hadPrevious = 1;
    }
    out->replaced = hadPrevious;

    res = HtmlElementImageReferenceClass(element, &reference, diag);
    if (res != RoboOk) {
        return res;
    }
    out->reference = reference;

    /*
     * A reference the runtime will not try never reaches the seam. The
     * resource that was there (if any) is released, which is exactly what a
     * mutated `src` must do — and the class says which policy refused it.
     */
    if (reference == RoboHtmlImageReferenceUnsupported ||
        reference == RoboHtmlImageReferenceInvalid) {
        res = HtmlElementImageDetach(element, diag);
        if (res != RoboOk) {
            return res;
        }
        out->state = (reference == RoboHtmlImageReferenceUnsupported)
                         ? RoboHtmlImageStateUnsupported
                         : RoboHtmlImageStateFailed;
        out->failed = 1;
        out->contentChanged = hadPrevious ? 1 : 0;
        out->sizeChanged = beforeHasSize ? 1 : 0;
        out->layoutAffected = out->contentChanged || out->sizeChanged;
        return RoboOk;
    }

    if (reference == RoboHtmlImageReferenceNone) {
        res = HtmlElementImageDetach(element, diag);
        if (res != RoboOk) {
            return res;
        }
        out->state = RoboHtmlImageStateNone;
        out->contentChanged = hadPrevious ? 1 : 0;
        out->sizeChanged = beforeHasSize ? 1 : 0;
        out->layoutAffected = out->contentChanged || out->sizeChanged;
        return RoboOk;
    }

    if (!resolver || !resolver->fetch) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "resolver is null or unusable");
    }

    fetchResult = HtmlElementImageResolve(element, resolver, diag);
    if (fetchResult != RoboOk && fetchResult != RoboBusy) {
        /*
         * A structural failure while storing the resource. The detach above
         * has not run, so the element is left as it was found.
         */
        return fetchResult;
    }
    out->requested = 1;
    out->fetchResult = fetchResult;

    res = ReadUsedSize(element,
                       &out->usedWidth, &out->usedHeight, &out->hasUsedSize,
                       &out->widthFromDeclaration, &out->heightFromDeclaration,
                       &out->declaredWidth, &out->declaredHeight,
                       &out->hasIntrinsic,
                       &out->intrinsicWidth, &out->intrinsicHeight, diag);
    if (res != RoboOk) {
        return res;
    }

    out->state = HtmlElementImageState(element);
    out->pending = (out->state == RoboHtmlImageStatePending) ? 1 : 0;
    out->loaded = (out->state == RoboHtmlImageStateDecoded) ? 1 : 0;
    {
        /* `resolved` is "a resource is attached", which is not the same as
         * "the state is not NONE": a PENDING element has a state and no
         * resource, and asking is the only honest way to tell them apart. */
        HtmlImage *attached = NULL;

        if (HtmlElementImageGet(element, &attached, NULL) != RoboOk) {
            attached = NULL;
        }
        out->resolved = attached ? 1 : 0;
        /* The seam's *why*: a resource that failed carries the Result that
         * produced its state, which is how a refusal stays distinguishable
         * from a missing asset. RoboOk for every other state, and a fetch
         * still outstanding (no resource yet) keeps its RoboBusy. */
        if (attached) {
            out->fetchResult = HtmlImageFailure(attached);
        }
    }
    out->failed = (out->state == RoboHtmlImageStateFailed ||
                   out->state == RoboHtmlImageStateUnsupported) ? 1 : 0;
    out->replaced = hadPrevious;

    /*
     * `contentChanged` answers "is there now a resource where there was not
     * one, or a different one" — for a first successful resolve that is a
     * real change, and for a re-resolve of the same source it is not
     * distinguishable here (the model keeps one resource per element, so a
     * replacement *is* a change). Reporting it is the honest answer.
     */
    out->contentChanged = out->resolved ? 1 : (beforeHasSize ? 1 : 0);
    out->sizeChanged = (beforeHasSize != out->hasUsedSize) ||
                       (out->hasUsedSize &&
                        (beforeWidth != out->usedWidth ||
                         beforeHeight != out->usedHeight));
    out->layoutAffected = out->contentChanged || out->sizeChanged;
    return RoboOk;
}

Result HtmlElementImageSize(const HtmlNodeHandle *element,
                            uint32_t *outWidth,
                            uint32_t *outHeight,
                            int *outHasSize,
                            HtmlDiag *diag)
{
    Result gated;

    if (!outWidth || !outHeight || !outHasSize) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null out");
    }
    *outWidth = 0u;
    *outHeight = 0u;
    *outHasSize = 0;
    gated = ImageLoadGate(element, diag);
    if (gated != RoboOk) {
        return gated;
    }
    return ReadUsedSize(element, outWidth, outHeight, outHasSize,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL, diag);
}

Result HtmlElementImageReplaceSource(const HtmlNodeHandle *element,
                                     const char *src,
                                     size_t srcLength,
                                     const HtmlImageResolver *resolver,
                                     HtmlImageLoadOutcome *out,
                                     HtmlDiag *diag)
{
    char buffer[RoboHtmlAttrValueMax];
    HtmlImageAttributes before;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null out");
    }
    HtmlImageLoadOutcomeInit(out);

    if (!src && srcLength != 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null src with non-zero length");
    }
    res = ImageLoadGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    res = HtmlSecurityRequireCapability(
        element->documentId, RoboHtmlCapabilityResourceLoading,
        "the document was not granted resource loading", diag);
    if (res != RoboOk) {
        return res;
    }

    /* Read the old reference so "did the source change" is a fact. */
    res = HtmlElementImageAttributes(element, &before, diag);
    if (res != RoboOk) {
        return res;
    }

    if (srcLength == 0u) {
        res = HtmlElementRemoveAttribute(element, "src", diag);
        if (res != RoboOk) {
            return res;
        }
    } else {
        if (srcLength >= RoboHtmlAttrValueMax) {
            /*
             * Too long to store as an attribute, so it cannot be a reference
             * this runtime will fetch. Reported as a refused reference rather
             * than truncated into a shorter one that would load the wrong
             * thing.
             */
            out->reference = RoboHtmlImageReferenceInvalid;
            out->state = RoboHtmlImageStateFailed;
            out->failed = 1;
            return RoboOk;
        }
        memcpy(buffer, src, srcLength);
        buffer[srcLength] = '\0';
        res = HtmlElementSetAttribute(element, "src", buffer, diag);
        if (res != RoboOk) {
            return res;
        }
    }

    res = HtmlElementImageLoad(element, resolver, out, diag);
    if (res != RoboOk) {
        return res;
    }
    out->sourceChanged =
        (before.sourceUsable != (srcLength != 0u)) ||
        (before.sourceUsable &&
         (before.sourceLength != srcLength ||
          memcmp(before.source, src, srcLength) != 0));
    /* A changed source always invalidates, even when the outcome matched. */
    if (out->sourceChanged) {
        out->contentChanged = out->contentChanged || out->resolved;
        out->layoutAffected = out->layoutAffected || out->contentChanged;
    }
    return RoboOk;
}

Result HtmlElementImageAbandon(const HtmlNodeHandle *element,
                               int *outWasPending,
                               HtmlDiag *diag)
{
    HtmlImageState before;

    if (!outWasPending) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInvalidArgument,
                            "null out");
    }
    *outWasPending = 0;

    {
        Result gated = ImageLoadGate(element, diag);

        if (gated != RoboOk) {
            return gated;
        }
    }
    before = HtmlElementImageState(element);
    *outWasPending = (before == RoboHtmlImageStatePending) ? 1 : 0;
    return HtmlElementImageDetach(element, diag);
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlImageLoadGetCapabilities(HtmlImageLoadCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->fetchSeam = 1;
    out->intrinsicSize = 1;
    out->declaredSize = 1;
    out->referencePolicy = 1;
    out->layoutInvalidation = 1;
    out->replacement = 1;
    out->removalAbandon = 1;
    out->srcset = 0;
    out->responsiveImages = 0;
    out->loadEvents = 0;
    out->asyncLoading = 0;
    out->networkAccess = 0;
    out->version = RoboHtmlVersionString;
    return RoboOk;
}
