import { Link } from "react-router-dom";
import type { ReactNode } from "react";
import { useMemo, useState } from "react";

import { InteractiveDesktop, InteractiveWindow } from "@/components/interactive/Shell";
import { Dock } from "@/components/interactive/Dock";
import { useDesktopSession } from "@/session/useDesktopSession";
import { BackgroundLayer } from "@/background/BackgroundLayer";
import { VideoSource } from "@/background/adapters/VideoSource";
import defaultBg from "@/assets/DefaultBackground.mp4";
import { FolderIcon } from "@/components/folder/FolderIcon";
import type { FolderColor, SystemFolderKind, FolderIconProps } from "@/components/folder/FolderIcon";


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
      <div className="relative h-[calc(100vh-2.5rem)] overflow-hidden bg-[#1C1C1E]">
        <p className="absolute top-10 left-1/2 -translate-x-1/2 text-sm text-white/50 pointer-events-none">
          Hover icons to magnify · Click to launch
        </p>
        <Dock session={session} />
        {session.status !== "Ready — click, type, drag" && (
          <p className="absolute bottom-20 left-1/2 -translate-x-1/2 text-xs text-white/50 font-mono">
            {session.status}
          </p>
        )}
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


export function BackgroundPage() {
  const source = useMemo(() => new VideoSource(defaultBg), []);
  return (
    <div className="relative h-screen w-screen overflow-hidden bg-black">
      <BackgroundLayer source={source} fitMode="fill" visible lowPower={false} />
    </div>
  );
}

// ── Folder Icon Lab ───────────────────────────────────────────────────────────

const FOLDER_COLORS: FolderColor[] = [
  "blue", "gray", "green", "purple", "red", "yellow", "orange", "pink",
];

const SYSTEM_FOLDERS: { kind: SystemFolderKind; label: string }[] = [
  { kind: "home",         label: "Home" },
  { kind: "documents",    label: "Documents" },
  { kind: "downloads",    label: "Downloads" },
  { kind: "desktop",      label: "Desktop" },
  { kind: "applications", label: "Applications" },
];

function FolderRow({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="mb-8">
      <p className="mb-3 text-[11px] font-semibold uppercase tracking-widest text-white/40">{label}</p>
      <div className="flex flex-wrap gap-6 items-end">{children}</div>
    </div>
  );
}

function FolderTile({ label, ...props }: FolderIconProps & { label?: string }) {
  return (
    <div className="flex flex-col items-center gap-1.5">
      <FolderIcon {...props} size={96} />
      {label && <p className="text-[11px] text-white/55 text-center max-w-[88px] leading-tight">{label}</p>}
    </div>
  );
}

export function FolderPage() {
  const [darkMode, setDarkMode] = useState(false);
  const [activeState, setActiveState] = useState<FolderIconProps["state"]>("rest");

  return (
    <LabChrome title="Folder Icon">
      <div className={`min-h-[calc(100vh-2.5rem)] overflow-y-auto p-8 ${
        darkMode ? "bg-[#1c1c1e]" : "bg-[#2c2c2e]"
      }`}>
        {/* Controls */}
        <div className="mb-8 flex flex-wrap gap-3">
          <button
            type="button"
            onClick={() => setDarkMode(d => !d)}
            className={`rounded-xl px-4 py-2 text-[12px] font-semibold transition ${
              darkMode
                ? "bg-white/15 text-white ring-1 ring-white/30"
                : "bg-white/8 text-white/70 hover:bg-white/15"
            }`}
          >
            {darkMode ? "● Dark Mode" : "◯ Light Mode"}
          </button>
          {(["rest", "selected", "open", "drag-over"] as const).map(s => (
            <button
              key={s}
              type="button"
              onClick={() => setActiveState(s)}
              className={`rounded-xl px-4 py-2 text-[12px] font-semibold transition capitalize ${
                activeState === s
                  ? "bg-white/20 text-white ring-1 ring-white/30"
                  : "bg-white/8 text-white/50 hover:bg-white/15"
              }`}
            >
              {s}
            </button>
          ))}
        </div>

        {/* Tint Colours */}
        <FolderRow label="Tint Colours — empty">
          {FOLDER_COLORS.map(c => (
            <FolderTile key={c} color={c} darkMode={darkMode} state={activeState} label={c} />
          ))}
        </FolderRow>

        {/* System Folders */}
        <FolderRow label="System Folders — fixed glyphs">
          {SYSTEM_FOLDERS.map(({ kind, label }) => (
            <FolderTile key={kind} color="blue" system={kind} darkMode={darkMode} state={activeState} label={label} />
          ))}
        </FolderRow>

        {/* Content Peeks */}
        <FolderRow label="Content Peeks — 1 / 2 / 3 items">
          <FolderTile color="blue" darkMode={darkMode} state={activeState} label="1 file"
            peeks={[{ accentColor: "#0A84FF" }]} />
          <FolderTile color="green" darkMode={darkMode} state={activeState} label="2 files"
            peeks={[{ accentColor: "#32D74B" }, { accentColor: "#FF9F0A" }]} />
          <FolderTile color="purple" darkMode={darkMode} state={activeState} label="3 files"
            peeks={[
              { accentColor: "#BF5AF2" },
              { accentColor: "#0A84FF" },
              { accentColor: "#FF375F" },
            ]} />
          <FolderTile color="orange" darkMode={darkMode} state={activeState} label="Sub-folders"
            peeks={[
              { isFolder: true, accentColor: "#FF9F0A" },
              { isFolder: true, accentColor: "#5AC8FA" },
              { isFolder: true, accentColor: "#BF5AF2" },
            ]} />
        </FolderRow>

        {/* Badges */}
        <FolderRow label="Badges">
          <FolderTile color="blue" darkMode={darkMode} state={activeState} label="Shared"
            badges={["shared"]} peeks={[{ accentColor: "#0A84FF" }]} />
          <FolderTile color="blue" darkMode={darkMode} state={activeState} label="iCloud"
            badges={["icloud"]} peeks={[{ accentColor: "#32D74B" }]} />
          <FolderTile color="gray" darkMode={darkMode} state={activeState} label="Locked"
            badges={["locked"]} />
          <FolderTile color="blue" darkMode={darkMode} state={activeState} label="Alias"
            badges={["alias"]} peeks={[{ accentColor: "#FF9F0A" }]} />
          <FolderTile color="blue" darkMode={darkMode} state={activeState} label="Shared + Locked"
            badges={["shared", "locked"]} peeks={[{ accentColor: "#BF5AF2" }]} />
        </FolderRow>

        {/* Size Scale */}
        <FolderRow label="Size Scale — 256 / 128 / 96 / 64 / 48 / 32 / 16">
          {[256, 128, 96, 64, 48, 32, 16].map(s => (
            <div key={s} className="flex flex-col items-center gap-1">
              <FolderIcon color="blue" darkMode={darkMode} state={activeState}
                peeks={[{ accentColor: "#0A84FF" }, { accentColor: "#FF9F0A" }]}
                size={s} />
              <p className="text-[10px] text-white/40">{s}px</p>
            </div>
          ))}
        </FolderRow>
      </div>
    </LabChrome>
  );
}

/* ── Track B — full macOS Sonoma desktop (14.24–14.48) ─────────────────────── */
import { TrackBDesktop } from "@/components/trackb/TrackBDesktop";

export function TrackBPage() {
  // No LabChrome — the desktop is the chrome
  return <TrackBDesktop />;
}
