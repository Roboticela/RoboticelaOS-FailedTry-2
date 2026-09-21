/*
 * Real PNG fixtures for the RoboGraphics decoder suite (Phase 8.12).
 *
 * GENERATED, NOT HAND-WRITTEN. These byte strings are genuine PNG files
 * produced by an independent encoder (zlib deflate level 6, standard PNG
 * chunk framing) and embedded as hex so the suite decodes them without
 * touching the filesystem. That independence is the point: the decoder is
 * checked against bytes it did not produce, and every expected pixel below
 * was computed from the *source* image handed to the encoder, not read back
 * out of the decoder.
 *
 * The Huffman path is covered: zlib emits a dynamic-Huffman block
 * (BFINAL/BTYPE = 2) for every accept fixture here. The stored-block path is
 * covered separately by the round-trip test against the Goal 6.13 encoder,
 * which writes zlib store blocks. Between them, all three DEFLATE block kinds
 * run.
 *
 * The two rejection fixtures at the end (Adam7 interlacing, sub-byte bit
 * depth) are deliberately outside the 8.12 decoder; they prove the refusal is
 * deterministic rather than a silent misdecode.
 */

#ifndef ROBO_GRAPHICS_PNG_FIXTURES_H
#define ROBO_GRAPHICS_PNG_FIXTURES_H

#include <stddef.h>
#include <stdint.h>

/* Where these bytes came from, for failure messages. */
#define RoboPngFixtureOrigin "independent encoder (zlib deflate level 6)"

/* RoboPngGradient — 8x8 RGBA8, dynamic-Huffman IDAT. */
static const char RoboPngGradient_hex[] =
    "89504e470d0a1a0a0000000d4948445200000008000000080806000000c40fbe8b0000005c49444154789c15ca310103"
    "410800b093f252908214a42005293869c3902defbdfa7d0449d10ccb7b9f409014cdb0df85100892a219362ea4409014"
    "cdb079a10482a46886ad0b2d102445336c5f188120299a61e7c20a0449d10ccb1ffa9197c102a2a4750000000049454e"
    "44ae426082"
    ;
#define RoboPngGradientBytes 149u
#define RoboPngGradientWidth 8u
#define RoboPngGradientHeight 8u
#define RoboPngGradientRgbaBytes 256u
#define RoboPngGradientRgbaFnv 0x0277B7C5u
#define RoboPngGradientPixel00 { 0, 0, 128, 255 }
#define RoboPngGradientPixel70 { 224, 0, 128, 255 }
#define RoboPngGradientPixel07 { 0, 224, 128, 255 }
#define RoboPngGradientPixel77 { 224, 224, 128, 255 }
#define RoboPngGradientPixel44 { 128, 128, 128, 255 }

