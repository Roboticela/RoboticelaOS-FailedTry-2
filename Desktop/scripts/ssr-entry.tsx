/*
 * The build's entry point: render each guest app document, and hand back markup + shared sheet.
 */

import React from "react";
import { renderToStaticMarkup } from "react-dom/server";

import {
  GuestInputLabApp,
  GuestOverviewApp,
  desktopStyleSheetOptions,
} from "../src/guest/GuestDesktopShell";
import { buildDesktopModel } from "../src/session/model";
import { renderDesktopStyleSheet } from "../src/theme/stylesheet";

export function renderDesktopPage() {
  const model = buildDesktopModel();
  const overview = renderToStaticMarkup(React.createElement(GuestOverviewApp));
  const inputLab = renderToStaticMarkup(React.createElement(GuestInputLabApp));
  const style = renderDesktopStyleSheet(desktopStyleSheetOptions(model));

  return {
    /** Primary markup for DesktopSceneMarkup() — Overview. */
    markup: overview,
    overview,
    inputLab,
    style,
    appearance: model.state.appearance,
    windows: model.state.windows.filter((w) => w.state !== "closed").length,
  };
}
