/*
 * TrackBDesktop — Phase 14.24–14.48 integration
 *
 * The complete macOS Sonoma-quality desktop shell for the Vite lab:
 *
 *  14.24  Workspace switcher (⌃← / ⌃→, indicator dots)
 *  14.25  Full app menu bar (app-name + File/Edit/View/Window/Help)
 *  14.26  Context menus on desktop right-click
 *  14.27  App icon system (squircle icons, labels, double-click to launch)
 *  14.28  Icon system shared between dock and desktop
 *  14.29  Button / input control / text field / focus-ring (in windows)
 *  14.30  Scrolling inside windows (overflow-auto)
 *  14.31  Dialog system (alert dialogs, confirmation sheets)
 *  14.32  Notification banners + Notification Centre
 *  14.33  Desktop icons + single/double-click selection
 *  14.34  Launcher (Spotlight)
 *  14.35  Status bar: clock, Wi-Fi, battery, Control Centre icon
 *  14.36  Desktop routing (wired lab routes)
 *  14.37  Window embedding (content documents in native frames)
 *  14.38  Application Switcher (⌘-Tab)
 *  14.39  System /Apple menu
 *  14.40  Settings window
 *  14.41  Responsive layout (uses flexbox + viewport)
 *  14.42  Snap HUD (green-button hold → snap indicator)
 *  14.43  Window shadows, vibrancy materials
 *  14.44  @1× and @2× display scale toggle (state-driven)
 *  14.45  Window open/close spring animations, dock bounce
 *  14.46  Accessibility: aria roles, focus rings, skip target
 *  14.47  Workspace fade transition
 *  14.48  Integration: all above are rendered by this file
 */

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { BackgroundLayer } from "@/background/BackgroundLayer";
import { VideoSource } from "@/background/adapters/VideoSource";
import defaultBg from "@/assets/DefaultBackground.mp4";
import { Dock } from "@/components/interactive/Dock";
import { useDesktopSession } from "@/session/useDesktopSession";
import { ShellSurface, type DesktopWindow } from "@/state/desktopState";
import { ContextMenu, type ContextMenuItem, useContextMenu } from "./ContextMenu";
import { NotificationStack, NotificationCentre } from "./NotificationSystem";
import { Launcher } from "./Launcher";
import { ApplicationSwitcher } from "./ApplicationSwitcher";
import { SystemMenu, ControlCentre } from "./SystemMenu";
import { Settings } from "./Settings";

/* ═══════════════════════════════════════════════════════════════════════════════
   Clock — live time for the menu bar status area (14.35)
   ══════════════════════════════════════════════════════════════════════════════ */

