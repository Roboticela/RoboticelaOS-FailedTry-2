# Builds Roboticela Runtime library (guest ELF) and the RoboTest host suite.
#
#   .\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test
#   .\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test
#   .\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter base
#   .\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
#   .\runtime\Tools\Build.ps1 -Configuration Debug -BuildTests
#   .\runtime\Tools\Build.ps1 -Clean
#
# -Test builds the host suite and runs it; -BuildTests only builds it. The
# distinction matters to the pipeline: a focused command such as `test-html`
# needs the binary to exist but must not run (and therefore must not fail on)
# suites it does not own.

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean,
    [switch]$Verify,
    [switch]$Test,
    [switch]$BuildTests,
    [switch]$Sanitize,
    [string]$Filter = "",
    [switch]$VerboseTests,
    [ValidateSet("", "Core", "Memory", "Containers", "Unicode", "I18n", "Font", "Typography", "Text", "Graphics", "Display", "Window", "Compositor", "Input", "Html", "Css", "Layout", "Paint", "Rasterizer", "Desktop", "RoboJS", "Runtime")]
    [string]$Module = "",
    [switch]$SkipGuest,
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "..\..\scripts\Utilities\RoboBuildProgress.ps1")

$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepoRoot = (Resolve-Path (Join-Path $RuntimeRoot "..")).Path
$OutDir = Join-Path $RepoRoot "Build\Runtime"
$LibDir = Join-Path $OutDir "lib"
$IncludeDir = Join-Path $RuntimeRoot "Include"
$SourceDir = Join-Path $RuntimeRoot "Source"
$TestsRoot = Join-Path $RepoRoot "Tests"
$TestsDir = Join-Path $TestsRoot "Runtime"
$FrameworkDir = Join-Path $TestsDir "Framework"
$SuitesDir = Join-Path $TestsDir "Suites"

