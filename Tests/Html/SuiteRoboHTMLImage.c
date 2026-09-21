/*
 * RoboHTML Phase 8.12 image suite (html.image.*).
 *
 * The decoder suite (gfx.decode.*) proves the pixels are right. This one
 * proves the *DOM side* is: that the parser builds the element the markup
 * asks for, that the attributes read back exactly as written, that a
 * reference travels from an element through the resolver seam to real
 * RoboGraphics pixels, and that every way that journey can fail leaves the
 * document untouched and the failure observable.
 *
 * The PNG bytes used here are produced by the real Goal 6.13 encoder rather
 * than embedded, because that is what makes the pixel assertions meaningful:
 * the test knows the surface it encoded, so it can compare the image that
 * came back out of the DOM against the surface that went in.
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
#include "RoboHTML/RoboHTMLMutation.h"
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

/* Defined with the traversal test below; declared here for the resolver
 * cases that need a fetch which cannot complete yet. */
static Result ImgFetchBusy(void *userdata,
                                    const HtmlImageRequest *request,
                                    const uint8_t **outBytes,
                                    size_t *outLength,
                                    HtmlDiag *diag);

static void ImgFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId ImgDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    ImgFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Parse @p text into @p id through the real tokenizer and tree builder. */
static void ImgBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle ImgRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct imgTagQuery {
    HtmlTag tag;
} ImgTagQuery;

static int ImgMatchTag(const HtmlNodeView *view, void *user)
{
    const ImgTagQuery *q = (const ImgTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static HtmlNodeHandle ImgFind(const HtmlNodeHandle *start,
                                        HtmlTag tag)
{
    ImgTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(
        HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  ImgMatchTag, &q, &out, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/** Build a 4x4 surface with a fixed, non-uniform pattern. */
static GfxSurface *ImgSurface(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    uint32_t x;
    uint32_t y;

    GfxSurfaceDescInit(&desc);
    desc.width = 4u;
    desc.height = 4u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            GfxColorRgba8 c;

            c.r = (uint8_t)(x * 60u + 1u);
            c.g = (uint8_t)(y * 60u + 2u);
            c.b = (uint8_t)((x + y) * 30u + 3u);
            c.a = 255u;
            RoboCheckEqInt(GfxSurfaceSetPixel(surface, x, y, c),
                              RoboOk);
        }
    }
    return surface;
}

/** Encode @p surface to real PNG bytes (the Goal 6.13 encoder). */
static void ImgEncode(GfxSurface *surface,
                       GfxEncodedBuffer *out)
{
    GfxEncodeOptions opts;

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(out, 0, sizeof(*out));
    RoboCheckEqInt(GfxSurfaceEncode(surface, &opts, out), RoboOk);
    RoboCheck(out->size > 0u);
}

/** Compare an attached resource's pixels against the surface encoded. */
static void ImgExpectSamePixels(const HtmlImage *image,
                                   GfxSurface *source)
{
    GfxImage *graphics = HtmlImageGraphics(image);
    GfxSurface *surface;
    uint32_t x;
    uint32_t y;

    RoboCheck(graphics != NULL);
    if (!graphics) {
        return;
    }
    RoboCheckEqInt((int)GfxImageWidth(graphics), 4);
    RoboCheckEqInt((int)GfxImageHeight(graphics), 4);
    RoboCheckEqInt(GfxImageFormat(graphics), RoboGfxFormatRgba8);
    surface = GfxImageSurface(graphics);
    RoboCheck(surface != NULL);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            GfxColorRgba8 want;
            GfxColorRgba8 got;

            RoboCheckEqInt(GfxSurfaceGetPixel(source, x, y, &want),
                              RoboOk);
            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &got),
                              RoboOk);
            RoboCheckEqInt(got.r, want.r);
            RoboCheckEqInt(got.g, want.g);
            RoboCheckEqInt(got.b, want.b);
            RoboCheckEqInt(got.a, want.a);
        }
    }
}

/*
 * One asset table plus the bytes it borrows. The encoded buffer must outlive
 * the table, and the table must outlive the resolver — so they are bundled
 * here and torn down in reverse order, which is the lifetime contract the
 * header documents.
 */
typedef struct imgFixture {
    GfxSurface *surface;
    GfxEncodedBuffer encoded;
    HtmlImageAssets *assets;
} ImgFixture;

static void ImgAssetFixtureBegin(ImgFixture *f)
{
    ImgFixtureBegin();
    f->surface = ImgSurface();
    ImgEncode(f->surface, &f->encoded);
    RoboCheckEqInt(
        HtmlImageAssetsCreate(NULL, 0u, &f->assets), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(f->assets, "Assets/Logo.png",
                                                 f->encoded.data,
                                                 f->encoded.size, NULL),
                      RoboOk);
}

static void ImgAssetFixtureEnd(ImgFixture *f)
{
    HtmlImageAssetsDestroy(f->assets);
    GfxEncodedBufferRelease(&f->encoded);
    GfxSurfaceRelease(f->surface);
}

/* ------------------------------------------------------------------ */
/* Parser + element                                                    */
/* ------------------------------------------------------------------ */

