/*
 * The build's entry point: render the shell once, and hand back both halves of the document.
 *
 * Markup and stylesheet come out of **one** model and one call, which is the property that keeps them
 * from drifting: `buildDesktopModel()` decides which windows exist, `DesktopShell` writes their markup
 * and `desktopStyleSheetOptions()` writes their rectangles into the sheet. A build that rendered the
 * two independently would be two lists that must agree, and the class-drift check would be the only
 * thing left to catch the difference.
 *
 * `renderToStaticMarkup` and not `renderToString`: the artifact is a static document, so no
 * hydration markers, no `<!-- -->` separators and no attribute ordering React reserves for a
 * resume. The guest has nothing to hydrate with, and a comment nobody reads is bytes in a C string
 * literal with a 64 KiB ceiling.
 */

import React from "react";
import { renderToStaticMarkup } from "react-dom/server";

import { DesktopShell, buildDesktopModel, desktopStyleSheetOptions } from "../src/DesktopShell.jsx";
import { renderDesktopStyleSheet } from "../src/theme/stylesheet.js";

/** The compiled page: the document's two halves plus the numbers the build reports and validates. */
export function renderDesktopPage() {
  const model = buildDesktopModel();
  const markup = renderToStaticMarkup(React.createElement(DesktopShell, { model }));
  const style = renderDesktopStyleSheet(desktopStyleSheetOptions(model));

  /*
   * The markup comes back **as React wrote it** — double-quoted attributes — and the export script
   * normalizes the quoting once, for the C side whose class scan reads `class='`. Doing it here would
   * put the conversion in a module whose job is rendering, and doing it twice is how two
   * representations of one document appear.
   */
  return {
    markup,
    style,
    appearance: model.state.appearance,
    windows: model.windows.length,
  };
}
