/*
 * RoboHTML Phase 8.12 — image resources.
 *
 * Three layers, each with one job:
 *
 *   attributes   read `src` / `alt` / `width` / `height` off an element and
 *                parse the dimensions safely. No fetching, no decoding.
 *   resource     HtmlImage — a state plus, when there is one, a
 *                decoded RoboGraphics image. Failure is a *state*, not an
 *                exception, which is what makes a bad `src` unable to break
 *                a document: every outcome leaves the DOM exactly as it was.
 *   attachment   a per-document table keyed by node identity, so an element
 *                can be asked for its picture later — by a renderer that did
 *                not load the document, and without either side holding a
 *                pointer into the other's memory.
 *
 * The fetch seam is the one place bytes come from, and RoboHTML never
 * provides those bytes itself: a resolver hands them over for the duration of
 * one call and ownership never crosses. The shipped resolver is an in-memory
 * asset table, which is why this phase needs no filesystem, no capability
 * check and no traversal defence — there is nothing to traverse.
 */

#include "RoboHTML/RoboHTMLImage.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsDecode.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* State vocabulary                                                    */
/* ------------------------------------------------------------------ */

int HtmlImageStateIsKnown(HtmlImageState state)
{
    switch (state) {
    case RoboHtmlImageStateNone:
    case RoboHtmlImageStatePending:
    case RoboHtmlImageStateDecoded:
    case RoboHtmlImageStateFailed:
    case RoboHtmlImageStateUnsupported:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlImageStateName(HtmlImageState state)
{
    switch (state) {
    case RoboHtmlImageStateNone:
        return "none";
    case RoboHtmlImageStatePending:
        return "pending";
    case RoboHtmlImageStateDecoded:
        return "decoded";
    case RoboHtmlImageStateFailed:
        return "failed";
    case RoboHtmlImageStateUnsupported:
        return "unsupported";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Dimension parsing                                                   */
/* ------------------------------------------------------------------ */

static int AsciiIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int AsciiIsDigit(char c)
{
    return c >= '0' && c <= '9';
}

Result HtmlImageParseDimension(const char *value, size_t length,
                                              int *outValid,
                                              uint32_t *outValue)
{
    size_t start = 0u;
    size_t end = length;
    uint64_t acc = 0u;
    size_t i;

    if (!outValid || !outValue) {
        return RoboInvalidArgument;
    }
    *outValid = 0;
    *outValue = 0u;
    if (!value) {
        /* Absent: valid as "no declared dimension". */
        return RoboOk;
    }
    while (start < end && AsciiIsSpace(value[start])) {
        start++;
    }
    while (end > start && AsciiIsSpace(value[end - 1u])) {
        end--;
    }
    if (start >= end) {
        /* Present but empty: there is a value and it is not a dimension.
         * Reported as invalid rather than as absent, so `<img width="">`
         * cannot be mistaken for `<img>`. */
        return RoboOk;
    }
    for (i = start; i < end; i++) {
        if (!AsciiIsDigit(value[i])) {
            /* Rejects "50%", "50px", "-5", "1e3", "0x10" — all of which are
             * either layout's business or simply not dimensions. */
            return RoboOk;
        }
        acc = acc * 10u + (uint64_t)(value[i] - '0');
        if (acc > (uint64_t)RoboHtmlImageMaxDeclaredDimension) {
            /* Out of range: refuse rather than clamp, so a typo cannot turn
             * into a silent resize. */
            return RoboOk;
        }
    }
    *outValid = 1;
    *outValue = (uint32_t)acc;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Attribute view                                                      */
/* ------------------------------------------------------------------ */

/** Borrowed value of @p name, plus whether the attribute exists at all. */
static const char *AttributeOf(const HtmlNodeHandle *element,
                                const char *name, size_t *outLength,
                                int *outPresent)
{
    const char *value = HtmlElementAttributeValue(element, name,
                                                          outLength);
    if (outPresent) {
        *outPresent = (value != NULL);
    }
    if (!value && outLength) {
        *outLength = 0u;
    }
    return value;
}

/** Length of @p bytes after trimming surrounding ASCII whitespace. */
static size_t TrimmedLength(const char *bytes, size_t length)
{
    size_t start = 0u;
    size_t end = length;

    if (!bytes) {
        return 0u;
    }
    while (start < end && AsciiIsSpace(bytes[start])) {
        start++;
    }
    while (end > start && AsciiIsSpace(bytes[end - 1u])) {
        end--;
    }
    return end - start;
}

Result HtmlElementImageAttributes(
    const HtmlNodeHandle *element, HtmlImageAttributes *out,
    HtmlDiag *diag)
{
    size_t length = 0u;
    int present = 0;

    if (!element || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null argument");
    }
    memset(out, 0, sizeof(*out));
    if (HtmlElementTag(element) != RoboHtmlTagImg) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboNotSupported,
                              "element is not an img");
    }

    out->source = AttributeOf(element, "src", &length, &present);
    out->sourceLength = present ? length : 0u;
    out->sourcePresent = present;
    out->sourceUsable =
        (present && TrimmedLength(out->source, out->sourceLength) > 0u) ? 1 : 0;

    out->alt = AttributeOf(element, "alt", &length, &present);
    out->altLength = present ? length : 0u;

    out->widthSet = 0;
    out->widthValid = 0;
    out->width = 0u;
    out->heightSet = 0;
    out->heightValid = 0;
    out->height = 0u;
    {
        const char *value = AttributeOf(element, "width", &length, &present);
        int valid = 0;
        uint32_t parsed = 0u;

        if (present) {
            out->widthSet = 1;
            (void)HtmlImageParseDimension(value, length, &valid, &parsed);
            out->widthValid = valid;
            out->width = parsed;
        }
    }
    {
        const char *value = AttributeOf(element, "height", &length, &present);
        int valid = 0;
        uint32_t parsed = 0u;

        if (present) {
            out->heightSet = 1;
            (void)HtmlImageParseDimension(value, length, &valid,
                                                  &parsed);
            out->heightValid = valid;
            out->height = parsed;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Resource object                                                     */
/* ------------------------------------------------------------------ */

struct HtmlImage {
    uint32_t refcount;
    HtmlImageState state;
    /** The code behind FAILED/UNSUPPORTED; RoboOk otherwise. */
    Result failure;
    /** Owned copy of the reference, or NULL. Not NUL-terminated by contract. */
    char *source;
    size_t sourceLength;
    /** Owned decoded pixels, or NULL unless the state is DECODED. */
    GfxImage *graphics;
};

static char *DupBytes(const char *bytes, size_t length)
{
    char *copy;

    if (!bytes || length == 0u) {
        return NULL;
    }
    copy = (char *)Malloc(length);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, bytes, length);
    return copy;
}

/**
 * Allocate one resource. The reference is taken as bytes plus a length
 * rather than as a C string, because that is how it arrives: attribute
 * values are byte runs with no terminator contract, and copying them through
 * a temporary terminator would be a second copy for nothing.
 */
static HtmlImage *ImageNew(HtmlImageState state,
                                    Result failure,
                                    const char *sourceReference,
                                    size_t sourceLength)
{
    HtmlImage *image =
        (HtmlImage *)RoboCalloc(1u, sizeof(HtmlImage));

    if (!image) {
        return NULL;
    }
    image->refcount = 1u;
    image->state = state;
    image->failure = failure;
    if (sourceReference && sourceLength > 0u) {
        image->source = DupBytes(sourceReference, sourceLength);
        /* The recorded name is diagnostic only: losing it is preferable to
         * failing a resolve that otherwise succeeded. */
        image->sourceLength = image->source ? sourceLength : 0u;
    }
    return image;
}

/**
 * Map a decode attempt to a state. The one distinction that matters:
 * "these bytes are not an image this runtime implements" is UNSUPPORTED,
 * while "these bytes claim to be an image and are wrong" is FAILED.
 */
static HtmlImageState StateForResult(Result result)
{
    if (result == RoboOk) {
        return RoboHtmlImageStateDecoded;
    }
    if (result == RoboNotSupported) {
        return RoboHtmlImageStateUnsupported;
    }
    return RoboHtmlImageStateFailed;
}

Result HtmlImageCreateDecoded(const void *bytes, size_t length,
                                            const char *sourceReference,
                                            HtmlImage **out,
                                            HtmlDiag *diag)
{
    HtmlImage *image;
    GfxImage *graphics = NULL;
    Result decode = RoboError;

    if (!out || (!bytes && length != 0u)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null bytes or out");
    }
    *out = NULL;
    if (bytes && length > 0u) {
        /* One decode. Its result code *is* the reason, so there is nothing to
         * probe for separately — and a resource that reports DECODED holds
         * pixels, never a promise of them. */
        decode = GfxImageDecode(bytes, length, NULL, &graphics);
    }
    image = ImageNew(StateForResult(decode), decode, sourceReference,
                      sourceReference ? strlen(sourceReference) : 0u);
    if (!image) {
        if (graphics) {
            GfxImageRelease(graphics);
        }
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboOutOfMemory,
                              "image allocation failed");
    }
    if (decode == RoboOk) {
        image->graphics = graphics;
    } else if (graphics) {
        /* The decoder only hands back pixels with RoboOk, but a resource
         * must never hold a picture its state says it does not have. */
        GfxImageRelease(graphics);
    }
    *out = image;
    return RoboOk;
}

void HtmlImageDestroy(HtmlImage *image)
{
    if (!image) {
        return;
    }
    if (image->graphics) {
        GfxImageRelease(image->graphics);
    }
    if (image->source) {
        RoboFree(image->source);
    }
    RoboFree(image);
}

void HtmlImageRetain(HtmlImage *image)
{
    if (image) {
        image->refcount++;
    }
}

void HtmlImageRelease(HtmlImage *image)
{
    if (!image || image->refcount == 0u) {
        return;
    }
    image->refcount--;
    if (image->refcount == 0u) {
        HtmlImageDestroy(image);
    }
}

HtmlImageState MakeHtmlImageState(const HtmlImage *image)
{
    return image ? image->state : RoboHtmlImageStateNone;
}

const char *HtmlImageSource(const HtmlImage *image,
                                   size_t *outLength)
{
    if (outLength) {
        *outLength = image ? image->sourceLength : 0u;
    }
    return image ? image->source : NULL;
}

GfxImage *HtmlImageGraphics(const HtmlImage *image)
{
    if (!image || image->state != RoboHtmlImageStateDecoded) {
        return NULL;
    }
    return image->graphics;
}

int HtmlImageIntrinsicSize(const HtmlImage *image,
                                   uint32_t *outWidth, uint32_t *outHeight)
{
    GfxImage *graphics;

    if (!image || !outWidth || !outHeight) {
        return 0;
    }
    graphics = HtmlImageGraphics(image);
    if (!graphics) {
        return 0;
    }
    *outWidth = GfxImageWidth(graphics);
    *outHeight = GfxImageHeight(graphics);
    return 1;
}

Result HtmlImageFailure(const HtmlImage *image)
{
    if (!image) {
        return RoboInvalidArgument;
    }
    return image->failure;
}

/* ------------------------------------------------------------------ */
/* Debug description                                                   */
/* ------------------------------------------------------------------ */

typedef struct textBuilder {
    char *out;
    size_t capacity;
    size_t used;
} TextBuilder;

static void AppendText(TextBuilder *b, const char *text)
{
    if (!b->out || b->capacity == 0u) {
        b->used += strlen(text);
        return;
    }
    while (*text != '\0') {
        if (b->used + 1u < b->capacity) {
            b->out[b->used] = *text;
        }
        b->used++;
        text++;
    }
    b->out[(b->used < b->capacity) ? b->used : b->capacity - 1u] = '\0';
}

static void AppendBytes(TextBuilder *b, const char *bytes, size_t length)
{
    size_t i;

    for (i = 0u; i < length; i++) {
        char one[2];

        one[0] = bytes[i];
        one[1] = '\0';
        AppendText(b, one);
    }
}

static void AppendUint(TextBuilder *b, uint32_t value)
{
    char digits[16];
    int at = 0;

    if (value == 0u) {
        AppendText(b, "0");
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
        AppendText(b, one);
    }
}

size_t HtmlImageDescribe(const HtmlImage *image, char *out,
                                size_t capacity)
{
    TextBuilder b;

    b.out = out;
    b.capacity = capacity;
    b.used = 0u;
    if (image && image->source) {
        AppendText(&b, "img src=\"");
        AppendBytes(&b, image->source, image->sourceLength);
        AppendText(&b, "\" ");
    } else {
        AppendText(&b, "img ");
    }
    AppendText(&b, "state=");
    AppendText(&b, HtmlImageStateName(MakeHtmlImageState(image)));
    if (image && image->state == RoboHtmlImageStateDecoded) {
        GfxImage *graphics = image->graphics;

        AppendText(&b, " intrinsic=");
        AppendUint(&b, GfxImageWidth(graphics));
        AppendText(&b, "x");
        AppendUint(&b, GfxImageHeight(graphics));
    } else if (image && image->failure != RoboOk) {
        AppendText(&b, " reason=");
        AppendUint(&b, (uint32_t)image->failure);
    }
    if (out && capacity > 0u) {
        out[(b.used < capacity) ? b.used : capacity - 1u] = '\0';
    }
    return b.used;
}

/* ------------------------------------------------------------------ */
/* Fetch seam + probes                                                 */
/* ------------------------------------------------------------------ */

void HtmlImageResolverInit(HtmlImageResolver *resolver)
{
    if (!resolver) {
        return;
    }
    resolver->fetch = NULL;
    resolver->userdata = NULL;
}

int HtmlImageResolverIsUsable(const HtmlImageResolver *resolver)
{
    return (resolver && resolver->fetch) ? 1 : 0;
}

Result HtmlImageBytesState(const void *bytes, size_t length,
                                          HtmlImageState *outState)
{
    GfxDecodeInfo info;
    Result res;

    if (!bytes || !outState) {
        return RoboInvalidArgument;
    }
    *outState = RoboHtmlImageStateFailed;
    res = GfxImageDecodeInfo(bytes, length, NULL, &info);
    if (res == RoboOk) {
        *outState = RoboHtmlImageStateDecoded;
    } else if (res == RoboNotSupported) {
        *outState = RoboHtmlImageStateUnsupported;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Local asset resolver                                                */
/* ------------------------------------------------------------------ */

typedef struct assetEntry {
    char *name;
    size_t nameLength;
    const uint8_t *bytes;
    size_t length;
} AssetEntry;

struct HtmlImageAssets {
    AssetEntry *entries;
    size_t count;
    size_t capacity;
};

static void AssetEntryClear(AssetEntry *entry)
{
    if (entry->name) {
        RoboFree(entry->name);
    }
    memset(entry, 0, sizeof(*entry));
}

static int AssetFind(const HtmlImageAssets *assets, const char *name,
                      size_t length)
{
    size_t i;

    for (i = 0u; i < assets->count; i++) {
        if (assets->entries[i].nameLength == length &&
            memcmp(assets->entries[i].name, name, length) == 0) {
            return (int)i;
        }
    }
    return -1;
}

Result HtmlImageAssetsAdd(HtmlImageAssets *assets,
                                         const char *name, const void *bytes,
                                         size_t length, HtmlDiag *diag)
{
    size_t nameLength;
    AssetEntry entry;

    if (!assets || !name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null asset argument");
    }
    nameLength = strlen(name);
    if (nameLength == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "empty asset name");
    }
    if (!bytes && length != 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null asset bytes");
    }
    if (AssetFind(assets, name, nameLength) >= 0) {
        /* Registering the same name twice is almost always a real mistake,
         * and silently shadowing would make a missing image look present. */
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboAlreadyExists, "duplicate asset name");
    }
    if (assets->count == assets->capacity) {
        size_t next = assets->capacity ? assets->capacity * 2u : 8u;
        AssetEntry *grown = (AssetEntry *)Realloc(
            assets->entries, next * sizeof(AssetEntry));

        if (!grown) {
            return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                                  RoboOutOfMemory, "asset table growth");
        }
        assets->entries = grown;
        assets->capacity = next;
    }
    memset(&entry, 0, sizeof(entry));
    entry.name = (char *)Malloc(nameLength + 1u);
    if (!entry.name) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboOutOfMemory, "asset name copy");
    }
    memcpy(entry.name, name, nameLength + 1u);
    entry.nameLength = nameLength;
    entry.bytes = (const uint8_t *)bytes;
    entry.length = length;
    assets->entries[assets->count++] = entry;
    return RoboOk;
}

Result HtmlImageAssetsCreate(
    const HtmlImageAsset *entries, size_t count,
    HtmlImageAssets **out)
{
    HtmlImageAssets *assets;
    size_t i;

    if (!out || (!entries && count != 0u)) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    assets = (HtmlImageAssets *)RoboCalloc(
        1u, sizeof(HtmlImageAssets));
    if (!assets) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < count; i++) {
        Result res = HtmlImageAssetsAdd(
            assets, entries[i].name, entries[i].bytes, entries[i].length, NULL);

        if (res != RoboOk) {
            HtmlImageAssetsDestroy(assets);
            return res;
        }
    }
    *out = assets;
    return RoboOk;
}

