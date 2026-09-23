/*
 * RoboLayout's font **selection** seam (Phase 12.17).
 *
 * `RoboLayoutFontRequest.h` produces the CSS-side request — an ordered family list plus the scalar
 * settings. This header is the other half of the seam: turning that request into *one face*, through a
 * **provider** the caller supplies, so that the layer between CSS and the font database owns no font
 * parsing, no coverage table and no directory.
 *
 *     LayoutFontRequest            (what CSS asked for)
 *           │  first family that yields a face
 *           ▼
 *     LayoutFontSelection          (what this build will actually set the text in)
 *           │
 *           ▼
 *     a borrowed FontFace          (measured with, and — since 12.16 — shaped with)
 *
 * ## Why a provider rather than a font manager
 *
 * RoboLayout must not open a font file, scan `/System/Fonts/` or hold a registry: those are RoboFont's
 * (`FontRegistry`, `FontSelect`, `FontGenericResolver`) and the system pack's. So the resolution is a
 * **vtable** the caller fills — the same shape `LayoutTextMetrics` uses for measurement and
 * `LayoutStyleSource` uses for computed styles. The one concrete implementation lives above this module
 * (`RoboDesktopFontProvider`, built from the pack and the registry), and both the *measurer* and the
 * *painter* are handed the same provider and the same request, so a run's width and its glyphs are
 * resolved from one selection rather than two that have to agree.
 *
 * ## One selection, threaded rather than re-derived
 *
 * The measurer resolves the request once and records the selected face **on the run**
 * (`LayoutTextRun.face`), and the paint pass draws with that face rather than resolving again. That is
 * the project's rule stated in one place: a fact a later layer needs is carried, not recomputed. The
 * face is **borrowed** — the caller owns whatever registry the provider selects out of, and the record
 * holds no reference it would have to release.
 *
 * ## A generic stays a generic until a configured mapping resolves it
 *
 * `sans-serif` is not a family name, and this module does not know one. A generic entry is handed to the
 * provider as a `RoboFontGenericFamily`, and the provider is what consults the configured mapping. The
 * order of the requested list is the fallback order, which is what makes a fallback a *data* change:
 * `"UI Text", sans-serif` tries the name first and the generic second, and `LayoutFontSelection.familyIndex`
 * records which one answered.
 */
#ifndef ROBO_LAYOUT_FONT_RESOLVER_H
#define ROBO_LAYOUT_FONT_RESOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontStyle.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutFontRequest.h"

#ifdef __cplusplus
extern "C" {
#endif

/** `familyIndex` for a selection no family answered. */
#define RoboLayoutFontNoFamily ((size_t)-1)

/**
 * One requested family entry in, one borrowed face out.
 *
 * The caller owns the implementation and the registry behind it; this module only calls it. A NULL
 * `outFace` (with `RoboOk`) is the honest answer for "nothing in this database matches that family",
 * and is not a failure.
 *
 * @return RoboOk and @p outFace (possibly NULL); RoboInvalidArgument for a NULL argument; a provider's
 *         own failure code when its database could not be consulted.
 */
typedef Result (*LayoutFontResolveFamilyFn)(
    void *userdata,
    const LayoutFontFamily *family,
    FontWeight weight,
    FontStretch stretch,
    FontStyle style,
    FontFace **outFace);

/** Who resolves a family, and what it knows. A record by value, owned by the caller. */
typedef struct LayoutFontProvider {
    void *userdata;
    /** The callback. NULL means "this provider resolves nothing", and every request answers unset. */
    LayoutFontResolveFamilyFn resolveFamily;
} LayoutFontProvider;

/** Clear @p provider to "resolves nothing". Safe on NULL. */
void LayoutFontProviderInit(LayoutFontProvider *provider);

/** Non-zero when @p provider has a callback. */
int LayoutFontProviderIsUsable(const LayoutFontProvider *provider);

/** What a request resolved to: the face, the settings it was selected at, and which family answered. */
typedef struct LayoutFontSelection {
    /** The borrowed face; NULL exactly when @ref resolved is 0. */
    FontFace *face;
    /** The weight the selection was recorded at (the request's own). */
    FontWeight weight;
    /** The stretch the selection was recorded at. */
    FontStretch stretch;
    /** The style the selection was recorded at. */
    FontStyle style;
    /** Which family entry answered; @ref RoboLayoutFontNoFamily when none did. */
    size_t familyIndex;
    /** Non-zero when a family other than the first answered (index > 0, or a generic's own mapping). */
    int fallback;
    /** Non-zero when @ref face names a face. */
    int resolved;
} LayoutFontSelection;

/** Clear @p selection to "nothing selected". Safe on NULL. */
void LayoutFontSelectionInit(LayoutFontSelection *selection);

/** Non-zero when @p selection names a face. Safe on NULL. */
int LayoutFontSelectionIsSet(const LayoutFontSelection *selection);

/** Field-for-field equality over the face identity and the settings. Safe on NULL. */
int LayoutFontSelectionEqual(const LayoutFontSelection *a, const LayoutFontSelection *b);

/** Human-readable one-line summary. Complete or absent (`RoboNotSupported` when it does not fit). */
Result LayoutFontSelectionWrite(const LayoutFontSelection *selection,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

/**
 * Select a face for @p request from @p provider.
 *
 * The requested families are tried **in author order**: the first entry that yields a face wins, and
 * `fallback` is set when that was not the first. The scalar settings are the request's own, carried into
 * the selection so a caller records what was asked for as well as what was found.
 *
 * A NULL or unusable @p provider, or a request whose every family matches nothing, is **not** a failure:
 * the selection comes back unset (`resolved == 0`), which is the honest answer and the one the caller
 * degrades to its own default face for.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for NULL @p request or @p out; a provider's own failure
 *         code when its database could not be consulted.
 */
Result LayoutFontRequestSelect(const LayoutFontRequest *request,
                               const LayoutFontProvider *provider,
                               LayoutFontSelection *out,
                               LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_FONT_RESOLVER_H */
