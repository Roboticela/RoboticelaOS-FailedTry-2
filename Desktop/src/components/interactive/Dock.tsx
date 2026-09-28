import { useState, useRef, useEffect, useCallback } from "react";
import type { DesktopController } from "@/session/useDesktopSession";
import type { DockItem } from "@/state/desktopState";

const BASE_SIZE = 48;

function getIconScale(hoveredIndex: number | null, index: number): number {
  if (hoveredIndex === null) return 1;
  const distance = Math.abs(index - hoveredIndex);
  if (distance === 0) return 1.15;
  if (distance === 1) return 1.08;
  if (distance === 2) return 1.03;
  return 1;
}

export function Dock({ session }: { session: DesktopController }) {
  const [hoveredApp, setHoveredApp] = useState<DockItem | null>(null);
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);
  const [bouncingAppId, setBouncingAppId] = useState<string | null>(null);
  const leaveTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const handleMouseLeave = useCallback(() => {
    if (leaveTimeoutRef.current) clearTimeout(leaveTimeoutRef.current);
    leaveTimeoutRef.current = setTimeout(() => {
      setHoveredApp(null);
      setHoveredIndex(null);
    }, 250);
  }, []);

  const handleMouseEnterIcon = useCallback((app: DockItem, index: number) => {
    if (leaveTimeoutRef.current) clearTimeout(leaveTimeoutRef.current);
    setHoveredApp(app);
    setHoveredIndex(index);
  }, []);

  useEffect(() => {
    return () => {
      if (leaveTimeoutRef.current) clearTimeout(leaveTimeoutRef.current);
    };
  }, []);

  const handleAppClick = useCallback(
    (item: DockItem) => {
      setBouncingAppId(item.id);
      setTimeout(() => {
        const existing = session.state.windows.find(
          (w) => w.applicationId === item.id
        );
        if (existing) {
          session.focus(existing.id);
        } else {
          session.launch(item.id);
        }
        setBouncingAppId(null);
      }, 200);
    },
    [session]
  );

  const { dock } = session.state;

  const isAppOpen = (appId: string) =>
    session.state.windows.some(
      (w) => w.applicationId === appId && w.state !== "closed"
    );

  return (
    <div
      className="absolute bottom-1 left-1/2 -translate-x-1/2 flex items-end px-1 py-1 rounded-2xl h-[56px] overflow-visible transition-all duration-300 z-[99999]"
      onMouseLeave={handleMouseLeave}
    >
      <div
        className="absolute inset-0 -z-10 rounded-2xl"
        style={{
          background: "rgba(255,255,255,0.18)",
          backdropFilter: "blur(20px) saturate(180%)",
          WebkitBackdropFilter: "blur(20px) saturate(180%)",
          border: "1px solid rgba(255,255,255,0.25)",
          boxShadow: "0 8px 32px rgba(0,0,0,0.22), inset 0 1px 0 rgba(255,255,255,0.3)",
        }}
      />

      {dock.map((app, index) => {
        if (app.id.startsWith("separator")) {
          return (
            <div
              key={app.id}
              className="w-px bg-white/20 rounded-full mx-0.5 self-stretch my-1"
            />
          );
        }

        const scale = getIconScale(hoveredIndex, index);
        const iconSize = BASE_SIZE * scale;
        const appWindows = session.state.windows.filter(
          (w) => w.applicationId === app.id && w.state !== "closed"
        );
        const hasWindows = appWindows.length > 0;

        return (
          <div
            key={app.id}
            onMouseEnter={() => handleMouseEnterIcon(app, index)}
            onClick={() => handleAppClick(app)}
            className={[
              "relative flex flex-col items-center justify-end cursor-pointer",
              bouncingAppId === app.id ? "animate-bounceOnce" : "",
            ].join(" ")}
            style={{
              transformOrigin: "bottom center",
              marginBottom: hoveredIndex !== null ? `${(scale - 1) * 20}px` : "0px",
              transition: "all 0.15s cubic-bezier(0.4, 0, 0.2, 1)",
            }}
          >
            {hoveredApp?.id === app.id && (
              hasWindows ? (
                <div
                  className="absolute -top-[140px] left-1/2 -translate-x-1/2 flex gap-3 p-3 rounded-xl bg-gray-950/90 text-white shadow-2xl backdrop-blur-xl pointer-events-auto animate-fadeSlide z-50 border border-white/10 min-w-[150px]"
                  onClick={(e) => e.stopPropagation()}
                  onMouseEnter={() => { if (leaveTimeoutRef.current) clearTimeout(leaveTimeoutRef.current); }}
                  onMouseLeave={handleMouseLeave}
                >
                  {appWindows.map((win) => (
                    <div
                      key={win.id}
                      onClick={() => session.focus(win.id)}
                      className={[
                        "relative group/preview flex flex-col items-center gap-1.5 p-2 rounded-lg transition duration-150 cursor-pointer min-w-[95px] max-w-[130px]",
                        win.state === "minimized"
                          ? "bg-white/5 opacity-75 hover:opacity-100 hover:bg-white/10"
                          : "bg-white/10 hover:bg-white/15",
                      ].join(" ")}
                    >
                      <div className="w-16 h-12 rounded bg-black/40 flex items-center justify-center relative shadow-inner border border-white/5">
                        {app.iconUrl ? (
                          <img src={app.iconUrl} alt="" className="w-8 h-8 object-contain" />
                        ) : (
                          <span className="text-white font-semibold text-lg">{app.name[0]}</span>
                        )}
                        {win.state === "minimized" && (
                          <span className="absolute bottom-1 right-1 text-[8px] bg-yellow-500/80 text-black px-1 rounded font-semibold scale-90">min</span>
                        )}
                      </div>
                      <span className="text-[10px] font-medium text-center truncate w-full text-gray-300 group-hover/preview:text-white px-0.5">{win.title}</span>
                      <button
                        onClick={(e) => { e.stopPropagation(); session.close(win.id); }}
                        className="absolute -top-1.5 -right-1.5 w-4 h-4 rounded-full bg-red-500/90 text-white flex items-center justify-center text-[10px] font-bold shadow-md opacity-0 group-hover/preview:opacity-100 transition-opacity hover:bg-red-600"
                      >x</button>
                    </div>
                  ))}
                  <div className="absolute left-1/2 -translate-x-1/2 -bottom-1 w-2 h-2 bg-gray-900/90 rotate-45 border-r border-b border-white/10" />
                </div>
              ) : (
                <div className="absolute -top-9 px-3 py-1 rounded-md bg-gray-900/95 text-white shadow-xl text-xs font-medium backdrop-blur-xl animate-fadeSlide pointer-events-none whitespace-nowrap z-50 border border-white/10">
                  {app.name}
                  <div className="absolute left-1/2 -translate-x-1/2 -bottom-1 w-2 h-2 bg-gray-900/95 rotate-45 border-r border-b border-white/10" />
                </div>
              )
            )}

            <div
              className="rounded-xl flex items-center justify-center overflow-hidden"
              style={{
                width: `${iconSize}px`,
                height: `${iconSize}px`,
                boxShadow: "0 2px 8px rgba(0,0,0,0.2)",
                transition: "all 0.15s cubic-bezier(0.4, 0, 0.2, 1)",
              }}
            >
              {app.iconUrl ? (
                <img
                  src={app.iconUrl}
                  alt={app.name}
                  className="w-full h-full object-cover rounded-xl"
                  draggable={false}
                />
              ) : (
                <div
                  className="w-full h-full rounded-xl flex items-center justify-center"
                  style={{ background: app.tint ?? "#8E8E93" }}
                >
                  <span className="font-semibold text-white select-none" style={{ fontSize: `${iconSize * 0.4}px` }}>
                    {app.name[0]}
                  </span>
                </div>
              )}
            </div>

            {isAppOpen(app.id) && (
              <div
                className="absolute -bottom-0.5 w-1 h-1 bg-white/90 rounded-full"
                style={{ boxShadow: "0 0 4px rgba(255,255,255,0.6)" }}
              />
            )}
          </div>
        );
      })}
    </div>
  );
}
