/*
 * Unified Roboticela Runtime test executable (Goal 1.5).
 */

#include "RoboBase/RoboDebug.h"
#include "RoboDesktop/RoboDesktopHost.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"

void TestRegisterBase(void);
void TestRegisterLog(void);
void TestRegisterError(void);
void TestRegisterDebug(void);
void TestRegisterMem(void);
void TestRegisterMemDebug(void);
void TestRegisterMemSystem(void);
void TestRegisterArena(void);
void TestRegisterPool(void);
void TestRegisterArray(void);
void TestRegisterString(void);
void TestRegisterUnicode(void);
void TestRegisterUnicodeProps(void);
void TestRegisterUnicodeNorm(void);
void TestRegisterUnicodeGrapheme(void);
void TestRegisterUnicodeCase(void);
void TestRegisterUnicodeWord(void);
void TestRegisterUnicodeSentence(void);
void TestRegisterUnicodeLine(void);
void TestRegisterUnicodeBidi(void);
void TestRegisterUnicodeScript(void);
void TestRegisterUnicodeEmoji(void);
void TestRegisterUnicodeInfra(void);
void TestRegisterI18n(void);
void TestRegisterI18nCldr(void);
void TestRegisterI18nNumber(void);
void TestRegisterI18nPlural(void);
void TestRegisterI18nDatetime(void);
void TestRegisterI18nDatetimeFormat(void);
void TestRegisterI18nInterval(void);
void TestRegisterI18nRelative(void);
void TestRegisterI18nDatetimeArith(void);
void TestRegisterI18nCollation(void);
void TestRegisterI18nDisplayNames(void);
void TestRegisterI18nMessage(void);
void TestRegisterI18nMessageFormat(void);
void TestRegisterI18nMessageValidate(void);
void TestRegisterI18nTimezone(void);
void TestRegisterI18nTimezoneDisplay(void);
void TestRegisterI18nLocaleService(void);
void TestRegisterI18nLocaleNegotiate(void);
void TestRegisterI18nOrdinal(void);
void TestRegisterI18nUnit(void);
void TestRegisterI18nMeasure(void);
void TestRegisterI18nCurrency(void);
void TestRegisterI18nParse(void);
void TestRegisterI18nDatetimeParse(void);
void TestRegisterI18nSearch(void);
void TestRegisterI18nTranslit(void);
void TestRegisterI18nConformance(void);
void TestRegisterI18nPhase4(void);
void TestRegisterFont(void);
void TestRegisterFontSfnt(void);
void TestRegisterFontTt(void);
void TestRegisterFontOt(void);
void TestRegisterFontCff(void);
void TestRegisterFontCmap(void);
void TestRegisterFontMetrics(void);
void TestRegisterFontOutline(void);
void TestRegisterFontName(void);
void TestRegisterFontClassification(void);
void TestRegisterFontSelection(void);
void TestRegisterFontDiscovery(void);
void TestRegisterFontLoader(void);
void TestRegisterFontCoverage(void);
void TestRegisterFontFallback(void);
void TestRegisterFontGeneric(void);
void TestRegisterFontEmoji(void);
void TestRegisterFontVariation(void);
void TestRegisterFontColor(void);
void TestRegisterFontValidate(void);
void TestRegisterFontConformance(void);
void TestRegisterFontPhase5(void);
void TestRegisterFontSystemPack(void);
/* Phase 12.3 — RoboTypography: the semantic role vocabulary, the one scale, the resolver that binds a
 * role to a face the system typeface pack installed, and the generated document defaults. The suites
 * own no module-wide state, so their order among the others does not matter; they are registered
 * beside the font suites because the pack is what they resolve against. */
void TestRegisterTypographyRoles(void);
void TestRegisterTypographyResolver(void);
void TestRegisterTypographyDocument(void);
/* Phase 12.4 — RoboText: the text model (buffer, units, ranges), the runs, the glyph record and glyph
 * runs, and the pipeline that turns a string into a measurement and the glyphs a painter walks. The
 * suites are registered beside the font and typography ones because the face they measure against comes
 * from the system typeface pack. */
void TestRegisterTextBuffer(void);
void TestRegisterTextRuns(void);
void TestRegisterTextGlyphs(void);
void TestRegisterTextShape(void);
void TestRegisterTextConformance(void);
void TestRegisterTextGallery(void);
/* Phase 12.5 — the Unicode/font integration: the module's one decoder and property interface, the shared
 * script resolver, the coverage-gated font selector with its fallback chain, and the analysis runs that
 * combine script, direction and font into a shape request. */