const uint8_t *HtmlImageAssetsLookup(
    const HtmlImageAssets *assets, const char *name,
    size_t *outLength)
{
    int index;

    if (outLength) {
        *outLength = 0u;
    }
    if (!assets || !name) {
        return NULL;
    }
    index = AssetFind(assets, name, strlen(name));
    if (index < 0) {
        return NULL;
    }
    if (outLength) {
        *outLength = assets->entries[index].length;
    }
    return assets->entries[index].bytes;
}

size_t HtmlImageAssetsCount(const HtmlImageAssets *assets)
{
    return assets ? assets->count : 0u;
}

void HtmlImageAssetsDestroy(HtmlImageAssets *assets)
{
    size_t i;

    if (!assets) {
        return;
    }
    for (i = 0u; i < assets->count; i++) {
        AssetEntryClear(&assets->entries[i]);
    }
    if (assets->entries) {
        RoboFree(assets->entries);
    }
    RoboFree(assets);
}

/*
 * A scheme is `ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"` at the very
 * start. Recognising it is the whole policy: anything with a scheme names
 * something this runtime will not fetch (no network, no files, no data URI),
 * and saying so is more useful than "not found", which would blame the
 * document for a limitation of the runtime.
 */
static int ReferenceHasScheme(const uint8_t *reference, size_t length)
{
    size_t i = 0u;

    if (length == 0u) {
        return 0;
    }
    if (!((reference[0] >= 'a' && reference[0] <= 'z') ||
          (reference[0] >= 'A' && reference[0] <= 'Z'))) {
        return 0;
    }
    for (i = 1u; i < length; i++) {
        uint8_t c = reference[i];

        if (c == ':') {
            return 1;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            continue;
        }
        return 0; /* the colon, if any, came after a non-scheme character */
    }
    return 0;
}