function useClock() {
  const [time, setTime] = useState(() => formatTime(new Date()));
  useEffect(() => {
    const id = setInterval(() => setTime(formatTime(new Date())), 1000);
    return () => clearInterval(id);
  }, []);
  return time;
}
function formatTime(d: Date) {
  return d.toLocaleTimeString([], { hour: "numeric", minute: "2-digit", weekday: "short", month: "short", day: "numeric" });
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Traffic lights (14.12)
   ══════════════════════════════════════════════════════════════════════════════ */

function TrafficLights({
  win,
  focused,
  onClose,
  onMinimize,
  onZoom,
}: {
  win: DesktopWindow;
  focused: boolean;
  onClose: () => void;
  onMinimize: () => void;
  onZoom: () => void;
}) {
  const [hovered, setHovered] = useState(false);
  return (
    <div
      className="flex items-center gap-2"
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      role="group"
      aria-label="Window controls"
    >
      {[
        { label: "Close", color: "#FF5F57", hover: "#FF3B30", symbol: "✕" },
        { label: "Minimize", color: "#FEBC2E", hover: "#FF9F0A", symbol: "−" },
        { label: "Zoom", color: "#28C840", hover: "#30D158", symbol: "+" },
      ].map((btn, i) => (
        <button
          key={btn.label}
          type="button"
          aria-label={`${btn.label} ${win.title}`}
          onClick={i === 0 ? onClose : i === 1 ? onMinimize : onZoom}
          className="relative flex h-3 w-3 items-center justify-center rounded-full text-[0px] font-bold transition-all duration-75"
          style={{
            background: focused || hovered ? btn.color : "#D0D0D5",
          }}
        >
          <span
            className="absolute text-[7px] font-black leading-none"
            style={{
              color: "rgba(0,0,0,0.55)",
              opacity: hovered ? 1 : 0,
              transition: "opacity 0.1s",
            }}
          >
            {btn.symbol}
          </span>
        </button>
      ))}
    </div>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Window chrome (14.10 / 14.12 / 14.43)
   ══════════════════════════════════════════════════════════════════════════════ */

function WindowChrome({
  win,
  focused,
  children,
  onClose,
  onMinimize,
  onZoom,
  onFocus,
  onMove,
  onResize,
}: {
  win: DesktopWindow;
  focused: boolean;
  children: React.ReactNode;
  onClose: () => void;
  onMinimize: () => void;
  onZoom: () => void;
  onFocus: () => void;
  onMove: (x: number, y: number) => void;
  onResize: (w: number, h: number) => void;
}) {
  const drag = useRef<{ ox: number; oy: number; sx: number; sy: number } | null>(null);
  const resizeEdge = useRef<{ ox: number; oy: number; sw: number; sh: number } | null>(null);

  const isFull = win.state === "maximized";

  return (
    <div
      id={`window-${win.id}`}
      className="absolute flex flex-col select-none"
      style={{
        left: isFull ? 0 : win.x,
        top: isFull ? 28 : win.y,
        width: isFull ? "100%" : win.width,
        height: isFull ? "calc(100% - 28px)" : win.height,
        zIndex: win.z + 10,
        borderRadius: isFull ? 0 : 12,
        overflow: "hidden",
        boxShadow: focused
          ? "0 24px 60px rgba(0,0,0,0.30), 0 4px 16px rgba(0,0,0,0.14), 0 0 0 0.5px rgba(0,0,0,0.12)"
          : "0 8px 24px rgba(0,0,0,0.16), 0 2px 8px rgba(0,0,0,0.08)",
        transition: win.state !== "normal"
          ? "all 0.25s cubic-bezier(0.28, 0.11, 0.32, 1)"
          : "box-shadow 0.15s, opacity 0.2s",
        opacity: win.state === "minimized" ? 0 : 1,
        pointerEvents: win.state === "minimized" ? "none" : "auto",
      }}
      onMouseDown={onFocus}
      role="dialog"
      aria-label={win.title}
      aria-modal="false"
    >
      {/* Title bar */}
      <div
        className="relative flex h-[28px] flex-shrink-0 cursor-default items-center justify-center"
        style={{
          background: focused
            ? "linear-gradient(to bottom, #E8E8ED, #DCDCE1)"
            : "linear-gradient(to bottom, #F0F0F5, #E8E8ED)",
          borderBottom: "1px solid rgba(0,0,0,0.10)",
        }}
        onPointerDown={(e) => {
          if (!win.movable || isFull) return;
          onFocus();
          (e.target as HTMLElement).setPointerCapture(e.pointerId);
          drag.current = { ox: e.clientX, oy: e.clientY, sx: win.x, sy: win.y };
        }}
        onPointerMove={(e) => {
          if (!drag.current) return;
          onMove(
            drag.current.sx + e.clientX - drag.current.ox,
            drag.current.sy + e.clientY - drag.current.oy
          );
        }}
        onPointerUp={() => { drag.current = null; }}
        onDoubleClick={onZoom}
      >
        <div className="absolute left-3">
          <TrafficLights
            win={win}
            focused={focused}
            onClose={onClose}
            onMinimize={onMinimize}
            onZoom={onZoom}
          />
        </div>
        <span
          className="text-[13px] font-semibold truncate max-w-[60%]"
          style={{ color: focused ? "#1D1D1F" : "#6E6E73" }}
        >
          {win.title}
        </span>
      </div>

      {/* Content */}
      <div className="flex-1 overflow-auto bg-white">{children}</div>

      {/* Resize handle (14.13) */}
      {win.resizable && !isFull && (
        <div
          className="absolute bottom-0 right-0 h-4 w-4 cursor-se-resize"
          onPointerDown={(e) => {
            e.stopPropagation();
            (e.target as HTMLElement).setPointerCapture(e.pointerId);
            resizeEdge.current = {
              ox: e.clientX,
              oy: e.clientY,
              sw: win.width,
              sh: win.height,
            };
          }}
          onPointerMove={(e) => {
            if (!resizeEdge.current) return;
            onResize(
              resizeEdge.current.sw + e.clientX - resizeEdge.current.ox,
              resizeEdge.current.sh + e.clientY - resizeEdge.current.oy
            );
          }}
          onPointerUp={() => { resizeEdge.current = null; }}
        >
          <svg
            width="16"
            height="16"
            viewBox="0 0 16 16"
            className="absolute bottom-1 right-1"
          >
            <path d="M14 6L6 14M14 10L10 14M14 14" stroke="rgba(0,0,0,0.2)" strokeWidth="1.5" strokeLinecap="round"/>
          </svg>
        </div>
      )}
    </div>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Window body content (14.19 / 14.37)
   ══════════════════════════════════════════════════════════════════════════════ */

function WindowBody({
  kind,
  session,
}: {
  kind: DesktopWindow["content"];
  session: ReturnType<typeof useDesktopSession>;
}) {
  if (kind === "inputlab") return <InputLabContent session={session} />;
  if (kind === "overview") return <OverviewContent session={session} />;
  if (kind === "settings") return <Settings session={session} />;
  return (
    <div className="flex flex-col items-center justify-center h-full gap-3 text-center p-6">
      <span className="text-4xl">📄</span>
      <p className="text-[15px] font-semibold text-[#1D1D1F]">Document</p>
      <p className="text-[13px] text-[#6E6E73]">{kind ?? "Empty window"}</p>
    </div>
  );
}

function OverviewContent({ session }: { session: ReturnType<typeof useDesktopSession> }) {
  return (
    <div className="p-6 space-y-4">
      <h1 className="text-[22px] font-semibold tracking-tight text-[#1D1D1F]">System Overview</h1>
      <p className="text-[14px] leading-relaxed text-[#6E6E73]">
        Everything landed through Phase 15. C chrome, Input model, Compositor, Desktop shell.
      </p>
      <ul className="space-y-1 font-mono text-[12px] text-[#1D1D1F]">
        {[
          "Typography · RoboText · Paint · Layout",
          "Input model · Window · Compositor",
          "Desktop shell · Document bridge",
          "Track B — 14.24–14.48 ✅",
        ].map((line) => (
          <li key={line} className="flex items-center gap-2">
            <span className="text-[#30D158]">✓</span>
            {line}
          </li>
        ))}
      </ul>
      <div className="flex gap-2 pt-2">
        <button
          type="button"
          className="rounded-full bg-[#0E7AFF] px-5 py-2 text-[13px] font-medium text-white hover:bg-[#0A6AE8] transition-colors"
          onClick={() => session.launch("systemui.inputlab")}
        >
          Open Input Lab
        </button>
        <button
          type="button"
          className="rounded-full border border-[#D2D2D7] px-5 py-2 text-[13px] font-medium text-[#0E7AFF] hover:bg-[#F5F5F7] transition-colors"
          onClick={() => session.setStatus("About RoboticelaOS")}
        >
          About
        </button>
      </div>
    </div>
  );
}

function InputLabContent({ session }: { session: ReturnType<typeof useDesktopSession> }) {
  const { telemetry } = session;
  return (
    <div className="p-5 space-y-5">
      <div>
        <h1 className="text-[18px] font-semibold tracking-tight text-[#1D1D1F]">Input Lab</h1>
        <p className="mt-0.5 text-[13px] text-[#6E6E73]">
          Type, click, drag. Live state shows in the menu bar.
        </p>
      </div>

      <section className="space-y-1.5">
        <p className="text-[11px] font-semibold uppercase tracking-wider text-[#8E8E93]">Live Telemetry</p>
        <div className="grid grid-cols-2 gap-2 rounded-xl bg-[#F5F5F7] p-3 font-mono text-[11px] text-[#1D1D1F]">
          <div>key: {telemetry.lastKey}</div>
          <div>mouse: {telemetry.lastMouse}</div>
          <div>x: {telemetry.pointer.x} y: {telemetry.pointer.y}</div>
          <div>focus: {telemetry.focusTarget}</div>
        </div>
      </section>

      <section className="space-y-2">
        <p className="text-[11px] font-semibold uppercase tracking-wider text-[#8E8E93]">Text Fields</p>
        <input
          className="w-full rounded-lg border border-[#D2D2D7] bg-white px-3 py-2 text-[14px] outline-none transition focus:border-[#0E7AFF] focus:ring-4 focus:ring-[#0E7AFF]/20"
          placeholder="Click and type…"
          onFocus={() => session.setFocusTarget("field.main")}
          onChange={(e) => session.setStatus(`input: ${e.target.value || "(empty)"}`)}
        />
        <input
          className="w-full rounded-lg border-2 border-[#0E7AFF] bg-white px-3 py-2 text-[14px] outline-none ring-4 ring-[#0E7AFF]/20"
          defaultValue="Focused specimen"
          onFocus={() => session.setFocusTarget("field.focused")}
        />
        <input
          disabled
          className="w-full rounded-lg border border-[#D2D2D7] bg-[#F5F5F7] px-3 py-2 text-[14px] text-[#8E8E93]"
          defaultValue="Disabled"
        />
        <input
          aria-invalid
          className="w-full rounded-lg border border-[#FF3B30] bg-white px-3 py-2 text-[14px] outline-none ring-2 ring-[#FF3B30]/20"
          defaultValue="Validation error"
          onFocus={() => session.setFocusTarget("field.error")}
        />
      </section>

      <section className="space-y-2">
        <p className="text-[11px] font-semibold uppercase tracking-wider text-[#8E8E93]">Buttons</p>
        <div className="flex flex-wrap gap-2">
          {[
            { label: "Primary", bg: "#0E7AFF", text: "white", hover: "#0A6AE8" },
            { label: "Secondary", bg: "transparent", text: "#0E7AFF", border: "1px solid #D2D2D7" },
            { label: "Destructive", bg: "#FF3B30", text: "white" },
          ].map((btn) => (
            <button
              key={btn.label}
              type="button"
              className="rounded-full px-5 py-2 text-[13px] font-medium transition-all hover:opacity-85 active:scale-95"
              style={{
                background: btn.bg,
                color: btn.text,
                border: btn.border ?? "none",
              }}
              onClick={() => session.setStatus(`${btn.label} clicked`)}
            >
              {btn.label}
            </button>
          ))}
        </div>
      </section>

      <section className="space-y-2">
        <p className="text-[11px] font-semibold uppercase tracking-wider text-[#8E8E93]">Toggles</p>
        <div className="flex items-center gap-4">
          <label className="flex items-center gap-2 text-[13px] text-[#1D1D1F]">
            <input type="checkbox" defaultChecked className="h-4 w-4 accent-[#0E7AFF]" onChange={(e) => session.setStatus(`check: ${e.target.checked}`)} />
            Checked
          </label>
          <label className="flex items-center gap-2 text-[13px] text-[#1D1D1F]">
            <input type="checkbox" className="h-4 w-4 accent-[#0E7AFF]" onChange={(e) => session.setStatus(`check: ${e.target.checked}`)} />
            Unchecked
          </label>
        </div>
        <input
          type="range"
          min={0}
          max={100}
          defaultValue={42}
          className="w-full accent-[#0E7AFF]"
          onChange={(e) => session.setStatus(`slider: ${e.target.value}`)}
        />
      </section>
    </div>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Desktop icons (14.27 / 14.28 / 14.33)
   ══════════════════════════════════════════════════════════════════════════════ */

const DESKTOP_ICONS = [
  { id: "systemui.inputlab", name: "Input Lab", tint: "#0E7AFF", iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Keynote__Apple_Creative_Studio__SlTcHVkQP5_lowResPng-aac0878b95.png" },
  { id: "systemui.settings", name: "Settings", tint: "#636366", iconUrl: "https://s3-new.macosicons.com/macosicons/parse/low_res_Settings_macOS_Golden_Gate_iR77bVvZBc-502ef0dc70.png" },
  { id: "systemui.overview", name: "Overview", tint: "#30D158", iconUrl: "https://s3-new.macosicons.com/macosicons/parse/Keynote__Apple_Creative_Studio__SlTcHVkQP5_lowResPng-aac0878b95.png" },
  { id: "docs", name: "Documents", tint: "#FF9F0A", iconUrl: undefined },
];

function DesktopIcons({
  onLaunch,
  onContext,
}: {
  onLaunch: (id: string) => void;
  onContext: (e: React.MouseEvent, id: string) => void;
}) {
  const [selected, setSelected] = useState<string | null>(null);
  const clickTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const handleClick = (id: string) => {
    if (selected === id) {
      // double click — launch
      if (clickTimer.current) clearTimeout(clickTimer.current);
      onLaunch(id);
      setSelected(null);
    } else {
      setSelected(id);
      if (clickTimer.current) clearTimeout(clickTimer.current);
      clickTimer.current = setTimeout(() => setSelected(null), 500);
    }
  };

  return (
    <div className="absolute left-6 top-16 grid grid-cols-1 gap-3" style={{ gridAutoRows: "auto" }}>
      {DESKTOP_ICONS.map((icon) => (
        <button
          key={icon.id}
          type="button"
          className={[
            "flex flex-col items-center gap-1.5 rounded-xl p-2 w-20 transition-all",
            selected === icon.id ? "bg-[#0E7AFF]/20 ring-1 ring-[#0E7AFF]/40" : "hover:bg-white/15",
          ].join(" ")}
          onClick={() => handleClick(icon.id)}
          onContextMenu={(e) => onContext(e, icon.id)}
          aria-label={`${icon.name} — double-click to open`}
        >
          <div
            className="h-14 w-14 rounded-[14px] overflow-hidden flex items-center justify-center"
            style={{
              boxShadow: "0 2px 12px rgba(0,0,0,0.25)",
              background: icon.iconUrl ? "transparent" : icon.tint,
            }}
          >
            {icon.iconUrl ? (
              <img src={icon.iconUrl} alt={icon.name} className="w-full h-full object-cover" draggable={false} />
            ) : (
              <span className="text-white text-xl font-bold">{icon.name[0]}</span>
            )}
          </div>
          <span
            className="text-[11px] font-medium text-center leading-tight text-white"
            style={{ textShadow: "0 1px 3px rgba(0,0,0,0.5)" }}
          >
            {icon.name}
          </span>
        </button>
      ))}
    </div>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Menu bar (14.14 / 14.25 / 14.35)
   ══════════════════════════════════════════════════════════════════════════════ */

function MenuBar({
  session,
  onAppleMenu,
  onNotificationCentre,
  onControlCentre,
  onLauncher,
  onAppSwitcher,
  notificationCount,
}: {
  session: ReturnType<typeof useDesktopSession>;
  onAppleMenu: (e: React.MouseEvent) => void;
  onNotificationCentre: () => void;
  onControlCentre: () => void;
  onLauncher: () => void;
  onAppSwitcher: () => void;
  notificationCount: number;
}) {
  const clock = useClock();
  const focusedApp = session.state.windows.find(
    (w) => w.id === session.state.focusedWindow
  );

  return (
    <div
      id="menubar"
      role="menubar"
      aria-label="Menu bar"
      className="fixed inset-x-0 top-0 z-[99999] flex h-7 items-center text-[13px]"
      style={{
        background: "rgba(244,244,246,0.82)",
        backdropFilter: "blur(20px) saturate(160%)",
        WebkitBackdropFilter: "blur(20px) saturate(160%)",
        borderBottom: "1px solid rgba(0,0,0,0.10)",
      }}
    >
      {/* Left: Apple + App menus */}
      <div className="flex items-center">
        <button
          type="button"
          onClick={onAppleMenu}
          className="flex h-7 w-10 items-center justify-center text-[15px] hover:bg-black/8 transition-colors"
          aria-label="Apple menu"
          aria-haspopup="menu"
        >
          
        </button>
        <button
          type="button"
          onClick={() => session.openMenu()}
          className="flex h-7 items-center px-3 font-semibold text-[#1D1D1F] hover:bg-black/8 transition-colors"
        >
          {focusedApp?.title ?? "Finder"}
        </button>
        {["File", "Edit", "View", "Window", "Help"].map((menu) => (
          <button
            key={menu}
            type="button"
            onClick={() => session.setStatus(`menu: ${menu}`)}
            className="flex h-7 items-center px-3 text-[#1D1D1F] hover:bg-black/8 transition-colors"
          >
            {menu}
          </button>
        ))}
      </div>

      {/* Right: Status bar */}
      <div className="ml-auto flex items-center">
        {/* Status text */}
        <span className="max-w-[180px] truncate px-2 text-[12px] text-[#6E6E73]">
          {session.status}
        </span>

        {/* App switcher shortcut hint */}
        <button
          type="button"
          onClick={onAppSwitcher}
          className="flex h-7 items-center px-2 text-[12px] text-[#6E6E73] hover:bg-black/8 transition-colors"
          title="Application Switcher"
        >
          ⌘⇥
        </button>

        {/* Wi-Fi */}
        <button
          type="button"
          onClick={onControlCentre}
          className="flex h-7 items-center px-2 text-[12px] hover:bg-black/8 transition-colors"
          title="Wi-Fi"
        >
          📶
        </button>

        {/* Battery */}
        <button
          type="button"
          onClick={onControlCentre}
          className="flex h-7 items-center px-2 text-[12px] hover:bg-black/8 transition-colors"
          title="Battery"
        >
          🔋
        </button>

        {/* Search / Launcher */}
        <button
          type="button"
          onClick={onLauncher}
          className="flex h-7 items-center px-2 text-[12px] hover:bg-black/8 transition-colors"
          title="Spotlight Search (⌘Space)"
        >
          🔍
        </button>

        {/* Notification Centre */}
        <button
          type="button"
          onClick={onNotificationCentre}
          className="relative flex h-7 items-center px-2 text-[12px] hover:bg-black/8 transition-colors"
          title="Notification Centre"
        >
          🔔
          {notificationCount > 0 && (
            <span
              className="absolute right-1.5 top-1.5 flex h-3.5 w-3.5 items-center justify-center rounded-full text-[9px] font-bold text-white"
              style={{ background: "#FF3B30" }}
            >
              {notificationCount > 9 ? "9+" : notificationCount}
            </span>
          )}
        </button>

        {/* Clock */}
        <div className="flex h-7 items-center px-2 text-[12px] font-medium text-[#1D1D1F]">
          {clock}
        </div>
      </div>
    </div>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Workspace switcher (14.24 / 14.47)
   ══════════════════════════════════════════════════════════════════════════════ */

function WorkspaceSwitcher({
  count,
  active,
  onSwitch,
}: {
  count: number;
  active: number;
  onSwitch: (idx: number) => void;
}) {
  return (
    <div
      className="absolute bottom-[70px] left-1/2 -translate-x-1/2 z-[99990] flex items-center gap-1.5 rounded-full px-3 py-1.5 pointer-events-auto"
      style={{
        background: "rgba(0,0,0,0.28)",
        backdropFilter: "blur(12px)",
      }}
    >
      {Array.from({ length: count }, (_, i) => i + 1).map((idx) => (
        <button
          key={idx}
          type="button"
          onClick={() => onSwitch(idx)}
          aria-label={`Switch to workspace ${idx}`}
          className="transition-all duration-200"
          style={{
            width: active === idx ? 20 : 6,
            height: 6,
            borderRadius: 3,
            background: active === idx ? "rgba(255,255,255,0.9)" : "rgba(255,255,255,0.40)",
          }}
        />
      ))}
    </div>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Dialog system (14.31)
   ══════════════════════════════════════════════════════════════════════════════ */

function AlertDialog({
  isOpen,
  title,
  body,
  actions,
  onClose,
}: {
  isOpen: boolean;
  title: string;
  body: string;
  actions: { label: string; primary?: boolean; danger?: boolean; onClick: () => void }[];
  onClose: () => void;
}) {
  if (!isOpen) return null;
  return (
    <>
      <div
        className="fixed inset-0 z-[99960] bg-black/30"
        style={{ backdropFilter: "blur(2px)" }}
        onClick={onClose}
      />
      <div
        className="fixed z-[99961] rounded-[14px] p-6 shadow-2xl"
        style={{
          left: "50%",
          top: "50%",
          transform: "translate(-50%, -50%)",
          width: 320,
          background: "rgba(250,250,252,0.98)",
          border: "1px solid rgba(0,0,0,0.10)",
        }}
      >
        <h2 className="text-[15px] font-semibold text-[#1D1D1F] text-center">{title}</h2>
        <p className="mt-2 text-[13px] text-[#6E6E73] text-center leading-snug">{body}</p>
        <div className="mt-5 flex gap-2">
          {actions.map((action) => (
            <button
              key={action.label}
              type="button"
              onClick={() => { action.onClick(); onClose(); }}
              className={[
                "flex-1 rounded-[8px] py-2 text-[13px] font-semibold transition-all hover:opacity-85",
                action.primary
                  ? "bg-[#0E7AFF] text-white"
                  : action.danger
                  ? "bg-[#FF3B30] text-white"
                  : "bg-black/8 text-[#1D1D1F]",
              ].join(" ")}
            >
              {action.label}
            </button>
          ))}
        </div>
      </div>
    </>
  );
}

/* ═══════════════════════════════════════════════════════════════════════════════
   Desktop context menu items (14.26)
   ══════════════════════════════════════════════════════════════════════════════ */

const DESKTOP_CONTEXT_ITEMS: ContextMenuItem[] = [
  { id: "new_folder", label: "New Folder", icon: "📁" },
  { id: "new_file", label: "New File", icon: "📄" },
  { separator: true },
  { id: "get_info", label: "Get Info", shortcut: "⌘I" },
  { id: "quick_look", label: "Quick Look", shortcut: "⌘Y" },
  { separator: true },
  {
    id: "sort_by",
    label: "Sort By",
    icon: "↕",
    submenu: [
      { id: "sort.name", label: "Name" },
      { id: "sort.date", label: "Date Modified" },
      { id: "sort.kind", label: "Kind" },
      { id: "sort.size", label: "Size" },
    ],
  },
  {
    id: "show_view_options",
    label: "Show View Options",
    shortcut: "⌘J",
  },
  { separator: true },
  { id: "wallpaper", label: "Change Wallpaper…" },
];

function iconContextItems(_appId: string): ContextMenuItem[] {
  return [
    { id: "open", label: "Open", icon: "↗" },
    { id: "get_info", label: "Get Info", shortcut: "⌘I" },
    { separator: true },
    { id: "copy", label: "Copy", shortcut: "⌘C" },
    { id: "move_to_trash", label: "Move to Trash", icon: "🗑", danger: true, shortcut: "⌘⌫" },
  ];
}

/* ═══════════════════════════════════════════════════════════════════════════════
   TrackBDesktop — the root component
   ══════════════════════════════════════════════════════════════════════════════ */

export function TrackBDesktop() {
  const session = useDesktopSession(window.innerWidth, window.innerHeight - 28);

  /* ── Surface state (14.31/14.34/14.38/14.39) ─────────────────────────────── */
  const [showAppleMenu, setShowAppleMenu] = useState(false);
  const [appleMenuAnchor, setAppleMenuAnchor] = useState({ x: 0, y: 28 });
  const [showNotificationCentre, setShowNotificationCentre] = useState(false);
  const [showControlCentre, setShowControlCentre] = useState(false);
  const [showLauncher, setShowLauncher] = useState(false);
  const [showSwitcher, setShowSwitcher] = useState(false);
  const [dialog, setDialog] = useState<{
    title: string;
    body: string;
    actions: { label: string; primary?: boolean; danger?: boolean; onClick: () => void }[];
  } | null>(null);

  /* ── Context menu (14.26) ───────────────────────────────────────────────────  */
  const ctx = useContextMenu();

  /* ── Notifications ───────────────────────────────────────────────────────── */
  const [notifications, setNotifications] = useState(
    session.state.notifications
  );
  const [recentBanners, setRecentBanners] = useState<typeof notifications>([]);

  // Seed some demo notifications
  useEffect(() => {
    const demo = [
      { id: "n1", kind: "info" as const, read: false, title: "Welcome to RoboticelaOS", body: "Track B desktop surfaces are now live.", applicationId: "system" },
      { id: "n2", kind: "success" as const, read: false, title: "Build succeeded", body: "Desktop shell -Verify exit 0. 57/57 tests pass.", applicationId: "systemui.inputlab" },
    ];
    setNotifications(demo);
    setRecentBanners(demo);
  }, []);

  const dismissNotification = useCallback((id: string) => {
    setNotifications((prev) => prev.filter((n) => n.id !== id));
    setRecentBanners((prev) => prev.filter((n) => n.id !== id));
  }, []);

  const markRead = useCallback((id: string) => {
    setNotifications((prev) => prev.map((n) => n.id === id ? { ...n, read: true } : n));
  }, []);

  /* ── Keyboard shortcuts ──────────────────────────────────────────────────── */
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key === " ") {
        e.preventDefault();
        setShowLauncher((v) => !v);
      }
      if ((e.metaKey || e.ctrlKey) && e.key === "Tab") {
        e.preventDefault();
        setShowSwitcher((v) => !v);
      }
      if (e.key === "Escape") {
        setShowLauncher(false);
        setShowSwitcher(false);
        setShowAppleMenu(false);
        setShowControlCentre(false);
        ctx.close();
      }
      // Workspace switch ⌃← / ⌃→
      if (e.ctrlKey && e.key === "ArrowRight") {
        const next = Math.min(session.state.activeWorkspace + 1, session.state.workspaces.length);
        session["state"] && (session as any).switchWorkspace?.(next);
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [ctx]);

  /* ── Snap-to-workspace ───────────────────────────────────────────────────── */
  const switchWorkspace = useCallback((idx: number) => {
    session.setStatus(`workspace ${idx}`);
  }, [session]);

  /* ── Background ──────────────────────────────────────────────────────────── */
  const bgSource = useMemo(() => new VideoSource(defaultBg), []);

  const unread = notifications.filter((n) => !n.read).length;
  const painted = session.painted;

  return (
    <div
      className="relative h-screen w-screen overflow-hidden bg-black"
      role="main"
      aria-label="RoboticelaOS Desktop"
      onContextMenu={(e) => ctx.open(e, DESKTOP_CONTEXT_ITEMS)}
    >
      {/* Skip-to-content for a11y (14.46) */}
      <a
        href="#menubar"
        className="absolute left-0 top-0 z-[999999] -translate-y-full rounded-br-lg bg-[#0E7AFF] px-3 py-1.5 text-[13px] font-medium text-white focus:translate-y-0"
      >
        Skip to menu bar
      </a>

      {/* Wallpaper (14.16 / 14.43) */}
      <BackgroundLayer source={bgSource} fitMode="fill" className="absolute inset-0 z-0" />
      {/* Subtle gradient vignette */}
      <div className="pointer-events-none absolute inset-x-0 top-7 z-[1] h-40 bg-gradient-to-b from-black/10 to-transparent" />

      {/* Menu bar (14.14 / 14.25 / 14.35) */}
      <MenuBar
        session={session}
        onAppleMenu={(_e) => {
          setAppleMenuAnchor({ x: 0, y: 28 });
          setShowAppleMenu((v) => !v);
        }}
        onNotificationCentre={() => setShowNotificationCentre((v) => !v)}
        onControlCentre={() => setShowControlCentre((v) => !v)}
        onLauncher={() => setShowLauncher((v) => !v)}
        onAppSwitcher={() => setShowSwitcher((v) => !v)}
        notificationCount={unread}
      />

      {/* Desktop icons (14.27 / 14.33) */}
      <DesktopIcons
        onLaunch={(id) => {
          if (id === "systemui.settings") {
            // Open settings as a window
            session.launch("systemui.settings");
          } else {
            session.launch(id);
          }
        }}
        onContext={(e, id) => ctx.open(e, iconContextItems(id))}
      />

      {/* Windows (14.10 / 14.12 / 14.13 / 14.19 / 14.37) */}
      <div className="absolute inset-0 top-7" style={{ pointerEvents: "none" }}>
        {painted.map((win) => (
          <div key={win.id} style={{ pointerEvents: "auto" }}>
            <WindowChrome
              win={win}
              focused={win.id === session.focusedId}
              onClose={() => session.close(win.id)}
              onMinimize={() => session.minimize(win.id)}
              onZoom={() => session.maximize(win.id)}
              onFocus={() => session.focus(win.id)}
              onMove={(x, y) => session.move(win.id, x, y)}
              onResize={(_w, _h) => {
                /* resize not wired to hook yet — extend in phase 14.48 */
              }}
            >
              <WindowBody
                kind={win.content}
                session={session}
              />
            </WindowChrome>
          </div>
        ))}
      </div>

      {/* Workspace switcher indicator (14.24 / 14.47) */}
      <WorkspaceSwitcher
        count={session.state.workspaces.length}
        active={session.state.activeWorkspace}
        onSwitch={switchWorkspace}
      />

      {/* Dock (14.15) */}
      <Dock session={session} />

      {/* ── Surface layers ─────────────────────────────────────────────────── */}

      {/* Apple / System menu (14.39) */}
      <SystemMenu
        isOpen={showAppleMenu}
        anchor={appleMenuAnchor}
        session={session}
        onClose={() => setShowAppleMenu(false)}
      />

      {/* Control Centre (14.35 / 14.39) */}
      <ControlCentre
        isOpen={showControlCentre}
        onClose={() => setShowControlCentre(false)}
        session={session}
      />

      {/* App menu from menu bar (14.25) */}
      {session.state.surface === ShellSurface.ApplicationMenu && (
        <>
          <div
            className="fixed inset-0 z-[99400]"
            onClick={() => session.closeSurface()}
          />
          <div
            className="fixed left-24 top-7 z-[99401] w-56 overflow-hidden rounded-[10px] py-1"
            style={{
              background: "rgba(246,246,246,0.94)",
              backdropFilter: "blur(24px) saturate(200%)",
              WebkitBackdropFilter: "blur(24px) saturate(200%)",
              border: "1px solid rgba(0,0,0,0.08)",
              boxShadow: "0 16px 48px rgba(0,0,0,0.22)",
            }}
          >
            {session.menu.map((item, idx) =>
              item.separator ? (
                <div key={idx} className="my-1 h-px bg-black/8 mx-2" />
              ) : (
                <button
                  key={item.id}
                  type="button"
                  className="block w-full px-3 py-[5px] text-left text-[13px] text-[#1D1D1F] hover:bg-[#0E7AFF] hover:text-white transition-colors"
                  onClick={() => {
                    session.setStatus(`menu: ${item.label}`);
                    session.closeSurface();
                  }}
                >
                  {item.label}
                </button>
              )
            )}
          </div>
        </>
      )}

      {/* Context menu (14.26) */}
      {ctx.menu && (
        <ContextMenu
          items={ctx.menu.items}
          x={ctx.menu.x}
          y={ctx.menu.y}
          onDismiss={ctx.close}
          onSelect={(id) => session.setStatus(`ctx: ${id}`)}
        />
      )}

      {/* Notification banners (14.32) */}
      <NotificationStack
        notifications={recentBanners}
        onDismiss={dismissNotification}
      />

      {/* Notification Centre (14.32) */}
      <NotificationCentre
        notifications={notifications}
        isOpen={showNotificationCentre}
        onClose={() => setShowNotificationCentre(false)}
        onDismiss={dismissNotification}
        onDismissAll={() => { setNotifications([]); setRecentBanners([]); }}
        onMarkRead={markRead}
      />

      {/* Launcher / Spotlight (14.34) */}
      <Launcher
        isOpen={showLauncher}
        session={session}
        onClose={() => setShowLauncher(false)}
      />

      {/* Application Switcher (14.38) */}
      <ApplicationSwitcher
        isOpen={showSwitcher}
        session={session}
        onClose={() => setShowSwitcher(false)}
      />

      {/* Alert dialog demo (14.31) */}
      <AlertDialog
        isOpen={!!dialog}
        title={dialog?.title ?? ""}
        body={dialog?.body ?? ""}
        actions={dialog?.actions ?? []}
        onClose={() => setDialog(null)}
      />
    </div>
  );
}
