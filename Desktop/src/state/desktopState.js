/*
 * The desktop's state — Phase 14.3's "centralized desktop state", as a pure reducer module.
 *
 * There is **one** state object and every manager is a function from a state to a state. Nothing here
 * imports React, touches the DOM, or reads a global: the shell's components render what this yields,
 * and `Desktop/tests/desktopState.test.mjs` drives the same functions in Node. That split is what
 * makes the interesting half of the shell testable without a renderer — "closing a window closes its
 * notifications" is a fact about two numbers, not about a screenshot.
 *
 * ## Why the managers are one object with a `state` field
 *
 * A reducer that returned a bare state would let a caller hold a stale window and mutate it, which is
 * the failure mode 14.3 exists to prevent ("do not allow unrelated components to maintain conflicting
 * copies"). Every mutation is therefore a **new** state and a **new** array of windows; a component
 * cannot grow a second copy by accident because there is nothing here to copy — and `Object.freeze` in
 * development is not needed to get that, only the discipline of never returning the argument.
 *
 * ## What the native runtime owns, and what this owns
 *
 * This models the *shell's* view: which windows exist on which workspace, which one has focus, what
 * the dock shows, what the notification centre holds. It does not own an application process, its
 * pixels, or its lifetime — `DesktopBridge` (14.40) is the door to that, and a window here carries an
 * opaque `applicationId` that the bridge is free to interpret. A window whose backing application has
 * gone away is therefore `closed` here *and* reported by the bridge, rather than this module pretending
 * to have killed a process.
 */

/** Window lifecycle, as the four states the phase names plus the two that are transitions. */
export const WindowState = Object.freeze({
  Normal: "normal",
  Minimized: "minimized",
  Maximized: "maximized",
  Fullscreen: "fullscreen",
  Closing: "closing",
  Closed: "closed",
});

/** Where a snapped window is anchored. Values are what a drag-to-edge gesture resolves to. */
export const SnapEdge = Object.freeze({
  Left: "left",
  Right: "right",
  Top: "top",
  TopLeft: "topLeft",
  TopRight: "topRight",
  BottomLeft: "bottomLeft",
  BottomRight: "bottomRight",
});

/** Notification severity, which selects the token the surface tints with. */
export const NotificationKind = Object.freeze({
  Info: "info",
  Success: "success",
  Warning: "warning",
  Error: "error",
  Progress: "progress",
});

/** The shell's own surfaces, for `menu.open` — one overlay is open at a time, and this says which. */
export const ShellSurface = Object.freeze({
  None: "none",
  ApplicationMenu: "applicationMenu",
  SystemMenu: "systemMenu",
  ContextMenu: "contextMenu",
  NotificationCentre: "notificationCentre",
  Launcher: "launcher",
  ApplicationSwitcher: "applicationSwitcher",
  Dialog: "dialog",
});

/** A minimum layout the display may not be smaller than, so a maximized window is never negative. */
function clamp(value, low, high) {
  return value < low ? low : value > high ? high : value;
}

/**
 * One window record. `z` is the shell's own ordering number: raising a window gives it the next
 * number, and the layer sorts by it rather than by array index, which is what keeps z-order
 * *deterministic* when a window is removed from the middle (14.18).
 */
export function createWindow({
  id,
  applicationId,
  title,
  icon = null,
  x = 0,
  y = 0,
  width = 520,
  height = 360,
  minWidth = 240,
  minHeight = 160,
  maxWidth = 0,
  maxHeight = 0,
  state = WindowState.Normal,
  resizable = true,
  movable = true,
  closable = true,
  modal = false,
  workspace = 1,
  z = 0,
  content = null,
}) {
  return Object.freeze({
    id,
    applicationId,
    title,
    icon,
    x,
    y,
    width,
    height,
    minWidth,
    minHeight,
    maxWidth,
    maxHeight,
    state,
    resizable,
    movable,
    closable,
    modal,
    workspace,
    z,
    content,
    /* The geometry to come back to from a maximize, a snap or a minimize. Restoring is a *return*
     * rather than a re-computation: a build that recomputed a restore would place a maximized window
     * wherever the restored size happens to fit, which is a different window than the user had. */
    restore: null,
  });
}