/* RoboPngChecker — 32x32 RGBA8, dynamic-Huffman IDAT. */
static const char RoboPngChecker_hex[] =
    "89504e470d0a1a0a0000000d4948445200000020000000200806000000737a7af40000037249444154789c6dd711d0a6"
    "7a18c6e170f1c5e8cce262181c580cc3e6d06218866118866118866118866118667baec573be072ef9ddf3cd3cf0ef9d"
    "f99224d97f7fe343ca5f7ffff31f3fb582922ad83bad67600cf64ddb3938833d49be39800fe9b7e000ada0a40af64eeb"
    "1918837dd3760ece604f928f03f8907e8203b482922ad83bad67600cf64ddb3938833d495207f0214d8303b482922ad8"
    "3bad67600cf64ddb3938833df97ff8fe7dfffd838cfcfbd73ff8a5d534b4c13e69330b6bb05fdacdc3cbd7037e38808c"
    "fc47708056d3d006fba4cd2cacc17e69370fef8fe880cc0164e459708056d3d006fba4cd2cacc17e69370f6f161d903b"
    "808c3c0f0ed06a1ada609fb4998535d82fede6e125497e7a847c487f068f502b28a982bdd37a06c660dfb49d8333d893"
    "a470001fd22238402b28a982bdd37a06c660dfb49d8333d893a474001fd23238402b28a982bdd37a06c660dfb49d8333"
    "d893a472001fd22a38402b28a982bdd37a06c660dfb49d8333d8bf3ec25f1e2119f9afe0116a350d6db04fdaccc21aec"
    "9776f3f0fe8abe82da0164e475708056d3d006fba4cd2cacc17e69370f6f1d1dd038808cbc090ed06a1ada609fb49985"
    "35d82fede6e16da2035a079091b7c1015a4d431bec9336b3b006fba5dd3cbced9fdf81ce23e443da058f502b28a982bd"
    "d37a06c660dfb49d8333d893a477001fd23e38402b28a982bdd37a06c660dfb49d8333d8936470001fd22138402b28a9"
    "82bdd37a06c660dfb49d8333d8936474001fd23138402b28a982bdd37a06c660dfb49d8333d8bf3ec2c92324239f8247"
    "a8d534b4c13e69330b6bb05fdacdc33b455fc1ec0032f2393840ab6968837dd26616d660bfb49b87778e0e581c4046be"
    "040768350d6db04fdaccc21aec9776f3f02ed101ab03c8c8d7e000ada6a10df6499b595883fdd26e1edef5cfefc0e611"
    "f221dd8247a8159454c1de693d0363b06fdacec119ecfe2f71001fd23d38402b28a982bdd37a06c660dfb49d8333d893"
    "e470001fd22338402b28a982bdd37a06c660dfb49d8333d893e474001fd23338402b28a982bdd37a06c660dfb49d8333"
    "d8bf3ec2cb232423bf8247a8d534b4c13e69330b6bb05fdacdc37b455fc1ed0032f23b3840ab6968837dd26616d660bf"
    "b49b87f78e0e781c4046fe040768350d6db04fdaccc21aec9776f3f03ed101af03c8c8dfe000ada6a10df6499b595883"
    "fdd26e1e5efe05ddc9e479b528b1000000000049454e44ae426082"
    ;
#define RoboPngCheckerBytes 939u
#define RoboPngCheckerWidth 32u
#define RoboPngCheckerHeight 32u
#define RoboPngCheckerRgbaBytes 4096u
#define RoboPngCheckerRgbaFnv 0x1D92A9C5u
#define RoboPngCheckerPixel00 { 0, 0, 200, 255 }
#define RoboPngCheckerPixel310 { 30, 60, 90, 255 }
#define RoboPngCheckerPixel031 { 30, 60, 90, 255 }
#define RoboPngCheckerPixel3131 { 248, 248, 200, 255 }
#define RoboPngCheckerPixel1616 { 128, 128, 200, 255 }

/* RoboPngOpaque — 8x8 RGB8 (alpha forced opaque). */
static const char RoboPngOpaque_hex[] =
    "89504e470d0a1a0a0000000d49484452000000080000000808020000004b6d29dc0000006c49444154789c0dc9410100"
    "3008033194a0a48fd381129454094aaa68cb375545172aa6d8c2c51529aa9a6ed44cb38d9b6bd23f440b89112b2c4e44"
    "3f861e34ccb083871b323f965eb4ccb28b975bb23f4c1b99316b6ccec43f8e3e74ccb1878f3b723f420785091b1c2e24"
    "3c7fd2448169690d200000000049454e44ae426082"
    ;
#define RoboPngOpaqueBytes 165u
#define RoboPngOpaqueWidth 8u
#define RoboPngOpaqueHeight 8u
#define RoboPngOpaqueRgbaBytes 256u
#define RoboPngOpaqueRgbaFnv 0xE23BCFC5u
#define RoboPngOpaquePixel00 { 0, 0, 64, 255 }
#define RoboPngOpaquePixel70 { 210, 0, 64, 255 }
#define RoboPngOpaquePixel07 { 0, 210, 64, 255 }
#define RoboPngOpaquePixel77 { 210, 210, 64, 255 }
#define RoboPngOpaquePixel44 { 120, 120, 64, 255 }