$ProdSrcs = @(
    (Join-Path $SourceDir "RoboBase\RoboError.c"),
    (Join-Path $SourceDir "RoboBase\RoboBase.c"),
    (Join-Path $SourceDir "RoboBase\RoboLog.c"),
    (Join-Path $SourceDir "RoboBase\RoboDebug.c"),
    (Join-Path $SourceDir "RoboMem\RoboMem.c"),
    (Join-Path $SourceDir "RoboMem\RoboArena.c"),
    (Join-Path $SourceDir "RoboMem\RoboPool.c"),
    (Join-Path $SourceDir "RoboContainers\RoboArray.c"),
    (Join-Path $SourceDir "RoboContainers\RoboString.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicode.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeProps.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeNorm.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeGrapheme.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeCase.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeWord.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeSentence.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeLine.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeBidi.c"),
    # UAX #9 L4's `Bidi_Mirroring_Glyph` pairing, generated from the vendored BidiMirroring.txt by
    # Tools/ucd/generate_mirror.py. Unicode data stays in RoboUnicode, so RoboText *asks* for a mirror
    # rather than carrying a second table (Phase 12.8).
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeBidiMirror.c"),
    # `Joining_Type` (Phase 12.19): the Arabic shaper's one Unicode input. Hand-maintained rather than
    # generated -- the tree carries the generated UCD data and not the sources it came from -- and RoboText
    # *asks* for it rather than keeping a range table of its own (12.19 §9).
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeJoining.c"),
    (Join-Path $SourceDir "RoboUnicode\RoboUnicodeEmoji.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18N.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NLocale.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NLocaleData.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NLocaleService.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NLocaleNegotiate.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NNumber.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NPlural.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NDateTime.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NTimeZone.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NCalendar.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NDateTimeFormat.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NDateTimeInterval.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NRelativeTime.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NUnit.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NMeasure.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NCurrency.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NParse.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NDateTimeParse.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NSearch.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NTranslit.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NDateTimeArith.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NCollation.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NDisplayNames.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NMessage.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NMessageFormat.c"),
    (Join-Path $SourceDir "RoboI18N\RoboI18NMessageValidate.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_ucd_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_norm_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_grapheme_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_case_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_word_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_sentence_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_line_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_bidi_data.c"),
    (Join-Path $SourceDir "RoboUnicode\data\robo_emoji_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_iana_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_cldr_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_plural_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_cal_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_rel_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_unit_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_measure_pref_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_currency_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_uca_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_coll_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_disp_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_msg_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_tz_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_hc_data.c"),
    (Join-Path $SourceDir "RoboI18N\data\robo_translit_data.c"),
    (Join-Path $SourceDir "RoboFont\RoboFont.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontStyle.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontMetrics.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontSource.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontFace.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontInstance.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontBinary.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontSfnt.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontTtTables.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontTtGlyf.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontTt.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOtCommon.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOtCmap.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOtLayout.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOtExtra.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOt.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCffIndex.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCffDict.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCffTables.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCffCharString.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCff2.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCff.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCmap.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOutline.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOutlineTt.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontOutlineCff.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontName.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontClassification.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontSelection.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontDiscovery.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontLoader.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontCoverage.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontFallback.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontGenericFamily.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontEmoji.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontVariation.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontColor.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontValidate.c"),
    (Join-Path $SourceDir "RoboFont\RoboFontConformance.c"),
    # Phase 12.2 — the system typeface pack. Binds the existing discovery /
    # registry / loader / selection pipeline to /System/Fonts and its manifest;
    # it adds no parser and no rasterizer of its own.
    (Join-Path $SourceDir "RoboFont\RoboFontSystemPack.c"),
    # Phase 12.3 -- the typography binding: semantic roles, the one scale that maps a role to a
    # type setting, and the resolver that turns a role into a face the pack installed. It adds no
    # parser, no metrics implementation and no selection rule of its own.
    (Join-Path $SourceDir "RoboTypography\RoboTypographyTypes.c"),
    (Join-Path $SourceDir "RoboTypography\RoboTypographyScale.c"),
    (Join-Path $SourceDir "RoboTypography\RoboTypographyResolver.c"),
    (Join-Path $SourceDir "RoboTypography\RoboTypographyVersion.c"),
    # Phase 12.4 -- the text layer: the canonical text model (buffer, ranges, units), the runs, the
    # glyph record and glyph runs, and the pipeline that turns a string into both a measurement and
    # the glyphs a painter walks. It owns no font parser (RoboFont parses) and no Unicode database
    # (Phase 4 owns that); `RoboTextGlyphGenerationCmapPerScalar` is the labelled placeholder the
    # later shaping goals replace, and it is isolated in RoboTextGlyphs.c so they can.
    (Join-Path $SourceDir "RoboText\RoboTextDiagnostics.c"),
    (Join-Path $SourceDir "RoboText\RoboTextVersion.c"),
    (Join-Path $SourceDir "RoboText\RoboTextTypes.c"),
    (Join-Path $SourceDir "RoboText\RoboTextInternal.c"),
    (Join-Path $SourceDir "RoboText\RoboTextBuffer.c"),
    (Join-Path $SourceDir "RoboText\RoboTextFont.c"),
    (Join-Path $SourceDir "RoboText\RoboTextRuns.c"),
    (Join-Path $SourceDir "RoboText\RoboTextGlyphs.c"),
    (Join-Path $SourceDir "RoboText\RoboTextShape.c"),
    # Phase 12.5 -- the Unicode/font integration. `RoboTextUnicode.c` is the module's one decoder and
    # property interface (every door in it is a call into RoboUnicode, which owns the tables),
    # `RoboTextScriptResolver.c` is the one implementation of the Common/Inherited neutrality rule,
    # `RoboTextFontSelection.c` is the coverage-gated selector with its fallback chain, and
    # `RoboTextAnalysis.c` is the runs that combine script, direction and font and the shape request
    # they populate. None of them parses a font or keeps a Unicode table.
    (Join-Path $SourceDir "RoboText\RoboTextUnicode.c"),
    (Join-Path $SourceDir "RoboText\RoboTextScriptResolver.c"),
    # Phase 12.7 -- the direction stage. `RoboTextDirection.c` is the module's one direction resolver: the
    # `Bidi_Class` -> direction-class projection, the paragraph direction a context names, and the single
    # pass that turns the two into direction runs.
    (Join-Path $SourceDir "RoboText\RoboTextDirection.c"),
    # Phase 12.8 -- the bidirectional stage. `RoboTextBidi.c` is the module's one resolver *and* its one
    # window onto Phase 4's UBA: the paragraph base (P2/P3, with P3's level-0 default), the levels, the
    # §5.2 retention of X9-removed scalars, the logical runs, L2's visual order and the L4 mirroring gate.
    (Join-Path $SourceDir "RoboText\RoboTextBidi.c"),
    # Phase 12.10 -- glyph substitution. `RoboTextGsub.c` is the engine (the feature plan the font resolves
    # for a script/language, and the one pass that applies its lookups in the Lookup List's order) and
    # `RoboTextGsubSubst.c` is the readers, one per GSUB lookup type. The table itself is Phase 5's, reached
    # through `RoboTextFontGsub`, so this module interprets offsets and never parses a container.
    (Join-Path $SourceDir "RoboText\RoboTextGsub.c"),
    (Join-Path $SourceDir "RoboText\RoboTextGsubSubst.c"),
    # Phase 12.11 -- glyph positioning. `RoboTextGpos.c` is the engine's spine (the defaults, the plan, the
    # apply loop, the trace and the writers) and `RoboTextGposSubst.c` the readers: a value record, single
    # adjustment, pair adjustment in both formats, and the extension wrapper. Both resolve their lookups with
    # the shared core `RoboTextGsub.c` implements, so GSUB and GPOS are one OpenType layout implementation
    # with two sets of readers rather than two parsers.
    (Join-Path $SourceDir "RoboText\RoboTextGpos.c"),
    (Join-Path $SourceDir "RoboText\RoboTextGposSubst.c"),
    # Phase 12.12 -- ligatures. `RoboTextLigature.c` is the *policy* layer and the record of what the font
    # produced: five tags (liga/clig/rlig/dlig/hlig) as caller-visible data, the stylesheet's
    # `font-variant-ligatures` vocabulary translated into that data, and the glyph-run report. It holds no
    # second engine: 12.10's GSUB substitutes, and a ligature is what a substitution looks like when one
    # glyph's source range covers several scalars.
    (Join-Path $SourceDir "RoboText\RoboTextLigature.c"),
    # Phase 12.13 -- kerning. `RoboTextKerning.c` is the *policy* over the `kern` feature, the resolution of
    # which source may answer (GPOS's pair lists, or a legacy `kern` table as a fallback and never as an
    # addition), and the record of what the face did to the run's advances. It holds no engine of its own:
    # 12.11's GPOS moves the glyphs, and a kerning pair is what one of its value records looks like.
    (Join-Path $SourceDir "RoboText\RoboTextKerning.c"),
    # Phase 12.14 -- glyph runs. `RoboTextRunList.c` is the *list* over the runs shaping produced: the key that
    # says what makes two runs different, the logical order that is the list itself and the visual order kept
    # beside it, the merge of two runs that are provably one, and the validation of the set. The run's own
    # clusters, flags and metrics are derived by `RoboTextGlyphs.c`'s finalize, and the visual order is
    # *read* from 12.8's paragraph rather than recomputed: this file contains no bidirectional algorithm.
    (Join-Path $SourceDir "RoboText\RoboTextRunList.c"),
    # Phase 12.15 -- measurement. `RoboTextMeasurement.c` is the module's one measurement engine over a
    # finalized run list: the advances summed from the runs' own totals, the line metrics aggregated by the
    # maximum rule, the logical box, the ink box the glyphs' outlines give, the tolerance, the validation and
    # the diagnostic block. It shapes nothing: `RoboTextShape.c` calls it, so a result's own measurement and a
    # caller's second measurement of the same result are one implementation rather than two that must agree.
    (Join-Path $SourceDir "RoboText\RoboTextMeasurement.c"),
    # Phase 12.19 -- complex scripts. `RoboTextComplex.c` is the *plan*: the script → strategy table, the
    # stages a strategy resolves, the per-scalar position masks and the two reorder steps as data. The two
    # script modules own their own rules and no engine: `RoboTextArabic.c` reads RoboUnicode's `Joining_Type`,
    # `RoboTextIndic.c` reads its own per-script tables (the UCD's positional categories are not in this
    # tree), and both produce *masks and stage lists* rather than glyphs. GSUB substitutes and GPOS positions,
    # exactly as 12.10–12.13 built them, with the masks as an extra positional condition.
    (Join-Path $SourceDir "RoboText\RoboTextComplex.c"),
    (Join-Path $SourceDir "RoboText\RoboTextArabic.c"),
    (Join-Path $SourceDir "RoboText\RoboTextIndic.c"),
    # Phase 12.20 -- vertical writing. `RoboTextVertical.c` is the *decision* half of the phase: the writing
    # mode's two axes, the upright/sideways orientation tables, and the `vert`/`vrt2` hook that turns a
    # vertical mode into two ordinary feature settings. There is no vertical shaping engine -- the placement
    # lives beside the `hmtx` line in `RoboTextGlyphs.c` and the vertical substitutions are 12.10's GSUB.
    (Join-Path $SourceDir "RoboText\RoboTextVertical.c"),
    # Phase 12.21 -- selection mapping. `RoboTextSelection.c` is the logical ↔ visual bridge: a source position
    # becomes a cluster boundary, a point becomes a position, and a selection becomes the rectangles a painter
    # draws. It shapes nothing -- every number it answers with is read off a run that was shaped before it was
    # asked, plus the placements the caller supplies -- and it writes no x/y swap: the inline/block projection
    # goes through 12.20's axes, so a vertical column and a horizontal line take the same path.
    (Join-Path $SourceDir "RoboText\RoboTextSelection.c"),
    # Phase 12.22 -- text editing. `RoboTextEditing.c` is the *contract* the input layer will call: an editor
    # that owns one buffer and one selection, every high-level operation expressed as a single
    # `Replace(range, text)`, a durable operation record with the deleted and inserted text in it, and an
    # invalidation record that says which ranges a consumer must redo. It shapes nothing and it edits no
    # glyph: a source edit is text, and the new glyphs are whatever the next shape produces.
    (Join-Path $SourceDir "RoboText\RoboTextEditing.c"),
    (Join-Path $SourceDir "RoboText\RoboTextFontSelection.c"),
    (Join-Path $SourceDir "RoboText\RoboTextAnalysis.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphics.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsGeometry.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsColor.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsPixelFormat.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsFramebuffer.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsSurface.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsBitmap.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsLine.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsPath.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsTransform.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsComposite.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsRaster.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsGlyph.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsText.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsEncode.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsDecode.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsScreenshot.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsImage.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsFrame.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsBackend.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsCapabilities.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplay.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplayTypes.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplayInfo.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplayScale.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplayBackend.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplayBackendHost.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboQemuFramebuffer.c"),
    (Join-Path $SourceDir "RoboDisplay\RoboDisplayBackendQemu.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindow.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowLifecycle.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowConfig.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowState.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowDisplay.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowEvent.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowEventLoop.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowKey.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowKeyboard.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowMouse.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowBackend.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowNative.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowThread.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowPresent.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowGraphics.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowSize.c"),
    (Join-Path $SourceDir "RoboWindow\RoboWindowBackendHost.c"),
    (Join-Path $SourceDir "RoboWindow\platform\qemu\RoboWindowBackendQemu.c"),
    # Phase 15 -- RoboCompositor. The module sits on RoboGraphics and nothing above it: a layer's content is
    # a `GfxSurface` somebody else rasterized, and the composite pass is the source-over blend that already
    # lives in `RoboGraphicsComposite.c`. Eight files, one per question the phase asks: the core (the tables,
    # the clock, the counters), the pool and its budget, the layer tree, the damage list, the tile grids and
    # the raster scheduler, the animation tracks, the frame pump with its post queue and fences, the
    # composite pass, and the diagnostics. `RoboCompositorTiles.c` is the seam that matters most: it decides
    # *what* work a frame has and calls the caller's `CompositorRasterFn` for *how*, so this module never
    # learns what a display list is.
    (Join-Path $SourceDir "RoboCompositor\RoboCompositor.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorSurface.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorLayer.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorDamage.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorTiles.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorAnimation.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorFrame.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorCompose.c"),
    (Join-Path $SourceDir "RoboCompositor\RoboCompositorDump.c"),
    # Phase 16 -- RoboJS. An independently implemented JavaScript runtime: the lexer and parser, the AST,
    # the value/object/environment models, the interpreter, and the host seams. One file per question the
    # phase asks. `RoboJSDom.c` binds a DOM through a vtable and names no HTML header; `RoboJSDomHtml.c` is
    # the one file that includes RoboHTML, which is the same discipline `RoboPaintDispatch.c` follows for
    # RoboInput -- the dependency is named, small and replaceable.
    (Join-Path $SourceDir "RoboJS\RoboJSVersion.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSTypes.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSHeap.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSValue.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSObject.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSEnvironment.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSFunction.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSException.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSLexer.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSAst.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSParser.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSInterpreter.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSBuiltins.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSRuntime.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSTimer.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSEventLoop.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSDom.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSEvent.c"),
    (Join-Path $SourceDir "RoboJS\RoboJSDomHtml.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTML.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTypes.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDiagnostics.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLLifecycle.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLSource.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLEncoding.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLNode.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDom.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLElement.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTag.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLImage.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLClient.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLResource.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLParser.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDocument.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLToken.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLCharRefData.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLCharRef.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTokenizer.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLPayload.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLText.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTreeBuilder.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTraversal.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLMutation.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDocumentModel.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLAnchor.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLForm.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDomQuery.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTable.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLFormTree.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLInput.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLNavigation.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLImageLoad.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTemplate.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLShadow.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLCustomElements.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLSecurity.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSInternal.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSS.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSTypes.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSDiagnostics.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSError.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSSource.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSToken.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSTokenizer.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSValue.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSProperty.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSDeclaration.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSSelector.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSRule.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSStyleSheet.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSParser.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSParserGrammar.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSComputed.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSStyle.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSSelectorParser.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSMatcher.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSSpecificity.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSCascade.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSPropertyRegistry.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSInherit.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSColor.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSUnit.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSBox.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSDisplay.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSPosition.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSBackground.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSBorder.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSOverflow.c"),
    # The opacity family (11.9) is the second value surface a non-9.x phase
    # registered -- `overflow` was 10.13's, this one is the paint stack's.
    (Join-Path $SourceDir "RoboCSS\RoboCSSOpacity.c"),
    (Join-Path $SourceDir "RoboCSS\RoboCSSComputedValue.c"),
    # The custom-property substitution pass (18.1): the second half of CSS
    # Variables, run at computed-value time over the computed custom properties.
    (Join-Path $SourceDir "RoboCSS\RoboCSSVariable.c"),
    # The object model (18.2): the model rendered back to canonical text, and
    # the mutable style declaration a caller can edit.
    (Join-Path $SourceDir "RoboCSS\RoboCSSOM.c"),
    # The flexbox vocabulary (18.3): the direction, wrap and alignment closed
    # sets and the classifications a layout pass dispatches on. It is the first
    # family whose values are three vocabularies rather than one.
    (Join-Path $SourceDir "RoboCSS\RoboCSSFlex.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayout.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutTypes.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutDiagnostics.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutConstraints.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutResult.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutObject.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutTree.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutContext.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutStyle.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutBuilder.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutDom.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutFormattingContext.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutBlock.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutInline.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutText.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutTextFont.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutFontRequest.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutFontResolver.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutLineBox.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutBaseline.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutSizing.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutMargin.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutPadding.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutBorder.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutPosition.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutAbsolute.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutOverflow.c"),
    (Join-Path $SourceDir "RoboLayout\RoboLayoutInvalidation.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaint.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintTypes.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDiagnostics.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintGeometry.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintClip.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintOpacity.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintOperation.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintList.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintContext.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDump.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDisplayCommand.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDisplayList.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDisplayListBuilder.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDisplayDump.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintColor.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintText.c"),
    # Phase 12.16 -- the paint/layout bridge. `RoboPaintFontTable.c` is the token-to-face mapping the glyph
    # runs in a display list need: a glyph id is meaningless without the face that produced it, and a token
    # keeps a record a pointer-free value so a list stays safe to copy and to rebuild.
    (Join-Path $SourceDir "RoboPaint\RoboPaintFontTable.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintHitTest.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintImage.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintInvalidation.c"),
    (Join-Path $SourceDir "RoboPaint\RoboPaintDecoration.c"),
    # Phase 12.24 -- the shared shaped-run seam. One shaping call and one glyph pen for every painter in the
    # tree: the document's recorder, the display-list replay and the shell's own labels all come through
    # `RoboPaintRun.c`, so there is one place where a glyph becomes ink and a shell label cannot drift from a
    # document's text by a rounding or a rule.
    (Join-Path $SourceDir "RoboPaint\RoboPaintRun.c"),
    # RoboRender (11.12) — the first module above the paint stack: an owned pixel target, a run over
    # a display list, an execution record and region-limited execution. It drives the walk below it
    # through `PaintDecorationPaintBounded` and adds no second executor of its own.
    (Join-Path $SourceDir "RoboRender\RoboRender.c"),
    (Join-Path $SourceDir "RoboRender\RoboRenderDiagnostics.c"),
    (Join-Path $SourceDir "RoboRender\RoboRenderRasterizer.c"),
    (Join-Path $SourceDir "RoboRender\RoboRenderFrameDump.c"),
    # RoboRender 11.13: the presentation door -- one frame into one destination surface (or a
    # framebuffer). It adds no pixel loop of its own: the destination is handed to RoboGraphics'
    # own composite, which is where the blend and the format conversion already live.
    (Join-Path $SourceDir "RoboRender\RoboRenderPresenter.c"),
    # RoboDesktop (11.13) -- the native shell: an owned screen target, a background, a panel and a
    # work area painted with graphics primitives, shell windows with decorations whose contents are
    # display lists, a deterministic frame lifecycle, a resize path and a present door. It reads
    # *no* markup: that is the boundary the scan below enforces, and `RoboDesktopDocument.c` is the
    # one file exempted from it, because a document bridge is not a shell.
    (Join-Path $SourceDir "RoboDesktop\RoboDesktop.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopCompose.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopTypes.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopTheme.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopGeometry.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopWindow.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopChrome.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopText.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopDiagnostics.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopDump.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopDocument.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopFontProvider.c"),
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopScript.c"),
    # RoboDesktop 13.66--13.77 -- the *input integration*: the one place in the tree where the shell and
    # the input model meet. It owns a session, a queue, a dispatcher, the shell's listener store, a
    # shortcut table, a command router and a cursor manager; it supplies the three providers
    # (`RoboInputDispatch.h`) from the shell's own window list; and it is the only file in this module that
    # includes RoboInput, which is what keeps every other file in it a *painter* or a *window*. See
    # `Documentation/Runtime/Desktop/InputIntegration.md`.
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopInput.c"),
    # RoboInput (12.1) -- the input *model*: an event type vocabulary and its payloads, a device and
    # source identity, four explicitly named coordinate planes with the conversions between them, a routing
    # target carried as inert identity, an owned FIFO queue and a deterministic dump. It reads no device
    # and dispatches nothing; the boundary scan below is what keeps it that way, and the one direction it
    # travels (`RoboInputBridge.c`) is a translation of a RoboWindow event rather than a device read.
    (Join-Path $SourceDir "RoboInput\RoboInput.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputTypes.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputDiagnostics.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputCoordinates.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputKeyboard.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputPointer.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputEvent.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputTarget.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputQueue.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputDebug.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputBridge.c"),
    # RoboInput 13.2 -- the *device* half: what a device is (a kind of hardware, what it can report, where
    # it came from) and the fixed table that owns them, whose disconnect **removes** an entry and hands it
    # back so no stale device can be found, asked for as a type's primary, or left holding a key. The three
    # payload records beside it are the vocabulary's new members: a stylus (a pointer sample plus what a
    # finger cannot report), a normalized controller, and an input method's preedit.
    (Join-Path $SourceDir "RoboInput\RoboInputDevice.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputPen.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputGamepad.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputComposition.c"),
    # RoboInput 13.2 continued -- the keyboard *state*: one held table from which the modifiers and the locks
    # are derived, so a device disconnect cannot strand a key or a modifier, and an auto-repeat cadence a
    # caller's clock drives one event at a time.
    (Join-Path $SourceDir "RoboInput\RoboInputKeyboardState.c"),
    # RoboInput 13.9/13.10 -- the *layout*: physical position plus modifier state becomes a character, levels
    # and caps lock are one rule (`InputLayoutLevelFor`), and the five dead accents carry a spacing form, a
    # combining form and a compose table that lists both cases rather than deriving one by an offset. Two
    # built-in tables ship (`US_ASCII`, `US_INTERNATIONAL`), and a table for another language is data this
    # module can hold rather than a parser it does not have.
    (Join-Path $SourceDir "RoboInput\RoboInputLayout.c"),
    # RoboInput 13.11 -- the *text pipeline*: the one door that turns a key event into zero, one or two
    # `TEXT_INPUT` events through the layout and the pending accent, so `KeyDown` never means "insert this
    # character" and an editor is only ever handed text.
    (Join-Path $SourceDir "RoboInput\RoboInputText.c"),
    # RoboInput 13.12/13.13/13.35/13.36 -- the *command layer*: a platform-neutral vocabulary of intentions
    # (`RoboInputCommand.c`), a bounded chain of two-field targets asked innermost-first where "not mine"
    # continues the walk while a real failure stops it, the one place a `PhysicalKey` becomes an intention
    # (`RoboInputShortcut.c`, scoped so a focused control wins an argument with a global binding), and the
    # bridge that turns a command into RoboText's editor doors (the only file in this module that includes
    # RoboText, so the editor never has an opinion about keys).
    (Join-Path $SourceDir "RoboInput\RoboInputCommand.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputShortcut.c"),
    (Join-Path $SourceDir "RoboInput\RoboInputTextBridge.c"),
    # RoboInput 13.14--13.19 -- the *pointer state machine*: the live pointer table, the click counter that
    # derives a count when the platform does not report one, pointer capture with the "only the holder may
    # release" rule, and hover whose one invariant is that a target is never entered twice. None of it
    # routes or hit-tests: whoever owns the tree resolves the target and tells this layer.
    (Join-Path $SourceDir "RoboInput\RoboInputPointerState.c"),
    # RoboInput 13.25--13.28 -- *focus*: one focus per window scope so a shell restores what was focused
    # when it brings a window back, a transition record naming both ends in the order 13.27 requires (lose
    # then gain) plus the events it produces, and a caller-built navigation chain this layer walks and never
    # invents. A destroyed target is released by identity; a geometric arrow is refused, not guessed.
    (Join-Path $SourceDir "RoboInput\RoboInputFocus.c"),
    # RoboInput 13.29--13.36 -- *dispatch*: hit testing, the path an event travels, the propagation context
    # a listener may write, the three-phase walk, and the default-action table that *identifies* what a
    # caller should do without doing it. Every dependency on the layer above is one function pointer, and
    # each absent one degrades to a stated case (the window, the target alone, nobody listening).
    (Join-Path $SourceDir "RoboInput\RoboInputDispatch.c"),
    # RoboInput 13.20/13.21 -- *touch*: contacts keyed by the hardware's own id (never an array index),
    # cancellation as a phase that keeps the contact until it is acknowledged, and the one conversion into
    # the pointer model so a consumer that understands a mouse understands a tap.
    (Join-Path $SourceDir "RoboInput\RoboInputTouch.c"),
    # RoboInput 13.23/13.24 -- *gestures*: the sample a recognizer consumes (one builder from a pointer
    # payload, one from the contact table that computes the centroid, spread and angle once), the arena that
    # picks one winner per sequence in the caller's priority order, and the heuristic recognizer this build
    # ships for the seven types the brief names.
    (Join-Path $SourceDir "RoboInput\RoboInputGesture.c"),
    # RoboInput 13.40/13.41 -- the *clipboard*: one fixed by-value text buffer (a clipboard that allocated
    # would need an owner, a lock and a lifetime), the character-of-a-range read arriving as a function
    # pointer because RoboText is not this module's to include, and the rule that an empty selection leaves
    # what is already there alone.
    (Join-Path $SourceDir "RoboInput\RoboInputClipboard.c"),
    # RoboInput 13.43 -- the *cursor*: one cursor for the frame, resolved from whatever the pointer is over,
    # with the precedence between a captured target's request and the hovered one's written once, and the
    # rule that a request from a target nobody is over is *counted* and ignored rather than obeyed.
    (Join-Path $SourceDir "RoboInput\RoboInputCursor.c"),
    # RoboInput 13.42 -- *drag and drop*: a session that owns its source, the escape key's cancellation as
    # the session's own transition, and the drop accepted only by the target the pointer is over while the
    # operation is one the source permitted.
    (Join-Path $SourceDir "RoboInput\RoboInputDragDrop.c"),
    # RoboInput 13.37--13.39 -- the *input method context*: the caret, the surrounding text a purpose may
    # permit, and the candidate rectangle anchored to the caret. Every client door is a function pointer
    # onto RoboText's editing/selection API, and the two rules this layer keeps for itself are the password
    # field that is never asked and the candidate that is refused rather than placed at the origin.
    (Join-Path $SourceDir "RoboInput\RoboInputIme.c"),
    # RoboInput 13.44/13.78 -- the *session*: the one object that owns every table a device can put state in,
    # which is what makes "no stale state may remain" a claim somebody can check. Three sweeps -- a device
    # went away, a window deactivated, a target was destroyed -- and one record of what each released.
    (Join-Path $SourceDir "RoboInput\RoboInputSession.c"),
    # RoboInput 13.49--13.51 -- *recording, replay and the privilege boundary*: a bounded by-value stream
    # whose rank only rises, a replay gated on the actor matching it, and the table that says how much
    # privilege each event type needs. The boundary is a number comparison because a note in a document is
    # not a boundary.
    (Join-Path $SourceDir "RoboInput\RoboInputReplay.c"),
    # RoboInput 13.54 -- *gamepad navigation*: D-pad and stick to a focus move, A to activate, B to back, with
    # the deadzone, the dominant-axis rule and the repeat cadence as the only state it owns. It maps an
    # action; it never applies one.
    (Join-Path $SourceDir "RoboInput\RoboInputGamepadNavigation.c"),
    # RoboInput 13.47 -- *event coalescing*: the rule (three move types, everything else a barrier), the
    # fixed-key table that merges them, and the lossless flush whose one failure mode is `RoboBusy` with the
    # remainder still held.
    (Join-Path $SourceDir "RoboInput\RoboInputCoalescing.c"),
    # RoboInput 13.48 -- *latency*: five stamps and four differences, with an out-of-order stamp refused and
    # counted rather than saturated into a plausible small number.
    (Join-Path $SourceDir "RoboInput\RoboInputLatency.c"),
    # RoboInput 13.52 -- *accessibility hooks*: the semantic vocabulary an accessibility client speaks, the
    # policy that keeps it out of the window and system commands, and one invoke door that routes through the
    # same `InputCommandRouter` a shortcut does.
    (Join-Path $SourceDir "RoboInput\RoboInputAccessibility.c")
)
# The desktop's checkpoint scene and its host launcher (Phase 11.13). Not part of $ProdSrcs, and
# deliberately: the shipping runtime has no scene and no launcher -- a scene is the fixture somebody looks
# at, and the shell it draws is the product. Both files are compiled into the host suite binary, and the
# scene is also compiled into the guest program (see `scripts/roboboot-build-common.ps1`), so the two
# backends run one definition of the checkpoint rather than two that drift.
$DesktopSceneSrcs = @(
    (Join-Path $SourceDir "RoboDesktop\RoboDesktopScene.c"),
    (Join-Path $SourceDir "RoboDesktop\platform\host\RoboDesktopHost.c")
)
$MainSrc = Join-Path $SourceDir "Main.c"
$TestFrameworkSrc = Join-Path $FrameworkDir "RoboTest.c"
$TestMainSrc = Join-Path $TestsDir "Main.c"
$SuiteSrcs = @(
    (Join-Path $SuitesDir "SuiteRoboBase.c"),
    (Join-Path $SuitesDir "SuiteRoboLog.c"),
    (Join-Path $SuitesDir "SuiteRoboError.c"),
    (Join-Path $SuitesDir "SuiteRoboDebug.c"),
    (Join-Path $SuitesDir "SuiteRoboMem.c"),
    (Join-Path $SuitesDir "SuiteRoboMemDebug.c"),
    (Join-Path $SuitesDir "SuiteRoboMemSystem.c"),
    (Join-Path $SuitesDir "SuiteRoboArena.c"),
    (Join-Path $SuitesDir "SuiteRoboPool.c"),
    (Join-Path $SuitesDir "SuiteRoboArray.c"),
    (Join-Path $SuitesDir "SuiteRoboString.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicode.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeProps.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeNorm.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeGrapheme.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeCase.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeWord.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeSentence.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeLine.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeBidi.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeScript.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeEmoji.c"),
    (Join-Path $SuitesDir "SuiteRoboUnicodeInfra.c"),
    (Join-Path $SuitesDir "SuiteRoboI18N.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NCldr.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NNumber.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NPlural.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NDateTime.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NDateTimeFormat.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NInterval.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NRelative.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NUnit.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NMeasure.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NCurrency.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NParse.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NDateTimeParse.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NSearch.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NTranslit.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NConformance.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NPhase4.c"),
    (Join-Path $SuitesDir "SuiteRoboFont.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontSfnt.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontTrueType.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontOpenType.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontCff.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontCmap.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontMetrics.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontOutline.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontName.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontClassification.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontSelection.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontDiscovery.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontLoader.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontCoverage.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontFallback.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontGenericFamily.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontEmoji.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontVariation.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontColor.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontValidate.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontConformance.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontPhase5.c"),
    (Join-Path $TestsRoot "Font\SuiteRoboFontSystemPack.c"),
    (Join-Path $TestsRoot "Typography\SuiteRoboTypographyRoles.c"),
    (Join-Path $TestsRoot "Typography\SuiteRoboTypographyResolver.c"),
    (Join-Path $TestsRoot "Typography\SuiteRoboTypographyDocument.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextBuffer.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextRuns.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextGlyphs.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShape.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextConformance.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextGallery.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextUnicode.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextFontSelection.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextAnalysis.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextScript.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextDirection.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextBidi.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextGsub.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextGpos.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextLigature.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextKerning.c"),
    # Phase 12.15 -- measurement: the engine over a finalized run list, the line-metrics aggregation over a
    # mixed-font line, the logical and ink boxes, the tolerance, the validation and the diagnostic block.
    (Join-Path $TestsRoot "Text\SuiteRoboTextMeasurement.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextGlyphRuns.c"),
    # Phase 12.18 -- Latin and European shaping conformance: the deterministic test fonts, the
    # machine-readable case model, and the Latin/Greek/Cyrillic, feature, kerning, mark, fallback and
    # integration suites that assert glyph sequences, clusters, advances and run structure.
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingLatin.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingScripts.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingFeatures.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingKerning.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingMarks.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingFallback.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingIntegration.c"),
    # Phase 12.19 -- complex scripts: the Arabic strategy (joining, the four contextual forms by position,
    # the lam/alef ligature, mark-to-base, mark-to-mark and cursive attachment) and the Indic strategy
    # (syllables, the base, the masked feature stages, the reph, pre-base matra reordering and conjuncts),
    # both over one built face whose bytes state every expected glyph id and anchor.
    (Join-Path $TestsRoot "Text\SuiteRoboTextArabic.c"),
    (Join-Path $TestsRoot "Text\SuiteRoboTextIndic.c"),
    # Phase 12.20 -- vertical writing: the writing mode and its axes, the upright/sideways orientation tables
    # and the rotated table that beats the script, the orientation cut, `vert` through the ordinary GSUB
    # engine, the `vmtx` advances and the synthesised-advance path, the measurement's `advanceY` and bidi in
    # a vertical column -- all over one built face that states both advances and both `vert` products.
    (Join-Path $TestsRoot "Text\SuiteRoboTextVertical.c"),
    # Phase 12.21 -- selection mapping: the pure vocabulary, the cluster snapping a click lands in, a caret in
    # both writing modes, the rectangles of a selection across lines, a bidi boundary and a fallback font, the
    # round trip position → caret → hit → position, and the fuzz entry.
    (Join-Path $TestsRoot "Text\SuiteRoboTextSelection.c"),
    # Phase 12.22 -- text editing: one mutation expressed as a replacement of a source range, the character
    # and cluster boundaries a delete uses, a base-and-its-marks and a ligature as editing units, Arabic
    # joining and a Devanagari conjunct reshaping after an edit, the paragraph-widened invalidation record,
    # composition, transactions, position/range transformation, the blocks and the fuzz entry.
    (Join-Path $TestsRoot "Text\SuiteRoboTextEditing.c"),
    # Phase 12.24 -- the API freeze. An aggregate-header completeness check (both ears: the include proves
    # the header compiles against the umbrella, the case reads the umbrella and matches it against the
    # directory), the initial values of every public `...Init`, the refusals a caller can provoke, and the
    # borrowing lifetime of a shape result's parts.
    (Join-Path $TestsRoot "Text\SuiteRoboTextApi.c"),
    # Phase 12.23 -- the whole-pipeline shaping corpus: the layer chain asked one door at a time, the script
    # families, Latin/European, a feature that moves the measurement, the scripts this build names as gaps,
    # bidi, fallback, vertical, the measurement/selection/editing round trip over one corpus, the malformed
    # matrix, a hundred repetitions per sample, and the diffable table written to Build.
    (Join-Path $TestsRoot "Text\SuiteRoboTextShapingCorpus.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphics.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsPixelFormat.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsFramebuffer.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsSurface.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsBitmap.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsGeometry.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsLine.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsPath.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsTransform.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsComposite.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsRaster.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsText.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsGlyph.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsTypography.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsEncode.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsDecode.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsScreenshot.c"),
    (Join-Path $TestsRoot "Graphics\SuiteRoboGraphicsPhase6.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindow.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowNative.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowPresent.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowGraphics.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowHost.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowSize.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowLifecycle.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowEventLoop.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowKeyboard.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowMouse.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboQemuDisplay.c"),
    (Join-Path $TestsRoot "Window\SuiteRoboWindowPhase7.c"),
    (Join-Path $TestsRoot "Display\SuiteRoboDisplay.c"),
    (Join-Path $TestsRoot "Display\SuiteRoboDisplayScale.c"),
    (Join-Path $TestsRoot "Display\SuiteRoboDisplayHost.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTML.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLDocument.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLParser.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLHooks.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLPhase8.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLToken.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLCharRef.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLTokenizerStates.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLTreeBuilder.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLDocumentModel.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLElement.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLText.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLAttribute.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLTraversal.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLMutation.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLLifecycle.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLTag.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLElements.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLImage.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLAnchor.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLForm.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLDom.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLDomQuery.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLTable.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLFormTree.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLInput.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLNavigation.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLImageLoad.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLTemplate.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLShadow.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLCustomElements.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLSecurity.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLDomConformance.c"),
    (Join-Path $TestsRoot "Html\SuiteRoboHTMLDomIntegration.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCss.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssSource.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssToken.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssTokenizer.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssTokenizeCore.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssTokenizeIdent.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssTokenizeLiteral.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssTokenizeSequence.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssValue.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssDeclaration.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssSelector.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssSheet.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssStyle.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssRobust.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssParseSheet.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssParseDeclaration.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssParseValue.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssParseRecover.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssParseUnicode.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssSelectParse.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssMatchSimple.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssMatchCombinator.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssMatchStructural.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssMatchLevels.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssSpecificity.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssSpecificityCascade.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssCascade.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssCascadeCollect.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssCascadeResolve.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssPropertyRegistry.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssInherit.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssInheritResolve.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssComputedValue.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssComputed.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssColor.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssUnit.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssBox.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssDisplay.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssPosition.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssBackground.c"),
    (Join-Path $TestsRoot "Css\SuiteRoboCssBorder.c"),
    # The overflow family (10.13) is the one whose phase is a *layout* phase: the
    # value is registered by the side that consumes it, so its suite sits with the
    # families it belongs beside rather than after the layout suites.
    (Join-Path $TestsRoot "Css\SuiteRoboCssOverflow.c"),
    # And the opacity family (11.9), registered by the paint stack's phase for the
    # same reason: the value exists because something downstream reads it.
    (Join-Path $TestsRoot "Css\SuiteRoboCssOpacity.c"),
    # The conformance suite (9.16) is the only one that spans the whole registry
    # and the whole pipeline rather than one family, so it sits last in the list
    # for the same reason it runs over everything the ones above it built.
    (Join-Path $TestsRoot "Css\SuiteRoboCssConformance.c"),
    # The architecture suite (18.1) is the second one that spans the module:
    # it is the contract for custom-property substitution, the half of CSS
    # Variables 9.x named as a boundary and left unsubstituted.
    (Join-Path $TestsRoot "Css\SuiteRoboCssArchitecture.c"),
    # The object-model suite (18.2): the contract for cssText and for the
    # mutable style declaration, the half of CSS that edits rather than reads.
    (Join-Path $TestsRoot "Css\SuiteRoboCssCssom.c"),
    # The flexbox suite (18.3): the three vocabularies, their classifications and
    # the four shapes the alignment set is read through.
    (Join-Path $TestsRoot "Css\SuiteRoboCssFlex.c"),
    # The selector/DOM integration suite deliberately lives outside css/: it
    # includes both RoboCSS and RoboHTML, and the two are peers whose guards
    # forbid each other's headers. A suite that must see both belongs in neither.
    (Join-Path $SuitesDir "SuiteRoboCssDomSelect.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayout.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutFormattingContext.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutBlock.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutInline.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutText.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutFontRequest.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutFontSelect.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutLineBox.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutBaseline.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutSizing.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutMargin.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutPadding.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutBorder.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutPosition.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutAbsolute.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutOverflow.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutInvalidation.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutTree.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutConstraints.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutContext.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutBuild.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutDom.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutConformance.c"),
    (Join-Path $TestsRoot "Layout\SuiteRoboLayoutIntegration.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaint.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintTraversal.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintConformance.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintRecords.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintDisplayList.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintBackground.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintBorder.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintText.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintImage.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintClip.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintOpacity.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintHitTest.c"),
    (Join-Path $TestsRoot "Paint\SuiteRoboPaintInvalidation.c"),
    (Join-Path $TestsRoot "Rasterizer\SuiteRoboRender.c"),
    (Join-Path $TestsRoot "Rasterizer\SuiteRoboRenderRaster.c"),
    (Join-Path $TestsRoot "Rasterizer\SuiteRoboRenderScene.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktop.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopGeometry.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopWindow.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopFrame.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopDocument.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopPresent.c"),
    # Phase 12.3 -- the desktop's typography binding: the chrome's roles, the document's generated
    # defaults, and the one-face guarantee between a document's measurement and its paint.
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopTypography.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopTypographyGallery.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopScene.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopInput.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopInputPointer.c"),
    (Join-Path $TestsRoot "Desktop\SuiteRoboDesktopInputContent.c"),
    # Phase 15 -- RoboCompositor. Three suites: the core (the vocabulary, the capability table, the clock,
    # the counters and the two doors that make a failure readable — the dump and the fuzz check), the visual
    # half (the tree, the composite pass, damage and the present, all proved by reading the destination's
    # pixels) and the scheduling half (the pool and its budget, tiles, the raster scheduler, the animation
    # tracks and the frame pump with vsync and fences).
    (Join-Path $TestsRoot "Compositor\SuiteRoboCompositor.c"),
    (Join-Path $TestsRoot "Compositor\SuiteRoboCompositorVisual.c"),
    (Join-Path $TestsRoot "Compositor\SuiteRoboCompositorScheduling.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputConformance.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputEvent.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputKeyboard.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputPointer.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputCoordinates.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputQueue.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputOwnership.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputTarget.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputDebug.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputBridge.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputDevice.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputPayloads.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputKeyboardState.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputLayout.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputText.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputCommand.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputPointerState.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputFocus.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputDispatch.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputTouch.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputGesture.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputClipboard.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputCursor.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputDragDrop.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputIme.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputSession.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputReplay.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputCoalescing.c"),
    # Phase 16 -- RoboJS. The core language suite (runtime, lexer, parser, AST, values, objects,
    # functions, closures, exceptions, interpreter, limits, security), the host suite (the DOM binding
    # over a fixture host, events, timers, the loop, microtasks) and the integration suite (the
    # RoboHTML adapter and the phase's required demos).
    (Join-Path $TestsRoot "RoboJS\SuiteRoboJS.c"),
    (Join-Path $TestsRoot "RoboJS\SuiteRoboJSHost.c"),
    (Join-Path $TestsRoot "RoboJS\SuiteRoboJSIntegration.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputLatency.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputAccessibility.c"),
    (Join-Path $TestsRoot "Input\SuiteRoboInputGamepadNavigation.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NDateTimeArith.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NCollation.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NDisplayNames.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NMessage.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NMessageFormat.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NMessageValidate.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NTimeZone.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NTimeZoneDisplay.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NLocaleService.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NLocaleNegotiate.c"),
    (Join-Path $SuitesDir "SuiteRoboI18NOrdinal.c")
)