void TestRegisterTextUnicode(void);
void TestRegisterTextFontSelection(void);
void TestRegisterTextAnalysis(void);
/* Phase 12.6 — the script stage: `Script_Extensions` beside `Script`, the two-sided neutrality rule, and
 * the script-run diagnostics. The algorithm lives in the same resolver the analysis consumes. */
void TestRegisterTextScript(void);
void TestRegisterTextGsub(void);
void TestRegisterTextGpos(void);
/* Phase 12.12 — the ligature layer: the five-tag policy, the stylesheet's vocabulary over it, and what the
 * font's substitutions actually produced. */
void TestRegisterTextLigature(void);
void TestRegisterTextKerning(void);
/* Phase 12.14 — glyph runs: the finalized run (clusters, flags, metrics, totals), the run list, its keys,
 * its two orders and the validation of the set. */
void TestRegisterTextGlyphRuns(void);
/* Phase 12.15 — measurement: the engine over a finalized run list (advances from the runs' own totals, the
 * line-metrics aggregation over a mixed-font line, the logical box, the ink box the glyphs' outlines give),
 * the tolerance, the validation and the diagnostic block. */
void TestRegisterTextMeasurement(void);
/* Phase 12.18 — Latin and European shaping conformance: the deterministic test fonts and the suites that
 * assert glyph sequences, clusters, advances, GPOS placement and run structure over them. */
void TestRegisterTextShapingLatin(void);
void TestRegisterTextShapingScripts(void);
void TestRegisterTextShapingFeatures(void);
void TestRegisterTextShapingKerning(void);
void TestRegisterTextShapingMarks(void);
void TestRegisterTextShapingFallback(void);
void TestRegisterTextShapingIntegration(void);
/* Phase 12.19 — complex scripts: the Arabic strategy (joining, the four contextual forms by position, the
 * lam/alef ligature, marks and cursive attachment) and the Indic strategy (syllables, the base, the masked
 * substitution stages, the reph and pre-base matra reordering, conjuncts), over one built face. */
void TestRegisterTextArabic(void);
void TestRegisterTextIndic(void);
/* Phase 12.20 -- vertical writing: the writing mode and its axes, the orientation tables (including the
 * rotated one that beats the script), the orientation cut, `vert` through the ordinary GSUB engine, the
 * `vmtx` advances and the synthesised path, the measurement's `advanceY` and bidi in a vertical column. */
void TestRegisterTextVertical(void);
/* Phase 12.21 -- selection mapping: the logical ↔ visual bridge (positions and affinity, the placement a
 * caller supplies, cluster-snapped hit testing, carets in both writing modes, selection rectangles across
 * lines, bidi boundaries and fallback fonts, and the round trip in both directions). */
void TestRegisterTextSelection(void);
/* Phase 12.22 -- text editing: an editor over a logical buffer whose every operation is one replacement of
 * a source range, the character and cluster boundaries a delete uses, composition, transactions, the
 * invalidation record and the transformation of positions and ranges across an edit. */
void TestRegisterTextEditing(void);
/* Phase 12.24 -- the API freeze (`Runtime/Include/RoboText/RoboText.h`'s completeness, the defaults, the
 * refusals and the borrowing rules). */
void TestRegisterTextApi(void);
void TestRegisterTextShapingCorpus(void);
void TestRegisterTextDirection(void);
/* Phase 12.8 — the bidi stage: the paragraph base and its four sources, the levels, the §5.2 retention of
 * X9-removed scalars, the level runs, L2's visual order, isolates, L4's mirroring and the dump. */
void TestRegisterTextBidi(void);
/* Phase 15 -- RoboCompositor: the core and the dump, the tree/composite/present half, and the
 * pool/tiles/scheduler/animations/frames half. */
