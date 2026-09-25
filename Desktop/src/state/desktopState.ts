/*
 * The desktop's state — Phase 14.3's centralized desktop state, as a pure reducer module.
 * Typed for the Vite lab; the guest never runs this module.
 */

export const WindowState = {
  Normal: "normal",
  Minimized: "minimized",
  Maximized: "maximized",
  Fullscreen: "fullscreen",
  Closing: "closing",
  Closed: "closed",
} as const;

export type WindowStateValue = (typeof WindowState)[keyof typeof WindowState];

export const SnapEdge = {
  Left: "left",
  Right: "right",
  Top: "top",
  TopLeft: "topLeft",
  TopRight: "topRight",
  BottomLeft: "bottomLeft",
  BottomRight: "bottomRight",
} as const;

export type SnapEdgeValue = (typeof SnapEdge)[keyof typeof SnapEdge];

export const NotificationKind = {
  Info: "info",
  Success: "success",
  Warning: "warning",
  Error: "error",
  Progress: "progress",
} as const;

export type NotificationKindValue = (typeof NotificationKind)[keyof typeof NotificationKind];

export const ShellSurface = {
  None: "none",
  ApplicationMenu: "applicationMenu",
  SystemMenu: "systemMenu",
  ContextMenu: "contextMenu",
  NotificationCentre: "notificationCentre",
  Launcher: "launcher",
  ApplicationSwitcher: "applicationSwitcher",
  Dialog: "dialog",
} as const;

export type ShellSurfaceValue = (typeof ShellSurface)[keyof typeof ShellSurface];

export type WindowContentKind = "inputlab" | "overview" | "inspector" | "specimen" | string | null;