static Result AssetsFetch(void *userdata,
                                  const HtmlImageRequest *request,
                                  const uint8_t **outBytes,
                                  size_t *outLength, HtmlDiag *diag)
{
    const HtmlImageAssets *assets =
        (const HtmlImageAssets *)userdata;
    const AssetEntry *found = NULL;
    size_t i;

    if (!assets || !request || !outBytes || !outLength) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null fetch argument");
    }
    *outBytes = NULL;
    *outLength = 0u;
    if (request->referenceLength == 0u || !request->reference) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotFound,
                              "empty reference");
    }
    if (ReferenceHasScheme((const uint8_t *)request->reference,
                             request->referenceLength)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboNotSupported,
                              "scheme references are not fetched");
    }
    /*
     * Exact byte-for-byte match against the registered names. This is the
     * security property, not a shortcut: because nothing is ever opened,
     * "..", an absolute path and a backslash are all just names that are not
     * registered. There is no traversal surface to defend.
     */
    for (i = 0u; i < assets->count; i++) {
        if (assets->entries[i].nameLength == request->referenceLength &&
            memcmp(assets->entries[i].name, request->reference,
                   request->referenceLength) == 0) {
            found = &assets->entries[i];
            break;
        }
    }
    if (!found) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotFound,
                              "asset not registered");
    }
    *outBytes = found->bytes;
    *outLength = found->length;
    return RoboOk;
}

