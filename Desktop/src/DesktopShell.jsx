/*
 * The desktop shell — the composition root, and the one door the build and the dev preview both call.
 *
 * ## What is compiled, and what the guest gets
 *
 * `buildDesktopModel()` is the *whole* shell state at build time, and `DesktopShell` renders it to
 * static markup. `Desktop/scripts/export-page.mjs` writes that markup and the generated stylesheet
 * into the C page the runtime builds a document from, so the artifact the OS loads is HTML plus CSS —
 * `Desktop/dist/` — and React is what *produced* it (14.42, 14.46). Nothing in this file runs in the
 * guest: there is no `useState`, no effect and no event handler, because a handler in the compiled
 * artifact would be a function the runtime had no script engine to call. The shell's *behaviour*
 * lives natively (`RoboDesktopScene`, `RoboDesktop.c`) and reaches this markup through the same
 * state model, which is why the model is a plain module and not a hook.
 *
 * ## Why the model is built here rather than imported
 *
 * One state, one dispatch: `desktop.create()` plus the managers in `state/desktopState.js` is what a
 * runtime action calls, and this function is the *initial* dispatch — the windows a fresh session has.
 * A reader looking for "what does the desktop look like when nothing has happened yet" reads the
 * `window.open()` calls below and nothing else, and the day the bridge hands over a restored session
 * the difference is the argument to this function rather than a second composition.
 *
 * ## The slot names
 *
 * Every window's rectangle has to reach the stylesheet, because the compiled document has no script to
 * position it (see `theme/stylesheet.js`). The name is derived here — `windowSlotN`, N being the
 * window's index in paint order — so the markup and the sheet are two halves of one list rather than
 * two lists that must agree. `desktopStyleSheetOptions()` is the other half, and it is generated from
 * the same model the markup was.
 */

import React from "react";

import { Dock, DesktopIcons, Dialog, Menu, MenuBar, Overlays, Wallpaper } from "./components/Shell.jsx";
import { WindowSurface } from "./components/Window.jsx";
import { ShellSurface, WindowState, desktop } from "./state/desktopState.js";
import { tokensFor } from "./theme/tokens.js";

/** The desktop's icons, as data (14.36). An icon is a name and a target, never a component. */
export const desktopIcons = [
  { id: "system", name: "System" },
  { id: "documents", name: "Documents" },
  { id: "typespecimen", name: "Specimen" },
];

/** The dock's applications (14.12), as data the bridge can launch by id. */
export const dockItems = [
  { id: "systemui.launcher", name: "Launcher", running: true, focused: true },
  { id: "systemui.files", name: "Files", running: true, focused: false },
  { id: "systemui.typespecimen", name: "Specimen", running: true, focused: false },
  { id: "systemui.inspector", name: "Inspector", running: false, focused: false },
  { id: "systemui.settings", name: "Settings", running: false, focused: false },
  { id: "systemui.terminal", name: "Terminal", running: false, focused: false },
];

/** The application menu's rows (14.9). A separator is a row, so the highlight indexes the rows. */
export const applicationMenu = [
  { id: "file.newWindow", label: "New Window" },
  { id: "file.newFolder", label: "New Folder" },
  { id: "file.open", label: "Open…" },
  { id: "file.close", label: "Close Window" },
  { separator: true },
  { id: "edit.copy", label: "Copy" },
  { id: "edit.paste", label: "Paste" },
  { separator: true },
  { id: "system.settings", label: "Display Settings…" },
  { id: "system.about", label: "About This System" },
];

/**
 * The initial session.
 *
 * Three windows, one workspace each side of the first: a *focused* specimen window, an unfocused
 * inspector, and a minimized one — because the three states a window can be in are exactly what a
 * reader needs to see in one frame, and a screenshot of three identical windows proves nothing about
 * the layer that draws them.
 */