/**
 * The initial shell state.
 *
 * `display` is the logical viewport in CSS pixels, which is what every snap and maximize resolves
 * against; the shell takes it from the bridge (14.40) at runtime and from a token default at build
 * time, and both go through this one function.
 */
export function createDesktopState({
  display = { width: 1024, height: 768, scale: 1 },
  windows = [],
  workspaces = 3,
  appearance = "light",
  dock = [],
} = {}) {
  const state = {
    display,
    appearance,
    workspaces: Array.from({ length: workspaces }, (_, index) => ({ index: index + 1, windows: [] })),
    activeWorkspace: 1,
    windows: [],
    nextWindowId: 1,
    nextZ: 1,
    focusedWindow: null,
    focusReason: "programmatic",
    pointer: { x: 0, y: 0, captured: null, hover: null },
    dock,
    surface: ShellSurface.None,
    surfaceAnchor: null,
    menuHighlight: -1,
    launcherQuery: "",
    notifications: [],
    nextNotificationId: 1,
    dialogs: [],
  };

  return windows.reduce((current, spec) => windowManager.open(current, spec), state);
}

/** Every window, sorted by z (lowest first), which is the order the window layer paints them in. */
export function windowsInPaintOrder(state) {
  return [...state.windows]
    .filter((window) => window.state !== WindowState.Closed)
    .sort((a, b) => (a.z === b.z ? a.id.localeCompare(b.id) : a.z - b.z));
}

/** The windows on a workspace, in paint order. A minimized window is present and not painted. */
export function windowsOnWorkspace(state, workspace = state.activeWorkspace) {
  return windowsInPaintOrder(state).filter(
    (window) => window.workspace === workspace && window.state !== WindowState.Minimized
  );
}

/** The window that owns the keyboard, or `null`. Modal windows take it from the window they belong to. */
export function focusedWindow(state) {
  if (!state.focusedWindow) {
    return null;
  }
  return state.windows.find((window) => window.id === state.focusedWindow) ?? null;
}

function replaceWindow(state, id, update) {
  return {
    ...state,
    windows: state.windows.map((window) => (window.id === id ? Object.freeze(update(window)) : window)),
  };
}

function withWorkspaceMembership(state, window) {
  return {
    ...state,
    workspaces: state.workspaces.map((workspace) =>
      workspace.index === window.workspace
        ? { ...workspace, windows: [...new Set([...workspace.windows, window.id])] }
        : workspace
    ),
  };
}

/**
 * The window manager (14.5).
 *
 * Every door here is `(state, …) => state`. The interesting ones are the three that have a *rule*
 * rather than an assignment:
 *
 *   `focus` raises by taking the next z, so focus and stacking cannot disagree — a focused window
 *   below an unfocused one is the classic symptom of two counters;
 *
 *   `minimize` moves focus to the next window **on the same workspace**, because a minimized window
 *   that kept the keyboard would type into nothing;
 *
 *   `close` drops the window's notifications and its focus in one step, and never leaves
 *   `focusedWindow` pointing at an id with no window behind it (13.44's rule, applied here).
 */
