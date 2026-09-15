# Builds Roboticela Runtime library (guest ELF) and the RoboTest host suite.
#
#   .\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test
#   .\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test
#   .\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter base
#   .\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
#   .\runtime\Tools\Build.ps1 -Clean

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean,
    [switch]$Verify,
    [switch]$Test,
    [switch]$Sanitize,
    [string]$Filter = "",
    [switch]$VerboseTests
)

$ErrorActionPreference = "Stop"

$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepoRoot = (Resolve-Path (Join-Path $RuntimeRoot "..")).Path
$OutDir = Join-Path $RuntimeRoot "Out"
$LibDir = Join-Path $OutDir "lib"
$IncludeDir = Join-Path $RuntimeRoot "Include"
$SourceDir = Join-Path $RuntimeRoot "Source"
$TestsDir = Join-Path $RuntimeRoot "Tests"
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
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsText.c"),
    (Join-Path $SourceDir "RoboGraphics\RoboGraphicsEncode.c"),
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
    (Join-Path $SourceDir "RoboHTML\RoboHTML.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTypes.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDiagnostics.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLLifecycle.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLSource.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLEncoding.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLNode.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDom.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLElement.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLClient.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLResource.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLParser.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLDocument.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLToken.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLCharRefData.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLCharRef.c"),
    (Join-Path $SourceDir "RoboHTML\RoboHTMLTokenizer.c")
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
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontSfnt.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontTrueType.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontOpenType.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontCff.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontCmap.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontMetrics.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontOutline.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontName.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontClassification.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontSelection.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontDiscovery.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontLoader.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontCoverage.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontFallback.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontGenericFamily.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontEmoji.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontVariation.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontColor.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontValidate.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontConformance.c"),
    (Join-Path $RuntimeRoot "font\tests\SuiteRoboFontPhase5.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphics.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsPixelFormat.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsFramebuffer.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsSurface.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsBitmap.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsGeometry.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsLine.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsPath.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsTransform.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsComposite.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsRaster.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsText.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsEncode.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsScreenshot.c"),
    (Join-Path $RuntimeRoot "graphics\tests\SuiteRoboGraphicsPhase6.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindow.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowNative.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowPresent.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowGraphics.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowHost.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowSize.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowLifecycle.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowEventLoop.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowKeyboard.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowMouse.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboQemuDisplay.c"),
    (Join-Path $RuntimeRoot "window\tests\SuiteRoboWindowPhase7.c"),
    (Join-Path $RuntimeRoot "display\tests\SuiteRoboDisplay.c"),
    (Join-Path $RuntimeRoot "display\tests\SuiteRoboDisplayScale.c"),
    (Join-Path $RuntimeRoot "display\tests\SuiteRoboDisplayHost.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTML.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLDocument.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLParser.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLHooks.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLPhase8.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLToken.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLCharRef.c"),
    (Join-Path $RuntimeRoot "html\tests\SuiteRoboHTMLTokenizerStates.c"),
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

$LibcInc = Join-Path $RepoRoot "libc\include"
$SdkInc = Join-Path $RepoRoot "sdk\include"
$LibcA = Join-Path $RepoRoot "libc\build\libc.a"
$Crt0 = Join-Path $RepoRoot "libc\build\obj\crt0.o"
$Crti = Join-Path $RepoRoot "libc\build\obj\crti.o"
$Crtn = Join-Path $RepoRoot "libc\build\obj\crtn.o"
$LdScript = Join-Path $RepoRoot "user\linker.ld"
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
        (Join-Path $RuntimeRoot "font")
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
        (Join-Path $RuntimeRoot "graphics")
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
        (Join-Path $RuntimeRoot "window")
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
        (Join-Path $RuntimeRoot "html")
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

