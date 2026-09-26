/*
 * RoboHTML Phase 17.5 image-load suite (html.imageload.*).
 *
 * The Phase 8.12 suite (html.image.*) proves the *resource*: bytes through the
 * seam, pixels through the decoder, states that fail without damaging the
 * document. This one proves the lifecycle a document layer drives — which
 * references this runtime will even try, how big the box is, whether a change
 * is layout-affecting, and what happens to a load that was outstanding when
 * the element goes away.
 *
 * The fixture images are encoded by the real Goal 6.13 encoder rather than
 * embedded, so the intrinsic size a load reports is a fact about pixels the
 * test built.
 */

#include "RoboBase/RoboBase.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLImageLoad.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

typedef struct loadFixture {
    GfxSurface *surface;
    GfxEncodedBuffer encoded;
    HtmlImageAssets *assets;
    HtmlImageResolver resolver;
} LoadFixture;

static void LoadFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static GfxSurface *LoadSurface(uint32_t width, uint32_t height)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    uint32_t x;
    uint32_t y;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    for (y = 0u; y < height; y++) {
        for (x = 0u; x < width; x++) {
            GfxColorRgba8 c;

            c.r = (uint8_t)(x * 40u + 1u);
            c.g = (uint8_t)(y * 40u + 2u);
            c.b = (uint8_t)((x + y) * 20u + 3u);
            c.a = 255u;
            RoboCheckEqInt(GfxSurfaceSetPixel(surface, x, y, c), RoboOk);
        }
    }
    return surface;
}

static void LoadEncode(GfxSurface *surface, GfxEncodedBuffer *out)
{
    GfxEncodeOptions opts;

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(out, 0, sizeof(*out));
    RoboCheckEqInt(GfxSurfaceEncode(surface, &opts, out), RoboOk);
    RoboCheck(out->size > 0u);
}

/** A 4x4 asset table plus the resolver over it. */
static void LoadFixtureInit(LoadFixture *f)
{
    LoadFixtureBegin();
    f->surface = LoadSurface(4u, 4u);
    LoadEncode(f->surface, &f->encoded);
    RoboCheckEqInt(HtmlImageAssetsCreate(NULL, 0u, &f->assets), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(f->assets, "Assets/Logo.png",
                                      f->encoded.data, f->encoded.size,
                                      NULL), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(f->assets, "Other.png",
                                      f->encoded.data, f->encoded.size,
                                      NULL), RoboOk);
    f->resolver = HtmlImageAssetsResolver(f->assets);
    RoboCheck(HtmlImageResolverIsUsable(&f->resolver));
}

static void LoadFixtureEnd(LoadFixture *f)
{
    HtmlImageAssetsDestroy(f->assets);
    GfxEncodedBufferRelease(&f->encoded);
    GfxSurfaceRelease(f->surface);
}

/*
 * One live document at a time, retired when the next case asks for one.
 *
 * The runtime caps how many documents may exist at once, and a suite that
 * leaked one per case would start failing its own setup — with an
 * out-of-memory from HtmlDocumentCreate, which reads like the model is broken
 * rather than the test. Retiring here keeps the bound at one and leaves each
 * case free to not care.
 */
static HtmlDocumentId LoadLiveDocument = RoboHtmlDocumentIdNone;

static HtmlDocumentId LoadDoc(void)
{
    LoadFixtureBegin();
    if (LoadLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(LoadLiveDocument, NULL), RoboOk);
        LoadLiveDocument = RoboHtmlDocumentIdNone;
    }
    RoboCheckEqInt(HtmlDocumentCreate(&LoadLiveDocument, NULL), RoboOk);
    RoboCheck(LoadLiveDocument != RoboHtmlDocumentIdNone);
    return LoadLiveDocument;
}

/** Destroy the current document — the case that wants a dead one. */
static void LoadDocEnd(void)
{
    if (LoadLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(LoadLiveDocument, NULL), RoboOk);
        LoadLiveDocument = RoboHtmlDocumentIdNone;
    }
}

static void LoadBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle LoadRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct loadTagQuery {
    HtmlTag tag;
} LoadTagQuery;

