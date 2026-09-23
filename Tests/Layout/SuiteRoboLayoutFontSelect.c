/*
 * RoboLayout font-selection suite (Phase 12.17) — layout.fontselect.*
 *
 * The subject is the **ordered walk** that turns a resolved font request into one face: which family
 * answered, whether that was a fallback, and what happens when nothing does. The provider is a stub built
 * on dummy face addresses, because the policy under test is family *order* and provenance rather than any
 * font byte -- the concrete database is exercised by the desktop suite.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboLayout/RoboLayoutFontResolver.h"

#include <string.h>

/* Two distinct non-NULL "faces". Only ever compared and stored, never dereferenced. */
static char fontSelectFaceA;
static char fontSelectFaceB;

#define FONT_SELECT_A ((FontFace *)&fontSelectFaceA)
#define FONT_SELECT_B ((FontFace *)&fontSelectFaceB)

/** A stub provider: answers @p answer for any entry whose name (or generic) matches @p wanted. */
typedef struct FontSelectStub {
    const char *wantedName;
    FontGenericFamily wantedGeneric;
    FontFace *answer;
    size_t calls;
    /** When non-zero, the provider fails instead of answering. */
    Result fail;
} FontSelectStub;

static Result FontSelectStubResolve(void *userdata,
                                    const LayoutFontFamily *family,
                                    FontWeight weight,
                                    FontStretch stretch,
                                    FontStyle style,
                                    FontFace **outFace)
{
    FontSelectStub *stub = (FontSelectStub *)userdata;

    (void)weight;
    (void)stretch;
    (void)style;
    *outFace = NULL;
    stub->calls++;
    if (stub->fail != RoboOk) {
        return stub->fail;
    }
    if (family->kind == LayoutFontFamilyGeneric && stub->wantedName == NULL &&
        family->generic == stub->wantedGeneric) {
        *outFace = stub->answer;
    } else if (family->kind == LayoutFontFamilyNamed && stub->wantedName &&
               CssSliceEqualsAsciiCi(CssSliceFromText(family->name), stub->wantedName)) {
        *outFace = stub->answer;
    }
    return RoboOk;
}

static LayoutFontProvider FontSelectProviderOf(FontSelectStub *stub)
{
    LayoutFontProvider provider;

    LayoutFontProviderInit(&provider);
    provider.userdata = stub;
    provider.resolveFamily = FontSelectStubResolve;
    return provider;
}

static void TestFontSelectProviderBasics(void)
{
    LayoutFontProvider provider;

    LayoutFontProviderInit(&provider);
    RoboCheck(!LayoutFontProviderIsUsable(&provider));
    RoboCheck(!LayoutFontProviderIsUsable(NULL));
    provider.resolveFamily = FontSelectStubResolve;
    RoboCheck(LayoutFontProviderIsUsable(&provider));

    LayoutFontProviderInit(NULL);
}

static void TestFontSelectInit(void)
{
    LayoutFontSelection selection;

    LayoutFontSelectionInit(&selection);
    RoboCheck(!LayoutFontSelectionIsSet(&selection));
    RoboCheckEqInt((int)selection.familyIndex, (int)RoboLayoutFontNoFamily);
    RoboCheckEqInt((int)selection.weight, (int)RoboFontWeightNormal);
    RoboCheckEqInt((int)selection.stretch, (int)RoboFontStretchNormal);
    RoboCheck(!selection.resolved);
    RoboCheck(!selection.fallback);
    LayoutFontSelectionInit(NULL);
}

static void TestFontSelectFirstFamily(void)
{
    LayoutFontRequest request;
    LayoutFontSelection selection;
    FontSelectStub stub;
    LayoutFontProvider provider;

    LayoutFontRequestInit(&request);
    RoboCheckEqInt(LayoutFontFamilyListParse("UI Text, sans-serif", 0u, &request.families),
                   RoboOk);

    memset(&stub, 0, sizeof(stub));
    stub.wantedName = "UI Text";
    stub.answer = FONT_SELECT_A;
    provider = FontSelectProviderOf(&stub);

    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, &selection, NULL), RoboOk);
    RoboCheck(LayoutFontSelectionIsSet(&selection));
    RoboCheck(selection.face == FONT_SELECT_A);
    RoboCheckEqInt((int)selection.familyIndex, 0);
    RoboCheck(!selection.fallback);
    /* The walk stops at the first hit. */
    RoboCheckEqInt((int)stub.calls, 1);

    /* A NULL argument is refused; a NULL provider is an honest unset selection, not a failure. */
    RoboCheckEqInt(LayoutFontRequestSelect(NULL, &provider, &selection, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutFontRequestSelect(&request, NULL, &selection, NULL), RoboOk);
    RoboCheck(!LayoutFontSelectionIsSet(&selection));
}

static void TestFontSelectFallback(void)
{
    LayoutFontRequest request;
    LayoutFontSelection selection;
    FontSelectStub stub;
    LayoutFontProvider provider;

    LayoutFontRequestInit(&request);
    RoboCheckEqInt(LayoutFontFamilyListParse("Missing, Fallback Family", 0u, &request.families),
                   RoboOk);

    memset(&stub, 0, sizeof(stub));
    stub.wantedName = "Fallback Family";
    stub.answer = FONT_SELECT_B;
    provider = FontSelectProviderOf(&stub);

    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, &selection, NULL), RoboOk);
    RoboCheck(LayoutFontSelectionIsSet(&selection));
    RoboCheck(selection.face == FONT_SELECT_B);
    RoboCheckEqInt((int)selection.familyIndex, 1);
    RoboCheck(selection.fallback);
    /* Both entries were tried: the first missed, the second hit. */
    RoboCheckEqInt((int)stub.calls, 2);
}