export const windowManager = {
  /** Open a window, focus it, and put it at the top of the stack. */
  open(state, spec) {
    const id = spec.id ?? `window-${state.nextWindowId}`;
    const workspace = spec.workspace ?? state.activeWorkspace;
    const window = createWindow({
      ...spec,
      id,
      workspace,
      z: state.nextZ,
    });
    const opened = {
      ...state,
      windows: [...state.windows, window],
      nextWindowId: state.nextWindowId + 1,
      nextZ: state.nextZ + 1,
      focusedWindow: id,
      focusReason: spec.focusReason ?? "programmatic",
    };

    return withWorkspaceMembership(opened, window);
  },

  /** Close a window: it leaves the stack, the workspace, the focus and the notification centre. */
  close(state, id) {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window || window.state === WindowState.Closed) {
      return state;
    }

    const remaining = state.windows.filter((candidate) => candidate.id !== id);
    const next = remaining
      .filter((candidate) => candidate.workspace === window.workspace)
      .sort((a, b) => b.z - a.z)[0];

    return {
      ...state,
      windows: remaining,
      workspaces: state.workspaces.map((workspace) => ({
        ...workspace,
        windows: workspace.windows.filter((member) => member !== id),
      })),
      focusedWindow: state.focusedWindow === id ? (next?.id ?? null) : state.focusedWindow,
      notifications: state.notifications.filter((notification) => notification.windowId !== id),
      dialogs: state.dialogs.filter((dialog) => dialog.windowId !== id),
      pointer: state.pointer.captured === id ? { ...state.pointer, captured: null } : state.pointer,
    };
  },

  /** Focus and raise. A focus request for a window that is gone is a refusal, not a no-op state. */
  focus(state, id, reason = "pointer") {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window || window.state === WindowState.Closed) {
      return state;
    }

    const raised = {
      ...state,
      nextZ: state.nextZ + 1,
      focusedWindow: id,
      focusReason: reason,
    };

    return replaceWindow(raised, id, (current) => ({ ...current, z: raised.nextZ - 1 }));
  },

  /** Switch to a workspace, and hand focus to its topmost window (or none). */
  activateWorkspace(state, index) {
    if (!state.workspaces.some((workspace) => workspace.index === index)) {
      return state;
    }

    const top = windowsInPaintOrder(state).filter(
      (window) => window.workspace === index && window.state !== WindowState.Minimized
    );

    return {
      ...state,
      activeWorkspace: index,
      focusedWindow: top.length ? top[top.length - 1].id : null,
      surface: ShellSurface.None,
    };
  },

  move(state, id, x, y) {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window || !window.movable) {
      return state;
    }

    const bounds = desktopWorkArea(state.display);
    /* A window may hang off an edge but its title bar always stays reachable: a window dragged
     * entirely past the top is one the user cannot get back without a menu, and that is a bug the
     * clamp prevents rather than a feature the phase asks for. */
    const nextX = clamp(x, -window.width + 64, bounds.width - 64);
    const nextY = clamp(y, bounds.top, bounds.height - 24);

    return replaceWindow(state, id, (current) => ({
      ...current,
      x: nextX,
      y: nextY,
      /* A moved window is no longer snapped: the drag is the unsnap. */
      restore: null,
    }));
  },

  /** Resize, clamped per axis by the window's own declared minimums and maximums. */
  resize(state, id, { x, y, width, height }) {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window || !window.resizable) {
      return state;
    }

    const maxWidth = window.maxWidth > 0 ? window.maxWidth : Number.MAX_SAFE_INTEGER;
    const maxHeight = window.maxHeight > 0 ? window.maxHeight : Number.MAX_SAFE_INTEGER;

    return replaceWindow(state, id, (current) => ({
      ...current,
      x: x ?? current.x,
      y: y ?? current.y,
      width: clamp(width ?? current.width, current.minWidth, maxWidth),
      height: clamp(height ?? current.height, current.minHeight, maxHeight),
      restore: null,
    }));
  },

  minimize(state, id) {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window) {
      return state;
    }

    const minimized = replaceWindow(state, id, (current) => ({
      ...current,
      state: WindowState.Minimized,
      restore: current.restore ?? { x: current.x, y: current.y, width: current.width, height: current.height },
    }));

    if (minimized.focusedWindow !== id) {
      return minimized;
    }

    const next = windowsInPaintOrder(minimized)
      .filter((candidate) => candidate.workspace === window.workspace && candidate.state !== WindowState.Minimized)
      .pop();

    return { ...minimized, focusedWindow: next?.id ?? null };
  },

  restore(state, id) {
    return replaceWindow(state, id, (current) => ({
      ...current,
      state: WindowState.Normal,
      ...(current.restore ?? {}),
      restore: null,
    }));
  },

  maximize(state, id) {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window) {
      return state;
    }
    if (window.state === WindowState.Maximized) {
      return windowManager.restore(state, id);
    }

    const bounds = desktopWorkArea(state.display);

    return replaceWindow(state, id, (current) => ({
      ...current,
      state: WindowState.Maximized,
      restore: current.restore ?? { x: current.x, y: current.y, width: current.width, height: current.height },
      x: 0,
      y: bounds.top,
      width: bounds.width,
      height: bounds.height,
    }));
  },

  /** Snap to an edge or a corner (14.17). A snap keeps the restore rectangle like a maximize does. */
  snap(state, id, edge) {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window) {
      return state;
    }

    const bounds = desktopWorkArea(state.display);
    const halfWidth = Math.round(bounds.width / 2);
    const halfHeight = Math.round(bounds.height / 2);

    const rectangles = {
      [SnapEdge.Left]: { x: 0, y: bounds.top, width: halfWidth, height: bounds.height },
      [SnapEdge.Right]: { x: bounds.width - halfWidth, y: bounds.top, width: halfWidth, height: bounds.height },
      [SnapEdge.Top]: { x: 0, y: bounds.top, width: bounds.width, height: bounds.height },
      [SnapEdge.TopLeft]: { x: 0, y: bounds.top, width: halfWidth, height: halfHeight },
      [SnapEdge.TopRight]: {
        x: bounds.width - halfWidth,
        y: bounds.top,
        width: halfWidth,
        height: halfHeight,
      },
      [SnapEdge.BottomLeft]: { x: 0, y: bounds.top + halfHeight, width: halfWidth, height: halfHeight },
      [SnapEdge.BottomRight]: {
        x: bounds.width - halfWidth,
        y: bounds.top + halfHeight,
        width: halfWidth,
        height: halfHeight,
      },
    };

    const rectangle = rectangles[edge];
    if (!rectangle) {
      return state;
    }

    return replaceWindow(state, id, (current) => ({
      ...current,
      state: current.state === WindowState.Maximized ? WindowState.Normal : current.state,
      restore: current.restore ?? { x: current.x, y: current.y, width: current.width, height: current.height },
      ...rectangle,
    }));
  },

  /** Cycle focus among a workspace's windows, which is the launcher's and the switcher's door. */
  cycleFocus(state, direction = 1) {
    const order = windowsInPaintOrder(state).filter(
      (window) => window.workspace === state.activeWorkspace && window.state !== WindowState.Minimized
    );
    if (order.length < 2) {
      return state;
    }

    const index = order.findIndex((window) => window.id === state.focusedWindow);
    const next = order[((index < 0 ? 0 : index) + direction + order.length) % order.length];

    return windowManager.focus(state, next.id, "navigation");
  },
};