void TestRegisterCompositor(void);
void TestRegisterCompositorVisual(void);
void TestRegisterCompositorScheduling(void);
void TestRegisterGraphics(void);
void TestRegisterGraphicsPixelFormat(void);
void TestRegisterGraphicsFramebuffer(void);
void TestRegisterGraphicsSurface(void);
void TestRegisterGraphicsBitmap(void);
void TestRegisterGraphicsGeometry(void);
void TestRegisterGraphicsLine(void);
void TestRegisterGraphicsPath(void);
void TestRegisterGraphicsTransform(void);
void TestRegisterGraphicsComposite(void);
void TestRegisterGraphicsRaster(void);
void TestRegisterGraphicsText(void);
void TestRegisterGraphicsGlyph(void);
void TestRegisterGraphicsTypography(void);
void TestRegisterGraphicsEncode(void);
void TestRegisterHtmlImage(void);
void TestRegisterHtmlAnchor(void);
void TestRegisterHtmlForm(void);
void TestRegisterHtmlDom(void);
void TestRegisterHtmlDomQuery(void);
void TestRegisterHtmlTable(void);
void TestRegisterHtmlFormTree(void);
void TestRegisterHtmlInput(void);
void TestRegisterHtmlNavigation(void);
void TestRegisterHtmlImageLoad(void);
void TestRegisterHtmlTemplate(void);
void TestRegisterHtmlShadow(void);
void TestRegisterHtmlCustomElements(void);
void TestRegisterHtmlSecurity(void);
void TestRegisterHtmlDomConformance(void);
void TestRegisterHtmlDomIntegration(void);
void TestRegisterGraphicsDecode(void);
void TestRegisterGraphicsScreenshot(void);
void TestRegisterGraphicsPhase6(void);
void TestRegisterWindow(void);
void TestRegisterWindowNative(void);
void TestRegisterWindowPresent(void);
void TestRegisterWindowGraphics(void);
void TestRegisterWindowHost(void);
void TestRegisterWindowSize(void);
void TestRegisterWindowLifecycle(void);
void TestRegisterWindowEventLoop(void);
void TestRegisterWindowKeyboard(void);
void TestRegisterWindowMouse(void);
void TestRegisterWindowQemuDisplay(void);
void TestRegisterWindowPhase7(void);
void TestRegisterDisplay(void);
void TestRegisterDisplayScale(void);
void TestRegisterDisplayHost(void);
void TestRegisterHtml(void);
void TestRegisterHtmlDocument(void);
void TestRegisterHtmlParser(void);
void TestRegisterHtmlHooks(void);
void TestRegisterHtmlPhase8(void);
void TestRegisterHtmlToken(void);
void TestRegisterHtmlCharref(void);
void TestRegisterHtmlTokenizerStates(void);
void TestRegisterHtmlTreeBuilder(void);
void TestRegisterHtmlDocumentModel(void);
void TestRegisterHtmlElement(void);
void TestRegisterHtmlAttribute(void);
void TestRegisterHtmlText(void);
void TestRegisterHtmlTraversal(void);
void TestRegisterHtmlMutation(void);
void TestRegisterHtmlLifecycle(void);
void TestRegisterHtmlTag(void);
void TestRegisterHtmlElements(void);
void TestRegisterCss(void);
void TestRegisterCssSource(void);
void TestRegisterCssToken(void);
void TestRegisterCssTokenizer(void);
void TestRegisterCssTokenizeCore(void);
void TestRegisterCssTokenizeIdent(void);
void TestRegisterCssTokenizeLiteral(void);
void TestRegisterCssTokenizeSequence(void);
void TestRegisterCssValue(void);
void TestRegisterCssDeclaration(void);
void TestRegisterCssSelector(void);
void TestRegisterCssSheet(void);
void TestRegisterCssStyle(void);
void TestRegisterCssRobust(void);
void TestRegisterCssParseSheet(void);
void TestRegisterCssParseDeclaration(void);
void TestRegisterCssParseValue(void);
void TestRegisterCssParseRecover(void);
void TestRegisterCssParseUnicode(void);
void TestRegisterCssSelectParse(void);
void TestRegisterCssSelectMatchSimple(void);
void TestRegisterCssSelectMatchCombinator(void);
void TestRegisterCssSelectMatchStructural(void);
void TestRegisterCssSelectMatchLevels(void);
void TestRegisterCssSpecificity(void);
void TestRegisterCssSpecificityCascade(void);
void TestRegisterCssCascadeLevels(void);
void TestRegisterCssCascadeCollect(void);
void TestRegisterCssCascadeResolve(void);
void TestRegisterCssPropertyRegistry(void);
void TestRegisterCssInherit(void);
void TestRegisterCssInheritResolve(void);
void TestRegisterCssComputedValue(void);
void TestRegisterCssComputed(void);
void TestRegisterCssColor(void);
void TestRegisterCssUnit(void);
void TestRegisterCssBox(void);
void TestRegisterCssDisplay(void);
void TestRegisterCssPosition(void);
void TestRegisterCssBackground(void);
void TestRegisterCssBorder(void);
void TestRegisterCssOverflow(void);
void TestRegisterCssOpacity(void);
void TestRegisterCssConformance(void);
void TestRegisterCssArchitecture(void);
void TestRegisterCssCssom(void);
void TestRegisterCssFlex(void);
void TestRegisterCssGrid(void);
void TestRegisterCssTable(void);
void TestRegisterCssColumn(void);
void TestRegisterCssWriting(void);
void TestRegisterCssTransform(void);
void TestRegisterCssTransition(void);
void TestRegisterCssAnimation(void);
void TestRegisterCssMedia(void);
void TestRegisterCssContainer(void);
void TestRegisterCssLogical(void);
void TestRegisterCssFont(void);
void TestRegisterCssText(void);
void TestRegisterCssImage(void);
void TestRegisterCssFilter(void);
void TestRegisterCssDomSelect(void);
void TestRegisterLayout(void);
void TestRegisterLayoutFormattingContext(void);
void TestRegisterLayoutBlock(void);
void TestRegisterLayoutInline(void);
void TestRegisterLayoutText(void);
void TestRegisterLayoutFontRequest(void);
void TestRegisterLayoutFontSelect(void);
void TestRegisterLayoutLineBox(void);
void TestRegisterLayoutBaseline(void);
void TestRegisterLayoutSizing(void);
void TestRegisterLayoutIntrinsic(void);
void TestRegisterLayoutMargin(void);
void TestRegisterLayoutPadding(void);
void TestRegisterLayoutBorder(void);
void TestRegisterLayoutPosition(void);
void TestRegisterLayoutAbsolute(void);
void TestRegisterLayoutOverflow(void);
void TestRegisterLayoutInvalidation(void);
void TestRegisterLayoutTree(void);
void TestRegisterLayoutConstraints(void);
void TestRegisterLayoutContext(void);
void TestRegisterLayoutBuild(void);
void TestRegisterLayoutDom(void);
void TestRegisterLayoutConformance(void);
void TestRegisterLayoutIntegration(void);
void TestRegisterPaint(void);
void TestRegisterPaintTraversal(void);
void TestRegisterPaintConformance(void);
void TestRegisterPaintRecords(void);
void TestRegisterDisplayList(void);
void TestRegisterPaintBackground(void);
void TestRegisterPaintBorder(void);
void TestRegisterPaintText(void);
void TestRegisterPaintImage(void);
void TestRegisterPaintClip(void);
void TestRegisterPaintOpacity(void);
void TestRegisterPaintHitTest(void);
void TestRegisterPaintInvalidation(void);
void TestRegisterRender(void);
void TestRegisterRenderRaster(void);
void TestRegisterRenderScene(void);
void TestRegisterDesktop(void);
void TestRegisterDesktopGeometry(void);
void TestRegisterDesktopWindow(void);
void TestRegisterDesktopFrame(void);
void TestRegisterDesktopDocument(void);
void TestRegisterDesktopPresent(void);
void TestRegisterRoboJS(void);
void TestRegisterRoboJSHost(void);
void TestRegisterRoboJSIntegration(void);
void TestRegisterDesktopTypography(void);
void TestRegisterDesktopTypographyGallery(void);
void TestRegisterDesktopScene(void);
/* Phase 13.66--13.77 -- the shell's *consumption* of RoboInput: the loop and its lifecycle, the window
 * manager's gestures and hit test, and the document/text/command/clipboard chain. Registered beside the
 * desktop suites because that is where the integration lives, and after them because the pointer cases
 * build the same windows the window suite does. */