HtmlImageResolver HtmlImageAssetsResolver(
    const HtmlImageAssets *assets)
{
    HtmlImageResolver resolver;

    resolver.fetch = AssetsFetch;
    resolver.userdata = (void *)assets;
    return resolver;
}

/* ------------------------------------------------------------------ */
/* Document attachment table                                           */
/* ------------------------------------------------------------------ */

static int HandleSameNode(const HtmlNodeHandle *a,
                            const HtmlNodeHandle *b)
{
    return a->documentId == b->documentId && a->nodeId == b->nodeId &&
           a->generation == b->generation;
}

static HtmlImageAttachment *AttachmentFind(
    HtmlDocumentSlot *slot, const HtmlNodeHandle *node)
{
    size_t i;

    for (i = 0u; i < slot->imageCount; i++) {
        if (HandleSameNode(&slot->images[i].node, node)) {
            return &slot->images[i];
        }
    }
    return NULL;
}

/** Release and remove entry @p index, compacting the table. */
static void AttachmentRemove(HtmlDocumentSlot *slot, size_t index)
{
    if (slot->images[index].image) {
        HtmlImageRelease(slot->images[index].image);
    }
    if (index + 1u < slot->imageCount) {
        memmove(&slot->images[index], &slot->images[index + 1u],
                (slot->imageCount - index - 1u) * sizeof(slot->images[0]));
    }
    slot->imageCount--;
}

