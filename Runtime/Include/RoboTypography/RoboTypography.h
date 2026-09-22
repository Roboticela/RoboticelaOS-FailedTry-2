/*
 * RoboTypography — the module's front door (Phase 12.3).
 *
 * The typing system of RoboticelaOS: the semantic roles every text surface speaks, the one scale
 * that maps them to type settings, and the resolver that turns a role into a loaded system face.
 *
 * 		Desktop / Chrome / Document UI
 * 		              │  RoboTypographyRole
 * 		              ▼
 * 		   RoboTypographyResolver  ──→  the system typeface pack (12.2)
 * 		              │  TypographyStyle: face + size + weight + leading + metrics
 * 		              ▼
 * 		    RoboLayout (measure) ─ RoboPaint (draw) ─ RoboGraphics (12.1 raster) ─ framebuffer
 *
 * ## The rule this module exists to keep
 *
 * A surface names a role; it never names a font file. Before 12.3 the shell held one `FontFace` and
 * one size and drew every label with them, and a document's type was whatever its author sheet said.
 * Both were bindings that lived in the surfaces themselves, and bindings that live in surfaces drift.
 * `Documentation/Runtime/Typography/TypeBinding.md` is the phase's contract; this header is the API.
 *
 * ## Where it sits
 *
 * Between RoboFont and the consumers that need *semantic* text (RoboDesktop today, the document
 * defaults through RoboDesktopDocument, and later RoboPaint/RoboLayout as RoboText arrives). It
 * depends on RoboFont and on nothing above it, and it is deliberately **not** in the graphics or
 * paint dependency path: rasterizing a glyph is RoboGraphics' (12.1), and shaping a run is
 * RoboText's (12.4).
 *
 * ## Defaults a caller can rely on
 *
 *   `RoboTypographyDefaultScale`      the one table of role → face role, weight, size, leading;
 *   `RoboTypographyResolve`           a role, with no failure for a missing family (a face-less
 *                                     style still carries the numbers a layout needs);
 *   `RoboTypographyResolveGlyphs`     coverage fallback, chosen by RoboFont's own chain;
 *   `...ResolverDocumentStyleSheet`   the document defaults, as CSS, from the same table.
 */
#ifndef ROBO_TYPOGRAPHY_H
#define ROBO_TYPOGRAPHY_H

#include "RoboTypography/RoboTypographyDiagnostics.h"
#include "RoboTypography/RoboTypographyResolver.h"
#include "RoboTypography/RoboTypographyScale.h"
#include "RoboTypography/RoboTypographyTypes.h"
#include "RoboTypography/RoboTypographyVersion.h"

#endif /* ROBO_TYPOGRAPHY_H */
