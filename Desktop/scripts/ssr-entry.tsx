/*
 * The build's entry point: render the guest shell once, and hand back both halves of the document.
 */

import React from "react";
import { renderToStaticMarkup } from "react-dom/server";

import { GuestDesktopShell, desktopStyleSheetOptions } from "../src/guest/GuestDesktopShell";
import { buildDesktopModel } from "../src/session/model";
import { renderDesktopStyleSheet } from "../src/theme/stylesheet";

export function renderDesktopPage() {
  const model = buildDesktopModel();
  const markup = renderToStaticMarkup(React.createElement(GuestDesktopShell, { model }));
  const style = renderDesktopStyleSheet(desktopStyleSheetOptions(model));

  return {
    markup,
    style,
    appearance: model.state.appearance,
    windows: model.state.windows.length,
  };
}
