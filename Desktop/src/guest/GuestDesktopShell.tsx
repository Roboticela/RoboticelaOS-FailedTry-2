/*
 * Guest desktop shell — static markup for RoboCSS / the OS scene.
 *
 * This tree is what `export-page.mjs` SSR-renders. It must stay one-class-per-element and free of
 * Tailwind utilities, handlers, and inline styles. The interactive Vite lab lives under
 * `components/interactive/` and never ships into DesktopPage.h.
 */

import type { ReactElement } from "react";

import { buildDesktopModel, type DesktopModel } from "@/session/model";
import { ShellSurface, WindowState, type DesktopWindow, type MenuRow } from "@/state/desktopState";
import { renderDesktopStyleSheet } from "@/theme/stylesheet";

function TitleBar({ window, focused }: { window: DesktopWindow; focused: boolean }) {
  return (
    <div className={focused ? "titleBar" : "titleBarInactive"} role="presentation">
      {window.closable ? (
        <div className="trafficClose" role="button" aria-label={`Close ${window.title}`} data-command="window.close" />
      ) : null}
      <div className="trafficMinimize" role="button" aria-label={`Minimize ${window.title}`} data-command="window.minimize" />
      <div className="trafficZoom" role="button" aria-label={`Zoom ${window.title}`} data-command="window.maximize" />
      <div className={focused ? "titleText" : "titleTextInactive"} role="heading" aria-level={1}>
        {window.title}
      </div>
    </div>
  );
}