static void TestFontSelectGeneric(void)
{
    LayoutFontRequest request;
    LayoutFontSelection selection;
    FontSelectStub stub;
    LayoutFontProvider provider;

    LayoutFontRequestInit(&request);
    RoboCheckEqInt(LayoutFontFamilyListParse("sans-serif", 0u, &request.families), RoboOk);
    RoboCheckEqInt((int)request.families.entries[0].kind, (int)LayoutFontFamilyGeneric);

    memset(&stub, 0, sizeof(stub));
    stub.wantedName = NULL;
    stub.wantedGeneric = RoboFontGenericSansSerif;
    stub.answer = FONT_SELECT_A;
    provider = FontSelectProviderOf(&stub);

    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, &selection, NULL), RoboOk);
    RoboCheck(LayoutFontSelectionIsSet(&selection));
    RoboCheck(selection.face == FONT_SELECT_A);
    RoboCheckEqInt((int)selection.familyIndex, 0);
}

static void TestFontSelectNone(void)
{
    LayoutFontRequest request;
    LayoutFontSelection selection;
    FontSelectStub stub;
    LayoutFontProvider provider;

    LayoutFontRequestInit(&request);
    RoboCheckEqInt(LayoutFontFamilyListParse("Nope, Also Nope", 0u, &request.families), RoboOk);

    memset(&stub, 0, sizeof(stub));
    stub.wantedName = "Not Present";
    stub.answer = FONT_SELECT_A;
    provider = FontSelectProviderOf(&stub);

    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, &selection, NULL), RoboOk);
    RoboCheck(!LayoutFontSelectionIsSet(&selection));
    RoboCheckEqInt((int)selection.familyIndex, (int)RoboLayoutFontNoFamily);
    RoboCheckEqInt((int)stub.calls, 2);

    /* A provider failure propagates rather than being read as "nothing matched". */
    stub.fail = RoboOutOfMemory;
    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, &selection, NULL),
                   RoboOutOfMemory);
}

static void TestFontSelectSettings(void)
{
    LayoutFontRequest request;
    LayoutFontSelection selection;
    FontSelectStub stub;
    LayoutFontProvider provider;

    LayoutFontRequestInit(&request);
    request.sizePx = 21.0;
    request.weight = 600;
    request.stretch = RoboFontStretchCondensed;
    request.italic = 1;
    RoboCheckEqInt(LayoutFontFamilyListParse("Inter", 0u, &request.families), RoboOk);

    memset(&stub, 0, sizeof(stub));
    stub.wantedName = "Inter";
    stub.answer = FONT_SELECT_A;
    provider = FontSelectProviderOf(&stub);

    RoboCheckEqInt(LayoutFontRequestSelect(&request, &provider, &selection, NULL), RoboOk);
    /* The requested settings are recorded on the selection, which is what a caller reports. */
    RoboCheckEqInt((int)selection.weight, 600);
    RoboCheckEqInt((int)selection.stretch, (int)RoboFontStretchCondensed);
    RoboCheckEqInt((int)selection.style, (int)RoboFontStyleItalic);
}

static void TestFontSelectEqualAndWrite(void)
{
    LayoutFontSelection a;
    LayoutFontSelection b;
    char buffer[128];
    size_t length = 0u;

    LayoutFontSelectionInit(&a);
    LayoutFontSelectionInit(&b);
    RoboCheck(LayoutFontSelectionEqual(&a, &b));
    RoboCheck(LayoutFontSelectionEqual(NULL, NULL));
    RoboCheck(!LayoutFontSelectionEqual(&a, NULL));

    a.face = FONT_SELECT_A;
    a.resolved = 1;
    a.familyIndex = 2u;
    a.fallback = 1;
    RoboCheck(!LayoutFontSelectionEqual(&a, &b));
    b = a;
    RoboCheck(LayoutFontSelectionEqual(&a, &b));

    RoboCheckEqInt(LayoutFontSelectionWrite(&a, buffer, sizeof(buffer), &length), RoboOk);
    RoboCheck(strstr(buffer, "face=") != NULL);
    RoboCheck(strstr(buffer, "fallback") != NULL);

    LayoutFontSelectionInit(&b);
    RoboCheckEqInt(LayoutFontSelectionWrite(&b, buffer, sizeof(buffer), &length), RoboOk);
    RoboCheck(strstr(buffer, "face=none") != NULL);

    RoboCheckEqInt(LayoutFontSelectionWrite(&a, buffer, 4u, &length), RoboNotSupported);
    RoboCheckEqInt(LayoutFontSelectionWrite(NULL, buffer, sizeof(buffer), &length),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutFontSelectionWrite(&a, NULL, sizeof(buffer), &length),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutFontSelect(void);
void TestRegisterLayoutFontSelect(void)
{
    TestRegister("layout.fontselect.provider", TestFontSelectProviderBasics);
    TestRegister("layout.fontselect.init", TestFontSelectInit);
    TestRegister("layout.fontselect.first_family", TestFontSelectFirstFamily);
    TestRegister("layout.fontselect.fallback", TestFontSelectFallback);
    TestRegister("layout.fontselect.generic", TestFontSelectGeneric);
    TestRegister("layout.fontselect.none", TestFontSelectNone);
    TestRegister("layout.fontselect.settings", TestFontSelectSettings);
    TestRegister("layout.fontselect.equal_write", TestFontSelectEqualAndWrite);
}