export function buildDesktopModel({ display = { width: 1024, height: 768, scale: 1 } } = {}) {
  let state = desktop.create({ display, appearance: "light", dock: dockItems });

  state = desktop.window.open(state, {
    applicationId: "systemui.typespecimen",
    title: "Welcome",
    x: 96,
    y: 96,
    width: 520,
    height: 364,
    content: "specimen",
    minWidth: 320,
    minHeight: 240,
  });
  state = desktop.window.open(state, {
    applicationId: "systemui.inspector",
    title: "Inspector",
    x: 536,
    y: 232,
    width: 400,
    height: 300,
    content: "inspector",
  });
  const minimized = desktop.window.open(state, {
    applicationId: "systemui.terminal",
    title: "Terminal",
    x: 140,
    y: 520,
    width: 480,
    height: 260,
  });
  state = desktop.window.minimize(minimized, "window-3");
  /* And the specimen keeps the keyboard: minimizing the third window moved focus to it, and a shell
   * whose focused window is the *minimized* one is the bug 14.5's `minimize` exists to prevent. */
  state = desktop.window.focus(state, "window-1", "programmatic");

  /* One transient surface open on a fresh session: the application menu, which is also what proves
   * the overlay layer is above the windows. */
  state = desktop.menu.open(state, ShellSurface.ApplicationMenu, { x: 96, y: 30 }, 0);

  const ordered = desktop.paintOrder(state);
  const windows = ordered.map((record, index) => ({ ...record, slot: `windowSlot${index}` }));

  return { state, windows, icons: desktopIcons, menu: applicationMenu };
}

/** The menu's own rows, or none when no surface is open — the overlay layer asks once. */
function menuFor(model) {
  return model.state.surface === ShellSurface.ApplicationMenu ? model.menu : [];
}

/**
 * The shell document.
 *
 * The order of the children **is** the layer order (14.18): wallpaper, icons, windows, chrome,
 * overlays. There is no `z-index` anywhere in the Desktop project, and that is a decision rather than
 * an omission — this build paints in document order, so the only place a stacking rule can live is
 * here, in one function a reviewer can read top to bottom.
 */
export function DesktopShell({ model = buildDesktopModel() }) {
  const { state, windows } = model;
  const menuRows = menuFor(model);
  /*
   * The window layer paints what is not minimized. A minimized window is *present in the state* -- it
   * has a rectangle, a restore point and a place in the z-order -- and it is **absent from the
   * document**, which is what "minimized" means in a build that has no way to hide a box: a `hidden`
   * window would still be laid out, still cost a slot rule, and still be one style away from visible.
   */
  const painted = windows.filter((record) => record.state !== WindowState.Minimized);

  return (
    <div className="desktop" role="application" aria-label="RoboticelaOS Desktop">
      <Wallpaper />
      <DesktopIcons icons={model.icons} selected={model.icons[2].id} />

      <div className="windows" role="group" aria-label="Windows">
        {painted.map((record) => (
          <WindowSurface key={record.id} window={record} focused={record.id === state.focusedWindow} />
        ))}
      </div>

      <MenuBar
        title="Specimen"
        workspace={state.activeWorkspace}
        workspaces={state.workspaces.length}
        status={`${state.display.width} × ${state.display.height}`}
      />

      <Dock items={state.dock} focusedApplication={desktop.focused(state)?.applicationId ?? null} highlight={1} />

      <Overlays
        state={state}
        menu={<Menu items={menuRows} highlight={state.menuHighlight} />}
        dialog={
          <Dialog
            title="Close Window"
            body="The specimen keeps its state and reopens where it was."
            actions={[
              { id: "dialog.cancel", label: "Cancel", primary: false },
              { id: "dialog.close", label: "Close", primary: true },
            ]}
          />
        }
      />
    </div>
  );
}

/** The stylesheet options for a model: the appearance, and every window's rectangle. */
export function desktopStyleSheetOptions(model) {
  const visible = model.windows.filter((record) => record.state !== "minimized");

  return {
    appearance: model.state.appearance,
    windows: visible.map((record) => ({
      slot: record.slot,
      x: record.x,
      y: record.y,
      width: record.width,
      height: record.height,
    })),
  };
}

/** The shell's tokens, for a component that needs a value React has to know (a size, an appearance). */
export function desktopTokens(model) {
  return tokensFor(model.state.appearance);
}