$RuntimeElf = Join-Path $OutDir "RoboticelaRuntime.elf"
$HostTest = Join-Path $OutDir "RoboTest.host.exe"

$LibcInc = Join-Path $RepoRoot "Libc\include"
$SdkInc = Join-Path $RepoRoot "Sdk\include"
$LibcA = Join-Path $RepoRoot "Build\Libc\libc.a"
$Crt0 = Join-Path $RepoRoot "Build\Libc\obj\crt0.o"
$Crti = Join-Path $RepoRoot "Build\Libc\obj\crti.o"
$Crtn = Join-Path $RepoRoot "Build\Libc\obj\crtn.o"
$LdScript = Join-Path $RepoRoot "User\linker.ld"
$ChromiumRoot = Join-Path $RepoRoot "Chromium"

if ($Configuration -eq "Debug") {
    $RoboDebugDefine = "1"
    $RoboMemDebugDefine = "1"
    $OptFlags = @("-O0", "-g")
} else {
    $RoboDebugDefine = "0"
    $RoboMemDebugDefine = "0"
    $OptFlags = @("-O2")
}

function Get-Tool([string]$Name, [string]$Hint) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    if (Test-Path $Hint) { return $Hint }
    throw "$Name not found. Install LLVM and add it to PATH."
}

function Get-Clang {
    Get-Tool "clang" "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\clang.exe"
}

