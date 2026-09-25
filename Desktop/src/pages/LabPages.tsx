import { Link } from "react-router-dom";
import type { ReactNode } from "react";

import { InteractiveDesktop, InteractiveWindow } from "@/components/interactive/Shell";
import { useDesktopSession } from "@/session/useDesktopSession";

function LabChrome({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="lab-shell">
      <div className="border-b border-hairline bg-white/80 px-4 py-2 text-[13px] backdrop-blur">
        <Link to="/" className="text-brand hover:underline">
          Desktop Lab
        </Link>
        <span className="mx-2 text-ink-tertiary">/</span>
        <span className="font-semibold">{title}</span>
      </div>
      {children}
    </div>
  );
}

export function DesktopPage() {
  const session = useDesktopSession();
  return (
    <LabChrome title="Desktop">
      <div className="h-[calc(100vh-2.5rem)]">
        <InteractiveDesktop session={session} />
      </div>
    </LabChrome>
  );
}

export function CombinedPage() {
  const session = useDesktopSession();
  return <InteractiveDesktop session={session} />;
}

export function WindowPage() {
  const session = useDesktopSession();
  const window = session.painted.find((item) => item.content === "inputlab") ?? session.painted[0];
  if (!window) {
    return null;
  }
  return (
    <LabChrome title="Window">
      <div className="relative h-[calc(100vh-2.5rem)] bg-[#B8C4D4] p-10">
        <InteractiveWindow
          window={{ ...window, x: 80, y: 40, width: 640, height: 560 }}
          focused
          session={session}
        />
      </div>
    </LabChrome>
  );
}

export function InputLabPage() {
  const session = useDesktopSession();
  const window = session.painted.find((item) => item.content === "inputlab");
  if (!window) {
    return null;
  }
  return (
    <LabChrome title="Input Lab">
      <div className="relative h-[calc(100vh-2.5rem)] bg-[#B8C4D4] p-8">
        <InteractiveWindow
          window={{ ...window, x: 120, y: 32, width: 720, height: 640 }}
          focused
          session={session}
        />
        <div className="absolute bottom-6 left-8 rounded-xl bg-black/70 px-4 py-3 font-mono text-xs text-white">
          key={session.telemetry.lastKey} · mouse={session.telemetry.lastMouse} · status={session.status}
        </div>
      </div>
    </LabChrome>
  );
}

export function ControlsPage() {
  const session = useDesktopSession();
  return (
    <LabChrome title="Controls">
      <div className="relative mx-auto h-[calc(100vh-2.5rem)] max-w-3xl bg-[#B8C4D4] p-8">
        <InteractiveWindow
          window={{
            id: "controls",
            applicationId: "lab.controls",
            title: "Controls",
            icon: null,
            x: 40,
            y: 24,
            width: 640,
            height: 680,
            minWidth: 320,
            minHeight: 240,
            maxWidth: 0,
            maxHeight: 0,
            state: "normal",
            resizable: true,
            movable: true,
            closable: true,
            modal: false,
            workspace: 1,
            z: 1,
            content: "inputlab",
            restore: null,
          }}
          focused
          session={session}
        />
        <p className="absolute bottom-4 left-8 text-sm text-ink">Status: {session.status}</p>
      </div>
    </LabChrome>
  );
}

export function DockPage() {
  const session = useDesktopSession();
  return (
    <LabChrome title="Dock">
      <div className="relative flex h-[calc(100vh-2.5rem)] items-end justify-center bg-[#B8C4D4] pb-16">
        <p className="absolute top-10 text-ink-secondary">Click a tile to launch or focus.</p>
        <div className="fixed bottom-8 left-1/2 z-50 flex -translate-x-1/2 items-end gap-2 rounded-2xl border border-black/10 bg-white/70 px-3 py-2 shadow-dock backdrop-blur-xl">
          {session.state.dock.map((item) => (
            <button
              key={item.id}
              type="button"
              className="group flex w-14 flex-col items-center"
              onClick={() => session.launch(item.id)}
            >
              <span
                className="h-10 w-10 rounded-xl border border-black/10 shadow-sm transition group-hover:-translate-y-2 group-hover:scale-110"
                style={{ background: item.tint }}
              />
              <span className="mt-1 text-[10px] text-ink">{item.name}</span>
            </button>
          ))}
        </div>
        <p className="absolute bottom-28 text-sm text-ink">{session.status}</p>
      </div>
    </LabChrome>
  );
}

export function MenuBarPage() {
  const session = useDesktopSession();
  return (
    <LabChrome title="Menu bar">
      <div className="relative h-[calc(100vh-2.5rem)] bg-[#B8C4D4]">
        <div className="flex h-7 items-center gap-4 border-b border-black/10 bg-white/80 px-4 text-[13px] backdrop-blur-xl">
          <button type="button" className="font-bold" onClick={session.openMenu}>
            Roboticela
          </button>
          <button type="button" className="font-semibold" onClick={session.openMenu}>
            File
          </button>
          <span>Edit</span>
          <span>View</span>
          <span className="ml-auto font-medium">{session.status}</span>
        </div>
        {session.state.surface !== "none" ? (
          <div className="absolute left-4 top-7 w-56 rounded-xl border border-hairline bg-white py-1 shadow-menu">
            {session.menu.map((item, index) =>
              item.separator ? (
                <div key={index} className="my-1 h-px bg-hairline" />
              ) : (
                <button
                  key={item.id}
                  type="button"
                  className="block w-full px-3 py-1.5 text-left text-[13px] hover:bg-brand/10"
                  onClick={() => {
                    session.setStatus(item.label ?? "");
                    session.closeSurface();
                  }}
                >
                  {item.label}
                </button>
              )
            )}
          </div>
        ) : null}
        <p className="p-8 text-ink-secondary">Click Roboticela or File. Type keys — status updates live.</p>
      </div>
    </LabChrome>
  );
}