static int LoadPredicateTag(const HtmlNodeView *view, void *user)
{
    const LoadTagQuery *q = (const LoadTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

static HtmlNodeHandle LoadFind(const HtmlNodeHandle *start, HtmlTag tag)
{
    LoadTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                     LoadPredicateTag, &q, &out, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/** Counts how many times the seam is consulted; bytes come from an asset. */
typedef struct loadCounting {
    const HtmlImageAssets *assets;
    int calls;
} LoadCounting;

static Result LoadFetchCounting(void *userdata,
                                const HtmlImageRequest *request,
                                const uint8_t **outBytes,
                                size_t *outLength,
                                HtmlDiag *diag)
{
    LoadCounting *state = (LoadCounting *)userdata;
    const uint8_t *bytes;
    char name[RoboHtmlNameMax + 8u];
    size_t length = 0u;
    size_t i;

    (void)diag;
    state->calls++;
    if (request->referenceLength >= sizeof(name)) {
        return RoboNotFound;
    }
    for (i = 0u; i < request->referenceLength; i++) {
        name[i] = request->reference[i];
    }
    name[request->referenceLength] = '\0';
    bytes = HtmlImageAssetsLookup(state->assets, name, &length);
    if (!bytes) {
        return RoboNotFound;
    }
    *outBytes = bytes;
    *outLength = length;
    return RoboOk;
}

static void LoadCountResolver(LoadCounting *state,
                              const HtmlImageAssets *assets,
                              HtmlImageResolver *out)
{
    state->assets = assets;
    state->calls = 0;
    out->fetch = LoadFetchCounting;
    out->userdata = state;
}

static Result LoadFetchBusy(void *userdata,
                            const HtmlImageRequest *request,
                            const uint8_t **outBytes,
                            size_t *outLength,
                            HtmlDiag *diag)
{
    (void)userdata;
    (void)request;
    (void)outBytes;
    (void)outLength;
    (void)diag;
    return RoboBusy;
}

/* ------------------------------------------------------------------ */
/* Reference classification                                            */
/* ------------------------------------------------------------------ */

static void TestImageLoadReferenceClass(void)
{
    HtmlImageReferenceClass cls;

    RoboCheckEqInt(
        HtmlImageReferenceClassify("Assets/Logo.png", 15u, "", 0u, &cls,
                                   NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceLocal);
    RoboCheckStrEq(HtmlImageReferenceClassName(cls), "local");

    RoboCheckEqInt(
        HtmlImageReferenceClassify("/Assets/Logo.png", 16u, "", 0u, &cls,
                                   NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceAbsolute);

    RoboCheckEqInt(
        HtmlImageReferenceClassify("https://example.test/a.png", 25u, "",
                                   0u, &cls, NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceExternal);

    RoboCheckEqInt(
        HtmlImageReferenceClassify("http://example.test/a.png", 24u, "",
                                   0u, &cls, NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceExternal);

    /* The same scheme policy a link is held to: these never reach a seam. */
    RoboCheckEqInt(
        HtmlImageReferenceClassify("file:///etc/passwd", 17u, "", 0u, &cls,
                                   NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceUnsupported);

    RoboCheckEqInt(
        HtmlImageReferenceClassify("data:image/png;base64,AAAA", 25u, "",
                                   0u, &cls, NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceUnsupported);

    RoboCheckEqInt(
        HtmlImageReferenceClassify("javascript:alert(1)", 18u, "", 0u, &cls,
                                   NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceUnsupported);

    RoboCheckEqInt(
        HtmlImageReferenceClassify("weird:thing", 11u, "", 0u, &cls, NULL),
        RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceUnsupported);

    /* A fragment names a place in this document, not bytes. */
    RoboCheckEqInt(
        HtmlImageReferenceClassify("#Overview", 9u, "", 0u, &cls, NULL),
        RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceInvalid);

    RoboCheckEqInt(
        HtmlImageReferenceClassify("", 0u, "", 0u, &cls, NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceNone);
    RoboCheckStrEq(HtmlImageReferenceClassName(cls), "none");

    /* A well-formed relative reference resolves against the document's own. */
    RoboCheckEqInt(
        HtmlImageReferenceClassify("Logo.png", 8u, "App/Page", 8u, &cls,
                                   NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceLocal);

    RoboCheckEqInt(HtmlImageReferenceClassIsKnown(
                       RoboHtmlImageReferenceUnsupported), 1);
    RoboCheckEqInt(HtmlImageReferenceClassIsKnown(
                       (HtmlImageReferenceClass)77), 0);
    RoboCheckStrEq(HtmlImageReferenceClassName(
                       (HtmlImageReferenceClass)77), "unknown");

    /* Errors. */
    RoboCheckEqInt(HtmlImageReferenceClassify("x", 1u, "", 0u, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlImageReferenceClassify(NULL, 3u, "", 0u, &cls, NULL),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Element-level classification                                        */
/* ------------------------------------------------------------------ */

static void TestImageLoadElementClass(void)
{
    HtmlDocumentId id = LoadDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlNodeHandle para;
    HtmlImageReferenceClass cls;

    LoadBuild(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\">"
                  "<img alt=\"No source\">"
                  "<img src=\"file:///secret\">"
                  "<p id=\"p1\">text</p>");
    root = LoadRoot(id);

    img = LoadFind(&root, RoboHtmlTagImg);
    RoboCheckEqInt(HtmlElementImageReferenceClass(&img, &cls, NULL), RoboOk);
    RoboCheckEqInt(cls, RoboHtmlImageReferenceLocal);

    /* Second <img>: no src is NONE, not an error. */
    {
        HtmlNodeHandle first = img;
        HtmlNodeHandle second;

        RoboCheckEqInt(HtmlNodeNextSibling(&first, &second, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementTag(&second), RoboHtmlTagImg);
        RoboCheckEqInt(HtmlElementImageReferenceClass(&second, &cls, NULL),
                       RoboOk);
        RoboCheckEqInt(cls, RoboHtmlImageReferenceNone);
        {
            HtmlNodeHandle third;

            RoboCheckEqInt(HtmlNodeNextSibling(&second, &third, NULL),
                           RoboOk);
            RoboCheckEqInt(HtmlElementImageReferenceClass(&third, &cls,
                                                          NULL), RoboOk);
            RoboCheckEqInt(cls, RoboHtmlImageReferenceUnsupported);
        }
    }

    para = LoadFind(&root, RoboHtmlTagP);
    RoboCheckEqInt(HtmlElementImageReferenceClass(&para, &cls, NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlElementImageReferenceClass(NULL, &cls, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageReferenceClass(&img, NULL, NULL),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The lifecycle against real bytes                                    */
/* ------------------------------------------------------------------ */

static void TestImageLoadDecoded(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;

    LoadFixtureInit(&f);
    id = LoadDoc();
    LoadBuild(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt(out.reference, RoboHtmlImageReferenceLocal);
    RoboCheckEqInt(out.requested, 1);
    RoboCheckEqInt(out.resolved, 1);
    RoboCheckEqInt(out.loaded, 1);
    RoboCheckEqInt(out.failed, 0);
    RoboCheckEqInt(out.pending, 0);
    RoboCheckEqInt(out.hasIntrinsic, 1);
    RoboCheckEqInt((int)out.intrinsicWidth, 4);
    RoboCheckEqInt((int)out.intrinsicHeight, 4);
    RoboCheckEqInt(out.hasUsedSize, 1);
    RoboCheckEqInt((int)out.usedWidth, 4);
    RoboCheckEqInt((int)out.usedHeight, 4);
    RoboCheckEqInt(out.widthFromDeclaration, 0);
    RoboCheckEqInt(out.heightFromDeclaration, 0);
    RoboCheckEqInt(out.fetchResult, RoboOk);
    RoboCheckEqInt(out.layoutAffected, 1);

    /* The describe line names the class, state and used size. */
    {
        char line[256];
        size_t need = HtmlImageLoadOutcomeDescribe(&out, line, sizeof(line));

        RoboCheck(need > 0u && need < sizeof(line));
        RoboCheck(strstr(line, "class=local") != NULL);
        RoboCheck(strstr(line, "state=decoded") != NULL);
        RoboCheck(strstr(line, "used=4x4") != NULL);
        RoboCheck(strstr(line, "intrinsic=4x4") != NULL);
        RoboCheck(strstr(line, "layout=1") != NULL);

        /* Truncation is bounded and NUL-terminated, never an overrun. */
        {
            char tiny[8];
            size_t full = HtmlImageLoadOutcomeDescribe(&out, tiny,
                                                      sizeof(tiny));

            RoboCheck(full == need);
            RoboCheck(tiny[sizeof(tiny) - 1u] == '\0');
        }
    }

    LoadFixtureEnd(&f);
}

static void TestImageLoadDeclaredSize(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;

    LoadFixtureInit(&f);
    id = LoadDoc();
    LoadBuild(id, "<img src=\"Assets/Logo.png\" width=\"9\" height=\"7\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.hasIntrinsic, 1);
    RoboCheckEqInt((int)out.intrinsicWidth, 4);
    RoboCheckEqInt(out.widthFromDeclaration, 1);
    RoboCheckEqInt(out.heightFromDeclaration, 1);
    RoboCheckEqInt((int)out.declaredWidth, 9);
    RoboCheckEqInt((int)out.declaredHeight, 7);
    RoboCheckEqInt(out.hasUsedSize, 1);
    RoboCheckEqInt((int)out.usedWidth, 9);
    RoboCheckEqInt((int)out.usedHeight, 7);

    /* An invalid declared dimension is reported, never applied. */
    {
        LoadFixture f2;
        HtmlDocumentId id2;
        HtmlNodeHandle root2;
        HtmlNodeHandle img2;
        HtmlImageLoadOutcome out2;

        LoadFixtureInit(&f2);
        id2 = LoadDoc();
        LoadBuild(id2, "<img src=\"Assets/Logo.png\" width=\"abc\">");
        root2 = LoadRoot(id2);
        img2 = LoadFind(&root2, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageLoad(&img2, &f2.resolver, &out2, NULL), RoboOk);
        RoboCheckEqInt(out2.widthFromDeclaration, 0);
        RoboCheckEqInt(out2.hasIntrinsic, 1);
        /* The intrinsic size still answers the used size. */
        RoboCheckEqInt((int)out2.usedWidth, 4);
        RoboCheckEqInt((int)out2.usedHeight, 4);
        LoadFixtureEnd(&f2);
    }

    LoadFixtureEnd(&f);
}

static void TestImageLoadOneAxisDeclared(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;
    uint32_t width = 0u;
    uint32_t height = 0u;
    int hasSize = 0;

    LoadFixtureInit(&f);
    id = LoadDoc();
    LoadBuild(id, "<img src=\"Assets/Logo.png\" width=\"9\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.widthFromDeclaration, 1);
    RoboCheckEqInt(out.heightFromDeclaration, 0);
    RoboCheckEqInt((int)out.usedWidth, 9);
    RoboCheckEqInt((int)out.usedHeight, 4);
    RoboCheckEqInt(out.hasUsedSize, 1);

    /* The dedicated reader agrees with the outcome, with no fetch at all. */
    RoboCheckEqInt(HtmlElementImageSize(&img, &width, &height, &hasSize,
                                        NULL), RoboOk);
    RoboCheckEqInt(hasSize, 1);
    RoboCheckEqInt((int)width, 9);
    RoboCheckEqInt((int)height, 4);

    LoadFixtureEnd(&f);
}

static void TestImageLoadNoSizeWithoutPixels(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;
    int hasSize = 0;

    LoadFixtureInit(&f);
    id = LoadDoc();
    /* Declared width, no height, and no decodable source. */
    LoadBuild(id, "<img src=\"Missing.png\" width=\"9\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.state, RoboHtmlImageStateFailed);
    RoboCheckEqInt(out.failed, 1);
    RoboCheckEqInt(out.fetchResult, RoboNotFound);
    RoboCheckEqInt(out.hasIntrinsic, 0);
    RoboCheckEqInt(out.hasUsedSize, 0);
    RoboCheckEqInt((int)out.usedWidth, 9);
    RoboCheckEqInt((int)out.usedHeight, 0);

    RoboCheckEqInt(HtmlElementImageSize(&img, &out.usedWidth,
                                        &out.usedHeight, &hasSize, NULL),
                   RoboOk);
    RoboCheckEqInt(hasSize, 0);

    LoadFixtureEnd(&f);
}

/*
 * A refused reference must not consult the seam even once: that is the whole
 * security claim of the classification, and counting calls is how it is
 * proved rather than asserted.
 */
static void TestImageLoadRefusedDoesNotFetch(void)
{
    LoadFixture f;
    LoadCounting counting;
    HtmlImageResolver countingResolver;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;

    LoadFixtureInit(&f);
    LoadCountResolver(&counting, f.assets, &countingResolver);
    id = LoadDoc();
    LoadBuild(id, "<img src=\"file:///secret.png\">"
                  "<img src=\"#Section\">"
                  "<img src=\"Missing.png\">"
                  "<img src=\"Assets/Logo.png\">");
    root = LoadRoot(id);

    img = LoadFind(&root, RoboHtmlTagImg);
    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &countingResolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.reference, RoboHtmlImageReferenceUnsupported);
    RoboCheckEqInt(out.state, RoboHtmlImageStateUnsupported);
    RoboCheckEqInt(out.requested, 0);
    RoboCheckEqInt(counting.calls, 0);

    {
        HtmlNodeHandle second;

        RoboCheckEqInt(HtmlNodeNextSibling(&img, &second, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlElementImageLoad(&second, &countingResolver, &out, NULL),
            RoboOk);
        RoboCheckEqInt(out.reference, RoboHtmlImageReferenceInvalid);
        RoboCheckEqInt(out.state, RoboHtmlImageStateFailed);
        RoboCheckEqInt(out.requested, 0);
        RoboCheckEqInt(counting.calls, 0);

        {
            HtmlNodeHandle third;

            RoboCheckEqInt(HtmlNodeNextSibling(&second, &third, NULL),
                           RoboOk);
            RoboCheckEqInt(
                HtmlElementImageLoad(&third, &countingResolver, &out, NULL),
                RoboOk);
            RoboCheckEqInt(out.state, RoboHtmlImageStateFailed);
            RoboCheckEqInt(out.fetchResult, RoboNotFound);
            RoboCheckEqInt(counting.calls, 1);

            {
                HtmlNodeHandle fourth;

                RoboCheckEqInt(HtmlNodeNextSibling(&third, &fourth, NULL),
                               RoboOk);
                RoboCheckEqInt(
                    HtmlElementImageLoad(&fourth, &countingResolver, &out,
                                         NULL), RoboOk);
                RoboCheckEqInt(out.state, RoboHtmlImageStateDecoded);
                RoboCheckEqInt(counting.calls, 2);
            }
        }
    }

    LoadFixtureEnd(&f);
}

static void TestImageLoadNoSource(void)
{
    LoadFixture f;
    LoadCounting counting;
    HtmlImageResolver countingResolver;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;

    LoadFixtureInit(&f);
    LoadCountResolver(&counting, f.assets, &countingResolver);
    id = LoadDoc();
    LoadBuild(id, "<img alt=\"Only alt text\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &countingResolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.state, RoboHtmlImageStateNone);
    RoboCheckEqInt(out.reference, RoboHtmlImageReferenceNone);
    RoboCheckEqInt(out.requested, 0);
    RoboCheckEqInt(out.resolved, 0);
    RoboCheckEqInt(out.layoutAffected, 0);
    RoboCheckEqInt(counting.calls, 0);

    /* The alt text is still the DOM's, untouched by the load. */
    {
        size_t length = 0u;
        const char *alt = HtmlElementAttributeValue(&img, "alt", &length);

        RoboCheck(alt != NULL);
        RoboCheckEqInt((int)length, 13);
    }

    LoadFixtureEnd(&f);
}

/* ------------------------------------------------------------------ */
/* Replacement and removal                                             */
/* ------------------------------------------------------------------ */

static void TestImageLoadReplaceSource(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;

    LoadFixtureInit(&f);
    id = LoadDoc();
    LoadBuild(id, "<img src=\"Assets/Logo.png\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt(out.replaced, 0);

    /* A different, valid source: the attribute moves and the resource is
     * replaced in one call. */
    RoboCheckEqInt(
        HtmlElementImageReplaceSource(&img, "Other.png", 9u, &f.resolver,
                                      &out, NULL), RoboOk);
    RoboCheckEqInt(out.sourceChanged, 1);
    RoboCheckEqInt(out.replaced, 1);
    RoboCheckEqInt(out.state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt((int)out.usedWidth, 4);

    {
        size_t length = 0u;
        const char *value = HtmlElementAttributeValue(&img, "src", &length);

        RoboCheck(value != NULL);
        RoboCheckEqInt((int)length, 9);
        RoboCheck(memcmp(value, "Other.png", 9u) == 0);
    }

    /* Replacing with the same bytes is not a source change. */
    RoboCheckEqInt(
        HtmlElementImageReplaceSource(&img, "Other.png", 9u, &f.resolver,
                                      &out, NULL), RoboOk);
    RoboCheckEqInt(out.sourceChanged, 0);

    /* A refused source releases the resource and reports why. */
    RoboCheckEqInt(
        HtmlElementImageReplaceSource(&img, "javascript:void(0)", 17u,
                                      &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.sourceChanged, 1);
    RoboCheckEqInt(out.state, RoboHtmlImageStateUnsupported);
    RoboCheckEqInt(out.replaced, 1);
    RoboCheckEqInt(out.contentChanged, 1);

    /* An empty source removes the attribute and leaves the element inert. */
    RoboCheckEqInt(
        HtmlElementImageReplaceSource(&img, "", 0u, &f.resolver, &out,
                                      NULL), RoboOk);
    RoboCheckEqInt(HtmlElementHasAttribute(&img, "src"), 0);
    RoboCheckEqInt(out.state, RoboHtmlImageStateNone);
    RoboCheckEqInt(out.sourceChanged, 1);

    /* A source too long to store is refused, not truncated. */
    {
        char huge[RoboHtmlAttrValueMax + 32u];
        size_t i;

        for (i = 0u; i < sizeof(huge); i++) {
            huge[i] = 'a';
        }
        RoboCheckEqInt(
            HtmlElementImageReplaceSource(&img, huge, sizeof(huge),
                                          &f.resolver, &out, NULL),
            RoboOk);
        RoboCheckEqInt(out.reference, RoboHtmlImageReferenceInvalid);
        RoboCheckEqInt(out.state, RoboHtmlImageStateFailed);
        RoboCheckEqInt(HtmlElementHasAttribute(&img, "src"), 0);
    }

    LoadFixtureEnd(&f);
}

static void TestImageLoadAbandon(void)
{
    LoadFixture f;
    HtmlImageResolver busy;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;
    int wasPending = 0;

    LoadFixtureInit(&f);
    busy.fetch = LoadFetchBusy;
    busy.userdata = NULL;

    id = LoadDoc();
    LoadBuild(id, "<img src=\"Assets/Logo.png\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(HtmlElementImageLoad(&img, &busy, &out, NULL), RoboOk);
    RoboCheckEqInt(out.state, RoboHtmlImageStatePending);
    RoboCheckEqInt(out.pending, 1);
    RoboCheckEqInt(out.requested, 1);
    RoboCheckEqInt(out.resolved, 0);
    RoboCheckEqInt(out.fetchResult, RoboBusy);
    RoboCheckEqInt(out.layoutAffected, 0);

    /* The element removed mid-load: the outstanding load is abandoned. */
    RoboCheckEqInt(HtmlElementImageAbandon(&img, &wasPending, NULL), RoboOk);
    RoboCheckEqInt(wasPending, 1);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0);

    /*
     * The pending fact is *derived* from the element's state (a usable `src`
     * with no resource attached), not a latch, so a second call reports the
     * same thing. What is proved here is the release: a resource that was
     * there really goes away.
     */
    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1);
    RoboCheckEqInt(HtmlElementImageAbandon(&img, &wasPending, NULL), RoboOk);
    RoboCheckEqInt(wasPending, 0);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0);
    RoboCheckEqInt(HtmlElementImageAbandon(&img, &wasPending, NULL), RoboOk);
    RoboCheckEqInt(wasPending, 1);

    RoboCheckEqInt(HtmlElementImageAbandon(&img, NULL, NULL),
                   RoboInvalidArgument);

    /* Not an <img> is a refusal, not a silent success. */
    {
        HtmlNodeHandle para;

        LoadBuild(id, "<img src=\"Assets/Logo.png\"><p id=\"p2\">x</p>");
        root = LoadRoot(id);
        para = LoadFind(&root, RoboHtmlTagP);
        RoboCheckEqInt(HtmlElementImageAbandon(&para, &wasPending, NULL),
                       RoboNotSupported);
        RoboCheckEqInt(wasPending, 0);
    }

    LoadFixtureEnd(&f);
}

/* ------------------------------------------------------------------ */
/* Layout invalidation                                                 */
/* ------------------------------------------------------------------ */

static void TestImageLoadInvalidation(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;

    LoadFixtureInit(&f);
    id = LoadDoc();
    LoadBuild(id, "<img src=\"Assets/Logo.png\" width=\"4\" height=\"4\">");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    /*
     * A declared box pins the size, so the load that fills it changes the
     * *content* and not the size — which is what keeps a repaint from being
     * scheduled for a size that never moved.
     */
    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.loaded, 1);
    RoboCheckEqInt(out.contentChanged, 1);
    RoboCheckEqInt(out.sizeChanged, 0);
    RoboCheckEqInt(out.layoutAffected, 1);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.replaced, 1);
    RoboCheckEqInt(out.sizeChanged, 0);

    /*
     * With no declaration the used size is the intrinsic size, so the first
     * load is a size change — the case a layout has to hear about.
     */
    {
        HtmlDocumentId bare = LoadDoc();
        HtmlNodeHandle bareRoot;
        HtmlNodeHandle bareImg;

        LoadBuild(bare, "<img src=\"Assets/Logo.png\">");
        bareRoot = LoadRoot(bare);
        bareImg = LoadFind(&bareRoot, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageLoad(&bareImg, &f.resolver, &out, NULL),
            RoboOk);
        RoboCheckEqInt(out.hasUsedSize, 1);
        RoboCheckEqInt(out.sizeChanged, 1);
        RoboCheckEqInt(out.contentChanged, 1);
        RoboCheckEqInt(out.layoutAffected, 1);
    }

    LoadFixtureEnd(&f);
}

/* ------------------------------------------------------------------ */
/* Capabilities and errors                                             */
/* ------------------------------------------------------------------ */

static void TestImageLoadCapabilities(void)
{
    HtmlImageLoadCapabilities caps;

    memset(&caps, 0xff, sizeof(caps));
    RoboCheckEqInt(HtmlImageLoadGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.fetchSeam, 1);
    RoboCheckEqInt(caps.intrinsicSize, 1);
    RoboCheckEqInt(caps.declaredSize, 1);
    RoboCheckEqInt(caps.referencePolicy, 1);
    RoboCheckEqInt(caps.layoutInvalidation, 1);
    RoboCheckEqInt(caps.replacement, 1);
    RoboCheckEqInt(caps.removalAbandon, 1);
    /* The stated non-goals, named rather than implied. */
    RoboCheckEqInt(caps.srcset, 0);
    RoboCheckEqInt(caps.responsiveImages, 0);
    RoboCheckEqInt(caps.loadEvents, 0);
    RoboCheckEqInt(caps.asyncLoading, 0);
    RoboCheckEqInt(caps.networkAccess, 0);
    RoboCheckStrEq(caps.version, RoboHtmlVersionString);

    RoboCheckEqInt(HtmlImageLoadGetCapabilities(NULL), RoboInvalidArgument);
}

static void TestImageLoadErrors(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle para;
    HtmlImageLoadOutcome out;
    int wasPending = 0;

    LoadFixtureInit(&f);

    RoboCheckEqInt(
        HtmlElementImageLoad(NULL, &f.resolver, &out, NULL),
        RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageLoad(NULL, &f.resolver, NULL, NULL),
                   RoboInvalidArgument);

    id = LoadDoc();
    LoadBuild(id, "<p>no images here</p>");
    root = LoadRoot(id);
    para = LoadFind(&root, RoboHtmlTagP);
    RoboCheckEqInt(HtmlElementImageLoad(&para, &f.resolver, &out, NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlElementImageSize(&para, &out.usedWidth,
                                        &out.usedHeight, &out.hasUsedSize,
                                        NULL), RoboNotSupported);

    /* A resolvable <img> with an unusable resolver is a caller error. */
    {
        HtmlImageResolver none;

        HtmlImageResolverInit(&none);
        LoadBuild(id, "<img src=\"Assets/Logo.png\">");
        root = LoadRoot(id);
        {
            HtmlNodeHandle img = LoadFind(&root, RoboHtmlTagImg);

            RoboCheckEqInt(
                HtmlElementImageLoad(&img, &none, &out, NULL),
                RoboInvalidArgument);
            RoboCheckEqInt(
                HtmlElementImageLoad(&img, NULL, &out, NULL),
                RoboInvalidArgument);
        }
    }

    /* A destroyed document: stale handles are refused, never followed. */
    {
        HtmlNodeHandle stale;

        LoadBuild(id, "<img src=\"Assets/Logo.png\">");
        root = LoadRoot(id);
        stale = LoadFind(&root, RoboHtmlTagImg);
        LoadDocEnd();
        RoboCheckEqInt(
            HtmlElementImageLoad(&stale, &f.resolver, &out, NULL),
            RoboNotFound);
        RoboCheckEqInt(HtmlElementImageAbandon(&stale, &wasPending, NULL),
                       RoboNotFound);
        RoboCheckEqInt(wasPending, 0);
    }

    LoadFixtureEnd(&f);
}

/* ------------------------------------------------------------------ */
/* Out-of-scope after a replace is still inert                          */
/* ------------------------------------------------------------------ */

static void TestImageLoadDetachedElement(void)
{
    LoadFixture f;
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageLoadOutcome out;
    int wasPending = 0;

    LoadFixtureInit(&f);
    id = LoadDoc();
    LoadBuild(id, "<div id=\"host\"><img src=\"Assets/Logo.png\"></div>");
    root = LoadRoot(id);
    img = LoadFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageLoad(&img, &f.resolver, &out, NULL), RoboOk);
    RoboCheckEqInt(out.loaded, 1);

    /* Detaching the element from the tree must not orphan its resource state:
     * the handle still answers, and the outcome still releases cleanly. */
    {
        HtmlNodeHandle parent;

        RoboCheckEqInt(HtmlNodeParent(&img, &parent, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeRemoveChild(&parent, &img, NULL), RoboOk);
    }
    RoboCheckEqInt(HtmlElementImageState(&img), RoboHtmlImageStateDecoded);
    RoboCheckEqInt(HtmlElementImageAbandon(&img, &wasPending, NULL), RoboOk);
    RoboCheckEqInt(wasPending, 0);
    RoboCheckEqInt(HtmlElementImageState(&img), RoboHtmlImageStatePending);

    LoadDocEnd();
    LoadFixtureEnd(&f);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlImageLoad(void);

void TestRegisterHtmlImageLoad(void)
{
    TestRegister("html.imageload.reference_class",
                 TestImageLoadReferenceClass);
    TestRegister("html.imageload.element_class", TestImageLoadElementClass);
    TestRegister("html.imageload.decoded", TestImageLoadDecoded);
    TestRegister("html.imageload.declared_size", TestImageLoadDeclaredSize);
    TestRegister("html.imageload.one_axis_declared",
                 TestImageLoadOneAxisDeclared);
    TestRegister("html.imageload.no_size_without_pixels",
                 TestImageLoadNoSizeWithoutPixels);
    TestRegister("html.imageload.refused_does_not_fetch",
                 TestImageLoadRefusedDoesNotFetch);
    TestRegister("html.imageload.no_source", TestImageLoadNoSource);
    TestRegister("html.imageload.replace_source",
                 TestImageLoadReplaceSource);
    TestRegister("html.imageload.abandon", TestImageLoadAbandon);
    TestRegister("html.imageload.invalidation",
                 TestImageLoadInvalidation);
    TestRegister("html.imageload.capabilities",
                 TestImageLoadCapabilities);
    TestRegister("html.imageload.errors", TestImageLoadErrors);
    TestRegister("html.imageload.detached_element",
                 TestImageLoadDetachedElement);
}
