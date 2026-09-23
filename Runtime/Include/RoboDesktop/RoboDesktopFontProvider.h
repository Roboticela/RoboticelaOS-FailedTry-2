/*
 * RoboDesktop's font provider (Phase 12.17) — the concrete `LayoutFontProvider`.
 *
 * `RoboLayoutFontResolver.h` defines a **seam**: a request in, one face out, with the actual font
 * database supplied by the caller. This is that database, and it is the only implementation in the tree.
 * It sits in RoboDesktop because RoboDesktop is the module allowed to see both halves — it may include
 * RoboLayout (the seam's vocabulary) and RoboFont (the registry, `FontSelect`, the generic resolver, the
 * system pack) — where neither RoboLayout nor RoboTypography may. So the *policy* (which family, in which
 * order) stays in RoboLayout, the *database* is here, and the font system beneath is unchanged.
 *
 * ## What it resolves
 *
 * For one `LayoutFontFamily` entry at a weight/stretch/style:
 *
 *   a **generic** (`sans-serif`, `system-ui`, `ui-monospace`, …) is resolved through RoboFont's own
 *   `FontGenericResolver`, configured from the system pack's own mappings
 *   (`FontSystemPackApplyGenericConfig`). Nothing here hard-codes `sans-serif → Inter`; the pack's
 *   `FontSystemPackApplyGenericConfig` is the one place that decision lives.
 *
 *   a **named** family is resolved by collecting the registered faces that claim it
 *   (`FontRegistryCollectFacesForFamily`) and asking `FontSelect` for the best one at the requested
 *   settings. A name the registry does not have — the common case for a document that asks for a web font
 *   this build does not ship — is an honest NULL rather than a guess, so the caller's next family is
 *   tried.
 *
 *   the two role names the system pack itself speaks (`ui-display`, `ui-text`) resolve to the pack's
 *   `display`/`text` roles, which is how a document asks for the *system's* UI face by name.
 *
 * A NULL face is `RoboOk`: "this family matches nothing" is an answer, and it is what makes the ordered
 * walk in `LayoutFontRequestSelect` a real fallback rather than one attempt.
 *
 * ## Ownership
 *
 * The provider **borrows** its registry and pack and owns only a generic resolver built over them. Every
 * face it hands back is borrowed from the registry, which owns it for as long as it is registered — so a
 * run may hold the pointer without a reference, exactly as `LayoutTextRun.face` documents.
 */
#ifndef ROBO_DESKTOP_FONT_PROVIDER_H
#define ROBO_DESKTOP_FONT_PROVIDER_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSystemPack.h"

#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboLayout/RoboLayoutFontResolver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DesktopFontProvider DesktopFontProvider;

/**
 * Create a provider over @p registry, using @p pack for its generic-family mappings.
 *
 * @param registry Required; borrowed and must outlive the provider. It owns every face the provider
 *                 selects.
 * @param pack     Optional; borrowed. When given, its `FontSystemPackApplyGenericConfig` mappings are
 *                 installed, so `system-ui`/`sans-serif`/`monospace` resolve the system's way. A NULL pack
 *                 still resolves named families and falls back to the pack roles it cannot ask about.
 * @param diag     Optional.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL @p registry or @p out; RoboOutOfMemory.
 */
Result DesktopFontProviderCreate(FontRegistry *registry,
                                 const FontSystemPack *pack,
                                 DesktopFontProvider **out,
                                 DesktopDiag *diag);

/** Destroy @p provider. Never touches the borrowed registry or pack. Safe on NULL. */
void DesktopFontProviderDestroy(DesktopFontProvider *provider);

/** The seam the measurer and painter are handed. Borrowed; live only while @p provider is. */
LayoutFontProvider DesktopFontProviderLayout(const DesktopFontProvider *provider);

/** Families the provider resolved to a face; for a diagnostic. 0 for NULL. */
uint32_t DesktopFontProviderResolved(const DesktopFontProvider *provider);
/** Family lookups that matched nothing; for a diagnostic. 0 for NULL. */
uint32_t DesktopFontProviderUnresolved(const DesktopFontProvider *provider);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_FONT_PROVIDER_H */