static void TestImageParserElement(void)
{
    HtmlDocumentId id = ImgDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlNodeHandle span;
    size_t children = 0u;

    ImgBuild(id, "<div><img src=\"Assets/Logo.png\" alt=\"Logo\">"
                  "<span>After image</span></div>");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);
    span = ImgFind(&root, RoboHtmlTagSpan);

    /* Stable identity, recognised as a void element by the tag table. */
    RoboCheckEqInt(HtmlElementTag(&img), RoboHtmlTagImg);
    RoboCheck(HtmlTagIsVoid(RoboHtmlTagImg));

    /* An unclosed <img> must not swallow the following sibling. */
    RoboCheckEqInt(HtmlNodeChildCount(&img, &children, NULL),
                      RoboOk);
    RoboCheckEqInt((int)children, 0);
    RoboCheck(HtmlNodeHandleIsWellformed(&span));
    {
        HtmlNodeHandle imgParent;
        HtmlNodeHandle spanParent;

        RoboCheckEqInt(HtmlNodeParent(&img, &imgParent, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeParent(&span, &spanParent, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&imgParent, &spanParent));
    }

    /* Document order: the span follows the image and is its sibling. */
    {
        HtmlNodeHandle next;

        RoboCheckEqInt(HtmlNodeNextSibling(&img, &next, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&next, &span));
    }

    /* Traversal reaches the image as an element. */
    RoboCheckEqInt(
        HtmlNodeFindFirst(&root, RoboHtmlNodeFilterElements,
                                  ImgMatchTag,
                                  &(ImgTagQuery){RoboHtmlTagImg}, &img,
                                  NULL),
        RoboOk);

    /* Malformed closing syntax must not corrupt the tree either. */
    {
        HtmlDocumentId second = ImgDoc();
        HtmlNodeHandle secondRoot;
        HtmlNodeHandle broken;

        ImgBuild(second, "<p><img src=\"a.png\"</p><img></img>");
        secondRoot = ImgRoot(second);
        broken = ImgFind(&secondRoot, RoboHtmlTagImg);
        RoboCheckEqInt(HtmlElementTag(&broken), RoboHtmlTagImg);
        RoboCheckEqInt(HtmlDocumentDestroy(second, NULL), RoboOk);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageAttributes(void)
{
    HtmlDocumentId id = ImgDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageAttributes attrs;
    size_t length = 0u;

    ImgBuild(id, "<img src=\"Assets/Logo.png\" alt=\"Example\" "
                  "width=\"100\" height=\"50\" data-note=\"custom\" "
                  "class=\"hero\">");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(HtmlElementImageAttributes(&img, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.sourcePresent, 1);
    RoboCheckEqInt(attrs.sourceUsable, 1);
    RoboCheckEqInt((int)attrs.sourceLength, 15);
    RoboCheck(attrs.source != NULL);
    RoboCheck(memcmp(attrs.source, "Assets/Logo.png", 15u) == 0);
    RoboCheckEqInt((int)attrs.altLength, 7);
    RoboCheck(memcmp(attrs.alt, "Example", 7u) == 0);
    RoboCheckEqInt(attrs.widthSet, 1);
    RoboCheckEqInt(attrs.widthValid, 1);
    RoboCheckEqInt((int)attrs.width, 100);
    RoboCheckEqInt(attrs.heightSet, 1);
    RoboCheckEqInt(attrs.heightValid, 1);
    RoboCheckEqInt((int)attrs.height, 50);

    /* Arbitrary attributes survive untouched — the image view is a lens over
     * the attribute model, not a filter on it. */
    RoboCheck(HtmlElementHasAttribute(&img, "data-note"));
    RoboCheck(HtmlElementHasAttribute(&img, "class"));
    RoboCheckStrEq(HtmlElementAttributeValue(&img, "class", &length),
                      "hero");
    RoboCheck(HtmlElementHasAttribute(&img, "src"));
    RoboCheck(HtmlElementHasAttribute(&img, "width"));

    /* A non-img element is a different answer, not a failed lookup. */
    {
        HtmlNodeHandle div = ImgFind(&root, RoboHtmlTagHtml);

        RoboCheckEqInt(
            HtmlElementImageAttributes(&div, &attrs, NULL),
            RoboNotSupported);
    }

    /* Missing src: present=false, usable=false — distinct from empty. */
    {
        HtmlDocumentId bare = ImgDoc();
        HtmlNodeHandle bareRoot;
        HtmlNodeHandle bareImg;

        ImgBuild(bare, "<img alt=\"No source\">");
        bareRoot = ImgRoot(bare);
        bareImg = ImgFind(&bareRoot, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageAttributes(&bareImg, &attrs, NULL),
            RoboOk);
        RoboCheckEqInt(attrs.sourcePresent, 0);
        RoboCheckEqInt(attrs.sourceUsable, 0);
        RoboCheck(attrs.source == NULL);
        RoboCheckEqInt((int)attrs.altLength, 9);
        RoboCheckEqInt(
            HtmlElementImageState(&bareImg),
            RoboHtmlImageStateNone);
        RoboCheckEqInt(HtmlDocumentDestroy(bare, NULL), RoboOk);
    }

    /* Empty src is present but not usable: nothing to fetch, still NONE. */
    {
        HtmlDocumentId empty = ImgDoc();
        HtmlNodeHandle emptyRoot;
        HtmlNodeHandle emptyImg;

        ImgBuild(empty, "<img src=\"\">");
        emptyRoot = ImgRoot(empty);
        emptyImg = ImgFind(&emptyRoot, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageAttributes(&emptyImg, &attrs, NULL),
            RoboOk);
        RoboCheckEqInt(attrs.sourcePresent, 1);
        RoboCheckEqInt(attrs.sourceUsable, 0);
        RoboCheckEqInt((int)attrs.sourceLength, 0);
        RoboCheckEqInt(
            HtmlElementImageState(&emptyImg),
            RoboHtmlImageStateNone);
        RoboCheckEqInt(HtmlDocumentDestroy(empty, NULL), RoboOk);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageDimensions(void)
{
    int valid = 0;
    uint32_t value = 0u;

    RoboCheckEqInt(
        HtmlImageParseDimension("128", 3u, &valid, &value), RoboOk);
    RoboCheckEqInt(valid, 1);
    RoboCheckEqInt((int)value, 128);

    RoboCheckEqInt(
        HtmlImageParseDimension(" 64 ", 4u, &valid, &value), RoboOk);
    RoboCheckEqInt(valid, 1);
    RoboCheckEqInt((int)value, 64);

    RoboCheckEqInt(
        HtmlImageParseDimension("0", 1u, &valid, &value), RoboOk);
    RoboCheckEqInt(valid, 1);
    RoboCheckEqInt((int)value, 0);

    /* Absent: valid-as-no-dimension, not invalid. */
    RoboCheckEqInt(HtmlImageParseDimension(NULL, 0u, &valid, &value),
                      RoboOk);
    RoboCheckEqInt(valid, 0);

    /* Everything below is present-and-not-a-dimension. */
    {
        const char *bad[] = {"", "   ", "abc", "50%", "-5", "+5", "12px",
                             "1e3", "0x10", "1000001", "99999999999999999999",
                             "12 34", "\t\n"};
        size_t i;

        for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
            valid = 1;
            value = 7u;
            RoboCheckEqInt(HtmlImageParseDimension(
                                  bad[i], strlen(bad[i]), &valid, &value),
                              RoboOk);
            RoboCheckEqInt(valid, 0);
            RoboCheckEqInt((int)value, 0);
        }
    }

    /* The boundary is inclusive: the maximum is a dimension, one past it is not. */
    RoboCheckEqInt(HtmlImageParseDimension("1000000", 7u, &valid,
                                                      &value),
                      RoboOk);
    RoboCheckEqInt(valid, 1);
    RoboCheckEqInt((int)value, 1000000);
    RoboCheckEqInt(HtmlImageParseDimension("1000001", 7u, &valid,
                                                      &value),
                      RoboOk);
    RoboCheckEqInt(valid, 0);

    /* Malformed dimensions on a real element are reported, not dropped. */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImageAttributes attrs;

        ImgBuild(id, "<img src=\"a.png\" width=\"abc\" height=\"\">");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageAttributes(&img, &attrs, NULL), RoboOk);
        RoboCheckEqInt(attrs.widthSet, 1);
        RoboCheckEqInt(attrs.widthValid, 0);
        RoboCheckEqInt(attrs.heightSet, 1);
        RoboCheckEqInt(attrs.heightValid, 0);
        /* The bad dimension does not disturb the source. */
        RoboCheckEqInt(attrs.sourceUsable, 1);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

static void TestImageAltUnicode(void)
{
    /* byte-exact preservation, no decoding, no normalisation, no shaping */
    static const char *cases[] = {
        "",
        "Logo",
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",             /* Arabic */
        "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C",             /* Persian */
        "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",                     /* Urdu */
        "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80",
        "\xE4\xB8\xAD\xE6\x96\x87",
        "\xF0\x9F\x9A\x80 rocket",
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImageAttributes attrs;
        char markup[256];

        /* Registration order avoids `"` and `<` in the value entirely. */
        markup[0] = '\0';
        strcpy(markup, "<img alt=\"");
        strcat(markup, cases[i]);
        strcat(markup, "\">");
        ImgBuild(id, markup);
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageAttributes(&img, &attrs, NULL), RoboOk);
        RoboCheckEqInt((int)attrs.altLength, (int)strlen(cases[i]));
        if (attrs.altLength > 0u) {
            RoboCheck(attrs.alt != NULL);
            RoboCheck(memcmp(attrs.alt, cases[i], attrs.altLength) == 0);
        }

        /* The same text survives being read as a plain attribute value. */
        {
            size_t length = 0u;
            const char *value =
                HtmlElementAttributeValue(&img, "alt", &length);

            RoboCheckEqInt((int)length, (int)strlen(cases[i]));
            if (length > 0u) {
                RoboCheck(memcmp(value, cases[i], length) == 0);
            }
        }
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* Resource resolution                                                 */
/* ------------------------------------------------------------------ */

static void TestImageResolveDecoded(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImage *attached = NULL;
    HtmlImageResolver resolver;
    HtmlImageState state;
    uint32_t width = 0u;
    uint32_t height = 0u;
    size_t sourceLength = 0u;

    ImgAssetFixtureBegin(&f);
    ImgBuild(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\" width=\"128\" "
                  "height=\"64\">");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);
    resolver = HtmlImageAssetsResolver(f.assets);

    /* Before resolving: the reference is known, the pixels are not. */
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStatePending);
    RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                      RoboOk);
    RoboCheck(attached == NULL);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0);

    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);

    state = HtmlElementImageState(&img);
    RoboCheckEqInt(state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1);

    RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                      RoboOk);
    RoboCheck(attached != NULL);
    if (!attached) {
        ImgAssetFixtureEnd(&f);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        return;
    }
    ImgExpectSamePixels(attached, f.surface);

    /* Intrinsic size is the bitmap's; declared dimensions are separate and
     * do not resize anything in this phase. */
    RoboCheck(HtmlImageIntrinsicSize(attached, &width, &height));
    RoboCheckEqInt((int)width, 4);
    RoboCheckEqInt((int)height, 4);
    RoboCheck(HtmlImageSource(attached, &sourceLength) != NULL);
    RoboCheckEqInt((int)sourceLength, 15);
    RoboCheckEqInt(MakeHtmlImageState(attached),
                      RoboHtmlImageStateDecoded);
    RoboCheckEqInt(HtmlImageFailure(attached), RoboOk);

    /* Declared dimensions still read back as written. */
    {
        HtmlImageAttributes attrs;

        RoboCheckEqInt(
            HtmlElementImageAttributes(&img, &attrs, NULL), RoboOk);
        RoboCheckEqInt((int)attrs.width, 128);
        RoboCheckEqInt((int)attrs.height, 64);
    }

    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageResolveFailures(void)
{
    ImgFixture f;
    HtmlImageResolver resolver;

    ImgAssetFixtureBegin(&f);
    RoboCheckEqInt(HtmlImageAssetsAdd(f.assets, "Broken.png",
                                                 "not an image at all", 19u,
                                                 NULL),
                      RoboOk);
    resolver = HtmlImageAssetsResolver(f.assets);

    /* Unknown reference → FAILED, and the call still succeeds. */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImage *attached = NULL;

        ImgBuild(id, "<img src=\"Assets/Missing.png\">");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageState(&img),
                          RoboHtmlImageStateFailed);
        RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                          RoboOk);
        RoboCheck(attached != NULL);
        if (attached) {
            RoboCheckEqInt(HtmlImageFailure(attached),
                              RoboNotFound);
            RoboCheck(HtmlImageGraphics(attached) == NULL);
            RoboCheck(HtmlImageIntrinsicSize(attached, &(uint32_t){0},
                                                      &(uint32_t){0}) == 0);
        }
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /*
     * Registered bytes that are not a recognisable image at all → UNSUPPORTED.
     * The decoder cannot tell a JPEG from a corrupt PNG, and saying "this
     * runtime does not implement that format" is more honest than claiming
     * the bytes are malformed.
     */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImage *attached = NULL;

        ImgBuild(id, "<div><img src=\"Broken.png\"><span>sibling</span></div>");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageState(&img),
                          RoboHtmlImageStateUnsupported);
        RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                          RoboOk);
        if (attached) {
            RoboCheckEqInt(HtmlImageFailure(attached),
                              RoboNotSupported);
        }
        {
            HtmlNodeHandle sibling = ImgFind(&root, RoboHtmlTagSpan);

            RoboCheck(HtmlNodeHandleIsWellformed(&sibling));
        }
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /*
     * A real PNG truncated inside its IDAT → FAILED with the decoder's own
     * code. This is the case the state exists for: the container is one we
     * implement, and the bytes are wrong.
     */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImage *attached = NULL;
        GfxEncodedBuffer full;

        ImgEncode(f.surface, &full);
        RoboCheckEqInt(HtmlImageAssetsAdd(f.assets, "Truncated.png",
                                                     full.data,
                                                     full.size - 2u, NULL),
                          RoboOk);
        ImgBuild(id, "<img src=\"Truncated.png\">");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageState(&img),
                          RoboHtmlImageStateFailed);
        RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                          RoboOk);
        if (attached) {
            RoboCheckEqInt(HtmlImageFailure(attached), RoboError);
            RoboCheck(HtmlImageGraphics(attached) == NULL);
        }
        GfxEncodedBufferRelease(&full);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* A reference that names an image but is not PNG/BMP bytes at all → the
     * decoder says "not a format we implement", which is UNSUPPORTED. */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImageState state = RoboHtmlImageStateNone;

        RoboCheckEqInt(
            HtmlImageAssetsAdd(f.assets, "Photo.jpg",
                                       "\xFF\xD8\xFF\xE0notreally", 12u, NULL),
            RoboOk);
        ImgBuild(id, "<img src=\"Photo.jpg\">");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageState(&img),
                          RoboHtmlImageStateUnsupported);
        RoboCheckEqInt(HtmlImageBytesState("\xFF\xD8\xFF\xE0xxxx",
                                                      8u, &state),
                          RoboOk);
        RoboCheckEqInt(state, RoboHtmlImageStateUnsupported);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    ImgAssetFixtureEnd(&f);
}

