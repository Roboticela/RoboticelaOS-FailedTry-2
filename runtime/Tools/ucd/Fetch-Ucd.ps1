# Download Unicode 16.0.0 UCD source files for RoboUnicode table generation.

param(
    [string]$Version = "16.0.0"
)

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$OutDir = Join-Path $Here "source"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$files = @(
    @{ Name = "UnicodeData.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/UnicodeData.txt" },
    @{ Name = "Scripts.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/Scripts.txt" },
    @{ Name = "EastAsianWidth.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/EastAsianWidth.txt" },
    @{ Name = "PropList.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/PropList.txt" },
    @{ Name = "DerivedCoreProperties.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/DerivedCoreProperties.txt" },
    @{ Name = "emoji-data.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/emoji/emoji-data.txt" },
    @{ Name = "PropertyValueAliases.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/PropertyValueAliases.txt" },
    @{ Name = "CompositionExclusions.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/CompositionExclusions.txt" },
    @{ Name = "DerivedNormalizationProps.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/DerivedNormalizationProps.txt" },
    @{ Name = "NormalizationTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/NormalizationTest.txt" },
    @{ Name = "GraphemeBreakProperty.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/GraphemeBreakProperty.txt" },
    @{ Name = "GraphemeBreakTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/GraphemeBreakTest.txt" },
    @{ Name = "SpecialCasing.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/SpecialCasing.txt" },
    @{ Name = "CaseFolding.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/CaseFolding.txt" },
    @{ Name = "WordBreakProperty.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/WordBreakProperty.txt" },
    @{ Name = "WordBreakTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/WordBreakTest.txt" },
    @{ Name = "SentenceBreakProperty.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/SentenceBreakProperty.txt" },
    @{ Name = "SentenceBreakTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/SentenceBreakTest.txt" },
    @{ Name = "LineBreak.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/LineBreak.txt" },
    @{ Name = "LineBreakTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/auxiliary/LineBreakTest.txt" },
    @{ Name = "DerivedBidiClass.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/extracted/DerivedBidiClass.txt" },
    @{ Name = "BidiBrackets.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/BidiBrackets.txt" },
    @{ Name = "BidiMirroring.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/BidiMirroring.txt" },
    @{ Name = "BidiTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/BidiTest.txt" },
    @{ Name = "BidiCharacterTest.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/BidiCharacterTest.txt" },
    @{ Name = "ScriptExtensions.txt"; Url = "https://www.unicode.org/Public/$Version/ucd/ScriptExtensions.txt" }
)

foreach ($f in $files) {
    $dest = Join-Path $OutDir $f.Name
    Write-Host "GET $($f.Url)"
    Invoke-WebRequest -Uri $f.Url -OutFile $dest -UseBasicParsing
    Write-Host ("OK {0} ({1} bytes)" -f $f.Name, (Get-Item $dest).Length)
}

Write-Host "Done. Run: python $($Here)\generate_ucd.py"
Write-Host "         python $($Here)\generate_norm.py"
Write-Host "         and: python $($Here)\generate_grapheme.py"
Write-Host "         and: python $($Here)\generate_case.py"
Write-Host "         and: python $($Here)\generate_word.py"
Write-Host "         and: python $($Here)\generate_sentence.py"
Write-Host "         and: python $($Here)\generate_linebreak.py"
Write-Host "         and: python $($Here)\generate_bidi.py"