/**
 * The area a snap or a maximize may fill: the viewport less the menu bar and the dock, which the
 * chrome tokens state. A window that covered the menu bar would hide the only door out of a bad
 * layout, so this is a rule rather than a preference.
 */
export function desktopWorkArea(display) {
  const menuBar = 30;
  const dock = 80;
  return {
    top: menuBar,
    width: display.width,
    height: Math.max(0, display.height - menuBar - dock),
  };
}

/**
 * The dock (14.11) and its launcher surface.
 *
 * An item is either running, focused or neither, and those are three facts rather than one: the
 * running dot is what the state says about *applications*, and the focused ring is what it says about
 * *windows*. Deriving one from the other is how a dock ends up lying about a minimized window.
 */
export const launcherManager = {
  setDock(state, dock) {
    return { ...state, dock };
  },

  open(state) {
    return { ...state, surface: ShellSurface.Launcher, launcherQuery: "", menuHighlight: 0 };
  },

  close(state) {
    return state.surface === ShellSurface.Launcher ? { ...state, surface: ShellSurface.None } : state;
  },

  setQuery(state, query) {
    return { ...state, launcherQuery: query, menuHighlight: 0 };
  },

  /** The dock's items filtered by the query, ranked so a prefix match beats a substring one. */
  results(state) {
    const query = state.launcherQuery.trim().toLowerCase();
    if (!query) {
      return state.dock;
    }

    return state.dock
      .filter((item) => item.name.toLowerCase().includes(query))
      .sort((a, b) => {
        const aPrefix = a.name.toLowerCase().startsWith(query) ? 0 : 1;
        const bPrefix = b.name.toLowerCase().startsWith(query) ? 0 : 1;
        return aPrefix === bPrefix ? a.name.localeCompare(b.name) : aPrefix - bPrefix;
      });
  },
};

