/*
 * NotificationBanner + NotificationCentre — Phase 14.32
 *
 * macOS Sonoma notification system: banners slide in from the top-right, 
 * auto-dismiss after 4 s, stack up to 3 visible. Notification Centre is a 
 * frosted-glass panel that slides in from the right with grouped notifications.
 */

import { useCallback, useEffect, useState } from "react";
import type { NotificationKindValue, NotificationRecord } from "@/state/desktopState";
import { NotificationKind } from "@/state/desktopState";

/* ── Icon lookup by kind ────────────────────────────────────────────────────── */

function kindIcon(kind: NotificationKindValue): string {
  switch (kind) {
    case NotificationKind.Success:  return "✅";
    case NotificationKind.Warning:  return "⚠️";
    case NotificationKind.Error:    return "🔴";
    case NotificationKind.Progress: return "🔄";
    default: return "ℹ️";
  }
}

function kindAccent(kind: NotificationKindValue): string {
  switch (kind) {
    case NotificationKind.Success:  return "#30D158";
    case NotificationKind.Warning:  return "#FF9F0A";
    case NotificationKind.Error:    return "#FF3B30";
    case NotificationKind.Progress: return "#0E7AFF";
    default: return "#636366";
  }
}

/* ── Single notification banner ─────────────────────────────────────────────── */

interface BannerProps {
  notification: NotificationRecord;
  onDismiss: (id: string) => void;
  index: number; // 0 = newest at top
}

export function NotificationBanner({ notification, onDismiss, index }: BannerProps) {
  const [visible, setVisible] = useState(false);
  const [leaving, setLeaving] = useState(false);

  useEffect(() => {
    // Stagger entrance
    const inTimer = setTimeout(() => setVisible(true), index * 80);
    // Auto-dismiss
    const outTimer = setTimeout(() => dismiss(), 4000 + index * 500);
    return () => { clearTimeout(inTimer); clearTimeout(outTimer); };
  }, []);

  const dismiss = useCallback(() => {
    setLeaving(true);
    setTimeout(() => onDismiss(notification.id), 300);
  }, [notification.id, onDismiss]);

  return (
    <div
      className={[
        "pointer-events-auto flex items-start gap-3 rounded-[14px] p-3 select-none",
        "transition-all duration-300 ease-[cubic-bezier(0.28,0.11,0.32,1)]",
        visible && !leaving
          ? "translate-x-0 opacity-100"
          : "translate-x-[120%] opacity-0",
      ].join(" ")}
      style={{
        background: "rgba(246,246,246,0.94)",
        backdropFilter: "blur(24px) saturate(180%)",
        WebkitBackdropFilter: "blur(24px) saturate(180%)",
        border: "1px solid rgba(0,0,0,0.08)",
        boxShadow: "0 8px 32px rgba(0,0,0,0.18), 0 2px 8px rgba(0,0,0,0.10)",
        width: 340,
      }}
    >
      {/* App icon placeholder */}
      <div
        className="mt-0.5 flex h-10 w-10 flex-shrink-0 items-center justify-center rounded-[10px] text-xl"
        style={{ background: kindAccent(notification.kind) + "22" }}
      >
        <span>{kindIcon(notification.kind)}</span>
      </div>
      <div className="min-w-0 flex-1">
        <p className="text-[13px] font-semibold leading-tight text-[#1D1D1F] truncate">
          {notification.title}
        </p>
        <p className="mt-0.5 text-[12px] leading-snug text-[#6E6E73] line-clamp-2">
          {notification.body}
        </p>
      </div>
      <button
        type="button"
        onClick={dismiss}
        className="ml-1 flex-shrink-0 rounded-full w-5 h-5 flex items-center justify-center text-[11px] text-[#8E8E93] hover:bg-black/10 transition-colors"
        aria-label="Dismiss"
      >
        ✕
      </button>
    </div>
  );
}

/* ── Notification stack (top-right corner) ───────────────────────────────────── */

interface NotificationStackProps {
  notifications: NotificationRecord[];
  onDismiss: (id: string) => void;
}

export function NotificationStack({ notifications, onDismiss }: NotificationStackProps) {
  const visible = notifications.slice(0, 3);
  return (
    <div className="pointer-events-none fixed right-4 top-10 z-[99990] flex flex-col gap-2">
      {visible.map((notification, idx) => (
        <NotificationBanner
          key={notification.id}
          notification={notification}
          onDismiss={onDismiss}
          index={idx}
        />
      ))}
    </div>
  );
}

/* ── Notification Centre panel ───────────────────────────────────────────────── */

interface NotificationCentreProps {
  notifications: NotificationRecord[];
  isOpen: boolean;
  onClose: () => void;
  onDismiss: (id: string) => void;
  onDismissAll: () => void;
  onMarkRead: (id: string) => void;
}

