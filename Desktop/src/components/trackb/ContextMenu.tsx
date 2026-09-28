/*
 * ContextMenu — Phase 14.26
 *
 * macOS Sonoma-style context menu: frosted glass, separators, keyboard shortcut
 * hints, danger items, nested submenu indicators. Native chrome owns the hit
 * area; this component is the React lab specimen.
 */

import { useEffect, useRef, useState } from "react";

export interface ContextMenuItem {
  id?: string;
  label?: string;
  shortcut?: string;
  danger?: boolean;
  disabled?: boolean;
  separator?: boolean;
  submenu?: ContextMenuItem[];
  icon?: string; // SF Symbol-style emoji or SVG string
}

interface ContextMenuProps {
  items: ContextMenuItem[];
  x: number;
  y: number;
  onDismiss: () => void;
  onSelect?: (id: string) => void;
}

export function ContextMenu({ items, x, y, onDismiss, onSelect }: ContextMenuProps) {
  const ref = useRef<HTMLDivElement>(null);
  const [highlight, setHighlight] = useState(-1);
  const [submenuIndex, setSubmenuIndex] = useState<number | null>(null);

  // clamp to viewport
  const clampedX = Math.min(x, window.innerWidth - 220);
  const clampedY = Math.min(y, window.innerHeight - items.length * 28 - 16);

  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) onDismiss();
    };
    const keyHandler = (e: KeyboardEvent) => {
      const actionable = items.filter((item) => !item.separator && !item.disabled);
      if (e.key === "Escape") { onDismiss(); return; }
      if (e.key === "ArrowDown") {
        setHighlight((h) => (h + 1) % actionable.length);
        e.preventDefault();
      }
      if (e.key === "ArrowUp") {
        setHighlight((h) => (h - 1 + actionable.length) % actionable.length);
        e.preventDefault();
      }
      if (e.key === "Enter" && highlight >= 0) {
        const item = actionable[highlight];
        if (item?.id) { onSelect?.(item.id); onDismiss(); }
      }
    };
    window.addEventListener("mousedown", handler);
    window.addEventListener("keydown", keyHandler);
    return () => {
      window.removeEventListener("mousedown", handler);
      window.removeEventListener("keydown", keyHandler);
    };
  }, [highlight, items, onDismiss, onSelect]);

  let actionableIndex = -1;

  return (
    <div
      ref={ref}
      className="fixed z-[9999] w-52 overflow-hidden rounded-[10px] border border-black/[0.08] py-1 shadow-[0_20px_60px_rgba(0,0,0,0.28),0_4px_12px_rgba(0,0,0,0.12)]"
      style={{
        left: clampedX,
        top: clampedY,
        background: "rgba(246,246,246,0.92)",
        backdropFilter: "blur(20px) saturate(180%)",
        WebkitBackdropFilter: "blur(20px) saturate(180%)",
      }}
      onContextMenu={(e) => e.preventDefault()}
    >
      {items.map((item, idx) => {
        if (item.separator) {
          return <div key={`sep-${idx}`} className="my-1 h-px bg-black/10 mx-2" />;
        }

        actionableIndex++;
        const isHighlighted = actionableIndex === highlight;
        const hasSubmenu = item.submenu && item.submenu.length > 0;

        return (
          <button
            key={item.id ?? idx}
            type="button"
            disabled={item.disabled}
            className={[
              "relative flex w-full items-center gap-2 px-3 py-[5px] text-[13px] text-left select-none transition-colors duration-75",
              item.disabled
                ? "text-black/30 cursor-not-allowed"
                : item.danger && isHighlighted
                ? "bg-[#FF3B30] text-white"
                : item.danger
                ? "text-[#FF3B30] hover:bg-[#FF3B30] hover:text-white"
                : isHighlighted
                ? "bg-[#0E7AFF] text-white"
                : "text-[#1D1D1F] hover:bg-[#0E7AFF] hover:text-white",
            ].join(" ")}
            onMouseEnter={() => {
              setHighlight(actionableIndex);
              setSubmenuIndex(hasSubmenu ? idx : null);
            }}
            onMouseLeave={() => {
              if (!hasSubmenu) setSubmenuIndex(null);
            }}
            onClick={() => {
              if (!item.disabled && !hasSubmenu) {
                onSelect?.(item.id ?? "");
                onDismiss();
              }
            }}
          >
            {item.icon && (
              <span className="w-4 text-center text-sm leading-none">{item.icon}</span>
            )}
            <span className="flex-1 leading-tight">{item.label}</span>
            {item.shortcut && (
              <span
                className={[
                  "ml-auto text-[11px] font-medium",
                  isHighlighted ? "text-white/70" : "text-black/35",
                ].join(" ")}
              >
                {item.shortcut}
              </span>
            )}
            {hasSubmenu && (
              <span
                className={[
                  "ml-1 text-[10px]",
                  isHighlighted ? "text-white/80" : "text-black/40",
                ].join(" ")}
              >
                ▶
              </span>
            )}
            {/* Nested submenu */}
            {hasSubmenu && submenuIndex === idx && (
              <div
                className="absolute left-full top-0 ml-0.5 z-50 w-48 overflow-hidden rounded-[10px] border border-black/[0.08] py-1 shadow-[0_20px_60px_rgba(0,0,0,0.28)]"
                style={{
                  background: "rgba(246,246,246,0.95)",
                  backdropFilter: "blur(20px) saturate(180%)",
                  WebkitBackdropFilter: "blur(20px) saturate(180%)",
                }}
              >
                {item.submenu!.map((sub, si) =>
                  sub.separator ? (
                    <div key={si} className="my-1 h-px bg-black/10 mx-2" />
                  ) : (
                    <button
                      key={sub.id ?? si}
                      type="button"
                      className="flex w-full items-center gap-2 px-3 py-[5px] text-[13px] text-[#1D1D1F] hover:bg-[#0E7AFF] hover:text-white"
                      onClick={() => {
                        onSelect?.(sub.id ?? "");
                        onDismiss();
                      }}
                    >
                      {sub.icon && <span className="w-4 text-center">{sub.icon}</span>}
                      <span className="flex-1">{sub.label}</span>
                      {sub.shortcut && (
                        <span className="ml-auto text-[11px] text-black/35">{sub.shortcut}</span>
                      )}
                    </button>
                  )
                )}
              </div>
            )}
          </button>
        );
      })}
    </div>
  );
}

/** Hook that wires right-click on a target element to open a ContextMenu. */
export function useContextMenu() {
  const [menu, setMenu] = useState<{ x: number; y: number; items: ContextMenuItem[] } | null>(
    null
  );

  const open = (e: React.MouseEvent, items: ContextMenuItem[]) => {
    e.preventDefault();
    e.stopPropagation();
    setMenu({ x: e.clientX, y: e.clientY, items });
  };

  const close = () => setMenu(null);

  return { menu, open, close };
}