static void TestImageResolveSchemes(void)
{
    ImgFixture f;
    HtmlImageResolver resolver;
    static const char *refs[] = {
        "https://example.com/Logo.png",
        "http://example.com/Logo.png",
        "file:///etc/passwd",
        "data:image/png;base64,AAAA",
        "//example.com/Logo.png",
    };
    size_t i;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);

    for (i = 0u; i < sizeof(refs) / sizeof(refs[0]); i++) {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        char markup[128];

        strcpy(markup, "<img src=\"");
        strcat(markup, refs[i]);
        strcat(markup, "\">");
        ImgBuild(id, markup);
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        /*
         * A scheme is refused *by policy* — UNSUPPORTED — not reported as
         * "not found". The distinction is the whole point: it says the
         * runtime will not fetch that, rather than blaming the document.
         * "//example.com/x" has no scheme and is simply not a registered
         * asset, so it is FAILED: it parsed as a relative reference.
         */
        if (i + 1u < sizeof(refs) / sizeof(refs[0])) {
            RoboCheckEqInt(HtmlElementImageState(&img),
                              RoboHtmlImageStateUnsupported);
        } else {
            RoboCheckEqInt(HtmlElementImageState(&img),
                              RoboHtmlImageStateFailed);
        }
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* Nothing was fetched, so the document never grew an attachment for the
     * refusals — but it did for the relative one. That asymmetry is the
     * evidence that the resolver ran rather than being skipped. */
    ImgAssetFixtureEnd(&f);
}