function Get-LdLld {
    Get-Tool "ld.lld" "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\ld.lld.exe"
}

function Get-LlvmAr {
    $cmd = Get-Command "llvm-ar" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $hint = "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\llvm-ar.exe"
    if (Test-Path $hint) { return $hint }
    return $null
}

function Get-LlvmReadobj {
    $cmd = Get-Command "llvm-readobj" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $hint = "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\llvm-readobj.exe"
    if (Test-Path $hint) { return $hint }
    return $null
}

function Assert-NoChromiumPath([string[]]$Paths) {
    $chromiumFull = [IO.Path]::GetFullPath($ChromiumRoot).TrimEnd('\', '/')
    foreach ($p in $Paths) {
        if (-not $p) { continue }
        if ($p.StartsWith("-")) { continue }
        if ($p -notmatch '[/\\]' -and -not (Test-Path -LiteralPath $p)) { continue }
        $full = [IO.Path]::GetFullPath($p)
        if ($full.StartsWith($chromiumFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            $full.Equals($chromiumFull, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Chromium path leaked into runtime build: $p"
        }
        if ($p -match '(?i)(?:^|[/\\])Chromium(?:[/\\]|$)') {
            throw "Chromium path leaked into runtime build: $p"
        }
    }
}

function Assert-RoboFontChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboFont"),
        (Join-Path $RuntimeRoot "Source\RoboFont"),
        (Join-Path $RuntimeRoot "Font")
    )
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia)|third_party[/\\](chromium|skia)|\bnamespace\s+(blink|skia)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboFont Chromium/Skia dependency leaked into sources:`n$sample"
        }
    }
    Write-Host "RoboFont Chromium-independence source scan: OK"
}

function Assert-RoboGraphicsChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboGraphics"),
        (Join-Path $RuntimeRoot "Source\RoboGraphics"),
        (Join-Path $RuntimeRoot "Graphics")
    )
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|vulkan|GL/gl|d3d1[12]|Metal/Metal)|third_party[/\\](chromium|skia)|\bnamespace\s+(blink|skia)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only|Chromium reference'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboGraphics Chromium/Skia/GPU API dependency leaked into sources:`n$sample"
        }
    }
    Write-Host "RoboGraphics Chromium-independence source scan: OK"
}

function Assert-RoboWindowChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboWindow"),
        (Join-Path $RuntimeRoot "Source\RoboWindow"),
        (Join-Path $RuntimeRoot "Window")
    )
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|vulkan|GL/gl|d3d1[12]|Metal/Metal|windows\.h|X11/|wayland|AppKit|Cocoa|SDL|GLFW|Qt)|third_party[/\\](chromium|skia)|\bnamespace\s+(blink|skia|ui)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Path -notmatch '(?i)[/\\]platform[/\\]win32[/\\]' -and
                $_.Path -notmatch '(?i)[/\\]platform[/\\]qemu[/\\]' -and
                $_.Path -notmatch '(?i)[/\\]window[/\\]tests[/\\]' -and
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only|Chromium reference|do not include|must not include|Win32/X11|not a dependency|platform/win32|platform/qemu|SYS_DISPLAY|robosys'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboWindow Chromium/OS-toolkit dependency leaked into sources:`n$sample"
        }
    }
    Write-Host "RoboWindow Chromium-independence source scan: OK"
}

function Assert-RoboHTMLChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboHTML"),
        (Join-Path $SourceDir "RoboHTML"),
        (Join-Path $RuntimeRoot "Html")
    )
    # RoboHTML must stay independent of Chromium AND of higher runtime
    # layers (CSS/layout/paint/render/window/graphics) per DEPENDENCIES.md.
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCSS|RoboLayout|RoboPaint|RoboRender|RoboCompositor|RoboWindow|RoboGraphics|RoboDisplay|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only|Chromium reference|not a dependency|RoboCSS/|RoboWindow/|RoboGraphics/|RoboDisplay/'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboHTML Chromium/higher-layer dependency leaked into sources:`n$sample"
        }
    }
    Write-Host "RoboHTML Chromium-independence source scan: OK"
}

function Assert-RoboCSSChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboCSS"),
        (Join-Path $SourceDir "RoboCSS"),
        (Join-Path $RuntimeRoot "Css")
    )
    # RoboCSS answers "what does this stylesheet say"; it must not reach into
    # Chromium, and it must not reach *up* either: it is a peer of RoboHTML, not
    # a client of it, and it knows nothing about layout, painting, rendering or
    # any windowing or drawing layer (see DEPENDENCIES.md and css/docs).
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboHTML|RoboLayout|RoboPaint|RoboRender|RoboCompositor|RoboWindow|RoboGraphics|RoboDisplay|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Line -notmatch '(?i)must never depend|never depend|no Chromium|without Chromium|independence|Chromium-specific|Chromium reference|not a dependency|Forbidden:'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboCSS Chromium/higher-layer dependency leaked into sources:`n$sample"
        }
    }
    Write-Host "RoboCSS Chromium-independence source scan: OK"
}

function Assert-RoboLayoutBoundaries {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboLayout"),
        (Join-Path $RuntimeRoot "Source\RoboLayout"),
        (Join-Path $RuntimeRoot "Layout")
    )
    # RoboLayout is the first module that may depend on BOTH RoboHTML and RoboCSS
    # (DEPENDENCIES.md: the two are peers, so whoever needs both sits above both),
    # and it may use RoboGraphics for its geometry. What it must never reach for is
    # anything above itself -- paint, rendering, compositing, JS, the shell -- nor
    # a window or display layer, which are below it in the graph but have no
    # business in a layout pass, nor Chromium.
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboPaint|RoboRender|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboWindow|RoboDisplay|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only|Chromium reference|not a dependency|does not depend on|has no business'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboLayout boundary leak (Chromium or a higher layer):`n$sample"
        }
    }
    Write-Host "RoboLayout boundary source scan: OK"
}

function Assert-RoboPaintBoundaries {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboPaint"),
        (Join-Path $RuntimeRoot "Source\RoboPaint"),
        (Join-Path $RuntimeRoot "Paint")
    )
    # RoboPaint sits directly above RoboLayout and directly below RoboRender
    # (DEPENDENCIES.md): it may read a layout tree, a computed style's paint-visible
    # properties and RoboGraphics' geometry. What it must never reach for is anything
    # above itself -- render, compositing, JS, the shell, the API layer -- nor a window
    # or display layer, which is the direction a layering argument would miss and the
    # one this scan is really for, nor Chromium.
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboRender|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboWindow|RoboDisplay|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only|Chromium reference|not a dependency|does not depend on|has no business'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboPaint boundary leak (Chromium or a higher layer):`n$sample"
        }
    }
    Write-Host "RoboPaint boundary source scan: OK"
}

function Assert-RoboRenderBoundaries {
    # The module itself is held to both halves of the rule, because both are about what a
    # *rasteriser* may consult: a display list is its input contract, so a document layer is as
    # forbidden to it as a layer above it.
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboRender"),
        (Join-Path $RuntimeRoot "Source\RoboRender")
    )
    # The suites and the module's own documents are held to the upward half only, and the
    # asymmetry is the paint scan's own, one layer down: a render fixture *is* a paint fixture
    # plus a target, so it builds a document on purpose (HTML, CSS, a layout pass) to produce
    # the list under test. Scanning them for a document-layer include would be scanning for the
    # thing they exist to do -- while an upward include in a suite is still a leak, and is what
    # would happen the day a suite reached for a window to look at its own frame.
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "Rasterizer"), (Join-Path $RepoRoot "Documentation\Runtime\Rasterizer"))

    # RoboRender sits directly above RoboPaint and directly below RoboCompositor
    # (DEPENDENCIES.md): it may read a display list, a paint invalidation set, the geometry
    # those carry and the RoboGraphics surface/raster it draws through. What it must never
    # reach for is anything above itself -- a compositor, JS, the shell, the API layer, the
    # window or the display -- nor, in the module's own sources, a document layer, because a
    # rasteriser that consulted a tree could not be tested without one. Nor Chromium.
    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboWindow|RoboDisplay|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $document = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboWindow|RoboDisplay|RoboHTML|RoboCSS|RoboLayout|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $exempt = '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|has no business|does not read|reaching for|a document layer|names absent|RoboLayout''s|document-layer include'

    $rules = @(
        @{ Roots = $moduleRoots; Pattern = $document },
        @{ Roots = $allRoots; Pattern = $upward }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboRender boundary leak (Chromium, a higher layer or a document layer):`n$sample"
            }
        }
    }
    Write-Host "RoboRender boundary source scan: OK"
}

function Assert-RoboDesktopBoundaries {
    # Two rules, and the asymmetry between them is the module's whole design.
    #
    # The **upward** rule is the one every module in this tree keeps: the shell may not reach for
    # Chromium, for JavaScript, for the API/shell layers, for the demo layer, or for anything above
    # itself. RoboCompositor is the deliberate exception in `RoboDesktopCompose.c`: Phase 15's
    # software compositor is how the shell keeps retained wallpaper / panel / window surfaces and
    # presents damage instead of clearing the whole screen every frame. The exemption is by name
    # rather than by directory so a *second* desktop file reaching for a compositor is still a leak.
    #
    # The **document** rule is the interesting one, and it is deliberately narrower than the render
    # module's: RoboDesktop must not read HTML, CSS or a layout tree, because the shell's job is to
    # draw a panel with graphics primitives and to composite pictures it is *handed* -- a shell that
    # needed a document pipeline to draw a taskbar would lose the screen whenever that pipeline did.
    # Four files are exempt by name, and the filter is a name list rather than a directory so that a
    # *fourth* file reaching for a document layer would be caught rather than covered by an exception
    # written for these: `RoboDesktopDocument.c/.h`, the bridge that turns markup into a window's
    # contents; `RoboDesktopScene.c/.h`, the checkpoint scene's own definition -- which is a
    # *consumer* of that bridge (one window showing one document) rather than a second bridge; and
    # `RoboDesktopHost.c`, the launcher that drives that scene into a native window, whose entire job
    # is to run the stages the shell only composites (`Tools/Build.ps1` compiles it into the host
    # suite binary and nothing else ships it).
    #
    # `RoboDesktopFontProvider.c/.h` is the fourth and the odd one out, because it is the only entry
    # here that is *not* a document bridge: it is Phase 12.17's **font provider**, the concrete
    # `LayoutFontProvider` the layout measurer asks which face a family resolves to. It needs
    # `RoboLayoutFontResolver.h` for the vtable it implements, and the alternative the boundary would
    # force -- a font database inside RoboLayout -- is exactly what 12.17's contract forbids. The
    # exemption is by name rather than by directory for the usual reason: a *second* desktop file
    # reaching for RoboLayout is still a leak.
    #
    # `RoboDesktopScript.c/.h` is the Phase 16 bridge: the one file that may include RoboJS and that
    # attaches a runtime to the document id `DesktopDocumentHtml` hands it. Scene consumes it; the
    # shell does not. The upward scan exempts it the same way `RoboDesktopCompose` is exempt for the
    # compositor — by name, so a second desktop file reaching for RoboJS is still a leak.
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboDesktop"),
        (Join-Path $RuntimeRoot "Source\RoboDesktop")
    )
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "Desktop"), (Join-Path $RepoRoot "Documentation\Runtime\Desktop"))

    # The suites and the module's own documents are held to the upward half only, for the render
    # scan's reason: a desktop fixture *is* a render fixture plus a shell, so it builds a document on
    # purpose -- scanning it for a document-layer include would be scanning for the thing it exists to
    # do, while an upward include in a suite (a suite reaching for JavaScript to check its own
    # frame) is still a leak.
    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCompositor|RoboJS|RoboShell|RoboAPI|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $document = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboHTML|RoboCSS|RoboLayout|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    # Prose that names the boundary it keeps is not a leak: these headers explain what they do not
    # depend on in the same words the rule is written in.
    $exempt = '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|has no business|does not read|reaching for|a document layer|names absent|document-layer include|not a document|no markup|reads \*no\* markup'

    $rules = @(
        @{ Roots = @((Join-Path $RuntimeRoot "Include\RoboDesktop")); Pattern = $document; Skip = @('RoboDesktopDocument', 'RoboDesktopScene', 'RoboDesktopHost', 'RoboDesktopFontProvider', 'RoboDesktopScript') },
        @{ Roots = @((Join-Path $RuntimeRoot "Source\RoboDesktop")); Pattern = $document; Skip = @('RoboDesktopDocument', 'RoboDesktopScene', 'RoboDesktopHost', 'RoboDesktopFontProvider', 'RoboDesktopScript') },
        @{ Roots = $allRoots; Pattern = $upward; Skip = @('RoboDesktopCompose', 'RoboDesktopScript') }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $skip = $rule.Skip
            $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
                Where-Object {
                    if ($null -eq $skip) { return $true }
                    foreach ($name in $skip) { if ($_.Name -like "*$name*") { return $false } }
                    return $true
                } |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboDesktop boundary leak (Chromium, a document layer in the shell, or a higher layer):`n$sample"
            }
        }
    }
    Write-Host "RoboDesktop boundary source scan: OK"
}