/** Menus, context menus and the system menu (14.9, 14.10, 14.51) — one highlight, one open surface. */
export const menuManager = {
  open(state, surface, anchor = null, highlight = 0) {
    return { ...state, surface, surfaceAnchor: anchor, menuHighlight: highlight };
  },

  close(state) {
    return { ...state, surface: ShellSurface.None, surfaceAnchor: null, menuHighlight: -1 };
  },

  highlight(state, index) {
    return state.surface === ShellSurface.None ? state : { ...state, menuHighlight: index };
  },

  /** Move the highlight through a list of @p count items, wrapping, skipping nothing. */
  move(state, count, direction = 1) {
    if (state.surface === ShellSurface.None || count <= 0) {
      return state;
    }

    const current = state.menuHighlight < 0 ? (direction > 0 ? -1 : 0) : state.menuHighlight;
    return { ...state, menuHighlight: (current + direction + count) % count };
  },
};

/** Notifications (14.32, 14.33): a bounded centre, newest first, grouped by source application. */
export const notificationManager = {
  push(state, notification, limit = 24) {
    const record = Object.freeze({
      id: `notification-${state.nextNotificationId}`,
      kind: NotificationKind.Info,
      read: false,
      ...notification,
    });

    return {
      ...state,
      notifications: [record, ...state.notifications].slice(0, limit),
      nextNotificationId: state.nextNotificationId + 1,
    };
  },

  dismiss(state, id) {
    return { ...state, notifications: state.notifications.filter((item) => item.id !== id) };
  },

  dismissAll(state) {
    return { ...state, notifications: [] };
  },

  markRead(state, id) {
    return {
      ...state,
      notifications: state.notifications.map((item) => (item.id === id ? { ...item, read: true } : item)),
    };
  },

  unread(state) {
    return state.notifications.filter((item) => !item.read).length;
  },

  /** Group by source, which is what the centre's sections are: one section per application. */
  grouped(state) {
    const groups = new Map();
    for (const item of state.notifications) {
      const key = item.applicationId ?? "system";
      if (!groups.has(key)) {
        groups.set(key, []);
      }
      groups.get(key).push(item);
    }
    return [...groups.entries()].map(([applicationId, items]) => ({ applicationId, items }));
  },
};

/** Modal surfaces (14.31): a dialog belongs to a window, and closing that window closes the dialog. */
export const dialogManager = {
  open(state, dialog) {
    return { ...state, dialogs: [...state.dialogs, Object.freeze(dialog)], surface: ShellSurface.Dialog };
  },

  close(state, id) {
    const dialogs = state.dialogs.filter((dialog) => dialog.id !== id);
    return { ...state, dialogs, surface: dialogs.length ? ShellSurface.Dialog : ShellSurface.None };
  },
};

/** Pointer bookkeeping (14.11, 14.37): hover, capture and the selection rectangle, all in one place. */
export const pointerManager = {
  move(state, x, y, hover = null) {
    return { ...state, pointer: { ...state.pointer, x, y, hover } };
  },

  capture(state, target) {
    return { ...state, pointer: { ...state.pointer, captured: target } };
  },

  release(state) {
    return { ...state, pointer: { ...state.pointer, captured: null } };
  },
};

/**
 * The shell's aggregate: one object, so a caller names what it wants to do rather than which
 * reducer to reach for. Composing the managers here is what lets the components take a single
 * `shell` argument and never import one of the five directly.
 */
export const desktop = {
  create: createDesktopState,
  paintOrder: windowsInPaintOrder,
  onWorkspace: windowsOnWorkspace,
  focused: focusedWindow,
  workArea: desktopWorkArea,
  window: windowManager,
  launcher: launcherManager,
  menu: menuManager,
  notification: notificationManager,
  dialog: dialogManager,
  pointer: pointerManager,
};