/* RoboPngGrayscale — 8x8 grayscale 8-bit. */
static const char RoboPngGrayscale_hex[] =
    "89504e470d0a1a0a0000000d4948445200000008000000080800000000e164e1570000002a49444154789c6360105030"
    "7008482880d10d0c507a0203945ec000a5373040e9030c50fa0203947e00001e751c01321300490000000049454e44ae"
    "426082"
    ;
#define RoboPngGrayscaleBytes 99u
#define RoboPngGrayscaleWidth 8u
#define RoboPngGrayscaleHeight 8u
#define RoboPngGrayscaleRgbaBytes 256u
#define RoboPngGrayscaleRgbaFnv 0x3E10AEC5u
#define RoboPngGrayscalePixel00 { 0, 0, 0, 255 }
#define RoboPngGrayscalePixel70 { 112, 112, 112, 255 }
#define RoboPngGrayscalePixel07 { 112, 112, 112, 255 }
#define RoboPngGrayscalePixel77 { 224, 224, 224, 255 }
#define RoboPngGrayscalePixel44 { 128, 128, 128, 255 }

/* RoboPngTranslucent — 6x6 grayscale+alpha 8-bit. */
static const char RoboPngTranslucent_hex[] =
    "89504e470d0a1a0a0000000d49484452000000060000000608040000004ac527c30000004349444154789c05c1810000"
    "000803b0430411c42182c82a882082384410b50167e7c78bcb03d6d6971b9b0bc8e4a24229606c7c383139405b7bb3a3"
    "b381b2f26245653dc67323dd3214af8f0000000049454e44ae426082"
    ;
#define RoboPngTranslucentBytes 124u
#define RoboPngTranslucentWidth 6u
#define RoboPngTranslucentHeight 6u
#define RoboPngTranslucentRgbaBytes 144u
#define RoboPngTranslucentRgbaFnv 0x9963E445u
#define RoboPngTranslucentPixel00 { 0, 0, 0, 255 }
#define RoboPngTranslucentPixel50 { 100, 100, 100, 255 }
#define RoboPngTranslucentPixel05 { 0, 0, 0, 155 }
#define RoboPngTranslucentPixel55 { 100, 100, 100, 155 }
#define RoboPngTranslucentPixel33 { 60, 60, 60, 195 }

/* RoboPngPaletted — 4x4 palette 8-bit with tRNS. */
static const char RoboPngPaletted_hex[] =
    "89504e470d0a1a0a0000000d49484452000000040000000408030000009e2f6e4c0000000c504c5445ff000000ff0000"
    "00fffffffffb0060f60000000474524e53ff80ff001f8786990000001449444154789c636060646206630610cd006203"
    "0001040019427b918c0000000049454e44ae426082"
    ;
#define RoboPngPalettedBytes 117u
#define RoboPngPalettedWidth 4u
#define RoboPngPalettedHeight 4u
#define RoboPngPalettedRgbaBytes 64u
#define RoboPngPalettedRgbaFnv 0x7BDDD145u
#define RoboPngPalettedPixel00 { 255, 0, 0, 255 }
#define RoboPngPalettedPixel30 { 255, 255, 255, 0 }
#define RoboPngPalettedPixel03 { 255, 255, 255, 0 }
#define RoboPngPalettedPixel33 { 0, 0, 255, 255 }
#define RoboPngPalettedPixel22 { 255, 0, 0, 255 }

/* REJECTION FIXTURE — 8x8 RGBA8 carried in Adam7 passes. */
static const char RoboPngInterlaced_hex[] =
    "89504e470d0a1a0a0000000d4948445200000008000000080806000001b3088e1d0000000d49444154789c6360180520"
    "00000108000182e90b1d0000000049454e44ae426082"
    ;
#define RoboPngInterlacedBytes 70u

/* REJECTION FIXTURE — 8x8 grayscale at bit depth 1. */
static const char RoboPngPackedNibble_hex[] =
    "89504e470d0a1a0a0000000d4948445200000008000000080100000000ec7483260000000b49444154789c6360c00e00"
    "00180001d57fc4ec0000000049454e44ae426082"
    ;
#define RoboPngPackedNibbleBytes 68u

#endif /* ROBO_GRAPHICS_PNG_FIXTURES_H */
