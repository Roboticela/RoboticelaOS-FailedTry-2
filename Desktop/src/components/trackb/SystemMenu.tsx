/*
 * SystemMenu — Phase 14.39
 *
 * The  menu and system status bar right-click panel — macOS Sonoma style.
 * Contains: Apple-menu section (About, System Settings, App Store, …),
 * plus the status-bar right panel (Control Centre-style quick toggles).
 */

import { useEffect } from "react";
import type { DesktopController } from "@/session/useDesktopSession";

interface SystemMenuProps {
  isOpen: boolean;
  anchor: { x: number; y: number };
  session: DesktopController;
  onClose: () => void;
}

interface MenuSection {
  items: SystemMenuItem[];
}

interface SystemMenuItem {
  id: string;
  label: string;
  shortcut?: string;
  separator?: boolean;
  danger?: boolean;
}

const APPLE_MENU: MenuSection[] = [
  {
    items: [
      { id: "about", label: "About This Mac" },
    ],
  },
  {
    items: [
      { id: "settings", label: "System Settings…", shortcut: "⌘," },
      { id: "appstore", label: "App Store…" },
    ],
  },
  {
    items: [
      { id: "recent", label: "Recent Items", shortcut: "▶" },
    ],
  },
  {
    items: [
      { id: "force_quit", label: "Force Quit…", shortcut: "⌥⌘⎋" },
    ],
  },
  {
    items: [
      { id: "sleep", label: "Sleep" },
      { id: "restart", label: "Restart…" },
      { id: "shutdown", label: "Shut Down…" },
    ],
  },
  {
    items: [
      { id: "lock", label: "Lock Screen", shortcut: "⌃⌘Q" },
      { id: "logout", label: "Log Out…", shortcut: "⇧⌘Q", danger: true },
    ],
  },
];

