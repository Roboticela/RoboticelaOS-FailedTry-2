/*
 * Launcher / Application Search — Phase 14.34
 *
 * macOS Spotlight-style launcher: ⌘ Space to open, full-width frosted glass
 * overlay, real-time fuzzy search across apps + system actions, keyboard nav,
 * result icons, category labels, smooth open/close animation.
 */

import { useEffect, useRef, useState, useMemo } from "react";
import type { DesktopController } from "@/session/useDesktopSession";

interface LauncherResult {
  id: string;
  name: string;
  category: string;
  iconUrl?: string;
  tint?: string;
  action: () => void;
}

interface LauncherProps {
  isOpen: boolean;
  session: DesktopController;
  onClose: () => void;
}

const SYSTEM_ACTIONS = [
  { id: "action.sleep", name: "Sleep", category: "System", icon: "🌙" },
  { id: "action.settings", name: "System Settings", category: "System", icon: "⚙️" },
  { id: "action.screenshot", name: "Take Screenshot", category: "System", icon: "📷" },
  { id: "action.lock", name: "Lock Screen", category: "System", icon: "🔒" },
];

export function Launcher({ isOpen, session, onClose }: LauncherProps) {
  const [query, setQuery] = useState("");
  const [selected, setSelected] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (isOpen) {
      setQuery("");
      setSelected(0);
      setTimeout(() => inputRef.current?.focus(), 60);
    }
  }, [isOpen]);

  const allApps = session.state.dock.filter((d) => !d.id.startsWith("separator"));

  const results: LauncherResult[] = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) {
      return allApps.slice(0, 8).map((app) => ({
        id: app.id,
        name: app.name,
        category: "Applications",
        iconUrl: app.iconUrl,
        tint: app.tint,
        action: () => { session.launch(app.id); onClose(); },
      }));
    }

    const appResults = allApps
      .filter((a) => a.name.toLowerCase().includes(q))
      .map((app) => ({
        id: app.id,
        name: app.name,
        category: "Applications",
        iconUrl: app.iconUrl,
        tint: app.tint,
        action: () => { session.launch(app.id); onClose(); },
      }));

    const systemResults = SYSTEM_ACTIONS.filter((a) => a.name.toLowerCase().includes(q)).map((a) => ({
      id: a.id,
      name: a.name,
      category: "System",
      iconUrl: undefined,
      tint: "#636366",
      action: () => { session.setStatus(`action: ${a.name}`); onClose(); },
    }));

    return [...appResults, ...systemResults].slice(0, 10);
  }, [query, allApps, session, onClose]);

  useEffect(() => {
    setSelected(0);
  }, [query]);

  useEffect(() => {
    if (!isOpen) return;
    const handler = (e: KeyboardEvent) => {
      if (e.key === "Escape") { onClose(); return; }
      if (e.key === "ArrowDown") {
        setSelected((s) => Math.min(s + 1, results.length - 1));
        e.preventDefault();
      }
      if (e.key === "ArrowUp") {
        setSelected((s) => Math.max(s - 1, 0));
        e.preventDefault();
      }
      if (e.key === "Enter" && results[selected]) {
        results[selected]!.action();
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [isOpen, results, selected, onClose]);

  const grouped = groupByCategory(results);

  return (
    <>
      {/* Scrim */}
      <div
        className={[
          "fixed inset-0 z-[99900] transition-opacity duration-200",
          isOpen ? "opacity-100 pointer-events-auto" : "opacity-0 pointer-events-none",
        ].join(" ")}
        style={{ background: "rgba(0,0,0,0.4)" }}
        onClick={onClose}
      />

      {/* Panel */}
      <div
        className={[
          "fixed z-[99901] transition-all duration-200 ease-[cubic-bezier(0.28,0.11,0.32,1)]",
          isOpen ? "opacity-100 scale-100" : "opacity-0 scale-95 pointer-events-none",
        ].join(" ")}
        style={{
          left: "50%",
          top: "30%",
          transform: "translateX(-50%)",
          width: 640,
          background: "rgba(246,246,246,0.92)",
          backdropFilter: "blur(40px) saturate(200%)",
          WebkitBackdropFilter: "blur(40px) saturate(200%)",
          borderRadius: 16,
          border: "1px solid rgba(0,0,0,0.10)",
          boxShadow: "0 40px 120px rgba(0,0,0,0.35), 0 8px 32px rgba(0,0,0,0.20)",
          overflow: "hidden",
        }}
      >
        {/* Search bar */}
        <div className="flex items-center gap-3 border-b border-black/[0.08] px-4 py-3">
          <svg width="18" height="18" viewBox="0 0 18 18" fill="none" className="flex-shrink-0 text-[#8E8E93]">
            <circle cx="7.5" cy="7.5" r="5.5" stroke="currentColor" strokeWidth="1.5"/>
            <path d="M11.5 11.5 16 16" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/>
          </svg>
          <input
            ref={inputRef}
            type="text"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Spotlight Search"
            className="flex-1 bg-transparent text-[17px] font-light text-[#1D1D1F] placeholder-[#8E8E93] outline-none"
            autoComplete="off"
            spellCheck={false}
          />
          {query && (
            <button
              type="button"
              onClick={() => setQuery("")}
              className="h-5 w-5 rounded-full bg-[#8E8E93]/30 text-[11px] text-[#6E6E73] flex items-center justify-center hover:bg-[#8E8E93]/50"
            >
              ✕
            </button>
          )}
        </div>

        {/* Results */}
        {results.length > 0 ? (
          <div className="max-h-80 overflow-y-auto py-2">
            {grouped.map(({ category, items }) => (
              <div key={category}>
                <p className="px-4 pt-3 pb-1 text-[11px] font-semibold uppercase tracking-wider text-[#8E8E93]">
                  {category}
                </p>
                {items.map((result) => {
                  const globalIndex = results.indexOf(result);
                  const isSelected = globalIndex === selected;
                  return (
                    <button
                      key={result.id}
                      type="button"
                      className={[
                        "flex w-full items-center gap-3 px-4 py-2 text-left transition-colors duration-75",
                        isSelected ? "bg-[#0E7AFF]/10" : "hover:bg-black/5",
                      ].join(" ")}
                      onClick={() => result.action()}
                      onMouseEnter={() => setSelected(globalIndex)}
                    >
                      <AppIcon item={result} size={32} />
                      <div className="min-w-0 flex-1">
                        <p
                          className={[
                            "text-[14px] font-medium truncate",
                            isSelected ? "text-[#0E7AFF]" : "text-[#1D1D1F]",
                          ].join(" ")}
                        >
                          {result.name}
                        </p>
                      </div>
                      {isSelected && (
                        <span className="text-[11px] text-[#8E8E93]">↵</span>
                      )}
                    </button>
                  );
                })}
              </div>
            ))}
          </div>
        ) : (
          <div className="flex flex-col items-center justify-center py-10 text-center">
            <p className="text-[15px] font-medium text-[#6E6E73]">No results for "{query}"</p>
            <p className="mt-1 text-[13px] text-[#8E8E93]">Try a different search term.</p>
          </div>
        )}
      </div>
    </>
  );
}

function AppIcon({ item, size }: { item: LauncherResult; size: number }) {
  if (item.iconUrl) {
    return (
      <img
        src={item.iconUrl}
        alt={item.name}
        className="object-cover rounded-[8px] flex-shrink-0"
        style={{ width: size, height: size }}
        draggable={false}
      />
    );
  }
  return (
    <div
      className="flex-shrink-0 flex items-center justify-center rounded-[8px] font-semibold text-white"
      style={{
        width: size,
        height: size,
        background: item.tint ?? "#636366",
        fontSize: size * 0.44,
      }}
    >
      {item.name[0]}
    </div>
  );
}

function groupByCategory(
  results: LauncherResult[]
): { category: string; items: LauncherResult[] }[] {
  const map = new Map<string, LauncherResult[]>();
  for (const r of results) {
    if (!map.has(r.category)) map.set(r.category, []);
    map.get(r.category)!.push(r);
  }
  return [...map.entries()].map(([category, items]) => ({ category, items }));
}