void TestRegisterDesktopInput(void);
void TestRegisterDesktopInputPointer(void);
void TestRegisterDesktopInputContent(void);
/* Phase 12.1 — RoboInput: the event model, its coordinate spaces, the queue and the window bridge. These
 * tests own no module-wide state (the module has none), so their order among the others does not matter and
 * they are registered before the desktop suite, which deliberately restarts the layers it uses. */
void TestRegisterInputConformance(void);
void TestRegisterInputEvent(void);
void TestRegisterInputKeyboard(void);
void TestRegisterInputPointer(void);
void TestRegisterInputCoordinates(void);
void TestRegisterInputQueue(void);
void TestRegisterInputOwnership(void);
void TestRegisterInputTarget(void);
void TestRegisterInputDebug(void);
void TestRegisterInputBridge(void);
/* Phase 13.2 — the device half: what a device is, and the table that owns them -- plus the three payload
 * records the grown vocabulary carries (a stylus, a controller, an input method's preedit). */
void TestRegisterInputDevice(void);
void TestRegisterInputPayloads(void);
void TestRegisterInputKeyboardState(void);
void TestRegisterInputLayout(void);
void TestRegisterInputText(void);
void TestRegisterInputCommand(void);
/* Phase 13.14--13.19 — the pointer state machine: the live pointer table, derived click counting, capture,
 * hover, and the wheel conversion a scroller needs. */
