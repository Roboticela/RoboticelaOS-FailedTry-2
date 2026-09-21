import React from "react";
import { createRoot } from "react-dom/client";
import { DesktopShell } from "./DesktopShell.jsx";
import "./desktop.css";

const root = document.getElementById("root");
if (root) {
  createRoot(root).render(
    <React.StrictMode>
      <DesktopShell />
    </React.StrictMode>
  );
}
