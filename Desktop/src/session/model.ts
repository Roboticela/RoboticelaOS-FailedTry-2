import type { DesktopIcon, DockItem, MenuRow } from "@/state/desktopState";
import { desktop, type DesktopState } from "@/state/desktopState";

export const desktopIcons: DesktopIcon[] = [
  { id: "inputlab", name: "Input Lab" },
  { id: "documents", name: "Documents" },
  { id: "overview", name: "Overview" },
];

export const dockItems: DockItem[] = [
  {
    id: "systemui.finder",
    name: "Finder",
    running: true,
    focused: false,
    tint: "#5AC8FA",
    iconUrl: "https://upload.wikimedia.org/wikipedia/commons/c/c9/Finder_Icon_macOS_Big_Sur.png",
  },
  {
    id: "systemui.launchpad",
    name: "Launchpad",
    running: false,
    focused: false,
    tint: "#FF6B6B",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Launchpad__MacOS_Tahoe__ncy8MiCAOA_lowResPng-ffd58d03cd.png",
  },
  {
    id: "systemui.safari",
    name: "Safari",
    running: false,
    focused: false,
    tint: "#0A84FF",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Safari__MacOS_Tahoe__utug9Rt8g6_lowResPng-e86f84b6e9.png",
  },
  {
    id: "systemui.messages",
    name: "Messages",
    running: false,
    focused: false,
    tint: "#30D158",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Messages_macOS_Golden_Gate_ow94O6GAvP-2934473a5d.png",
  },
  {
    id: "systemui.maps",
    name: "Maps",
    running: false,
    focused: false,
    tint: "#30B0C7",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Maps_macOS_Golden_Gate_jOxvDFgSHw-b6ceac8cd4.png",
  },
  {
    id: "systemui.photos",
    name: "Photos",
    running: false,
    focused: false,
    tint: "#FF6B81",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Photos_macOS_Golden_Gate_mxVHKSuSHM-06d7e83102.png",
  },
  {
    id: "systemui.facetime",
    name: "FaceTime",
    running: false,
    focused: false,
    tint: "#30D158",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Facetime_macOS_Golden_Gate_Qxnt8DtlwP-e0839d42d8.png",
  },
  {
    id: "systemui.calendar",
    name: "Calendar",
    running: false,
    focused: false,
    tint: "#FF3B30",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Calender_25y2FI4DUz_lowResPng-d649f6c721.png",
  },
  {
    id: "systemui.music",
    name: "Music",
    running: false,
    focused: false,
    tint: "#FF2D55",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Music_macOS_Golden_Gate_LJox0IObSI-d626e3640a.png",
  },
  {
    id: "systemui.podcasts",
    name: "Podcasts",
    running: false,
    focused: false,
    tint: "#9B59B6",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Podcasts_macOS_Golden_Gate_PLHwgdc3Fl-4abdcae811.png",
  },
  {
    id: "systemui.appstore",
    name: "App Store",
    running: false,
    focused: false,
    tint: "#0A84FF",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/App_Store__MacOS_Tahoe__ZTpqalXxE3_lowResPng-b755fc1237.png",
  },
  // ── divider ──
  { id: "separator-1", name: "", running: false, focused: false },
  {
    id: "systemui.inputlab",
    name: "Input Lab",
    running: true,
    focused: true,
    tint: "#0E7AFF",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Keynote__Apple_Creative_Studio__SlTcHVkQP5_lowResPng-aac0878b95.png",
  },
  {
    id: "systemui.settings",
    name: "Settings",
    running: false,
    focused: false,
    tint: "#8E8E93",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Settings_macOS_Golden_Gate_iR77bVvZBc-502ef0dc70.png",
  },
  {
    id: "systemui.terminal",
    name: "Terminal",
    running: false,
    focused: false,
    tint: "#1D1D1F",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/GitHub_mm0mJOQEAS_lowResPng-6e5b90d2c7.png",
  },
  // ── divider ──
  { id: "separator-2", name: "", running: false, focused: false },
  {
    id: "systemui.trash",
    name: "Trash",
    running: false,
    focused: false,
    tint: "#8E8E93",
    iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Bin_Empty_Tahoe_x2cZW1cg7Y_lowResPng-cc516d2e3c.png",
  },
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