function Assert-RoboInputBoundaries {
    # The input model is *below* the layers that will consume it, and two rules say so.
    #
    # The **upward** rule is the one every module in this tree keeps: no Chromium, no Skia, no V8, no
    # compositor, no JavaScript, and nothing from the API/shell layers above. A module that could reach a
    # compositor would stop being the thing that says what an input event *is*.
    #
    # The **consumer** rule is this module's own, and it is the interesting half: RoboInput may not include
    # RoboPaint, RoboRender or RoboDesktop -- the three modules that will read its records -- because a model
    # that depended on its consumers would invert the graph, and the day one of them changed shape this
    # module would change with it. Its two document-layer includes are *identities* and nothing else:
    # `RoboHTMLNode.h` for the inert node handle and `RoboLayoutTypes.h` for the node id, both of which are
    # values a target carries. Any other header from those two layers is refused by name, which is what keeps
    # "a target names a node" from turning into "the input model reads a tree".
    #
    # The suites are held to the upward rule only, for the desktop scan's reason: `input.target.from_hit_test`
    # exists to prove the three hit-test flags are mirrored *equal to paint's*, and a suite that compared a
    # mirror to its origin has to include the origin. That is a test proving a boundary, not a module
    # crossing one.
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboInput"),
        (Join-Path $RuntimeRoot "Source\RoboInput")
    )
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "Input"))

    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboDemo|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $consumer = '(?i)#\s*include\s*[<"](RoboPaint|RoboRender|RoboDesktop|RoboHTML/(?!RoboHTMLNode\.h)|RoboLayout/(?!RoboLayoutTypes\.h))'
    # Prose that names the boundary it keeps is not a leak, exactly as in the desktop scan.
    $exempt = '(?i)must never depend|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|no compositor|does not read|reaching for|not a document|reads \\*no\\* markup|may not depend|below RoboWindow and RoboPaint'

    $rules = @(
        @{ Roots = $moduleRoots; Pattern = $consumer; Skip = $null },
        @{ Roots = $allRoots; Pattern = $upward; Skip = $null }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboInput boundary leak (Chromium, a consumer layer, or a document layer beyond the two identity headers):`n$sample"
            }
        }
    }
    Write-Host "RoboInput boundary source scan: OK"
}

function Assert-RoboCompositorBoundaries {
    # This module has the narrowest allowed dependency set of anything added since RoboGraphics: **it may
    # include RoboGraphics and nothing above it.**
    #
    # The **upward** rule is the one this scan exists for, and it is unusually strict because the temptation
    # is unusually concrete. A compositor is the one place where "just walk the display list here" reads like
    # a simplification -- a layer's content arrives as a `GfxSurface`, and the next step from there looks like
    # "so let me rasterize it myself". The moment that happens the module cannot be tested without RoboRender,
    # its frame cost stops being a number and starts being somebody else's code, and the 15.6 seam
    # (`CompositorRasterFn`) becomes a formality. So RoboRender, RoboPaint, RoboLayout, RoboCSS, RoboHTML,
    # RoboWindow, RoboDisplay, RoboDesktop and RoboInput are all refused, as are Chromium, Skia and V8.
    #
    # Its only dependencies are the values it speaks in (`GfxSurface`, `GfxRect`, the one source-over blend)
    # and RoboBase/RoboMem. The suites are held to the same rule for the input scan's reason: a compositor
    # test that reached a painter would be testing two modules and proving neither.
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboCompositor"),
        (Join-Path $RuntimeRoot "Source\RoboCompositor")
    )
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "Compositor"))

    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboJS|RoboShell|RoboAPI|RoboDemo|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $consumer = '(?i)#\s*include\s*[<"]((RoboHTML|RoboCSS|RoboLayout|RoboPaint|RoboRender|RoboWindow|RoboDisplay|RoboDesktop|RoboInput|RoboText|RoboFont|RoboTypography)/)'
    # Prose that names the boundary it keeps is not a leak, exactly as in the desktop, input and typography
    # scans -- and this module's comments name RoboRender and RoboPaint a great many times on purpose.
    $exempt = '(?i)must never depend|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|no compositor|does not read|reaching for|not a renderer|rather than a renderer|RoboRender/?RoboPaint|not RoboRender'

    $rules = @(
        @{ Roots = $moduleRoots; Pattern = $consumer },
        @{ Roots = $allRoots; Pattern = $upward }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboCompositor boundary leak (Chromium, or a layer above RoboGraphics):`n$sample"
            }
        }
    }
    Write-Host "RoboCompositor boundary source scan: OK"
}

function Assert-RoboTypographyBoundaries {
    # Two rules, and the second is the one that makes this module a *binding* rather than a renderer.
    #
    # The **upward** rule is the one every module in this tree keeps: no Chromium, no Skia, no V8, no
    # compositor and nothing from the API/shell layers above. A typography system that could reach a
    # compositor would stop being the thing that says what a role means.
    #
    # The **consumer** rule is this module's own, and it is unusually wide because the module sits
    # *below* the document and drawing layers while being consumed by them: it may not include
    # RoboHTML, RoboCSS, RoboLayout, RoboPaint, RoboRender, RoboWindow or RoboDisplay. The temptation
    # the rule exists to stop is a real one -- this module *generates CSS text* for a document's
    # defaults, and the obvious next step from there is to link a CSS property table or a layout
    # style reader "just to check" the text. The text is a string, the check is a test, and the
    # module keeps its two dependencies (RoboBase/RoboMem and RoboFont).
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboTypography"),
        (Join-Path $RuntimeRoot "Source\RoboTypography")
    )
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "Typography"))

    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboDemo|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $consumer = '(?i)#\s*include\s*[<"]((RoboHTML|RoboCSS|RoboLayout|RoboPaint|RoboRender|RoboWindow|RoboDisplay)/|RoboGraphics/)'
    # Prose that names the boundary it keeps is not a leak, exactly as in the desktop and input scans.
    $exempt = '(?i)must never depend|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|no compositor|does not read|reaching for|rather than a renderer|not in the graphics'

    $rules = @(
        @{ Roots = $moduleRoots; Pattern = $consumer },
        @{ Roots = $allRoots; Pattern = $upward }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboTypography boundary leak (Chromium, a consumer layer above, or a graphics layer):`n$sample"
            }
        }
    }
    Write-Host "RoboTypography boundary source scan: OK"
}

function Assert-RoboTextBoundaries {
    # RoboText sits between the font layer and the document/drawing layers that consume it, so its rule
    # has two halves and the second is the one that keeps it a *text* module.
    #
    # The **upward** rule is the tree-wide one: no Chromium, no Skia, no V8, no compositor, nothing from
    # the shell or API layers.
    #
    # The **consumer** rule is this module's own, and it is the phase's whole point: RoboText may not
    # include RoboHTML, RoboCSS, RoboLayout, RoboPaint, RoboRender, RoboWindow, RoboDisplay,
    # RoboGraphics or RoboTypography. Every one of those is a *consumer* of text -- they ask the text
    # layer what a string measures and what glyphs it is made of -- and a text layer that could reach a
    # layout style record or a paint list would stop being the thing that decides what the answer is.
    # RoboTypography is on the list for the same reason read the other way round: it is a *binding* of
    # roles to faces that sits above text, so the arrow only ever points one way.
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboText"),
        (Join-Path $RuntimeRoot "Source\RoboText")
    )
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "Text"))

    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|RoboCompositor|RoboJS|RoboShell|RoboAPI|RoboDemo|third_party[/\\](chromium|skia))|\bnamespace\s+(blink|skia)\b)'
    $consumer = '(?i)#\s*include\s*[<"]((RoboHTML|RoboCSS|RoboLayout|RoboPaint|RoboRender|RoboWindow|RoboDisplay|RoboTypography)/|RoboGraphics/)'
    # Prose that names the boundary it keeps is not a leak, exactly as in the typography and desktop scans.
    $exempt = '(?i)must never depend|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|no compositor|does not read|reaching for|not a font parser|may \*\*not\*\*'

    $rules = @(
        @{ Roots = $moduleRoots; Pattern = $consumer },
        @{ Roots = $allRoots; Pattern = $upward }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboText boundary leak (Chromium, a consumer layer above, or a typography module):`n$sample"
            }
        }
    }
    Write-Host "RoboText boundary source scan: OK"
}

function Assert-RoboTextIntegrationRules {
    # Phase 12.24's final integration rules, as a scan rather than as prose. `Assert-RoboTextBoundaries`
    # above checks where RoboText may *include from*; this checks what the rest of the tree may do *to*
    # text, which is the half that had already been violated once.
    #
    # Each rule is a pattern and the modules allowed to match it, and every allowance is a named file or
    # module rather than a wildcard, because the value of the scan is that a *new* call site fails it. The
    # four rules are the four ways a subsystem can grow a text engine of its own:
    #
    #   one glyph selector        a `cmap` lookup outside the font layer and the text layer is a painter
    #                             selecting glyphs -- which is what `DesktopChrome.c` did before this phase,
    #                             with its own UTF-8 decoder in front of it.
    #   no shaping above text     including `RoboTextGsub.h`, `RoboTextGpos.h` and the complex-script
    #                             headers from a consumer means running OpenType passes by hand instead of
    #                             asking RoboText for a glyph run.
    #   one ink path              a glyph becomes ink through `RoboGraphics`' bridges and through the one
    #                             pen in `RoboPaintRun.c`; a third `GfxRasterDrawGlyph` loop is a third pen.
    #   a textless rasterizer     `RoboGraphics` must not know RoboText exists at all: it is handed a face,
    #                             a glyph id, a position and a size, and nothing about a script.
    #
    # The scan reads source files only, and a hit is reported with the file and the line so the fix is
    # obvious. Prose that *names* a boundary is not a leak, exactly as in the module scans above.
    $sourceRoot = Join-Path $RuntimeRoot "Source"
    $includeRoot = Join-Path $RuntimeRoot "Include"
    # The consumer modules, by name, in both halves of the tree. `RoboText` itself is deliberately absent:
    # its own headers include its own headers, and the rule is about the tree *above* it.
    $consumerIncludes = @("RoboLayout", "RoboPaint", "RoboRender", "RoboWindow", "RoboDisplay",
                         "RoboDesktop", "RoboTypography", "RoboInput") |
                        ForEach-Object { Join-Path $includeRoot $_ }
    $exempt = '(?i)(must never|never depend|does not depend|not a font parser|no second|one pen|by hand|would be a second|reaching for|instead of|before this phase|used to|replaces|the old|a third|forbidden|not allowed)'

    $rules = @(
        @{
            Name = "one glyph selector"
            Roots = @((Join-Path $sourceRoot "RoboLayout"),
                      (Join-Path $sourceRoot "RoboPaint"),
                      (Join-Path $sourceRoot "RoboRender"),
                      (Join-Path $sourceRoot "RoboDesktop"),
                      (Join-Path $sourceRoot "RoboWindow"),
                      (Join-Path $sourceRoot "RoboDisplay"),
                      (Join-Path $sourceRoot "RoboTypography"))
            Pattern = '(?i)\b(FontFaceLookupGlyph|FontCmapLookup)\s*\('
            Allow = @()
        },
        @{
            Name = "no shaping above the text layer"
            Roots = @((Join-Path $sourceRoot "RoboLayout"),
                      (Join-Path $sourceRoot "RoboPaint"),
                      (Join-Path $sourceRoot "RoboRender"),
                      (Join-Path $sourceRoot "RoboDesktop"),
                      (Join-Path $sourceRoot "RoboWindow"),
                      (Join-Path $sourceRoot "RoboDisplay"),
                      (Join-Path $sourceRoot "RoboTypography")) + $consumerIncludes
            Pattern = '#\s*include\s*[<"]RoboText/RoboText(Gsub|Gpos|OtLayout|Complex|Arabic|Indic|Glyphs|Ligature|Kerning|ScriptResolver)\.h'
            Allow = @()
        },
        @{
            Name = "one ink path for a glyph"
            Roots = @($sourceRoot)
            Pattern = '(?i)\b(GfxRasterDrawGlyph|GfxRasterDrawGlyphCoverage)\s*\('
            Allow = @((Join-Path $sourceRoot "RoboGraphics"),
                      (Join-Path $sourceRoot "RoboPaint\RoboPaintRun.c"))
        },
        @{
            Name = "a rasterizer that knows no text"
            Roots = @((Join-Path $sourceRoot "RoboGraphics"), (Join-Path $includeRoot "RoboGraphics"))
            Pattern = '(?i)#\s*include\s*[<"]RoboText/'
            Allow = @()
        }
    )

    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $files = if ((Get-Item -LiteralPath $root).PSIsContainer) {
                Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h
            } else {
                Get-Item -LiteralPath $root
            }
            # An allowance is a *file* or a *directory*, and the two are matched differently so that a
            # directory covers the module it names and nothing that merely begins with its name.
            $allowFiles = @()
            $allowDirs = @()
            foreach ($entry in $rule.Allow) {
                if ((Test-Path -LiteralPath $entry -PathType Container)) {
                    $allowDirs += ($entry.ToLowerInvariant() + [IO.Path]::DirectorySeparatorChar)
                } else {
                    $allowFiles += $entry.ToLowerInvariant()
                }
            }
            $hits = $files |
                Where-Object {
                    $path = $_.FullName.ToLowerInvariant()
                    $allowed = $allowFiles -contains $path
                    if (-not $allowed) {
                        foreach ($dir in $allowDirs) { if ($path.StartsWith($dir)) { $allowed = $true; break } }
                    }
                    return -not $allowed
                } |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboText integration rule '$($rule.Name)' is violated:$sample"
            }
        }
    }
    Write-Host "RoboText integration rule scan: OK (four rules)"
}