/**
 * Give @p image to @p node, replacing any resource already there.
 *
 * The table *consumes* the caller's reference on RoboOk — that is the whole
 * ownership rule for an attachment, and the reason resolve() must not release
 * after storing. On failure the reference is left with the caller, so the
 * caller releases it and nothing leaks either way.
 */
static Result AttachmentStore(HtmlDocumentSlot *slot,
                                      const HtmlNodeHandle *node,
                                      HtmlImage *image,
                                      HtmlDiag *diag)
{
    HtmlImageAttachment *existing = AttachmentFind(slot, node);

    if (existing) {
        if (existing->image == image) {
            /* Already attached: keep one reference, not two. */
            HtmlImageRelease(image);
            return RoboOk;
        }
        if (existing->image) {
            HtmlImageRelease(existing->image);
        }
        existing->image = image;
        return RoboOk;
    }
    if (slot->imageCount == slot->imageCapacity) {
        size_t next = slot->imageCapacity ? slot->imageCapacity * 2u : 4u;
        HtmlImageAttachment *grown =
            (HtmlImageAttachment *)Realloc(
                slot->images, next * sizeof(slot->images[0]));

        if (!grown) {
            return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                                  RoboOutOfMemory, "image table growth");
        }
        slot->images = grown;
        slot->imageCapacity = next;
    }
    slot->images[slot->imageCount].node = *node;
    slot->images[slot->imageCount].image = image;
    slot->imageCount++;
    return RoboOk;
}