export interface WindowRestore {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface DesktopWindow {
  id: string;
  applicationId: string;
  title: string;
  icon: string | null;
  x: number;
  y: number;
  width: number;
  height: number;
  minWidth: number;
  minHeight: number;
  maxWidth: number;
  maxHeight: number;
  state: WindowStateValue;
  resizable: boolean;
  movable: boolean;
  closable: boolean;
  modal: boolean;
  workspace: number;
  z: number;
  content: WindowContentKind;
  restore: WindowRestore | null;
}

export interface DockItem {
  id: string;
  name: string;
  running: boolean;
  focused: boolean;
  iconClass?: string;
  tint?: string;
}

export interface DesktopIcon {
  id: string;
  name: string;
}

export interface MenuRow {
  id?: string;
  label?: string;
  separator?: boolean;
  disabled?: boolean;
}

export interface NotificationRecord {
  id: string;
  kind: NotificationKindValue;
  read: boolean;
  title: string;
  body: string;
  applicationId?: string;
  windowId?: string;
}

export interface DialogRecord {
  id: string;
  windowId?: string;
  title: string;
  body: string;
}

export interface DisplayInfo {
  width: number;
  height: number;
  scale: number;
}

export interface WorkspaceRecord {
  index: number;
  windows: string[];
}

export interface PointerState {
  x: number;
  y: number;
  captured: string | null;
  hover: string | null;
}

export interface DesktopState {
  display: DisplayInfo;
  appearance: "light" | "dark";
  workspaces: WorkspaceRecord[];
  activeWorkspace: number;
  windows: DesktopWindow[];
  nextWindowId: number;
  nextZ: number;
  focusedWindow: string | null;
  focusReason: string;
  pointer: PointerState;
  dock: DockItem[];
  surface: ShellSurfaceValue;
  surfaceAnchor: { x: number; y: number } | null;
  menuHighlight: number;
  launcherQuery: string;
  notifications: NotificationRecord[];
  nextNotificationId: number;
  dialogs: DialogRecord[];
}

export type WindowOpenSpec = Partial<DesktopWindow> & {
  applicationId: string;
  title: string;
  focusReason?: string;
};

function clamp(value: number, low: number, high: number): number {
  return value < low ? low : value > high ? high : value;
}

export function createWindow(spec: WindowOpenSpec & { id: string; z: number; workspace: number }): DesktopWindow {
  return Object.freeze({
    id: spec.id,
    applicationId: spec.applicationId,
    title: spec.title,
    icon: spec.icon ?? null,
    x: spec.x ?? 0,
    y: spec.y ?? 0,
    width: spec.width ?? 520,
    height: spec.height ?? 360,
    minWidth: spec.minWidth ?? 240,
    minHeight: spec.minHeight ?? 160,
    maxWidth: spec.maxWidth ?? 0,
    maxHeight: spec.maxHeight ?? 0,
    state: spec.state ?? WindowState.Normal,
    resizable: spec.resizable ?? true,
    movable: spec.movable ?? true,
    closable: spec.closable ?? true,
    modal: spec.modal ?? false,
    workspace: spec.workspace,
    z: spec.z,
    content: spec.content ?? null,
    restore: null,
  });
}

export function createDesktopState({
  display = { width: 1024, height: 768, scale: 1 },
  windows = [],
  workspaces = 3,
  appearance = "light",
  dock = [],
}: {
  display?: DisplayInfo;
  windows?: WindowOpenSpec[];
  workspaces?: number;
  appearance?: "light" | "dark";
  dock?: DockItem[];
} = {}): DesktopState {
  const state: DesktopState = {
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

export function windowsInPaintOrder(state: DesktopState): DesktopWindow[] {
  return [...state.windows]
    .filter((window) => window.state !== WindowState.Closed)
    .sort((a, b) => (a.z === b.z ? a.id.localeCompare(b.id) : a.z - b.z));
}

export function windowsOnWorkspace(state: DesktopState, workspace = state.activeWorkspace): DesktopWindow[] {
  return windowsInPaintOrder(state).filter(
    (window) => window.workspace === workspace && window.state !== WindowState.Minimized
  );
}

export function focusedWindow(state: DesktopState): DesktopWindow | null {
  if (!state.focusedWindow) {
    return null;
  }
  return state.windows.find((window) => window.id === state.focusedWindow) ?? null;
}

function replaceWindow(
  state: DesktopState,
  id: string,
  update: (window: DesktopWindow) => DesktopWindow
): DesktopState {
  return {
    ...state,
    windows: state.windows.map((window) => (window.id === id ? Object.freeze(update(window)) : window)),
  };
}

function withWorkspaceMembership(state: DesktopState, window: DesktopWindow): DesktopState {
  return {
    ...state,
    workspaces: state.workspaces.map((workspace) =>
      workspace.index === window.workspace
        ? { ...workspace, windows: [...new Set([...workspace.windows, window.id])] }
        : workspace
    ),
  };
}

export function desktopWorkArea(display: DisplayInfo): {
  top: number;
  width: number;
  height: number;
} {
  const menuBar = 28;
  const dock = 92;
  return {
    top: menuBar,
    width: display.width,
    height: Math.max(0, display.height - menuBar - dock),
  };
}

export const windowManager = {
  open(state: DesktopState, spec: WindowOpenSpec): DesktopState {
    const id = spec.id ?? `window-${state.nextWindowId}`;
    const workspace = spec.workspace ?? state.activeWorkspace;
    const window = createWindow({
      ...spec,
      id,
      workspace,
      z: state.nextZ,
    });
    const opened: DesktopState = {
      ...state,
      windows: [...state.windows, window],
      nextWindowId: state.nextWindowId + 1,
      nextZ: state.nextZ + 1,
      focusedWindow: id,
      focusReason: spec.focusReason ?? "programmatic",
    };

    return withWorkspaceMembership(opened, window);
  },

  close(state: DesktopState, id: string): DesktopState {
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

  focus(state: DesktopState, id: string, reason = "pointer"): DesktopState {
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

  activateWorkspace(state: DesktopState, index: number): DesktopState {
    if (!state.workspaces.some((workspace) => workspace.index === index)) {
      return state;
    }

    const top = windowsInPaintOrder(state).filter(
      (window) => window.workspace === index && window.state !== WindowState.Minimized
    );

    return {
      ...state,
      activeWorkspace: index,
      focusedWindow: top.length ? top[top.length - 1]!.id : null,
      surface: ShellSurface.None,
    };
  },

  move(state: DesktopState, id: string, x: number, y: number): DesktopState {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window || !window.movable) {
      return state;
    }

    const bounds = desktopWorkArea(state.display);
    const nextX = clamp(x, -window.width + 64, bounds.width - 64);
    const nextY = clamp(y, bounds.top, bounds.height - 24);

    return replaceWindow(state, id, (current) => ({
      ...current,
      x: nextX,
      y: nextY,
      restore: null,
    }));
  },

  resize(
    state: DesktopState,
    id: string,
    { x, y, width, height }: { x?: number; y?: number; width?: number; height?: number }
  ): DesktopState {
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

  minimize(state: DesktopState, id: string): DesktopState {
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
      .filter(
        (candidate) => candidate.workspace === window.workspace && candidate.state !== WindowState.Minimized
      )
      .pop();

    return { ...minimized, focusedWindow: next?.id ?? null };
  },

  restore(state: DesktopState, id: string): DesktopState {
    return replaceWindow(state, id, (current) => ({
      ...current,
      state: WindowState.Normal,
      ...(current.restore ?? {}),
      restore: null,
    }));
  },

  maximize(state: DesktopState, id: string): DesktopState {
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

  snap(state: DesktopState, id: string, edge: SnapEdgeValue): DesktopState {
    const window = state.windows.find((candidate) => candidate.id === id);
    if (!window) {
      return state;
    }

    const bounds = desktopWorkArea(state.display);
    const halfWidth = Math.round(bounds.width / 2);
    const halfHeight = Math.round(bounds.height / 2);

    const rectangles: Record<SnapEdgeValue, { x: number; y: number; width: number; height: number }> = {
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

  cycleFocus(state: DesktopState, direction = 1): DesktopState {
    const order = windowsInPaintOrder(state).filter(
      (window) => window.workspace === state.activeWorkspace && window.state !== WindowState.Minimized
    );
    if (order.length < 2) {
      return state;
    }

    const index = order.findIndex((window) => window.id === state.focusedWindow);
    const next = order[((index < 0 ? 0 : index) + direction + order.length) % order.length]!;

    return windowManager.focus(state, next.id, "navigation");
  },
};

export const launcherManager = {
  setDock(state: DesktopState, dock: DockItem[]): DesktopState {
    return { ...state, dock };
  },

  open(state: DesktopState): DesktopState {
    return { ...state, surface: ShellSurface.Launcher, launcherQuery: "", menuHighlight: 0 };
  },

  close(state: DesktopState): DesktopState {
    return state.surface === ShellSurface.Launcher ? { ...state, surface: ShellSurface.None } : state;
  },

  setQuery(state: DesktopState, query: string): DesktopState {
    return { ...state, launcherQuery: query, menuHighlight: 0 };
  },

  results(state: DesktopState): DockItem[] {
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

export const menuManager = {
  open(
    state: DesktopState,
    surface: ShellSurfaceValue,
    anchor: { x: number; y: number } | null = null,
    highlight = 0
  ): DesktopState {
    return { ...state, surface, surfaceAnchor: anchor, menuHighlight: highlight };
  },

  close(state: DesktopState): DesktopState {
    return { ...state, surface: ShellSurface.None, surfaceAnchor: null, menuHighlight: -1 };
  },

  highlight(state: DesktopState, index: number): DesktopState {
    return state.surface === ShellSurface.None ? state : { ...state, menuHighlight: index };
  },

  move(state: DesktopState, count: number, direction = 1): DesktopState {
    if (state.surface === ShellSurface.None || count <= 0) {
      return state;
    }

    const current = state.menuHighlight < 0 ? (direction > 0 ? -1 : 0) : state.menuHighlight;
    return { ...state, menuHighlight: (current + direction + count) % count };
  },
};

export const notificationManager = {
  push(
    state: DesktopState,
    notification: Omit<NotificationRecord, "id" | "kind" | "read"> & Partial<Pick<NotificationRecord, "kind" | "read">>,
    limit = 24
  ): DesktopState {
    const record: NotificationRecord = Object.freeze({
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

  dismiss(state: DesktopState, id: string): DesktopState {
    return { ...state, notifications: state.notifications.filter((item) => item.id !== id) };
  },

  dismissAll(state: DesktopState): DesktopState {
    return { ...state, notifications: [] };
  },

  markRead(state: DesktopState, id: string): DesktopState {
    return {
      ...state,
      notifications: state.notifications.map((item) => (item.id === id ? { ...item, read: true } : item)),
    };
  },

  unread(state: DesktopState): number {
    return state.notifications.filter((item) => !item.read).length;
  },

  grouped(state: DesktopState): { applicationId: string; items: NotificationRecord[] }[] {
    const groups = new Map<string, NotificationRecord[]>();
    for (const item of state.notifications) {
      const key = item.applicationId ?? "system";
      if (!groups.has(key)) {
        groups.set(key, []);
      }
      groups.get(key)!.push(item);
    }
    return [...groups.entries()].map(([applicationId, items]) => ({ applicationId, items }));
  },
};

export const dialogManager = {
  open(state: DesktopState, dialog: DialogRecord): DesktopState {
    return { ...state, dialogs: [...state.dialogs, Object.freeze(dialog)], surface: ShellSurface.Dialog };
  },

  close(state: DesktopState, id: string): DesktopState {
    const dialogs = state.dialogs.filter((dialog) => dialog.id !== id);
    return { ...state, dialogs, surface: dialogs.length ? ShellSurface.Dialog : ShellSurface.None };
  },
};

export const pointerManager = {
  move(state: DesktopState, x: number, y: number, hover: string | null = null): DesktopState {
    return { ...state, pointer: { ...state.pointer, x, y, hover } };
  },

  capture(state: DesktopState, target: string): DesktopState {
    return { ...state, pointer: { ...state.pointer, captured: target } };
  },

  release(state: DesktopState): DesktopState {
    return { ...state, pointer: { ...state.pointer, captured: null } };
  },
};

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
