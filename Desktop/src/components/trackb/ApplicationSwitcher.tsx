/*
 * ApplicationSwitcher — Phase 14.38
 *
 * macOS ⌘-Tab style: horizontal strip of app icons with frosted-glass pill,
 * keyboard-driven (left/right arrows or Tab/Shift-Tab), release-to-activate.
 * In the lab this is triggered by a button; in production it would be Alt+Tab
 * from the input layer.
 */

import { useEffect, useState, useRef } from "react";
import type { DesktopController } from "@/session/useDesktopSession";

interface AppSwitcherProps {
  isOpen: boolean;
  session: DesktopController;
  onClose: () => void;
}

export function ApplicationSwitcher({ isOpen, session, onClose }: AppSwitcherProps) {
  const apps = session.state.dock.filter((d) => !d.id.startsWith("separator"));
  const [selected, setSelected] = useState(0);
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (isOpen) setSelected(0);
  }, [isOpen]);

  useEffect(() => {
    if (!isOpen) return;
    const handler = (e: KeyboardEvent) => {
      if (e.key === "Escape" || e.key === "Tab") {
        if (e.key === "Tab" && !e.shiftKey) {
          e.preventDefault();
          setSelected((s) => (s + 1) % apps.length);
        } else if (e.key === "Tab" && e.shiftKey) {
          e.preventDefault();
          setSelected((s) => (s - 1 + apps.length) % apps.length);
        } else {
          onClose();
        }
        return;
      }
      if (e.key === "ArrowRight") {
        setSelected((s) => (s + 1) % apps.length);
        e.preventDefault();
      }
      if (e.key === "ArrowLeft") {
        setSelected((s) => (s - 1 + apps.length) % apps.length);
        e.preventDefault();
      }
      if (e.key === "Enter") {
        const app = apps[selected];
        if (app) { session.launch(app.id); onClose(); }
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [isOpen, apps, selected, session, onClose]);

  if (!isOpen) return null;

  const selectedApp = apps[selected];

  return (
    <>
      {/* Scrim */}
      <div
        className="fixed inset-0 z-[99850]"
        style={{ background: "rgba(0,0,0,0.3)" }}
        onClick={onClose}
      />
      {/* Strip */}
      <div
        ref={containerRef}
        className="fixed z-[99860] flex flex-col items-center gap-3"
        style={{
          left: "50%",
          top: "50%",
          transform: "translate(-50%, -50%)",
        }}
      >
        <div
          className="flex items-center gap-4 rounded-[20px] p-4"
          style={{
            background: "rgba(26,26,28,0.88)",
            backdropFilter: "blur(32px) saturate(180%)",
            WebkitBackdropFilter: "blur(32px) saturate(180%)",
            border: "1px solid rgba(255,255,255,0.12)",
            boxShadow: "0 24px 80px rgba(0,0,0,0.5), 0 4px 16px rgba(0,0,0,0.3)",
          }}
        >
          {apps.map((app, idx) => {
            const isSelected = idx === selected;
            return (
              <button
                key={app.id}
                type="button"
                onClick={() => { session.launch(app.id); onClose(); }}
                onMouseEnter={() => setSelected(idx)}
                className="relative flex flex-col items-center gap-2 rounded-[16px] p-2 transition-all duration-100"
                style={{
                  background: isSelected ? "rgba(255,255,255,0.15)" : "transparent",
                  outline: isSelected ? "2px solid rgba(255,255,255,0.4)" : "none",
                }}
              >
                {/* App icon */}
                <div
                  className="relative overflow-hidden rounded-[14px]"
                  style={{
                    width: 64,
                    height: 64,
                    transform: isSelected ? "scale(1.08)" : "scale(1)",
                    transition: "transform 0.15s cubic-bezier(0.28,0.11,0.32,1)",
                    boxShadow: isSelected
                      ? "0 8px 24px rgba(0,0,0,0.4)"
                      : "0 2px 8px rgba(0,0,0,0.3)",
                  }}
                >
                  {app.iconUrl ? (
                    <img src={app.iconUrl} alt={app.name} className="w-full h-full object-cover" draggable={false} />
                  ) : (
                    <div
                      className="w-full h-full flex items-center justify-center font-bold text-2xl text-white"
                      style={{ background: app.tint ?? "#636366" }}
                    >
                      {app.name[0]}
                    </div>
                  )}
                </div>

                {/* Running dot */}
                {session.state.windows.some((w) => w.applicationId === app.id && w.state !== "closed") && (
                  <div
                    className="absolute bottom-1.5 left-1/2 -translate-x-1/2 h-1.5 w-1.5 rounded-full"
                    style={{ background: "rgba(255,255,255,0.7)" }}
                  />
                )}
              </button>
            );
          })}
        </div>

        {/* App name label */}
        {selectedApp && (
          <div
            className="rounded-lg px-4 py-1.5"
            style={{
              background: "rgba(26,26,28,0.88)",
              backdropFilter: "blur(20px)",
              border: "1px solid rgba(255,255,255,0.10)",
            }}
          >
            <p className="text-[14px] font-semibold text-white">{selectedApp.name}</p>
          </div>
        )}
      </div>
    </>
  );
}