export function NotificationCentre({
  notifications,
  isOpen,
  onClose,
  onDismiss,
  onDismissAll,
  onMarkRead,
}: NotificationCentreProps) {
  const unread = notifications.filter((n) => !n.read).length;
  const grouped = groupByApp(notifications);

  useEffect(() => {
    if (!isOpen) return;
    const handler = (e: KeyboardEvent) => { if (e.key === "Escape") onClose(); };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [isOpen, onClose]);

  return (
    <>
      {/* Scrim */}
      {isOpen && (
        <div
          className="fixed inset-0 z-[99980]"
          onClick={onClose}
        />
      )}
      {/* Panel */}
      <div
        className={[
          "fixed top-0 bottom-0 right-0 z-[99985] flex flex-col",
          "transition-transform duration-300 ease-[cubic-bezier(0.28,0.11,0.32,1)]",
          isOpen ? "translate-x-0" : "translate-x-full",
        ].join(" ")}
        style={{
          width: 360,
          background: "rgba(236,236,238,0.96)",
          backdropFilter: "blur(32px) saturate(200%)",
          WebkitBackdropFilter: "blur(32px) saturate(200%)",
          borderLeft: "1px solid rgba(0,0,0,0.10)",
          boxShadow: "-12px 0 40px rgba(0,0,0,0.15)",
        }}
      >
        {/* Header */}
        <div className="flex items-center justify-between border-b border-black/10 px-5 py-4">
          <div>
            <h2 className="text-[17px] font-semibold text-[#1D1D1F]">Notification Centre</h2>
            {unread > 0 && (
              <p className="text-[12px] text-[#6E6E73]">{unread} unread</p>
            )}
          </div>
          <div className="flex items-center gap-2">
            {notifications.length > 0 && (
              <button
                type="button"
                onClick={onDismissAll}
                className="text-[13px] text-[#0E7AFF] hover:opacity-75 font-medium"
              >
                Clear All
              </button>
            )}
            <button
              type="button"
              onClick={onClose}
              className="flex h-7 w-7 items-center justify-center rounded-full bg-black/8 text-[13px] text-[#6E6E73] hover:bg-black/12 transition-colors"
            >
              ✕
            </button>
          </div>
        </div>

        {/* Body */}
        <div className="flex-1 overflow-y-auto px-4 py-3">
          {notifications.length === 0 ? (
            <div className="flex flex-col items-center justify-center h-full gap-3 py-20">
              <div className="text-4xl">🔔</div>
              <p className="text-[14px] text-[#6E6E73] font-medium">No Notifications</p>
              <p className="text-[12px] text-[#8E8E93] text-center">
                Notifications from apps appear here.
              </p>
            </div>
          ) : (
            <div className="space-y-4">
              {grouped.map(({ applicationId, items }) => (
                <div key={applicationId}>
                  <p className="mb-2 text-[11px] font-semibold uppercase tracking-wider text-[#8E8E93]">
                    {applicationId === "system" ? "System" : applicationId}
                  </p>
                  <div className="space-y-1.5">
                    {items.map((n) => (
                      <div
                        key={n.id}
                        className={[
                          "group flex items-start gap-3 rounded-xl p-3 transition-colors",
                          n.read
                            ? "bg-black/5 hover:bg-black/8"
                            : "bg-white shadow-sm hover:bg-white/90",
                        ].join(" ")}
                        onClick={() => onMarkRead(n.id)}
                      >
                        <div
                          className="mt-0.5 flex h-8 w-8 flex-shrink-0 items-center justify-center rounded-[8px] text-base"
                          style={{ background: kindAccent(n.kind) + "20" }}
                        >
                          {kindIcon(n.kind)}
                        </div>
                        <div className="min-w-0 flex-1">
                          {!n.read && (
                            <div
                              className="mb-1 inline-block h-2 w-2 rounded-full"
                              style={{ background: "#0E7AFF" }}
                            />
                          )}
                          <p className="text-[13px] font-medium leading-tight text-[#1D1D1F] truncate">
                            {n.title}
                          </p>
                          <p className="mt-0.5 text-[12px] text-[#6E6E73] line-clamp-2">
                            {n.body}
                          </p>
                        </div>
                        <button
                          type="button"
                          onClick={(e) => { e.stopPropagation(); onDismiss(n.id); }}
                          className="flex-shrink-0 h-5 w-5 rounded-full flex items-center justify-center text-[11px] text-[#8E8E93] opacity-0 group-hover:opacity-100 hover:bg-black/10 transition"
                        >
                          ✕
                        </button>
                      </div>
                    ))}
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </>
  );
}

function groupByApp(
  notifications: NotificationRecord[]
): { applicationId: string; items: NotificationRecord[] }[] {
  const map = new Map<string, NotificationRecord[]>();
  for (const n of notifications) {
    const key = n.applicationId ?? "system";
    if (!map.has(key)) map.set(key, []);
    map.get(key)!.push(n);
  }
  return [...map.entries()].map(([applicationId, items]) => ({ applicationId, items }));
}