export function SystemMenu({ isOpen, anchor, session, onClose }: SystemMenuProps) {
  useEffect(() => {
    if (!isOpen) return;
    const handler = (e: KeyboardEvent) => { if (e.key === "Escape") onClose(); };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [isOpen, onClose]);

  if (!isOpen) return null;

  const handleItem = (id: string) => {
    session.setStatus(`apple-menu: ${id}`);
    onClose();
  };

  return (
    <>
      <div className="fixed inset-0 z-[99800]" onClick={onClose} />
      <div
        className="fixed z-[99810] overflow-hidden rounded-[10px] py-1"
        style={{
          left: anchor.x,
          top: anchor.y,
          width: 220,
          background: "rgba(246,246,246,0.94)",
          backdropFilter: "blur(24px) saturate(200%)",
          WebkitBackdropFilter: "blur(24px) saturate(200%)",
          border: "1px solid rgba(0,0,0,0.08)",
          boxShadow: "0 16px 48px rgba(0,0,0,0.22), 0 4px 12px rgba(0,0,0,0.12)",
        }}
      >
        {APPLE_MENU.map((section, si) => (
          <div key={si}>
            {si > 0 && <div className="my-1 h-px bg-black/[0.08] mx-2" />}
            {section.items.map((item) => (
              <button
                key={item.id}
                type="button"
                onClick={() => handleItem(item.id)}
                className={[
                  "flex w-full items-center px-3 py-[5px] text-[13px] text-left transition-colors duration-75",
                  item.danger
                    ? "text-[#FF3B30] hover:bg-[#FF3B30] hover:text-white"
                    : "text-[#1D1D1F] hover:bg-[#0E7AFF] hover:text-white",
                ].join(" ")}
              >
                <span className="flex-1 leading-tight">{item.label}</span>
                {item.shortcut && (
                  <span className="ml-4 text-[11px] text-black/35 group-hover:text-white/70 font-medium">
                    {item.shortcut}
                  </span>
                )}
              </button>
            ))}
          </div>
        ))}
      </div>
    </>
  );
}

/* ── Control Centre quick-toggle panel ─────────────────────────────────────── */

interface ControlCentreProps {
  isOpen: boolean;
  onClose: () => void;
  session: DesktopController;
}

export function ControlCentre({ isOpen, onClose, session }: ControlCentreProps) {

  useEffect(() => {
    if (!isOpen) return;
    const handler = (e: KeyboardEvent) => { if (e.key === "Escape") onClose(); };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [isOpen, onClose]);

  return (
    <>
      <div
        className={[
          "fixed inset-0 z-[99700] transition-opacity duration-150",
          isOpen ? "opacity-100 pointer-events-auto" : "opacity-0 pointer-events-none",
        ].join(" ")}
        onClick={onClose}
      />
      <div
        className={[
          "fixed top-8 right-3 z-[99710] rounded-[18px] p-4 transition-all duration-200 ease-[cubic-bezier(0.28,0.11,0.32,1)]",
          isOpen ? "opacity-100 scale-100" : "opacity-0 scale-95 pointer-events-none",
        ].join(" ")}
        style={{
          width: 320,
          background: "rgba(246,246,246,0.90)",
          backdropFilter: "blur(32px) saturate(200%)",
          WebkitBackdropFilter: "blur(32px) saturate(200%)",
          border: "1px solid rgba(0,0,0,0.10)",
          boxShadow: "0 20px 60px rgba(0,0,0,0.22), 0 4px 16px rgba(0,0,0,0.12)",
        }}
      >
        <h3 className="mb-3 text-[13px] font-semibold text-[#1D1D1F]">Control Centre</h3>

        {/* Quick toggles grid */}
        <div className="grid grid-cols-2 gap-2 mb-3">
          <QuickToggle
            icon="📶"
            label="Wi-Fi"
            sublabel="Roboticela Net"
            active={true}
            color="#0E7AFF"
          />
          <QuickToggle
            icon="🔵"
            label="Bluetooth"
            sublabel="On"
            active={true}
            color="#0E7AFF"
          />
          <QuickToggle
            icon="🔆"
            label="AirDrop"
            sublabel="Contacts Only"
            active={true}
            color="#0E7AFF"
          />
          <QuickToggle
            icon="🎯"
            label="Focus"
            sublabel="Off"
            active={false}
            color="#636366"
          />
        </div>

        {/* Brightness slider */}
        <div className="mb-2 rounded-xl bg-black/5 p-3">
          <div className="flex items-center gap-2 mb-2">
            <span className="text-sm">🔆</span>
            <span className="text-[12px] font-medium text-[#1D1D1F]">Display</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="text-[11px] text-[#8E8E93]">☀</span>
            <input
              type="range"
              min={0}
              max={100}
              defaultValue={75}
              className="flex-1 accent-[#0E7AFF]"
              onChange={(e) => session.setStatus(`brightness: ${e.target.value}%`)}
            />
            <span className="text-[11px] text-[#8E8E93]">☀️</span>
          </div>
        </div>

        {/* Volume slider */}
        <div className="rounded-xl bg-black/5 p-3">
          <div className="flex items-center gap-2 mb-2">
            <span className="text-sm">🔊</span>
            <span className="text-[12px] font-medium text-[#1D1D1F]">Sound</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="text-[11px] text-[#8E8E93]">🔈</span>
            <input
              type="range"
              min={0}
              max={100}
              defaultValue={60}
              className="flex-1 accent-[#0E7AFF]"
              onChange={(e) => session.setStatus(`volume: ${e.target.value}%`)}
            />
            <span className="text-[11px] text-[#8E8E93]">🔊</span>
          </div>
        </div>
      </div>
    </>
  );
}

interface QuickToggleProps {
  icon: string;
  label: string;
  sublabel: string;
  active: boolean;
  color: string;
}

function QuickToggle({ icon, label, sublabel, active, color }: QuickToggleProps) {
  return (
    <button
      type="button"
      className={[
        "flex flex-col items-start gap-0.5 rounded-xl p-3 text-left transition-colors",
        active ? "hover:brightness-95" : "bg-black/5 hover:bg-black/10",
      ].join(" ")}
      style={active ? { background: color + "20", border: `1px solid ${color}30` } : {}}
    >
      <div className="flex items-center gap-1.5">
        <span className="text-base">{icon}</span>
        <span className={[
          "text-[13px] font-semibold",
          active ? "text-[#1D1D1F]" : "text-[#6E6E73]",
        ].join(" ")}>{label}</span>
      </div>
      <span className="ml-6 text-[11px] text-[#6E6E73]">{sublabel}</span>
    </button>
  );
}