function Assert-RoboJSBoundaries {
    # Phase 16's rules, as a scan rather than as prose, because the phase's claim is an *ownership*
    # claim and every one of the three is a rule a well-meaning change would break.
    #
    #   one runtime     RoboJS includes RoboBase, RoboMem and the C standard library and nothing else.
    #                   Nothing above it -- the shell, a window, a display, a renderer, a painter,
    #                   layout, CSS, the text layer, typography, input, the compositor, the font layer
    #                   or the rasterizer -- may appear in a RoboJS source. A runtime that could reach
    #                   a paint list or a layout style record would stop being the thing that decides
    #                   what a script means, and one that reached *up* for a host capability would be
    #                   building the binding this module exists to own.
    #   one DOM seam    `RoboHTML/` may appear in exactly one production file, `RoboJSDomHtml.c`, plus
    #                   the integration suite that exercises the adapter by name. `RoboJSDom.h` names
    #                   no HTML type at all, which is what makes the adapter replaceable rather than
    #                   load-bearing.
    #   no engine       the tree-wide rule with this module's own nouns added: no Chromium, no Skia,
    #                   no V8 and no other embedded interpreter (QuickJS, Duktape, MuJS, JerryScript)
    #                   as a build or link dependency, in the module, its suites or its documents.
    #
    # Prose that names a boundary it keeps is not a leak, exactly as in the text and typography scans.
    $moduleRoots = @(
        (Join-Path $RuntimeRoot "Include\RoboJS"),
        (Join-Path $RuntimeRoot "Source\RoboJS")
    )
    $allRoots = $moduleRoots + @((Join-Path $TestsRoot "RoboJS"), (Join-Path $RepoRoot "Documentation\RoboJS"))

    $upward = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|v8|quickjs|duktape|mujs|jerryscript|RoboCompositor|RoboShell|RoboAPI|RoboDemo|third_party[/\\](chromium|skia|quickjs))|\bnamespace\s+(blink|skia|v8)\b)'
    $consumer = '(?i)#\s*include\s*[<"]((RoboDesktop|RoboWindow|RoboDisplay|RoboRender|RoboPaint|RoboLayout|RoboCSS|RoboText|RoboTypography|RoboInput|RoboGraphics|RoboFont)/)'
    $dom = '(?i)#\s*include\s*[<"]RoboHTML/'
    # The adapter and the suite that proves it, by name rather than by directory, for the usual reason:
    # a *second* module file reaching for RoboHTML is still a leak.
    $domSkip = @('RoboJSDomHtml', 'SuiteRoboJSIntegration')
    $exempt = '(?i)must never depend|never depend|no Chromium|without Chromium|independence|Chromium reference|not a dependency|does not depend on|no compositor|does not read|reaching for|the only file|the one file'

    $rules = @(
        @{ Roots = $moduleRoots; Pattern = $consumer; Skip = @() },
        @{ Roots = $allRoots; Pattern = $upward; Skip = @() },
        @{ Roots = $allRoots; Pattern = $dom; Skip = $domSkip }
    )
    foreach ($rule in $rules) {
        foreach ($root in $rule.Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $files = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md
            if ($rule.Skip.Count -gt 0) {
                $files = $files | Where-Object {
                    $name = $_.BaseName
                    foreach ($skip in $rule.Skip) { if ($name -eq $skip) { return $false } }
                    return $true
                }
            }
            $hits = $files |
                Select-String -Pattern $rule.Pattern -SimpleMatch:$false |
                Where-Object { $_.Line -notmatch $exempt }
            if ($hits) {
                $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
                throw "RoboJS boundary leak (a second engine, a consumer layer above, or a second RoboHTML include):`n$sample"
            }
        }
    }
    Write-Host "RoboJS boundary source scan: OK (three rules)"
}

function Assert-RoboDisplayChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboDisplay"),
        (Join-Path $RuntimeRoot "Source\RoboDisplay"),
        (Join-Path $RuntimeRoot "Display"),
        (Join-Path $TestsRoot "Display")
    )
    $pat = '(?i)(#\s*include\s*[<"][^>"]*(chromium|skia|vulkan|GL/gl|d3d1[12]|Metal/Metal|windows\.h|X11/|wayland|AppKit|Cocoa|SDL|GLFW|Qt)|third_party[/\\](chromium|skia)|\bnamespace\s+(blink|skia|ui|display)\b)'
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hits = Get-ChildItem -LiteralPath $root -Recurse -File -Include *.c,*.h,*.md |
            Select-String -Pattern $pat -SimpleMatch:$false |
            Where-Object {
                $_.Path -notmatch '(?i)[/\\]platform[/\\]win32[/\\]' -and
                $_.Path -notmatch '(?i)[/\\]display[/\\]tests[/\\]' -and
                $_.Line -notmatch '(?i)must never depend|Forbidden:|temporary technical reference|never depend|no Chromium|without Chromium|independence|Chromium is only|Chromium reference|do not include|must not include|Win32/X11|not a dependency|ui/display|QEMU|SYS_DISPLAY|GOP|robosys|platform/win32'
            }
        if ($hits) {
            $sample = ($hits | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
            throw "RoboDisplay Chromium/OS-toolkit dependency leaked into sources:`n$sample"
        }
    }
    Write-Host "RoboDisplay Chromium-independence source scan: OK"
}

function Test-ElfFile([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 5 -or $bytes[0] -ne 0x7F -or $bytes[1] -ne [byte][char]'E' -or
        $bytes[2] -ne [byte][char]'L' -or $bytes[3] -ne [byte][char]'F') {
        throw "Not ELF: $Path"
    }
    $ro = Get-LlvmReadobj
    if ($ro) {
        $fmt = & $ro "--file-header" $Path 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $fmt -notmatch "Format:\s*elf64") {
            throw "Expected elf64: $Path`n$fmt"
        }
        $needed = & $ro "--needed-libs" $Path 2>&1 | Out-String
        if ($needed -match "libc\.so" -or $needed -match "ld-linux" -or $needed -match "libgcc_s") {
            throw "Host libc leakage in $Path`n$needed"
        }
    }
}

function Get-RoboObjectPath([string]$Src, [string]$ObjDir) {
    $rel = $Src
    if ($Src.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $Src.Substring($RepoRoot.Length).TrimStart("\", "/")
    }
    $safe = (($rel -replace '[\\/:]', '_') -replace '\.c$', '.o')
    return (Join-Path $ObjDir $safe)
}

function Get-RoboHeaderStamp {
    $roots = @($IncludeDir, $FrameworkDir, $TestsRoot)
    $latest = [datetime]::MinValue
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        Get-ChildItem -LiteralPath $root -Recurse -File -Include *.h -ErrorAction SilentlyContinue |
            ForEach-Object {
                if ($_.LastWriteTimeUtc -gt $latest) { $latest = $_.LastWriteTimeUtc }
            }
    }
    return $latest
}

function Test-RoboCompileNeeded([string]$Src, [string]$Obj, [datetime]$HeaderStamp) {
    return (Test-RoboObjectStale -Src $Src -Obj $Obj -HeaderStamp $HeaderStamp -Force:$Rebuild)
}

function Invoke-RoboCompileObject {
    param(
        [string]$Clang,
        [string[]]$Flags,
        [string]$Src,
        [string]$Obj
    )
    $objParent = Split-Path $Obj -Parent
    if (-not (Test-Path -LiteralPath $objParent)) {
        New-Item -ItemType Directory -Force -Path $objParent | Out-Null
    }
    if (-not (Test-RoboCompileNeeded $Src $Obj $script:RoboHeaderStamp)) {
        Write-RoboBuildProgress -Skipped -Src $Src
        return $false
    }
    $dep = Get-RoboDepPath $Obj
    if (-not $script:RoboBuildProgress) {
        Write-Host "CC     $Src"
    }
    & $Clang @Flags -MMD -MP -MF $dep -c $Src -o $Obj
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $Src" }
    Write-RoboBuildProgress -Compiled -Src $Src
    return $true
}

function Invoke-RoboCompileSources {
    param(
        [string]$Label,
        [string[]]$Sources,
        [string]$ObjDir,
        [string]$Clang,
        [string[]]$Flags
    )
    New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
    $objs = @()
    $stale = 0
    foreach ($src in $Sources) {
        $obj = Get-RoboObjectPath $src $ObjDir
        if (Test-RoboCompileNeeded $src $obj $script:RoboHeaderStamp) { $stale++ }
        $objs += $obj
    }
    Initialize-RoboBuildProgress -Total $Sources.Count -ToCompile $stale -Cached ($Sources.Count - $stale) -Label $Label
    $anyCompiled = $false
    $i = 0
    foreach ($src in $Sources) {
        $obj = $objs[$i]
        $i++
        if (Invoke-RoboCompileObject -Clang $Clang -Flags $Flags -Src $src -Obj $obj) {
            $anyCompiled = $true
        }
    }
    Complete-RoboBuildProgress
    return @{ Objects = $objs; Compiled = $anyCompiled }
}

if ($Module -and -not $Filter) {
    $moduleFilter = @{
        Core = "base."; Memory = "mem."; Containers = "array."; Unicode = "unicode."; I18n = "i18n."
        Font = "font."; Typography = "typography."; Text = "text."; Graphics = "graphics."; Display = "display."; Window = "window."
        Input = "input."; Html = "html."; Css = "css."; Layout = "layout."
        Paint = "paint."; Rasterizer = "render."; Desktop = "desktop."
    }
    if ($moduleFilter.ContainsKey($Module)) { $Filter = $moduleFilter[$Module] }
}