static void TestImageResolveWithoutSource(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageResolver resolver;
    HtmlImageAttributes attrs;
    size_t length = 0u;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);
    ImgBuild(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\">");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStateDecoded);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1);

    /*
     * Remove `src`. The already-resolved resource stays attached until
     * something resolves or detaches it — removing an attribute is not a
     * signal to the resource layer, and pretending otherwise would mean the
     * attribute model reached into the image model.
     */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&img, "src", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStateDecoded);
    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStateNone);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0);

    /* Put it back: the element becomes resolvable again, which is what makes
     * "restore src" work without any reload machinery. */
    RoboCheckEqInt(
        HtmlElementSetAttribute(&img, "src", "Assets/Logo.png", NULL),
        RoboOk);
    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStateDecoded);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1);

    /* Setting src to empty is a third case: present, unusable, NONE. */
    RoboCheckEqInt(HtmlElementSetAttribute(&img, "src", "", NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementImageAttributes(&img, &attrs, NULL), RoboOk);
    RoboCheckEqInt(attrs.sourcePresent, 1);
    RoboCheckEqInt(attrs.sourceUsable, 0);
    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStateNone);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0);

    RoboCheckStrEq(HtmlElementAttributeValue(&img, "alt", &length),
                      "Logo");
    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageReresolveReplaces(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageResolver resolver;
    HtmlImage *attached = NULL;

    ImgAssetFixtureBegin(&f);
    /* A second, differently sized image, so "which one is attached" is
     * answerable from the pixels rather than from a count. */
    {
        GfxSurfaceDesc desc;
        GfxSurface *other = NULL;
        GfxEncodedBuffer encoded;
        uint32_t i;

        GfxSurfaceDescInit(&desc);
        desc.width = 2u;
        desc.height = 6u;
        desc.format = RoboGfxFormatRgba8;
        desc.access = RoboGfxSurfaceAccessReadWrite;
        RoboCheckEqInt(GfxSurfaceCreate(&desc, &other), RoboOk);
        for (i = 0u; i < 12u; i++) {
            GfxColorRgba8 c;

            c.r = 200u;
            c.g = 10u;
            c.b = 20u;
            c.a = 255u;
            RoboCheckEqInt(
                GfxSurfaceSetPixel(other, i % 2u, i / 2u, c), RoboOk);
        }
        ImgEncode(other, &encoded);
        RoboCheckEqInt(HtmlImageAssetsAdd(
                              f.assets, "Assets/Wide.png", encoded.data,
                              encoded.size, NULL),
                          RoboOk);

        resolver = HtmlImageAssetsResolver(f.assets);
        ImgBuild(id, "<img src=\"Assets/Logo.png\">");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);

        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1);

        /* Change src, resolve again: one resource, not two. */
        RoboCheckEqInt(
            HtmlElementSetAttribute(&img, "src", "Assets/Wide.png",
                                            NULL),
            RoboOk);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1);
        RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                          RoboOk);
        RoboCheck(attached != NULL);
        if (attached) {
            uint32_t w = 0u;
            uint32_t h = 0u;
            size_t length = 0u;

            RoboCheck(HtmlImageIntrinsicSize(attached, &w, &h));
            RoboCheckEqInt((int)w, 2);
            RoboCheckEqInt((int)h, 6);
            RoboCheck(HtmlImageSource(attached, &length) != NULL);
            RoboCheckEqInt((int)length, 15);
            RoboCheck(memcmp(HtmlImageSource(attached, NULL),
                              "Assets/Wide.png", 15u) == 0);
        }

        /* Resolving the same reference repeatedly is stable, not cumulative. */
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1);

        GfxEncodedBufferRelease(&encoded);
        GfxSurfaceRelease(other);
    }
    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageDetach(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImageResolver resolver;
    HtmlImage *attached = NULL;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);
    ImgBuild(id, "<img src=\"Assets/Logo.png\">");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);

    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1);

    /* A retained resource outlives the element's claim on it — this is the
     * one case where the reference count matters. */
    RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                      RoboOk);
    RoboCheck(attached != NULL);
    HtmlImageRetain(attached);

    RoboCheckEqInt(HtmlElementImageDetach(&img, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0);
    RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                      RoboOk);
    RoboCheck(attached == NULL);
    /* Detaching is not deleting the element's attributes. */
    RoboCheck(HtmlElementHasAttribute(&img, "src"));
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStatePending);

    HtmlImageRelease(attached);
    /* Idempotent. */
    RoboCheckEqInt(HtmlElementImageDetach(&img, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageDetach(&img, NULL), RoboOk);

    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageArgumentSafety(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlNodeHandle div;
    HtmlImage *image = NULL;
    HtmlImageState state = RoboHtmlImageStateNone;
    HtmlImageResolver resolver;
    HtmlImageResolver unusable;
    HtmlImageAssets *empty = NULL;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);
    HtmlImageResolverInit(&unusable);

    ImgBuild(id, "<div><img src=\"Assets/Logo.png\"></div>");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);
    div = ImgFind(&root, RoboHtmlTagDiv);

    /* NULL arguments are programming errors, and say so. */
    RoboCheckEqInt(HtmlElementImageAttributes(NULL, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageGet(NULL, &image, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageGet(&img, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageResolve(NULL, &resolver, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageResolve(&img, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementImageResolve(&img, &unusable, NULL),
                      RoboInvalidArgument);
    RoboCheck(!HtmlImageResolverIsUsable(&unusable));
    RoboCheck(HtmlImageResolverIsUsable(&resolver));
    RoboCheckEqInt(HtmlImageBytesState(NULL, 8u, &state),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlImageBytesState("abc", 3u, NULL),
                      RoboInvalidArgument);
    HtmlImageResolverInit(NULL); /* must not crash */

    /* A non-img element is NOT_SUPPORTED; a stale handle is NOT_FOUND. */
    RoboCheckEqInt(HtmlElementImageResolve(&div, &resolver, NULL),
                      RoboNotSupported);
    RoboCheckEqInt(HtmlElementImageGet(&div, &image, NULL),
                      RoboNotSupported);
    RoboCheckEqInt(HtmlElementImageAttributes(&div, NULL, NULL),
                      RoboInvalidArgument);
    {
        HtmlNodeHandle stale = img;

        stale.documentId = (HtmlDocumentId)9999u;
        RoboCheckEqInt(HtmlElementImageGet(&stale, &image, NULL),
                          RoboNotFound);
        RoboCheckEqInt(
            HtmlElementImageResolve(&stale, &resolver, NULL),
            RoboNotFound);
        RoboCheckEqInt(HtmlElementImageState(&stale),
                          RoboHtmlImageStateNone);
    }

    /*
     * A resolver that reports BUSY leaves the element PENDING: work that has
     * not finished is not a failure, and must not be recorded as one.
     */
    {
        HtmlImageResolver busyResolver;

        busyResolver.fetch = ImgFetchBusy;
        busyResolver.userdata = NULL;
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &busyResolver, NULL),
            RoboBusy);
        RoboCheckEqInt(HtmlElementImageState(&img),
                          RoboHtmlImageStatePending);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 0);
    }

    /* Empty asset tables are legal and simply never match. */
    RoboCheckEqInt(HtmlImageAssetsCreate(NULL, 0u, &empty), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsCount(empty), 0);
    RoboCheckEqInt(HtmlImageAssetsAdd(empty, "", NULL, 0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlImageAssetsAdd(NULL, "x", NULL, 0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlImageAssetsAdd(empty, "x", NULL, 4u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlImageAssetsAdd(empty, "x", "abcd", 4u, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(empty, "x", "abcd", 4u, NULL),
                      RoboAlreadyExists);
    {
        size_t length = 0u;

        RoboCheckEqInt(HtmlImageAssetsCount(empty), 1);
        RoboCheck(HtmlImageAssetsLookup(empty, "x", &length) != NULL);
        RoboCheckEqInt((int)length, 4);
        RoboCheck(HtmlImageAssetsLookup(empty, "y", &length) == NULL);
        RoboCheckEqInt((int)length, 0);
        RoboCheck(HtmlImageAssetsLookup(NULL, "x", &length) == NULL);
    }
    HtmlImageAssetsDestroy(empty);
    HtmlImageAssetsDestroy(NULL); /* must not crash */

    /* Null/absent image handles never crash the accessors. */
    RoboCheckEqInt(MakeHtmlImageState(NULL), RoboHtmlImageStateNone);
    RoboCheck(HtmlImageGraphics(NULL) == NULL);
    RoboCheck(HtmlImageSource(NULL, NULL) == NULL);
    RoboCheck(!HtmlImageIntrinsicSize(NULL, &(uint32_t){0},
                                               &(uint32_t){0}));
    RoboCheckEqInt(HtmlImageFailure(NULL), RoboInvalidArgument);
    HtmlImageRelease(NULL);
    HtmlImageRetain(NULL);
    HtmlImageDestroy(NULL);
    HtmlImageDescribe(NULL, NULL, 0u);

    /* 16 KiB of garbage registered under a name is still just FAILED. */
    RoboCheckEqInt(
        HtmlImageBytesState("\x89PNG\r\n\x1a\nshort", 13u, &state),
        RoboOk);
    RoboCheckEqInt(state, RoboHtmlImageStateFailed);

    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Ownership, lifecycle, traversal, mutation                           */
/* ------------------------------------------------------------------ */

typedef struct imgResolveCtx {
    const HtmlImageResolver *resolver;
    size_t resolved;
} ImgResolveCtx;

/* Set by the visitor so the assertion below can read it back out. */
static size_t imgLastVisitedCount = 0u;

static Result ImgResolveVisitor(const HtmlNodeView *view,
                                         size_t depth, void *user)
{
    ImgResolveCtx *ctx = (ImgResolveCtx *)user;

    (void)depth;
    if (!HtmlElementIsTag(&view->handle, RoboHtmlTagImg)) {
        return RoboOk;
    }
    if (HtmlElementImageResolve(&view->handle, ctx->resolver, NULL) !=
        RoboOk) {
        return RoboError;
    }
    ctx->resolved++;
    imgLastVisitedCount = ctx->resolved;
    return RoboOk;
}

/** A fetch function that reports work in progress, for the BUSY case. */
static Result ImgFetchBusy(void *userdata,
                                    const HtmlImageRequest *request,
                                    const uint8_t **outBytes,
                                    size_t *outLength,
                                    HtmlDiag *diag)
{
    (void)userdata;
    (void)request;
    (void)diag;
    *outBytes = NULL;
    *outLength = 0u;
    return RoboBusy;
}

static void TestImageDocumentTeardown(void)
{
    ImgFixture f;
    HtmlImageResolver resolver;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);

    /* Several images in one document: the table is per element, not global. */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        ImgTagQuery q;
        HtmlNodeHandle found;
        size_t visited = 0u;

        ImgBuild(id, "<div><img src=\"Assets/Logo.png\">"
                      "<p><img src=\"Assets/Logo.png\"></p>"
                      "<img src=\"Assets/Missing.png\"></div>");
        root = ImgRoot(id);

        /* Resolve every img by walking the tree we already have. The walk
         * is the document-order foundation from 8.8, so this also proves the
         * image layer composes with traversal rather than beside it. */
        q.tag = RoboHtmlTagImg;
        RoboCheckEqInt(
            HtmlNodeWalkDescendants(
                &root, RoboHtmlNodeFilterElements, ImgResolveVisitor,
                &(ImgResolveCtx){&resolver, 0u}, NULL),
            RoboOk);
        RoboCheckEqInt((int)imgLastVisitedCount, 3);
        RoboCheckEqInt((int)HtmlDocumentImageCount(id), 3);
        (void)found;
        (void)visited;

        /* A parsed document is COMPLETE, and images resolve there — the
         * normal case, since that is the state a loaded page is in. */
        {
            HtmlDocumentLifecycle life =
                RoboHtmlDocumentLifecycleUninitialized;

            RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life),
                              RoboOk);
            RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);
        }

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 0);
    }

    /* A retained resource survives its document, because ownership followed
     * the reference count and not the document. */
    {
        HtmlDocumentId id = ImgDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle img;
        HtmlImage *attached = NULL;
        uint32_t width = 0u;
        uint32_t height = 0u;

        ImgBuild(id, "<img src=\"Assets/Logo.png\">");
        root = ImgRoot(id);
        img = ImgFind(&root, RoboHtmlTagImg);
        RoboCheckEqInt(
            HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                          RoboOk);
        RoboCheck(attached != NULL);
        HtmlImageRetain(attached);

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 0);
        RoboCheckEqInt(MakeHtmlImageState(attached),
                          RoboHtmlImageStateDecoded);
        RoboCheck(HtmlImageIntrinsicSize(attached, &width, &height));
        RoboCheckEqInt((int)width, 4);
        RoboCheckEqInt((int)height, 4);
        ImgExpectSamePixels(attached, f.surface);
        HtmlImageRelease(attached);

        /* After the document is gone the element no longer resolves at all. */
        RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlElementImageState(&img),
                          RoboHtmlImageStateNone);
    }

    ImgAssetFixtureEnd(&f);
}