void TestRegisterInputPointerState(void);
/* Phase 13.25--13.28 — focus: one focus per window scope, the both-ends transition and the navigation
 * chain. */
void TestRegisterInputFocus(void);
/* Phase 13.29--13.36 — hit testing, the event path, propagation, the three-phase walk and the
 * default-action table. */
void TestRegisterInputDispatch(void);
/* Phase 13.20/13.21 — touch contacts keyed by the hardware's id, and the pointer normalization. */
void TestRegisterInputTouch(void);
/* Phase 13.23/13.24 — the gesture sample, the arena that picks one winner, and the shipped recognizer. */
void TestRegisterInputGesture(void);
/* Phase 13.40/13.41 — the clipboard: formats, the by-value entry, and copy/paste over logical text. */
void TestRegisterInputClipboard(void);
/* Phase 13.43 — the cursor: the shape vocabulary, the default table, the overrides and the hit-test decision. */
void TestRegisterInputCursor(void);
/* Phase 13.42 — drag and drop: the negotiation, the arrival, the drop and the cancel. */
void TestRegisterInputDragDrop(void);
/* Phase 13.37--13.39 — the input-method context: caret geometry, surrounding text and the candidate rect. */
void TestRegisterInputIme(void);
/* Phase 13.44/13.78 — the session: hotplug's by-device sweep and the transient reset. */
void TestRegisterInputSession(void);
/* Phase 13.49--13.51 — recording, replay and the injection privilege table. */
void TestRegisterInputReplay(void);
/* Phase 13.47 — merging high-frequency moves without ever merging a press, a key or a text event. */
void TestRegisterInputCoalescing(void);
/* Phase 13.48 — the five stamps from hardware to present, and the three numbers they produce. */
void TestRegisterInputLatency(void);
/* Phase 13.52 — the semantic commands an accessibility client invokes, and the policy that bounds them. */
void TestRegisterInputAccessibility(void);
/* Phase 13.54 — a controller's D-pad, stick and face buttons as focus moves and activation. */
void TestRegisterInputGamepadNavigation(void);

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--child-panic") == 0) {
        SetPanicHandler(NULL, NULL);
        Panic("controlled child panic");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-assert") == 0) {
        SetPanicHandler(NULL, NULL);
        RoboAssert(0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-double-free") == 0) {
        void *p;
        MemDebugEnable();
        MemDebugSetFaultHandler(NULL, NULL);
        p = Malloc(16);
        RoboFree(p);
        RoboFree(p);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-invalid-free") == 0) {
        int local = 1;
        MemDebugEnable();
        MemDebugSetFaultHandler(NULL, NULL);
        RoboFree(&local);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-underflow") == 0) {
        void *p;
        MemDebugEnable();
        MemDebugSetFaultHandler(NULL, NULL);
        p = Malloc(32);
        MemDebugCorruptGuardForTest(p, 0);
        RoboFree(p);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-overflow") == 0) {
        void *p;
        MemDebugEnable();
        MemDebugSetFaultHandler(NULL, NULL);
        p = Malloc(32);
        MemDebugCorruptGuardForTest(p, 1);
        RoboFree(p);
        return 0;
    }
    /* The desktop shell's checkpoint (Phase 11.13): the shared scene in a real
     * host window, with the frame read back and asserted. */
    if (argc >= 2 && strcmp(argv[1], "--desktop") == 0) {
        return DesktopHostMain(argc - 1, argv + 1);
    }

    TestRegisterBase();
    TestRegisterLog();
    TestRegisterError();
    TestRegisterDebug();
    TestRegisterMem();
    TestRegisterMemDebug();
    TestRegisterMemSystem();
    TestRegisterArena();
    TestRegisterPool();
    TestRegisterArray();
    TestRegisterString();
    TestRegisterUnicode();
    TestRegisterUnicodeProps();
    TestRegisterUnicodeNorm();
    TestRegisterUnicodeGrapheme();
    TestRegisterUnicodeCase();
    TestRegisterUnicodeWord();
    TestRegisterUnicodeSentence();
    TestRegisterUnicodeLine();
    TestRegisterUnicodeBidi();
    TestRegisterUnicodeScript();
    TestRegisterUnicodeEmoji();
    TestRegisterUnicodeInfra();
    TestRegisterI18n();
    TestRegisterI18nCldr();
    TestRegisterI18nNumber();
    TestRegisterI18nPlural();
    TestRegisterI18nDatetime();
    TestRegisterI18nDatetimeFormat();
    TestRegisterI18nInterval();
    TestRegisterI18nRelative();
    TestRegisterI18nDatetimeArith();
    TestRegisterI18nCollation();
    TestRegisterI18nDisplayNames();
    TestRegisterI18nMessage();
    TestRegisterI18nMessageFormat();
    TestRegisterI18nMessageValidate();
    TestRegisterI18nTimezone();
    TestRegisterI18nTimezoneDisplay();
    TestRegisterI18nLocaleService();
    TestRegisterI18nLocaleNegotiate();
    TestRegisterI18nOrdinal();
    TestRegisterI18nUnit();
    TestRegisterI18nMeasure();
    TestRegisterI18nCurrency();
    TestRegisterI18nParse();
    TestRegisterI18nDatetimeParse();
    TestRegisterI18nSearch();
    TestRegisterI18nTranslit();
    TestRegisterI18nConformance();
    TestRegisterI18nPhase4();
    TestRegisterFont();
    TestRegisterFontSfnt();
    TestRegisterFontTt();
    TestRegisterFontOt();
    TestRegisterFontCff();
    TestRegisterFontCmap();
    TestRegisterFontMetrics();
    TestRegisterFontOutline();
    TestRegisterFontName();
    TestRegisterFontClassification();
    TestRegisterFontSelection();
    TestRegisterFontDiscovery();
    TestRegisterFontLoader();
    TestRegisterFontCoverage();
    TestRegisterFontFallback();
    TestRegisterFontGeneric();
    TestRegisterFontEmoji();
    TestRegisterFontVariation();
    TestRegisterFontColor();
    TestRegisterFontValidate();
    TestRegisterFontConformance();
    TestRegisterFontPhase5();
    TestRegisterFontSystemPack();
    TestRegisterTypographyRoles();
    TestRegisterTypographyResolver();
    TestRegisterTypographyDocument();
    TestRegisterTextBuffer();
    TestRegisterTextRuns();
    TestRegisterTextGlyphs();
    TestRegisterTextShape();
    TestRegisterTextConformance();
    TestRegisterTextGallery();
    TestRegisterTextUnicode();
    TestRegisterTextFontSelection();
    TestRegisterTextAnalysis();
    TestRegisterTextScript();
    TestRegisterTextDirection();
    TestRegisterTextBidi();
    TestRegisterTextGsub();
    TestRegisterTextGpos();
    TestRegisterTextLigature();
    TestRegisterTextKerning();
    TestRegisterTextGlyphRuns();
    TestRegisterTextMeasurement();
    TestRegisterTextShapingLatin();
    TestRegisterTextShapingScripts();
    TestRegisterTextShapingFeatures();
    TestRegisterTextShapingKerning();
    TestRegisterTextShapingMarks();
    TestRegisterTextShapingFallback();
    TestRegisterTextShapingIntegration();
    TestRegisterTextArabic();
    TestRegisterTextIndic();
    TestRegisterTextVertical();
    TestRegisterTextSelection();
    TestRegisterTextEditing();
    TestRegisterTextApi();
    TestRegisterTextShapingCorpus();
    TestRegisterCompositor();
    TestRegisterCompositorVisual();
    TestRegisterCompositorScheduling();
    TestRegisterGraphics();
    TestRegisterGraphicsPixelFormat();
    TestRegisterGraphicsFramebuffer();
    TestRegisterGraphicsSurface();
    TestRegisterGraphicsBitmap();
    TestRegisterGraphicsGeometry();
    TestRegisterGraphicsLine();
    TestRegisterGraphicsPath();
    TestRegisterGraphicsTransform();
    TestRegisterGraphicsComposite();
    TestRegisterGraphicsRaster();
    TestRegisterGraphicsText();
    TestRegisterGraphicsGlyph();
    TestRegisterGraphicsTypography();
    TestRegisterGraphicsEncode();
    TestRegisterGraphicsDecode();
    TestRegisterGraphicsScreenshot();
    TestRegisterGraphicsPhase6();
    TestRegisterWindow();
    TestRegisterWindowNative();
    TestRegisterWindowPresent();
    TestRegisterWindowGraphics();
    TestRegisterWindowHost();
    TestRegisterWindowSize();
    TestRegisterWindowLifecycle();
    TestRegisterWindowEventLoop();
    TestRegisterWindowKeyboard();
    TestRegisterWindowMouse();
    TestRegisterWindowQemuDisplay();
    TestRegisterWindowPhase7();
    TestRegisterDisplay();
    TestRegisterDisplayScale();
    TestRegisterDisplayHost();
    TestRegisterHtml();
    TestRegisterHtmlDomQuery();
    TestRegisterHtmlTable();
    TestRegisterHtmlFormTree();
    TestRegisterHtmlInput();
    TestRegisterHtmlNavigation();
    TestRegisterHtmlImageLoad();
    TestRegisterHtmlTemplate();
    TestRegisterHtmlShadow();
    TestRegisterHtmlCustomElements();
    TestRegisterHtmlSecurity();
    TestRegisterHtmlDomConformance();
    TestRegisterHtmlDomIntegration();
    TestRegisterHtmlDocument();
    TestRegisterHtmlParser();
    TestRegisterHtmlHooks();
    TestRegisterHtmlPhase8();
    TestRegisterHtmlToken();
    TestRegisterHtmlCharref();
    TestRegisterHtmlTokenizerStates();
    TestRegisterHtmlTreeBuilder();
    TestRegisterHtmlDocumentModel();
    TestRegisterHtmlElement();
    TestRegisterHtmlAttribute();
    TestRegisterHtmlText();
    TestRegisterHtmlTraversal();
    TestRegisterHtmlMutation();
    TestRegisterHtmlLifecycle();
    TestRegisterHtmlTag();
    TestRegisterHtmlElements();
    TestRegisterHtmlImage();
    TestRegisterHtmlAnchor();
    TestRegisterHtmlForm();
    TestRegisterHtmlDom();
    TestRegisterCss();
    TestRegisterCssSource();
    TestRegisterCssToken();
    TestRegisterCssTokenizer();
    TestRegisterCssTokenizeCore();
    TestRegisterCssTokenizeIdent();
    TestRegisterCssTokenizeLiteral();
    TestRegisterCssTokenizeSequence();
    TestRegisterCssValue();
    TestRegisterCssDeclaration();
    TestRegisterCssSelector();
    TestRegisterCssSheet();
    TestRegisterCssStyle();
    TestRegisterCssRobust();
    TestRegisterCssParseSheet();
    TestRegisterCssParseDeclaration();
    TestRegisterCssParseValue();
    TestRegisterCssParseRecover();
    TestRegisterCssParseUnicode();
    TestRegisterCssSelectParse();
    TestRegisterCssSelectMatchSimple();
    TestRegisterCssSelectMatchCombinator();
    TestRegisterCssSelectMatchStructural();
    TestRegisterCssSelectMatchLevels();
    TestRegisterCssSpecificity();
    TestRegisterCssSpecificityCascade();
    TestRegisterCssCascadeLevels();
    TestRegisterCssCascadeCollect();
    TestRegisterCssCascadeResolve();
    TestRegisterCssPropertyRegistry();
    TestRegisterCssInherit();
    TestRegisterCssInheritResolve();
    TestRegisterCssComputedValue();
    TestRegisterCssComputed();
    TestRegisterCssColor();
    TestRegisterCssUnit();
    TestRegisterCssBox();
    TestRegisterCssDisplay();
    TestRegisterCssPosition();
    TestRegisterCssBackground();
    TestRegisterCssBorder();
    TestRegisterCssOverflow();
    TestRegisterCssOpacity();
    TestRegisterCssConformance();
    TestRegisterCssArchitecture();
    TestRegisterCssCssom();
    TestRegisterCssFlex();
    TestRegisterCssGrid();
    TestRegisterCssTable();
    TestRegisterCssColumn();
    TestRegisterCssWriting();
    TestRegisterCssTransform();
    TestRegisterCssTransition();
    TestRegisterCssAnimation();
    TestRegisterCssMedia();
    TestRegisterCssContainer();
    TestRegisterCssLogical();
    TestRegisterCssFont();
    TestRegisterCssText();
    TestRegisterCssImage();
    TestRegisterCssFilter();
    TestRegisterCssDomSelect();

    TestRegisterLayout();
    TestRegisterLayoutFormattingContext();
    TestRegisterLayoutBlock();
    TestRegisterLayoutInline();
    TestRegisterLayoutText();
    TestRegisterLayoutFontRequest();
    TestRegisterLayoutFontSelect();
    TestRegisterLayoutLineBox();
    TestRegisterLayoutBaseline();
    TestRegisterLayoutSizing();
    TestRegisterLayoutIntrinsic();
    TestRegisterLayoutMargin();
    TestRegisterLayoutPadding();
    TestRegisterLayoutBorder();
    TestRegisterLayoutPosition();
    TestRegisterLayoutAbsolute();
    TestRegisterLayoutOverflow();
    TestRegisterLayoutInvalidation();
    TestRegisterLayoutTree();
    TestRegisterLayoutConstraints();
    TestRegisterLayoutContext();
    TestRegisterLayoutBuild();
    TestRegisterLayoutDom();
    TestRegisterLayoutConformance();
    TestRegisterLayoutIntegration();
    TestRegisterPaint();
    TestRegisterPaintTraversal();
    TestRegisterPaintConformance();
    TestRegisterPaintRecords();
    TestRegisterDisplayList();
    TestRegisterPaintBackground();
    TestRegisterPaintBorder();
    TestRegisterPaintText();
    TestRegisterPaintImage();
    TestRegisterPaintClip();
    TestRegisterPaintOpacity();
    TestRegisterPaintHitTest();
    TestRegisterPaintInvalidation();
    TestRegisterRender();
    TestRegisterRenderRaster();
    TestRegisterRenderScene();
    TestRegisterDesktop();
    TestRegisterDesktopGeometry();
    TestRegisterDesktopWindow();
    TestRegisterDesktopFrame();
    TestRegisterDesktopDocument();
    TestRegisterDesktopPresent();
    /* Phase 12.3 -- registered before the scene suite, which shuts the layers down and brings them back. */
    TestRegisterDesktopTypography();
    TestRegisterDesktopTypographyGallery();
    TestRegisterDesktopInput();
    TestRegisterDesktopInputPointer();
    TestRegisterDesktopInputContent();
    TestRegisterInputConformance();
    TestRegisterInputEvent();
    TestRegisterInputKeyboard();
    TestRegisterInputPointer();
    TestRegisterInputCoordinates();
    TestRegisterInputQueue();
    TestRegisterInputOwnership();
    TestRegisterInputTarget();
    TestRegisterInputDebug();
    TestRegisterInputBridge();
    TestRegisterInputDevice();
    TestRegisterInputPayloads();
    TestRegisterInputKeyboardState();
    TestRegisterInputLayout();
    TestRegisterInputText();
    TestRegisterInputCommand();
    TestRegisterInputPointerState();
    TestRegisterInputFocus();
    TestRegisterInputDispatch();
    TestRegisterInputTouch();
    TestRegisterInputGesture();
    TestRegisterInputClipboard();
    TestRegisterInputCursor();
    TestRegisterInputDragDrop();
    TestRegisterInputIme();
    TestRegisterInputSession();
    TestRegisterInputReplay();
    TestRegisterInputCoalescing();
    TestRegisterInputLatency();
    TestRegisterInputAccessibility();
    TestRegisterInputGamepadNavigation();
    /* Phase 16 -- RoboJS. The core language suite, the host suite (the DOM binding over a fixture
     * host, events, timers, the loop, microtasks) and the integration suite (the RoboHTML adapter
     * and the phase's required demos). */
    TestRegisterRoboJS();
    TestRegisterRoboJSHost();
    TestRegisterRoboJSIntegration();
    /* Registered last: desktop.scene.lifetime intentionally shuts the layers
     * down and brings them back, so nothing may follow it. */
    TestRegisterDesktopScene();
    return TestMain(argc, argv);
}
