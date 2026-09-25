import type { DesktopIcon, DockItem, MenuRow } from "@/state/desktopState";
import { desktop, type DesktopState } from "@/state/desktopState";

export const desktopIcons: DesktopIcon[] = [
  { id: "inputlab", name: "Input Lab" },
  { id: "documents", name: "Documents" },
  { id: "overview", name: "Overview" },
];

export const dockItems: DockItem[] = [
  { id: "systemui.finder", name: "Finder", running: true, focused: false, tint: "#5AC8FA", iconClass: "dockIconFinder" },
  { id: "systemui.inputlab", name: "Input Lab", running: true, focused: true, tint: "#0E7AFF", iconClass: "dockIconLab" },
  { id: "systemui.overview", name: "Overview", running: true, focused: false, tint: "#AF52DE", iconClass: "dockIconOverview" },
  { id: "systemui.files", name: "Files", running: false, focused: false, tint: "#FF9F0A", iconClass: "dockIconFiles" },
  { id: "systemui.settings", name: "Settings", running: false, focused: false, tint: "#8E8E93", iconClass: "dockIconSettings" },
  { id: "systemui.terminal", name: "Terminal", running: false, focused: false, tint: "#1D1D1F", iconClass: "dockIconTerminal" },
];

export const applicationMenu: MenuRow[] = [
  { id: "file.newWindow", label: "New Window" },
  { id: "file.newFolder", label: "New Folder" },
  { id: "file.open", label: "Open…" },
  { id: "file.close", label: "Close Window" },
  { separator: true },
  { id: "edit.copy", label: "Copy" },
  { id: "edit.paste", label: "Paste" },
  { separator: true },
  { id: "system.settings", label: "System Settings…" },
  { id: "system.about", label: "About This Mac" },
];

export interface DesktopModel {
  state: DesktopState;
  icons: DesktopIcon[];
  menu: MenuRow[];
}

/** Fresh interactive session used by the Vite lab and the guest SSR snapshot. */
export function buildDesktopModel({
  display = { width: 1024, height: 768, scale: 1 },
}: {
  display?: { width: number; height: number; scale: number };
} = {}): DesktopModel {
  let state = desktop.create({ display, appearance: "light", dock: dockItems });

  state = desktop.window.open(state, {
    applicationId: "systemui.overview",
    title: "System Overview",
    x: 72,
    y: 72,
    width: 380,
    height: 300,
    content: "overview",
    minWidth: 280,
    minHeight: 200,
  });
  state = desktop.window.open(state, {
    applicationId: "systemui.inputlab",
    title: "Input Lab",
    x: 280,
    y: 96,
    width: 560,
    height: 540,
    content: "inputlab",
    minWidth: 400,
    minHeight: 360,
  });
  state = desktop.window.focus(state, "window-2", "programmatic");

  return { state, icons: desktopIcons, menu: applicationMenu };
}