static void TestImageMutationAndTraversal(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle container;
    HtmlNodeHandle img;
    HtmlNodeHandle span;
    HtmlImageResolver resolver;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);
    ImgBuild(id, "<div><span>before</span><img src=\"Assets/Logo.png\">"
                  "<span>after</span></div>");
    root = ImgRoot(id);
    container = ImgFind(&root, RoboHtmlTagDiv);
    img = ImgFind(&root, RoboHtmlTagImg);
    span = ImgFind(&root, RoboHtmlTagSpan);

    /* Sibling relationships around a void element are ordinary ones. */
    {
        HtmlNodeHandle next;
        HtmlNodeHandle prev;
        size_t depth = 0u;

        RoboCheckEqInt(HtmlNodeNextSibling(&img, &next, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementTag(&next), RoboHtmlTagSpan);
        RoboCheckEqInt(HtmlNodePrevSibling(&img, &prev, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementTag(&prev), RoboHtmlTagSpan);
        /* document(0) → html(1) → body(2) → div(3) → img(4) */
        RoboCheckEqInt(HtmlNodeDepth(&img, &depth, NULL), RoboOk);
        RoboCheckEqInt((int)depth, 4);
    }

    /* Traversal reaches it in document order between its two siblings. */
    {
        ImgTagQuery q;
        HtmlNodeHandle first;

        q.tag = RoboHtmlTagImg;
        RoboCheckEqInt(
            HtmlNodeFindFirst(&container, RoboHtmlNodeFilterElements,
                                      ImgMatchTag, &q, &first, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&first, &img));
    }

    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageState(&img),
                      RoboHtmlImageStateDecoded);

    /* Move the image: the resource travels with the node's identity, so an
     * element that is moved does not silently lose its picture. */
    {
        HtmlNodeHandle detached = img;
        HtmlNodeHandle parent;

        RoboCheckEqInt(HtmlNodeRemoveChild(&container, &img, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1);
        RoboCheckEqInt(HtmlElementImageState(&detached),
                          RoboHtmlImageStateDecoded);
        RoboCheckEqInt(HtmlNodeAppendChild(&container, &detached,
                                                      NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeParent(&detached, &parent, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&parent, &container));
        RoboCheckEqInt(HtmlElementImageState(&detached),
                          RoboHtmlImageStateDecoded);
    }

    /* Replacing an image with a span leaves one resource table entry for the
     * image (which still exists as a detached node) and none for the span. */
    {
        HtmlNodeHandle replacement;

        RoboCheckEqInt(HtmlNodeRemoveChild(&container, &img, NULL),
                          RoboOk);
        /* The span is still a child, so it must be detached before it can be
         * re-placed — the one-parent rule from 8.9. */
        RoboCheckEqInt(HtmlNodeRemoveChild(&container, &span, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&container, &span, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1);
        replacement = ImgFind(&container, RoboHtmlTagSpan);
        RoboCheck(HtmlNodeHandleIsWellformed(&replacement));
        RoboCheckEqInt(HtmlElementImageState(&replacement),
                          RoboHtmlImageStateNone);
    }

    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestImageDebugDescribe(void)
{
    HtmlDocumentId id = ImgDoc();
    ImgFixture f;
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    HtmlImage *attached = NULL;
    HtmlImageResolver resolver;
    char line[160];
    size_t written;

    ImgAssetFixtureBegin(&f);
    resolver = HtmlImageAssetsResolver(f.assets);
    ImgBuild(id, "<img src=\"Assets/Logo.png\" alt=\"Logo\">");
    root = ImgRoot(id);
    img = ImgFind(&root, RoboHtmlTagImg);

    /* Before resolving there is no resource to describe, so an
     * element-derived description is only available after one exists. */
    RoboCheckEqInt(HtmlImageStateIsKnown(
                          RoboHtmlImageStateDecoded),
                      1);
    RoboCheckEqInt(HtmlImageStateIsKnown((HtmlImageState)99),
                      0);
    RoboCheckStrEq(HtmlImageStateName(RoboHtmlImageStateNone),
                      "none");
    RoboCheckStrEq(HtmlImageStateName(RoboHtmlImageStatePending),
                      "pending");
    RoboCheckStrEq(HtmlImageStateName(RoboHtmlImageStateDecoded),
                      "decoded");
    RoboCheckStrEq(HtmlImageStateName(RoboHtmlImageStateFailed),
                      "failed");
    RoboCheckStrEq(
        HtmlImageStateName(RoboHtmlImageStateUnsupported),
        "unsupported");
    RoboCheckStrEq(HtmlImageStateName((HtmlImageState)99),
                      "unknown");

    RoboCheckEqInt(
        HtmlElementImageResolve(&img, &resolver, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementImageGet(&img, &attached, NULL),
                      RoboOk);
    RoboCheck(attached != NULL);
    if (attached) {
        memset(line, 0, sizeof(line));
        written = HtmlImageDescribe(attached, line, sizeof(line));
        RoboCheck(written > 0u);
        RoboCheck(strstr(line, "state=decoded") != NULL);
        RoboCheck(strstr(line, "intrinsic=4x4") != NULL);
        RoboCheck(strstr(line, "Assets/Logo.png") != NULL);

        /* Truncation terminates and reports the untruncated length. */
        {
            char small[8];
            size_t full = HtmlImageDescribe(attached, small,
                                                   sizeof(small));

            RoboCheckEqInt((int)full, (int)written);
            RoboCheckEqInt((int)strlen(small), 7);
        }
        RoboCheckEqInt((int)HtmlImageDescribe(attached, NULL, 0u),
                          (int)written);
    }

    /* A failure resource describes its reason instead of a size. */
    {
        HtmlImage *failed = NULL;

        RoboCheckEqInt(HtmlImageCreateDecoded(
                              NULL, 0u, "Assets/Missing.png", &failed, NULL),
                          RoboOk);
        RoboCheck(failed != NULL);
        if (failed) {
            memset(line, 0, sizeof(line));
            HtmlImageDescribe(failed, line, sizeof(line));
            RoboCheck(strstr(line, "state=failed") != NULL);
            RoboCheck(strstr(line, "reason=") != NULL);
            RoboCheckEqInt(MakeHtmlImageState(failed),
                              RoboHtmlImageStateFailed);
            HtmlImageRelease(failed);
        }
    }

    ImgAssetFixtureEnd(&f);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/*
 * A consumer discovers a model through the capability query, like every other
 * model in this runtime — so it is asserted here rather than assumed, and
 * asserted *narrowly*: `images` means an `<img>` holds real decoded pixels
 * reachable through a resolver. It is not a claim of network access, and it is
 * not a claim that images can be sized, styled or laid out.
 */
static void TestImageCapabilityReported(void)
{
    HtmlDomCapabilities caps;

    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.images, 1);
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.events, 0);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlImage(void)
{
    TestRegister("html.image.parser_element", TestImageParserElement);
    TestRegister("html.image.attributes", TestImageAttributes);
    TestRegister("html.image.dimensions", TestImageDimensions);
    TestRegister("html.image.alt_unicode", TestImageAltUnicode);
    TestRegister("html.image.resolve_decoded", TestImageResolveDecoded);
    TestRegister("html.image.resolve_failures",
                       TestImageResolveFailures);
    TestRegister("html.image.resolve_schemes", TestImageResolveSchemes);
    TestRegister("html.image.resolve_without_source",
                       TestImageResolveWithoutSource);
    TestRegister("html.image.reresolve_replaces",
                       TestImageReresolveReplaces);
    TestRegister("html.image.detach", TestImageDetach);
    TestRegister("html.image.argument_safety",
                       TestImageArgumentSafety);
    TestRegister("html.image.document_teardown",
                       TestImageDocumentTeardown);
    TestRegister("html.image.mutation_and_traversal",
                       TestImageMutationAndTraversal);
    TestRegister("html.image.debug_describe", TestImageDebugDescribe);
    TestRegister("html.image.capability_reported",
                       TestImageCapabilityReported);
}