void HtmlDocumentImagesFree(HtmlDocumentSlot *slot)
{
    size_t i;

    if (!slot) {
        return;
    }
    for (i = 0u; i < slot->imageCount; i++) {
        if (slot->images[i].image) {
            HtmlImageRelease(slot->images[i].image);
        }
    }
    if (slot->images) {
        RoboFree(slot->images);
    }
    slot->images = NULL;
    slot->imageCount = 0u;
    slot->imageCapacity = 0u;
}

size_t HtmlDocumentImageCount(HtmlDocumentId documentId)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    return slot ? slot->imageCount : 0u;
}

/* ------------------------------------------------------------------ */
/* Element-side API                                                    */
/* ------------------------------------------------------------------ */

/**
 * Shared argument/eligibility gate for every element-side entry point.
 *
 * Liveness is asked of the element model itself rather than inferred from a
 * tag lookup: "missing node" and "not an img" are different answers, and a
 * tag query alone cannot tell them apart because both report UNKNOWN for a
 * stale handle and for a custom element respectively.
 */
static Result ImageElementGate(const HtmlNodeHandle *element,
                                        HtmlDiag *diag)
{
    HtmlElementView view;
    Result res;

    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null element");
    }
    if (!HtmlIsInitialized()) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboInitError,
                              "RoboHTML module not initialized");
    }
    memset(&view, 0, sizeof(view));
    res = HtmlElementViewGet(element, &view, diag);
    if (res != RoboOk) {
        return res; /* a document/node that does not resolve stays NOT_FOUND */
    }
    if (HtmlElementTag(element) != RoboHtmlTagImg) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboNotSupported, "element is not an img");
    }
    return RoboOk;
}

