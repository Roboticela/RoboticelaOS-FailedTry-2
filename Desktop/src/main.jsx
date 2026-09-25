/*
 * The development preview (`npm run dev`).
 *
 * This file is **not** part of the runtime artifact and nothing in the guest loads it: the compiled
 * page is `DesktopShell` rendered to static markup, and the OS builds a document from that markup plus
 * the generated sheet. What the preview is for is looking at the shell in a browser while it is being
 * written, and it earns its place by exercising the same two functions the build does —
 * `buildDesktopModel()` and `renderDesktopStyleSheet()` — so a value that looks wrong in the preview is
 * wrong in the artifact rather than only in the preview.
 *
 * The sheet is *injected as text* rather than imported as a `.css` file, which is the point: there is
 * no hand-written stylesheet in this project for Vite to process, so a token change cannot reach the
 * preview by a different path than it reaches the image.
 */

import React from "react";
import { createRoot } from "react-dom/client";

import { DesktopShell, buildDesktopModel, desktopStyleSheetOptions } from "./DesktopShell.jsx";
import { renderDesktopStyleSheet } from "./theme/stylesheet.js";

const model = buildDesktopModel({
  display: {
    width: Math.max(800, window.innerWidth),
    height: Math.max(600, window.innerHeight),
    scale: 1,
  },
});

const sheet = document.createElement("style");
sheet.setAttribute("data-source", "renderDesktopStyleSheet");
sheet.textContent = renderDesktopStyleSheet(desktopStyleSheetOptions(model));
document.head.append(sheet);

const root = document.getElementById("root");
if (root) {
  createRoot(root).render(<DesktopShell model={model} />);
}
