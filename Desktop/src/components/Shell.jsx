/*
 * The desktop's surfaces — everything that is not a window.
 *
 * One rule governs every element below, and it is the stylesheet's rule seen from the other side:
 * **one class per element.** The scene's drift check takes the whole class attribute as a single name
 * (`class='a b'` would be looked up as `.a b`), so a component that wants a base appearance *and* a
 * variant picks one class, and a component that wants two things at once nests two elements — which
 * is why every window is a `.windowSlotN` wrapper around a `.window`, and why the dock's icons are
 * `.dockIcon` *or* `.dockIconRunning` rather than both.
 *
 * Accessibility is written here rather than later (14.61): the roles and names are what a control
 * announces, and a shell retrofitted with them is a shell whose structure assumed the wrong thing.
 * They cost nothing in this build — the document carries them, the bridge exposes them, Phase 31
 * consumes them.
 */

import React from "react";
import { NotificationKind, ShellSurface } from "../state/desktopState.js";

/** The background: a base surface and one tinted band, because this build paints no gradient. */
export function Wallpaper() {
  return (
    <>
      <div className="wallpaper" role="presentation" />
      <div className="wallpaperSheen" role="presentation" />
    </>
  );
}

/**
 * Desktop icons (14.36).
 *
 * Selection is a *state* fact, so the selected tile is its own class rather than a second class
 * appended to the first — the same one-class rule, and it has a real consequence here: a document
 * cannot select an icon yet (the guest has no script), so what the compiled artifact shows is the
 * *initial* selection rather than an interaction it cannot perform.
 */
export function DesktopIcons({ icons = [], selected = null }) {
  return (
    <div className="desktopIcons" role="list" aria-label="Desktop icons">
      {icons.map((icon) => (
        <div
          key={icon.id}
          className="desktopIcon"
          role="listitem"
          aria-selected={icon.id === selected ? "true" : "false"}
          data-icon={icon.id}
        >
          <div className={icon.id === selected ? "desktopIconTileActive" : "desktopIconTile"} />
          <div className="desktopIconLabel">{icon.name}</div>
        </div>
      ))}
    </div>
  );
}

/**
 * The system bar (14.8).
 *
 * Three facts and no more: which application owns the menu, which workspace is current, and the
 * clock with the system's own status. Everything else a real bar might grow — a notification badge,
 * a battery percentage — is a slot the bridge fills, so they are named here as *text* the state
 * supplies rather than as components with fake data inside them.
 */
export function MenuBar({ title, workspace, workspaces, status }) {
  return (
    <div className="menuBar" role="toolbar" aria-label="System bar">
      <div className="menuBarTitle">{title}</div>
      <div className="menuBarWorkspaces">
        {`${workspace} of ${workspaces}`}
      </div>
      <div className="menuBarStatus">{status}</div>
    </div>
  );
}

/**
 * The dock (14.11, 14.12).
 *
 * The running dot is drawn only for a running application, so "not running" is an absent element
 * rather than a hidden one — which is what keeps the drift check honest, since a class that is never
 * written is a class the sheet may not define.
 */
export function Dock({ items = [], focusedApplication = null, highlight = -1 }) {
  return (
    <div className="dock" role="toolbar" aria-label="Applications">
      {items.map((item, index) => (
        <div
          key={item.id}
          className="dockItem"
          role="button"
          aria-label={item.name}
          aria-pressed={item.focused ? "true" : "false"}
          aria-current={index === highlight ? "true" : undefined}
          data-application={item.id}
        >
          <div className={item.focused || item.id === focusedApplication ? "dockIconRunning" : "dockIcon"} />
          {item.running ? <div className="dockIndicator" /> : null}
        </div>
      ))}
    </div>
  );
}

/** A menu (14.9, 14.10, 14.51): one highlighted row, and separators that are elements, not margins. */
export function Menu({ items = [], highlight = 0 }) {
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

/** A notification (14.32). The kind is the caller's; the surface reads a token for it. */
export function Notification({ title, body, kind = NotificationKind.Info }) {
  return (
    <div className="notification" role="status" aria-live="polite" data-kind={kind}>
      <div className="notificationTitle">{title}</div>
      <div className="notificationBody">{body}</div>
    </div>
  );
}

/**
 * A modal dialog (14.31).
 *
 * `aria-modal` is on the dialog and the buttons are named, because the two things a modal has to get
 * right are the focus scope and the escape route; both are properties the document states rather than
 * behaviour it performs. The buttons are drawn, and their commands are the bridge's.
 */
export function Dialog({ title, body, actions = [], labelledBy }) {
  return (
    <div className="dialog" role="dialog" aria-modal="true" aria-labelledby={labelledBy}>
      <div className="dialogTitle">{title}</div>
      <div className="dialogBody">{body}</div>
      <div className="actions">
        {actions.map((action) => (
          <div
            key={action.id}
            className={action.primary ? "buttonPrimary" : "button"}
            role="button"
            aria-label={action.label}
            data-command={action.id}
          >
            {action.label}
          </div>
        ))}
      </div>
    </div>
  );
}

/**
 * The overlay layer: whichever shell surface the state has open, and nothing else.
 *
 * The order of these `if`s *is* the layer order — paint is document order in this build, so a menu
 * that must be above a notification is rendered after it, and no `z-index` appears anywhere in the
 * Desktop project. `layerOrder` in the tokens file names that order once.
 */
export function Overlays({ state, menu, dialog }) {
  if (state.surface === ShellSurface.None) {
    return <div className="overlays" role="presentation" />;
  }

  return (
    <div className="overlays" role="presentation">
      {state.surface === ShellSurface.ApplicationMenu || state.surface === ShellSurface.ContextMenu
        ? menu
        : null}
      {state.surface === ShellSurface.Dialog ? dialog : null}
      {state.notifications.length ? (
        <Notification
          title={state.notifications[0].title}
          body={state.notifications[0].body}
          kind={state.notifications[0].kind}
        />
      ) : null}
    </div>
  );
}