function WindowContent({ kind }: { kind: DesktopWindow["content"] }) {
  if (kind === "inputlab") {
    return (
      <div className="windowContent">
        <div className="heading">Input Lab</div>
        <div className="body">
          Press keys to see them in the menu-bar status. Click a window title to raise it. Click the red
          light to close. Escape ends the session.
        </div>
        <div className="sectionLabel">Text fields</div>
        <div className="field" role="textbox" aria-label="Empty field">
          Empty
        </div>
        <div className="fieldFocused" role="textbox" aria-label="Focused field">
          Focused — type to update status
        </div>
        <div className="fieldDisabled" role="textbox" aria-disabled="true" aria-label="Disabled field">
          Disabled
        </div>
        <div className="fieldError" role="textbox" aria-invalid="true" aria-label="Error field">
          Needs a value
        </div>
        <div className="sectionLabel">Buttons</div>
        <div className="row">
          <div className="buttonPrimary" role="button" aria-label="Primary">
            Primary
          </div>
          <div className="button" role="button" aria-label="Secondary">
            Secondary
          </div>
        </div>
        <div className="row">
          <div className="buttonActive" role="button" aria-pressed="true" aria-label="Pressed">
            Pressed
          </div>
          <div className="buttonDisabled" role="button" aria-disabled="true" aria-label="Disabled">
            Disabled
          </div>
        </div>
        <div className="sectionLabel">Toggles and checks</div>
        <div className="row">
          <div className="toggleOn" role="switch" aria-checked="true" aria-label="On" />
          <div className="toggle" role="switch" aria-checked="false" aria-label="Off" />
          <div className="checkboxOn" role="checkbox" aria-checked="true" aria-label="Checked" />
          <div className="checkbox" role="checkbox" aria-checked="false" aria-label="Unchecked" />
        </div>
        <div className="sectionLabel">Slider</div>
        <div className="sliderTrack">
          <div className="sliderFill" />
        </div>
        <div className="sectionLabel">Pointer and keyboard</div>
        <div className="labBadge">Mouse · raise · close · cursor</div>
        <div className="labBadge">Keyboard · LogicalKey · modifiers · Escape</div>
        <div className="caption">Live key names appear in the menu bar on every KeyDown.</div>
      </div>
    );
  }

  if (kind === "overview") {
    return (
      <div className="windowContent">
        <div className="heading">System Overview</div>
        <div className="body">Everything landed through Phase 15 that you can see on this desk.</div>
        <div className="mono">Typography · RoboText · Paint · Layout</div>
        <div className="mono">Input model · Window · Compositor</div>
        <div className="mono">Desktop shell · Document bridge</div>
        <div className="caption">Open Input Lab to exercise keyboard and pointer.</div>
        <div className="row">
          <div className="buttonPrimary" role="button" aria-label="Open Input Lab">
            Open Input Lab
          </div>
          <div className="button" role="button" aria-label="About">
            About
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="windowContent">
      <div className="specimenTitle">Beautiful by default</div>
      <div className="specimenBody">
        RoboticelaOS shapes every label with RoboText: one glyph run per script, one face from the system
        pack, one pen.
      </div>
    </div>
  );
}

function GuestWindow({ window, focused }: { window: DesktopWindow & { slot: string }; focused: boolean }) {
  return (
    <div className={window.slot} role="presentation">
      <div className={focused ? "window" : "windowInactive"} role="group" aria-label={window.title}>
        <TitleBar window={window} focused={focused} />
        <WindowContent kind={window.content} />
      </div>
    </div>
  );
}

function GuestMenu({ items, highlight }: { items: MenuRow[]; highlight: number }) {
  return (
    <div className="menu" role="menu" aria-label="Menu">
      {items.map((item, index) =>
        item.separator ? (
          <div key={`separator-${index}`} className="menuSeparator" role="separator" />
        ) : (
          <div
            key={item.id}
            className={index === highlight ? "menuItemSelected" : "menuItem"}
            role="menuitem"
            aria-disabled={item.disabled ? "true" : "false"}
            data-command={item.id}
          >
            {item.label}
          </div>
        )
      )}
    </div>
  );
}

/** Static guest document root. */
export function GuestDesktopShell({ model = buildDesktopModel() }: { model?: DesktopModel }): ReactElement {
  const { state } = model;
  const ordered = model.state.windows
    .filter((record) => record.state !== WindowState.Closed)
    .sort((a, b) => (a.z === b.z ? a.id.localeCompare(b.id) : a.z - b.z))
    .map((record, index) => ({ ...record, slot: `windowSlot${index}` }));
  const painted = ordered.filter((record) => record.state !== WindowState.Minimized);
  const menuRows = state.surface === ShellSurface.ApplicationMenu ? model.menu : [];
  const focusedApplication = state.windows.find((window) => window.id === state.focusedWindow)?.applicationId ?? null;

  return (
    <div className="desktop" id="desktopRoot" role="application" aria-label="RoboticelaOS Desktop">
      <div className="wallpaper" role="presentation" />
      <div className="wallpaperSheen" role="presentation" />
      <div className="desktopIcons" role="list" aria-label="Desktop icons">
        {model.icons.map((icon) => (
          <div
            key={icon.id}
            className="desktopIcon"
            role="listitem"
            aria-selected={icon.id === model.icons[0]?.id ? "true" : "false"}
            data-icon={icon.id}
          >
            <div className={icon.id === model.icons[0]?.id ? "desktopIconTileActive" : "desktopIconTile"} />
            <div className="desktopIconLabel">{icon.name}</div>
          </div>
        ))}
      </div>
      <div className="windows" role="group" aria-label="Windows">
        {painted.map((record) => (
          <GuestWindow key={record.id} window={record} focused={record.id === state.focusedWindow} />
        ))}
      </div>
      <div className="menuBar" role="toolbar" aria-label="System bar">
        <div className="menuBarBrand">Roboticela</div>
        <div className="menuBarTitle">Input Lab</div>
        <div className="menuBarMenus">File  Edit  View  Window  Help</div>
        <div className="menuBarWorkspaces">{`${state.activeWorkspace} of ${state.workspaces.length}`}</div>
        <div className="menuBarStatus">Ready</div>
      </div>
      <div className="dock" role="toolbar" aria-label="Applications">
        {state.dock.map((item, index) => {
          const running = item.focused || item.id === focusedApplication;
          const iconClass = item.iconClass
            ? item.iconClass
            : running
              ? "dockIconRunning"
              : "dockIcon";
          return (
            <div
              key={item.id}
              className="dockItem"
              role="button"
              aria-label={item.name}
              aria-pressed={item.focused ? "true" : "false"}
              aria-current={index === 1 ? "true" : undefined}
              data-application={item.id}
            >
              <div className={iconClass} />
              {item.running ? <div className="dockIndicator" /> : null}
            </div>
          );
        })}
      </div>
      <div className="overlays" role="presentation">
        {menuRows.length ? <GuestMenu items={menuRows} highlight={state.menuHighlight} /> : null}
      </div>
    </div>
  );
}

export function desktopStyleSheetOptions(model: DesktopModel) {
  const visible = model.state.windows
    .filter((record) => record.state !== WindowState.Minimized && record.state !== WindowState.Closed)
    .sort((a, b) => (a.z === b.z ? a.id.localeCompare(b.id) : a.z - b.z))
    .map((record, index) => ({
      slot: `windowSlot${index}`,
      x: record.x,
      y: record.y,
      width: record.width,
      height: record.height,
    }));

  return {
    appearance: model.state.appearance,
    windows: visible,
  };
}

export function renderGuestPage(model = buildDesktopModel()) {
  return {
    model,
    style: renderDesktopStyleSheet(desktopStyleSheetOptions(model)),
  };
}