HtmlImageState HtmlElementImageState(
    const HtmlNodeHandle *element)
{
    HtmlDocumentSlot *slot;
    HtmlImageAttachment *attachment;
    HtmlImageAttributes attributes;

    if (!element || !HtmlIsInitialized() ||
        HtmlElementTag(element) != RoboHtmlTagImg) {
        return RoboHtmlImageStateNone;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (slot) {
        attachment = AttachmentFind(slot, element);
        if (attachment && attachment->image) {
            return MakeHtmlImageState(attachment->image);
        }
    }
    if (HtmlElementImageAttributes(element, &attributes, NULL) !=
        RoboOk) {
        return RoboHtmlImageStateNone;
    }
    return attributes.sourceUsable ? RoboHtmlImageStatePending
                                    : RoboHtmlImageStateNone;
}

Result HtmlElementImageGet(const HtmlNodeHandle *element,
                                          HtmlImage **out,
                                          HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlImageAttachment *attachment;
    Result res;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument, "null out");
    }
    *out = NULL;
    res = ImageElementGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotFound,
                              "document does not resolve");
    }
    attachment = AttachmentFind(slot, element);
    *out = attachment ? attachment->image : NULL;
    return RoboOk;
}

Result HtmlElementImageDetach(
    const HtmlNodeHandle *element, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    size_t i;
    Result res = ImageElementGate(element, diag);

    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotFound,
                              "document does not resolve");
    }
    for (i = 0u; i < slot->imageCount; i++) {
        if (HandleSameNode(&slot->images[i].node, element)) {
            AttachmentRemove(slot, i);
            break;
        }
    }
    return RoboOk;
}

Result HtmlElementImageResolve(
    const HtmlNodeHandle *element,
    const HtmlImageResolver *resolver, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlImageAttributes attributes;
    HtmlImageRequest request;
    HtmlImage *image = NULL;
    Result res;
    const uint8_t *bytes = NULL;
    size_t length = 0u;
    Result fetch;

    if (!resolver || !resolver->fetch) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboInvalidArgument,
                              "resolver is null or unusable");
    }
    res = ImageElementGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboNotFound,
                              "document does not resolve");
    }
    res = HtmlElementImageAttributes(element, &attributes, diag);
    if (res != RoboOk) {
        return res;
    }

    /*
     * No usable src: the element has no resource, so any earlier one is
     * released. That is what makes `removeAttribute("src")` observable
     * without teaching this function about attributes being mutable.
     */
    if (!attributes.sourceUsable) {
        Result detached = HtmlElementImageDetach(element, diag);

        return (detached == RoboOk)
                   ? RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboOk,
                                    "no usable source")
                   : detached;
    }

    request.reference = attributes.source;
    request.referenceLength = attributes.sourceLength;
    request.alt = attributes.alt;
    request.altLength = attributes.altLength;
    request.documentName = slot->sourceName;

    fetch = resolver->fetch(resolver->userdata, &request, &bytes, &length, diag);
    if (fetch == RoboBusy) {
        /* An acquisition is already outstanding: leave the element PENDING
         * rather than inventing a failure for work that has not finished. */
        return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboBusy,
                              "image fetch already outstanding");
    }
    if (fetch == RoboOk && (!bytes || length == 0u)) {
        /* A resolver that says OK without bytes is broken; treat it as a
         * failed fetch rather than storing an empty resource. */
        fetch = RoboError;
    }

    if (fetch == RoboOk) {
        res = HtmlImageCreateDecoded(bytes, length, attributes.source,
                                             &image, diag);
    } else {
        /*
         * The fetch failed, and the element still gets a resource — one whose
         * state records why. Without it, "failed to load" would be
         * indistinguishable from "not loaded yet", and a renderer could not
         * tell a broken image from an unresolved one.
         */
        image = ImageNew(StateForResult(fetch), fetch, attributes.source,
                          attributes.sourceLength);
        res = image ? RoboOk : RoboOutOfMemory;
    }
    if (res != RoboOk) {
        return res;
    }
    if (!image) {
        return RoboHtmlDiag(diag, RoboHtmlDomainImage,
                              RoboOutOfMemory, "image allocation");
    }

    LogDebug("robo_html: img resolved src=\"%s\" state=%s",
                   attributes.source ? attributes.source : "",
                   HtmlImageStateName(MakeHtmlImageState(image)));

    /*
     * The table consumes this reference on success, which is what makes
     * resolving twice a *replacement* rather than an accumulation: the
     * previous resource loses the table's reference and dies unless someone
     * else retained it.
     */
    res = AttachmentStore(slot, element, image, diag);
    if (res != RoboOk) {
        HtmlImageRelease(image);
        return res;
    }
    return RoboHtmlDiag(diag, RoboHtmlDomainImage, RoboOk,
                          HtmlImageStateName(
                              HtmlElementImageState(element)));
}
