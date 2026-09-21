import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { DesktopShell } from "../src/DesktopShell.jsx";

export function renderDesktopHtml() {
  return renderToStaticMarkup(React.createElement(DesktopShell));
}