if ($Module -and $Module -ne "Runtime") {
    $suiteNeedles = @{
        Core        = @("SuiteRoboBase.c", "SuiteRoboLog.c", "SuiteRoboError.c", "SuiteRoboDebug.c")
        Memory      = @("SuiteRoboMem", "SuiteRoboArena", "SuiteRoboPool")
        Containers  = @("SuiteRoboArray", "SuiteRoboString")
        Unicode     = @("SuiteRoboUnicode")
        I18n        = @("SuiteRoboI18N")
        Font        = @("\Font\", "/Font/", "SuiteRoboFont.c")
        Typography  = @("\Typography\", "/Typography/")
        Text        = @("\Text\", "/Text/")
        Graphics    = @("\Graphics\", "/Graphics/")
        Display     = @("\Display\", "/Display/")
        Window      = @("\Window\", "/Window/")
        Compositor  = @("\Compositor\", "/Compositor/")
        Input       = @("\Input\", "/Input/")
        Html        = @("\Html\", "/Html/")
        Css         = @("\Css\", "/Css/", "SuiteRoboCssDomSelect")
        Layout      = @("\Layout\", "/Layout/")
        Paint       = @("\Paint\", "/Paint/")
        Rasterizer  = @("\Rasterizer\", "/Rasterizer/")
        Desktop     = @("\Desktop\", "/Desktop/")
        RoboJS      = @("\RoboJS\", "/RoboJS/")
    }
    $needles = $suiteNeedles[$Module]
    if ($needles) {
        $SuiteSrcs = @($SuiteSrcs | Where-Object {
            $src = $_
            foreach ($needle in $needles) {
                if ($src -like ("*" + $needle + "*")) { return $true }
            }
            return $false
        })
        Write-Host "MODULE $Module tests ($($SuiteSrcs.Count) suites)"
    }
}

if ($Clean) {
    if (Test-Path $OutDir) {
        Remove-Item -Recurse -Force $OutDir
        Write-Host "Removed $OutDir"
    } else {
        Write-Host "Nothing to clean."
    }
    exit 0
}

$required = @($IncludeDir) + $ProdSrcs
if ($Test -or $BuildTests) {
    $required += $SuiteSrcs + @($TestFrameworkSrc, $TestMainSrc)
}
if (-not $SkipGuest) {
    $required += @($MainSrc, $LibcInc, $SdkInc, $LibcA, $Crt0, $Crti, $Crtn, $LdScript)
} else {
    $required += @($LibcInc, $SdkInc)
}
foreach ($req in $required) {
    if (-not (Test-Path $req)) { throw "Missing required input: $req" }
}

New-Item -ItemType Directory -Force -Path $LibDir | Out-Null

$clang = Get-Clang
$ld = $null
if (-not $SkipGuest) { $ld = Get-LdLld }
$commonDefines = @(
    "-DRoboDebug=$RoboDebugDefine",
    "-DRoboMemDebug=$RoboMemDebugDefine"
)

$guestFlags = @(
    "-target", "x86_64-unknown-none",
    "-std=c11"
) + $OptFlags + $commonDefines + @(
    "-ffreestanding", "-nostdlib", "-static",
    "-fno-pic", "-fno-pie", "-fno-stack-protector",
    "-fno-asynchronous-unwind-tables",
    "-mcmodel=large",
    "-mno-red-zone", "-fno-builtin",
    "-Wall", "-Wextra", "-Werror",
    "-isystem", $LibcInc,
    "-I", $LibcInc,
    "-I", $SdkInc,
    "-I", $IncludeDir
)

$hostFlags = @(
    "-std=c11"
) + $OptFlags + $commonDefines + @(
    "-Wall", "-Wextra", "-Werror",
    "-I", $IncludeDir,
    "-I", $FrameworkDir,
    # SuiteRoboCssDomSelect.c lives in Tests/Suites because it is the one suite
    # that must see RoboCSS and RoboHTML together, and it shares css/tests'
    # fixture header. A suite in its own directory resolves that relatively; a
    # suite outside one needs the path here.
    "-I", (Join-Path $TestsRoot "Css"),
    # layout/tests has the same shape as css/tests: its own fixture header, and
    # suites that resolve it relatively. The suites live there because RoboLayout
    # is the first module allowed to see both RoboHTML and RoboCSS, so its
    # integration suites belong beside it rather than in Tests/Suites.
    "-I", (Join-Path $TestsRoot "Layout"),
    # paint/tests has the same shape: its own fixture header, and suites that resolve it
    # relatively. RoboPaint's suites live there because the module reads a layout tree, so
    # its fixtures belong beside the layer they build on.
    "-I", (Join-Path $TestsRoot "Paint"),
    # desktop/tests reaches the render fixture, because a desktop case that has to prove a window's
    # contents reached the screen needs the whole chain -- markup, style, layout, records, display
    # list, a synthetic face -- and `render/tests/RoboRenderTestSupport.h` already owns exactly that.
    # Its suites resolve both headers, which is why both paths are here.
    "-I", (Join-Path $TestsRoot "Rasterizer"),
    "-I", (Join-Path $TestsRoot "Desktop"),
    # input/tests carries the input suites' fixtures, which build events through the public doors rather
    # than by filling structs -- so a suite cannot construct something the model's own validator refuses.
    "-I", (Join-Path $TestsRoot "Input"),
    # html/tests carries the HTML suites' document of record (`dom_checkpoint_html`), which the CSS suite in
    # Tests/Suites shares: its selector repertoire and element counts name this document's own elements, so
    # both suites must read one copy of it rather than two that drift.
    "-I", (Join-Path $TestsRoot "Html"),
    # typography/tests carries its fixture header, which opens the *shipped* system typeface pack through
    # a host IO adapter so the same pack, discovery and selection code the guest runs is what the suites
    # exercise -- rather than a synthetic stand-in that could pass while the real pack was broken.
    "-I", (Join-Path $TestsRoot "Typography"),
    # text/tests carries its fixture header, which opens the *shipped* system typeface pack through the
    # pack's own host IO adapter -- the same face, discovery and selection the guest runs, so a suite
    # that passes cannot be passing against a synthetic stand-in for a pack that is broken.
    "-I", (Join-Path $TestsRoot "Text"),
    "-I", (Join-Path $SourceDir "RoboWindow\platform\win32"),
    "-I", (Join-Path $SourceDir "RoboDisplay\platform\win32"),
    "-D_CRT_SECURE_NO_WARNINGS",
    "-DRoboWindowHasWin32=1"
)

$BidiConfPath = (Join-Path $TestsDir "Data\robo_bidi_character_conformance.bin") -replace "\\", "/"
$BidiConfHeader = Join-Path $OutDir "robo_bidi_conf_path.h"
$BidiConfBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_BIDI_CONF_PATH_H
#define ROBO_BIDI_CONF_PATH_H
#define RoboBidiConfPath "$BidiConfPath"
#endif
"@
Set-Content -Path $BidiConfHeader -Value $BidiConfBody -Encoding ascii
$hostFlags += "-I", $OutDir
$hostFlags += "-DRoboBidiConfPathFromBuild=1"

$ScriptConfPath = (Join-Path $TestsDir "Data\robo_script_conformance.bin") -replace "\\", "/"
$ScriptConfHeader = Join-Path $OutDir "robo_script_conf_path.h"
$ScriptConfBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_SCRIPT_CONF_PATH_H
#define ROBO_SCRIPT_CONF_PATH_H
#define RoboScriptConfPath "$ScriptConfPath"
#endif
"@
Set-Content -Path $ScriptConfHeader -Value $ScriptConfBody -Encoding ascii
$hostFlags += "-DRoboScriptConfPathFromBuild=1"

$EmojiConfPath = (Join-Path $TestsDir "Data\robo_emoji_conformance.bin") -replace "\\", "/"
$EmojiConfHeader = Join-Path $OutDir "robo_emoji_conf_path.h"
$EmojiConfBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_EMOJI_CONF_PATH_H
#define ROBO_EMOJI_CONF_PATH_H
#define RoboEmojiConfPath "$EmojiConfPath"
#endif
"@
Set-Content -Path $EmojiConfHeader -Value $EmojiConfBody -Encoding ascii
$hostFlags += "-DRoboEmojiConfPathFromBuild=1"

$PluralSamplesPath = (Join-Path $TestsDir "Data\robo_plural_samples.txt") -replace "\\", "/"
$PluralSamplesHeader = Join-Path $OutDir "robo_plural_samples_path.h"
$PluralSamplesBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_PLURAL_SAMPLES_PATH_H
#define ROBO_PLURAL_SAMPLES_PATH_H
#define RoboPluralSamplesPath "$PluralSamplesPath"
#endif
"@
Set-Content -Path $PluralSamplesHeader -Value $PluralSamplesBody -Encoding ascii
$hostFlags += "-DRoboPluralSamplesPathFromBuild=1"

$CollationTestPath = (Join-Path $RuntimeRoot "Tools\i18n\source\uca-16.0.0\CollationTest\CollationTest\CollationTest_NON_IGNORABLE_SHORT.txt") -replace "\\", "/"
$CollationTestHeader = Join-Path $OutDir "robo_collation_test_path.h"
$CollationTestBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_COLLATION_TEST_PATH_H
#define ROBO_COLLATION_TEST_PATH_H
#define RoboCollationTestPath "$CollationTestPath"
#endif
"@
Set-Content -Path $CollationTestHeader -Value $CollationTestBody -Encoding ascii
$hostFlags += "-DRoboCollationTestPathFromBuild=1"

# A suite that has to read the *source tree* (the aggregate-header hygiene scan is the one case: it checks
# that `RoboText.h` names every public header, which needs the header on disk and not the include the C
# compiler already followed) cannot use a path relative to the process working directory, because the two
# ways this binary is run do not share one: `TestRoboText.ps1` starts it from the repository root, while
# `TestRuntime.ps1` starts it from `Build/Runtime`, where a run's own artifacts belong. So the build states
# its own root the way it states every other absolute path, and the suite reads it from here.
$RepoRootPath = $RepoRoot -replace "\\", "/"
$RepoRootHeader = Join-Path $OutDir "robo_repo_root.h"
$RepoRootBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_REPO_ROOT_H
#define ROBO_REPO_ROOT_H
#define RoboRepoRoot "$RepoRootPath"
#endif
"@
Set-Content -Path $RepoRootHeader -Value $RepoRootBody -Encoding ascii
$hostFlags += "-DRoboRepoRootFromBuild=1"

# The checkpoint's face, as an absolute path the *build* knows. The launcher reads it from here rather
# than from a relative path so a run from any working directory finds the same bytes; the guest program
# reads the same face out of the OS image at /System/Fonts/UI/Inter.ttf, installed by the image build
# from the system typeface pack (Phase 12.2, `System/Fonts/`).
$FontAssetPath = (Join-Path $RepoRoot "System\Fonts\UI\Inter.ttf") -replace "\\", "/"
$SystemFontRoot = (Join-Path $RepoRoot "System\Fonts") -replace "\\", "/"
$FontAssetHeader = Join-Path $OutDir "robo_font_asset_path.h"
$FontAssetBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_FONT_ASSET_PATH_H
#define ROBO_FONT_ASSET_PATH_H
#define RoboFontAssetPath "$FontAssetPath"
#endif
"@
Set-Content -Path $FontAssetHeader -Value $FontAssetBody -Encoding ascii
$SystemFontRootHeader = Join-Path $OutDir "robo_system_font_root.h"
$SystemFontRootBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_SYSTEM_FONT_ROOT_H
#define ROBO_SYSTEM_FONT_ROOT_H
#define RoboSystemFontRoot "$SystemFontRoot"
#endif
"@
Set-Content -Path $SystemFontRootHeader -Value $SystemFontRootBody -Encoding ascii
$hostFlags += "-DRoboFontAssetPathFromBuild=1"
$hostFlags += "-DRoboSystemFontRootFromBuild=1"

$asanEnabled = $false
$ubsanEnabled = $false
if ($Sanitize) {
    Write-Host "SANITIZE probing AddressSanitizer / UndefinedBehaviorSanitizer..."
    $probe = Join-Path $OutDir "asan_probe.exe"
    $probeSrc = Join-Path $OutDir "asan_probe.c"
    Set-Content -Path $probeSrc -Value "int main(void){return 0;}" -Encoding ascii

    & $clang -std=c11 -fsanitize=address -o $probe $probeSrc 2>$null
    if ($LASTEXITCODE -eq 0 -and (Test-Path $probe)) {
        $hostFlags += "-fsanitize=address"
        $asanEnabled = $true
        Write-Host "SANITIZE AddressSanitizer enabled for host tests"
    } else {
        Write-Host "SANITIZE AddressSanitizer not available on this toolchain; skipping"
    }

    $probeUb = Join-Path $OutDir "ubsan_probe.exe"
    & $clang -std=c11 -fsanitize=undefined -o $probeUb $probeSrc 2>$null
    if ($LASTEXITCODE -eq 0 -and (Test-Path $probeUb)) {
        $hostFlags += "-fsanitize=undefined"
        $ubsanEnabled = $true
        Write-Host "SANITIZE UndefinedBehaviorSanitizer enabled for host tests"
    } else {
        Write-Host "SANITIZE UndefinedBehaviorSanitizer not available on this toolchain; skipping"
    }
}

Assert-NoChromiumPath ($ProdSrcs + $DesktopSceneSrcs + $SuiteSrcs + @($MainSrc, $IncludeDir) + $guestFlags)

Write-Host "CC     $clang"
Write-Host "LD     $ld"
Write-Host "CONFIG $Configuration (RoboDebug=$RoboDebugDefine RoboMemDebug=$RoboMemDebugDefine)"

$script:RoboHeaderStamp = Get-RoboHeaderStamp
$guestObjDir = Join-Path $OutDir "obj\guest"

# --- Production guest library objects ---
$guestSrcs = $ProdSrcs
if ($Module -and $Module -ne "Runtime") {
    $prefixMap = @{
        Core = @("RoboBase")
        Memory = @("RoboBase", "RoboMem")
        Containers = @("RoboBase", "RoboMem", "RoboContainers")
        Unicode = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N")
        I18n = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N")
        Font = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont")
        Typography = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboTypography")
        Text = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboText")
        Graphics = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboGraphics")
        # RoboDisplay itself does not reach RoboWindow (the direction is the other way), but its suite set does:
        # `SuiteRoboDisplayWindow.c` creates a window against a display, and the Win32 backend this module's
        # host extras compile (`Window*`) links the same way -- so the row names RoboWindow for the *test* half,
        # exactly as the `Input` and `Css` rows do for theirs. No production RoboDisplay source gains a
        # dependency.
        Display = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboGraphics", "RoboDisplay", "RoboWindow")
        Window = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboGraphics", "RoboDisplay", "RoboWindow")
        # RoboCompositor speaks in `GfxSurface`/`GfxRect` and the one source-over blend, so it sits on
        # RoboGraphics and nothing higher: no document, no paint, no shell.
        Compositor = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboGraphics", "RoboCompositor")
        # RoboInput sits *below* the layers that consume it but not below the ones it speaks in: a
        # position is a `GfxPoint` (RoboGraphics), a key is a `PhysicalKey` and the modifier bits are
        # RoboWindow's (RoboWindowKey.h), a display is a `DisplayId` (RoboDisplay), and the one direction
        # the module travels is a translation of a `WindowEvent` (RoboWindow). The earlier list named
        # only the two *identity* headers it carries from the document layers and none of the three
        # modules those identities' siblings already needed, so `-Module Input` never linked.
        Input = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboText", "RoboGraphics", "RoboDisplay", "RoboWindow", "RoboHTML", "RoboCSS", "RoboLayout", "RoboInput")
        # `RoboHTMLImage.c` decodes through RoboGraphics (`GfxImageDecode`, `GfxImageSurface`, ...), so the
        # module sits on RoboFont/RoboGraphics and not merely on the base layers. `SuiteRoboHTMLImage.c`
        # calls the same doors, so the *test* build needs them too -- which is why they are in this list
        # rather than implied by the whole-tree link.
        Html = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboGraphics", "RoboHTML")
        # RoboCSS itself sits on the base layers and nothing above them, so the *module* could be listed as
        # base + RoboCSS. Its suite set, though, includes `SuiteRoboCssDomSelect.c` -- the one suite kept in
        # `Tests/Suites` precisely because it must see RoboCSS and RoboHTML together -- and a *link* needs the
        # objects it names, not merely their headers. So the row names the whole chain that suite walks
        # (HTML, layout, paint, the rasterizer), exactly as the `Input` row does for its own suite's needs. The
        # dependency direction is unchanged: no production RoboCSS source reaches any of them.
        Css = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboText", "RoboGraphics", "RoboHTML", "RoboCSS", "RoboLayout", "RoboPaint", "RoboRender")
        Layout = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboText", "RoboGraphics", "RoboHTML", "RoboCSS", "RoboLayout")
        Paint = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboText", "RoboGraphics", "RoboHTML", "RoboCSS", "RoboLayout", "RoboPaint")
        Rasterizer = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboText", "RoboGraphics", "RoboHTML", "RoboCSS", "RoboLayout", "RoboPaint", "RoboRender")
        Desktop = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboTypography", "RoboText", "RoboGraphics", "RoboDisplay", "RoboWindow", "RoboHTML", "RoboCSS", "RoboLayout", "RoboPaint", "RoboRender", "RoboCompositor", "RoboDesktop", "RoboInput", "RoboJS")
        # RoboJS is an independent JavaScript runtime: it speaks in its own value model and its own
        # heap, and it reads a DOM only through `RoboJSDom.c`'s vtable -- `RoboJSDomHtml.c` is the one
        # file that includes a RoboHTML header. So the list is the base layers plus RoboHTML, and
        # nothing above either.
        #
        # RoboHTML drags RoboFont/RoboGraphics (its image path) and RoboI18N, and a *link* needs those
        # objects present rather than merely declared, so they are named here for the same reason the
        # `Html` row names them. The dependency direction is unchanged: RoboJS still reaches nothing
        # above RoboHTML, and `RoboJSDomHtml.c` remains the only file that knows RoboHTML exists.
        RoboJS = @("RoboBase", "RoboMem", "RoboContainers", "RoboUnicode", "RoboI18N", "RoboFont", "RoboGraphics", "RoboHTML", "RoboJS")
    }
    $want = $prefixMap[$Module]
    if ($want) {
        $guestSrcs = @($ProdSrcs | Where-Object {
            $src = $_
            foreach ($pre in $want) {
                if ($src -like "*\$pre\*" -or $src -like "*/$pre/*") { return $true }
            }
            return $false
        })
        Write-Host "MODULE $Module ($($guestSrcs.Count) production sources)"
    }
}

$libObjs = @()
$guestCompile = Invoke-RoboCompileSources -Label "guest" -Sources $guestSrcs -ObjDir $guestObjDir `
    -Clang $clang -Flags $guestFlags
$libObjs = @($guestCompile.Objects)

$ar = Get-LlvmAr
$libA = Join-Path $LibDir "librobobase.a"
if ($Module -and $Module -ne "Runtime") {
    $libA = Join-Path $LibDir ("libRobo" + $Module + ".a")
}
if ($ar -and $libObjs.Count -gt 0) {
    if (Test-Path $libA) { Remove-Item $libA -Force }
    Write-Host "AR     $libA"
    #
    # The object list goes through a **response file**, and it has to. A part whose dependency list
    # names most of the tree (`-Module Desktop` names eighteen prefixes, so its archive is hundreds of
    # objects) produced a command line past what Windows can hand a process: `llvm-ar` failed with
    # "The filename or extension is too long" before it read a single object, which reads as a toolchain
    # failure rather than as the build script's own limit. `llvm-ar` accepts `@file` exactly as the
    # linker does, so the list is written once and passed by name -- one line, and a part's archive stops
    # depending on how many modules it happens to depend on.
    #
    $arResponse = Join-Path $OutDir ("ar-" + (Split-Path $libA -Leaf) + ".rsp")
    Set-Content -LiteralPath $arResponse -Value $libObjs -Encoding ASCII
    & $ar "rcs" $libA ("@" + $arResponse)
    if ($LASTEXITCODE -ne 0) { throw "Archive failed" }
    Write-Host "Build OK: $libA"
}

if (-not $SkipGuest) {
    $mainObj = Get-RoboObjectPath $MainSrc $guestObjDir
    [void](Invoke-RoboCompileObject -Clang $clang -Flags $guestFlags -Src $MainSrc -Obj $mainObj)

    $elfStale = $Rebuild -or -not (Test-Path $RuntimeElf)
    if (-not $elfStale) {
        $elfTime = (Get-Item $RuntimeElf).LastWriteTimeUtc
        foreach ($o in (@($Crti, $Crt0) + $libObjs + @($mainObj, $LibcA, $Crtn))) {
            if ((Test-Path $o) -and ((Get-Item $o).LastWriteTimeUtc -gt $elfTime)) { $elfStale = $true }
        }
    }
    if ($elfStale) {
        Write-Host "LD     $RuntimeElf"
        $linkobjs = @($Crti, $Crt0) + $libObjs + @($mainObj, $LibcA, $Crtn)
        #
        # The guest's object list goes through a **response file** too, for the reason the archive above
        # does and one more: this link names every module the kernel and the userland pull in, and on
        # Windows the command line reached the OS limit before the linker read a single object --
        # `ld.lld.exe` failed to start with "The filename or extension is too long", which reads as a
        # toolchain failure rather than as this script's own limit. Each path is written on its own
        # line, quoted, because a linker's response file may hold one argument per line and an
        # unquoted path with a space would be two.
        #
        $ldResponse = Join-Path $OutDir "ld-guest.rsp"
        Set-Content -LiteralPath $ldResponse -Encoding ASCII -Value ($linkobjs | ForEach-Object { '"' + $_ + '"' })
        & $ld "-m" "elf_x86_64" "-static" "-no-pie" "-T" $LdScript "-o" $RuntimeElf ("@" + $ldResponse)
        if ($LASTEXITCODE -ne 0) { throw "Link failed: Runtime ELF" }
    } else {
        Write-Host "SKIP   $RuntimeElf"
    }

    Write-Host "Build OK: $RuntimeElf (production)"
    Test-ElfFile $RuntimeElf
    Write-Host "OK   guest ELF integrity"
    Assert-NoChromiumPath @($RuntimeElf)
    Write-Host "OK   no Chromium paths"
} else {
    Write-Host "GUEST  ELF skipped (-SkipGuest); module objects were compiled"
}

if ($Verify) {
    Assert-RoboFontChromiumIndependence
    Assert-RoboGraphicsChromiumIndependence
    Assert-RoboWindowChromiumIndependence
    Assert-RoboDisplayChromiumIndependence
    Assert-RoboHTMLChromiumIndependence
    Assert-RoboCSSChromiumIndependence
    Assert-RoboLayoutBoundaries
    Assert-RoboPaintBoundaries
    Assert-RoboRenderBoundaries
    Assert-RoboDesktopBoundaries
    Assert-RoboInputBoundaries
    Assert-RoboCompositorBoundaries
    Assert-RoboTypographyBoundaries
    Assert-RoboTextBoundaries
    Assert-RoboTextIntegrationRules
    Assert-RoboJSBoundaries
    Write-Host "VERIFY PASS: production runtime ($Configuration)"
}

# --- Host test executable (framework + suites; does not ship in production) ---
#
# The suite is the *test* half of this script: it needs a desktop session for
# the interactive window checks and it is not part of producing the runtime.
# Gating it on -Test (which this script's own usage header has always
# documented, and which every test caller already passes) lets the pipeline's
# build step build without running tests, while test steps run everything here.
if (-not $Test -and -not $BuildTests) {
    Write-Host "BUILD OK: production runtime ($Configuration) - host suite skipped (pass -Test to build and run it)"
    exit 0
}

Write-Host "HOST   building $HostTest"
$Win32BackendSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowBackendWin32.c"
$Win32GpuPresentSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowPresentWin32Gpu.c"
$Win32KeyboardSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowKeyboardWin32.c"
$Win32MouseSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowMouseWin32.c"
$Win32DisplaySrc = Join-Path $SourceDir "RoboDisplay\platform\win32\RoboDisplayBackendWin32.c"
$hostProd = $ProdSrcs
if ($Module -and $Module -ne "Runtime") {
    $hostProd = $guestSrcs
}
$hostExtras = @($TestFrameworkSrc, $TestMainSrc)
if (-not $Module -or $Module -eq "Runtime" -or $Module -eq "Desktop") {
    $hostExtras = $DesktopSceneSrcs + $hostExtras
}
if (-not $Module -or $Module -eq "Runtime" -or $Module -eq "Desktop" -or $Module -eq "Window" -or $Module -eq "Display") {
    $hostExtras += @($Win32BackendSrc, $Win32GpuPresentSrc, $Win32KeyboardSrc, $Win32MouseSrc, $Win32DisplaySrc)
}
if ($Module -and $Module -ne "Runtime") {
    $declared = @()
    Select-String -LiteralPath $TestMainSrc -Pattern '^void (TestRegister\w+)\(void\);' | ForEach-Object {
        $declared += $_.Matches[0].Groups[1].Value
    }
    $provided = @{}
    foreach ($suite in $SuiteSrcs) {
        Select-String -LiteralPath $suite -Pattern '^void (TestRegister\w+)\(void\)' | ForEach-Object {
            $provided[$_.Matches[0].Groups[1].Value] = $true
        }
    }
    $stubLines = @("/* Generated: empty registrars for suites this module does not compile. */", "#include `"RoboTest.h`"", "")
    foreach ($name in $declared) {
        if (-not $provided.ContainsKey($name)) {
            $stubLines += "void $name(void) { }", ""
        }
    }
    #
    #
    # One non-registrar stub, and it is here for the same reason the registrars are: the host
    # launcher's `main` names the desktop checkpoint's entry point (`DesktopHostMain`, the
    # `--desktop` mode), which lives in the desktop *scene* sources a single-module build
    # deliberately does not compile. Without this line every `-Module` build of the host suite
    # fails to link at that one symbol, which is why the per-module test scripts could not run
    # from a clean tree. The checkpoint stays unavailable in a module build, exactly as it
    # should: exit code 2 is the launcher's own "usage" answer.
    #
    # The guard is the same one `$hostExtras` uses to decide whether the scene sources are in
    # this link: `-Module Desktop` compiles them, so stubbing the door there would be a second
    # definition rather than a missing one.
    #
    if ($Module -ne "Desktop") {
        $stubLines += "/* The desktop checkpoint, absent from a single-module build. */", ""
        $stubLines += "int DesktopHostMain(int argc, char **argv) { (void)argc; (void)argv; return 2; }", ""
    }
    $stubSrc = Join-Path $OutDir "RoboTestModuleStubs.c"
    $stubText = ($stubLines -join "`n") + "`n"
    $writeStub = $true
    if (Test-Path -LiteralPath $stubSrc) {
        if ((Get-Content -LiteralPath $stubSrc -Raw) -eq $stubText) { $writeStub = $false }
    }
    if ($writeStub) {
        Set-Content -LiteralPath $stubSrc -Value $stubText -Encoding ascii
    }
    $hostExtras += $stubSrc
    Write-Host "MODULE stubs ($($declared.Count - $provided.Count) empty registrars)"
}
$hostSrcs = @($hostProd) + $hostExtras + $SuiteSrcs

$hostObjDir = Join-Path $OutDir "obj\host"
if (-not $script:RoboHeaderStamp) { $script:RoboHeaderStamp = Get-RoboHeaderStamp }
$hostCompile = Invoke-RoboCompileSources -Label "host" -Sources $hostSrcs -ObjDir $hostObjDir `
    -Clang $clang -Flags $hostFlags
$hostObjs = @($hostCompile.Objects)
$hostCompiled = [bool]$hostCompile.Compiled

$linkStale = $Rebuild -or $hostCompiled -or -not (Test-Path $HostTest)
if (-not $linkStale) {
    $exeTime = (Get-Item $HostTest).LastWriteTimeUtc
    foreach ($o in $hostObjs) {
        if ((Get-Item $o).LastWriteTimeUtc -gt $exeTime) { $linkStale = $true; break }
    }
}

if ($linkStale) {
    $hostRsp = Join-Path $OutDir "RoboTest.host.rsp"
    $hostArgs = @($hostFlags) + @("-o", $HostTest) + $hostObjs + @("-luser32", "-lgdi32", "-ld3d11", "-ldxgi", "-ldxguid")
    $hostArgChars = 0
    foreach ($hostArg in $hostArgs) { $hostArgChars += $hostArg.Length + 1 }
    Write-Host "LD     $($hostArgs.Count) arguments ($hostArgChars chars) -> $hostRsp"
    Set-Content -Path $hostRsp -Value ($hostArgs | ForEach-Object {
        $token = $_ -replace '\\', '/'
        if ($token -match '\s') { '"' + $token + '"' } else { $token }
    }) -Encoding ascii
    & $clang "@$hostRsp"
    if ($LASTEXITCODE -ne 0) { throw "Host RoboTest link failed" }
} else {
    Write-Host "SKIP   $HostTest"
}
Write-Host "Build OK: $HostTest (tests only)"

# -BuildTests stops here: the artifact exists and can be run by a filtered test
# command, but this invocation does not run - and so cannot fail on - suites it
# was not asked about.
if (-not $Test) {
    Write-Host "BUILD OK: $HostTest built (suite not run; pass -Test to run it)"
    exit 0
}

$testArgs = @()
if ($Filter) { $testArgs += "--filter=$Filter" }
if ($VerboseTests) { $testArgs += "--verbose" }

Write-Host "HOST   running RoboTest ($Configuration)"
# Native stderr (e.g. RoboMemDebug reports) must not abort under Stop.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $HostTest @testArgs
$testExit = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($testExit -ne 0) { throw "RoboTest failed ($Configuration)" }
Write-Host "TEST PASS: RoboTest suite ($Configuration)"

Write-Host "HOST   child --child-panic"
$ErrorActionPreference = "Continue"
& $HostTest "--child-panic"
$childExit = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($childExit -eq 0) { throw "child-panic unexpectedly succeeded" }
Write-Host "TEST PASS: panic terminates (exit $childExit)"

Write-Host "HOST   child --child-assert"
$ErrorActionPreference = "Continue"
& $HostTest "--child-assert"
$childExit = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($childExit -eq 0) { throw "child-assert unexpectedly succeeded" }
Write-Host "TEST PASS: assert terminates (exit $childExit)"

if ($RoboMemDebugDefine -eq "1") {
    foreach ($mode in @(
        "--child-mem-double-free",
        "--child-mem-invalid-free",
        "--child-mem-underflow",
        "--child-mem-overflow"
    )) {
        Write-Host "HOST   child $mode"
        $ErrorActionPreference = "Continue"
        & $HostTest $mode
        $childExit = $LASTEXITCODE
        $ErrorActionPreference = $prevEap
        if ($childExit -eq 0) { throw "$mode unexpectedly succeeded" }
        Write-Host "TEST PASS: $mode terminates (exit $childExit)"
    }
}

if ($asanEnabled -or $ubsanEnabled) {
    $parts = @()
    if ($asanEnabled) { $parts += "AddressSanitizer" }
    if ($ubsanEnabled) { $parts += "UndefinedBehaviorSanitizer" }
    Write-Host ("SANITIZE PASS: host suite built with " + ($parts -join " + "))
}

Write-Host "ALL TESTS PASS ($Configuration)"
exit 0