function Assert-RoboDisplayChromiumIndependence {
    $roots = @(
        (Join-Path $RuntimeRoot "Include\RoboDisplay"),
        (Join-Path $RuntimeRoot "Source\RoboDisplay"),
        (Join-Path $RuntimeRoot "display")
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

if ($Clean) {
    if (Test-Path $OutDir) {
        Remove-Item -Recurse -Force $OutDir
        Write-Host "Removed $OutDir"
    } else {
        Write-Host "Nothing to clean."
    }
    exit 0
}

foreach ($req in ($ProdSrcs + @(
    $MainSrc, $TestFrameworkSrc, $TestMainSrc, $IncludeDir,
    $LibcInc, $SdkInc, $LibcA, $Crt0, $Crti, $Crtn, $LdScript
) + $SuiteSrcs)) {
    if (-not (Test-Path $req)) { throw "Missing required input: $req" }
}

New-Item -ItemType Directory -Force -Path $LibDir | Out-Null

$clang = Get-Clang
$ld = Get-LdLld
$commonDefines = @(
    "-DROBO_DEBUG=$RoboDebugDefine",
    "-DROBO_MEM_DEBUG=$RoboMemDebugDefine"
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
    "-I", (Join-Path $SourceDir "RoboWindow\platform\win32"),
    "-I", (Join-Path $SourceDir "RoboDisplay\platform\win32"),
    "-D_CRT_SECURE_NO_WARNINGS",
    "-DROBO_WINDOW_HAS_WIN32=1"
)

$BidiConfPath = (Join-Path $TestsDir "Data\robo_bidi_character_conformance.bin") -replace "\\", "/"
$BidiConfHeader = Join-Path $OutDir "robo_bidi_conf_path.h"
$BidiConfBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_BIDI_CONF_PATH_H
#define ROBO_BIDI_CONF_PATH_H
#define ROBO_BIDI_CONF_PATH "$BidiConfPath"
#endif
"@
Set-Content -Path $BidiConfHeader -Value $BidiConfBody -Encoding ascii
$hostFlags += "-I", $OutDir
$hostFlags += "-DROBO_BIDI_CONF_PATH_FROM_BUILD=1"

$ScriptConfPath = (Join-Path $TestsDir "Data\robo_script_conformance.bin") -replace "\\", "/"
$ScriptConfHeader = Join-Path $OutDir "robo_script_conf_path.h"
$ScriptConfBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_SCRIPT_CONF_PATH_H
#define ROBO_SCRIPT_CONF_PATH_H
#define ROBO_SCRIPT_CONF_PATH "$ScriptConfPath"
#endif
"@
Set-Content -Path $ScriptConfHeader -Value $ScriptConfBody -Encoding ascii
$hostFlags += "-DROBO_SCRIPT_CONF_PATH_FROM_BUILD=1"

$EmojiConfPath = (Join-Path $TestsDir "Data\robo_emoji_conformance.bin") -replace "\\", "/"
$EmojiConfHeader = Join-Path $OutDir "robo_emoji_conf_path.h"
$EmojiConfBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_EMOJI_CONF_PATH_H
#define ROBO_EMOJI_CONF_PATH_H
#define ROBO_EMOJI_CONF_PATH "$EmojiConfPath"
#endif
"@
Set-Content -Path $EmojiConfHeader -Value $EmojiConfBody -Encoding ascii
$hostFlags += "-DROBO_EMOJI_CONF_PATH_FROM_BUILD=1"

$PluralSamplesPath = (Join-Path $TestsDir "Data\robo_plural_samples.txt") -replace "\\", "/"
$PluralSamplesHeader = Join-Path $OutDir "robo_plural_samples_path.h"
$PluralSamplesBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_PLURAL_SAMPLES_PATH_H
#define ROBO_PLURAL_SAMPLES_PATH_H
#define ROBO_PLURAL_SAMPLES_PATH "$PluralSamplesPath"
#endif
"@
Set-Content -Path $PluralSamplesHeader -Value $PluralSamplesBody -Encoding ascii
$hostFlags += "-DROBO_PLURAL_SAMPLES_PATH_FROM_BUILD=1"

$CollationTestPath = (Join-Path $RuntimeRoot "Tools\i18n\source\uca-16.0.0\CollationTest\CollationTest\CollationTest_NON_IGNORABLE_SHORT.txt") -replace "\\", "/"
$CollationTestHeader = Join-Path $OutDir "robo_collation_test_path.h"
$CollationTestBody = @"
/* Auto-generated by Build.ps1 - DO NOT EDIT. */
#ifndef ROBO_COLLATION_TEST_PATH_H
#define ROBO_COLLATION_TEST_PATH_H
#define ROBO_COLLATION_TEST_PATH "$CollationTestPath"
#endif
"@
Set-Content -Path $CollationTestHeader -Value $CollationTestBody -Encoding ascii
$hostFlags += "-DROBO_COLLATION_TEST_PATH_FROM_BUILD=1"

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

Assert-NoChromiumPath ($ProdSrcs + $SuiteSrcs + @($MainSrc, $IncludeDir) + $guestFlags)

Write-Host "CC     $clang"
Write-Host "LD     $ld"
Write-Host "CONFIG $Configuration (ROBO_DEBUG=$RoboDebugDefine ROBO_MEM_DEBUG=$RoboMemDebugDefine)"

# --- Production guest library objects ---
$libObjs = @()
foreach ($src in $ProdSrcs) {
    $obj = Join-Path $LibDir ([IO.Path]::GetFileNameWithoutExtension($src) + ".o")
    Write-Host "CC     $src -> $obj"
    & $clang @guestFlags -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $src" }
    $libObjs += $obj
}

$ar = Get-LlvmAr
$libA = Join-Path $LibDir "librobobase.a"
if ($ar) {
    if (Test-Path $libA) { Remove-Item $libA -Force }
    Write-Host "AR     $libA"
    & $ar "rcs" $libA @libObjs
    if ($LASTEXITCODE -ne 0) { throw "Archive failed" }
}

$mainObj = Join-Path $LibDir "Main.o"
Write-Host "CC     $MainSrc"
& $clang @guestFlags -c $MainSrc -o $mainObj
if ($LASTEXITCODE -ne 0) { throw "Compile failed: Main.c" }

Write-Host "LD     $RuntimeElf"
$linkobjs = @($Crti, $Crt0) + $libObjs + @($mainObj, $LibcA, $Crtn)
& $ld "-m" "elf_x86_64" "-static" "-no-pie" "-T" $LdScript "-o" $RuntimeElf @linkobjs
if ($LASTEXITCODE -ne 0) { throw "Link failed: Runtime ELF" }

Write-Host "Build OK: $RuntimeElf (production)"
if (Test-Path $libA) { Write-Host "Build OK: $libA (production library)" }

Test-ElfFile $RuntimeElf
Write-Host "OK   guest ELF integrity"
Assert-NoChromiumPath @($RuntimeElf)
Write-Host "OK   no Chromium paths"

if ($Verify) {
    Assert-RoboFontChromiumIndependence
    Assert-RoboGraphicsChromiumIndependence
    Assert-RoboWindowChromiumIndependence
    Assert-RoboDisplayChromiumIndependence
    Assert-RoboHTMLChromiumIndependence
    Write-Host "VERIFY PASS: production runtime ($Configuration)"
}

# --- Host test executable (framework + suites; does not ship in production) ---
Write-Host "HOST   building $HostTest"
$Win32BackendSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowBackendWin32.c"
$Win32KeyboardSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowKeyboardWin32.c"
$Win32MouseSrc = Join-Path $SourceDir "RoboWindow\platform\win32\RoboWindowMouseWin32.c"
$Win32DisplaySrc = Join-Path $SourceDir "RoboDisplay\platform\win32\RoboDisplayBackendWin32.c"
$hostSrcs = $ProdSrcs + @(
    $Win32BackendSrc, $Win32KeyboardSrc, $Win32MouseSrc, $Win32DisplaySrc,
    $TestFrameworkSrc, $TestMainSrc
) + $SuiteSrcs
& $clang @hostFlags -o $HostTest @hostSrcs "-luser32" "-lgdi32"
if ($LASTEXITCODE -ne 0) { throw "Host RoboTest compile failed" }
Write-Host "Build OK: $HostTest (tests only)"

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
