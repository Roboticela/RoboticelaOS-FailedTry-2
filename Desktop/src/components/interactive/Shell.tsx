import type { DesktopController } from "@/session/useDesktopSession";
import type { DesktopWindow } from "@/state/desktopState";
import { ShellSurface } from "@/state/desktopState";
import { useRef } from "react";

function TrafficLights({
  window,
  onClose,
  onMinimize,
  onZoom,
}: {
  window: DesktopWindow;
  onClose: () => void;
  onMinimize: () => void;
  onZoom: () => void;
}) {
  return (
    <div className="absolute left-3 top-1/2 flex -translate-y-1/2 gap-2">
      <button
        type="button"
        aria-label={`Close ${window.title}`}
        className="h-3 w-3 rounded-full bg-traffic-close shadow-sm transition hover:brightness-95"
        onClick={(event) => {
          event.stopPropagation();
          onClose();
        }}
      />
      <button
        type="button"
        aria-label={`Minimize ${window.title}`}
        className="h-3 w-3 rounded-full bg-traffic-minimize shadow-sm transition hover:brightness-95"
        onClick={(event) => {
          event.stopPropagation();
          onMinimize();
        }}
      />
      <button
        type="button"
        aria-label={`Zoom ${window.title}`}
        className="h-3 w-3 rounded-full bg-traffic-zoom shadow-sm transition hover:brightness-95"
        onClick={(event) => {
          event.stopPropagation();
          onZoom();
        }}
      />
    </div>
  );
}

export function InteractiveWindow({
  window,
  focused,
  session,
}: {
  window: DesktopWindow;
  focused: boolean;
  session: DesktopController;
}) {
  const drag = useRef<{ ox: number; oy: number; sx: number; sy: number } | null>(null);

  return (
    <div
      className={`absolute overflow-hidden rounded-xl border shadow-window transition duration-200 ease-apple ${
        focused ? "border-black/10 bg-white z-20" : "border-black/5 bg-[#FAFAFA] z-10 opacity-95"
      }`}
      style={{ left: window.x, top: window.y, width: window.width, height: window.height }}
      onMouseDown={() => session.focus(window.id)}
    >
      <div
        className={`relative flex h-7 cursor-default items-center justify-center border-b ${
          focused ? "border-hairline bg-[#E8E8ED]" : "border-[#E5E5EA] bg-[#F0F0F5]"
        }`}
        onPointerDown={(event) => {
          if (!window.movable) {
            return;
          }
          session.focus(window.id);
          (event.target as HTMLElement).setPointerCapture(event.pointerId);
          drag.current = { ox: event.clientX, oy: event.clientY, sx: window.x, sy: window.y };
        }}
        onPointerMove={(event) => {
          if (!drag.current) {
            return;
          }
          const dx = event.clientX - drag.current.ox;
          const dy = event.clientY - drag.current.oy;
          session.move(window.id, drag.current.sx + dx, drag.current.sy + dy);
        }}
        onPointerUp={() => {
          drag.current = null;
        }}
      >
        <TrafficLights
          window={window}
          onClose={() => session.close(window.id)}
          onMinimize={() => session.minimize(window.id)}
          onZoom={() => session.maximize(window.id)}
        />
        <div className={`text-[13px] font-semibold ${focused ? "text-ink" : "text-ink-secondary"}`}>
          {window.title}
        </div>
      </div>
      <div className="h-[calc(100%-1.75rem)] overflow-auto bg-white p-6">
        <WindowBody kind={window.content} session={session} />
      </div>
    </div>
  );
}

function WindowBody({ kind, session }: { kind: DesktopWindow["content"]; session: DesktopController }) {
  if (kind === "inputlab") {
    return <InputLabBody session={session} />;
  }
  if (kind === "overview") {
    return (
      <div className="space-y-4">
        <h1 className="text-xl font-semibold tracking-tight">System Overview</h1>
        <p className="text-[15px] leading-relaxed text-ink-secondary">
          Everything landed through Phase 15 that you can exercise in this lab.
        </p>
        <ul className="space-y-1 font-mono text-xs text-ink">
          <li>Typography · RoboText · Paint · Layout</li>
          <li>Input model · Window · Compositor</li>
          <li>Desktop shell · Document bridge</li>
        </ul>
        <div className="flex gap-2 pt-2">
          <button
            type="button"
            className="rounded-full bg-brand px-5 py-2 text-[15px] font-medium text-white transition hover:bg-brand-hover"
            onClick={() => session.launch("systemui.inputlab")}
          >
            Open Input Lab
          </button>
          <button
            type="button"
            className="rounded-full border border-hairline px-5 py-2 text-[15px] font-medium text-brand transition hover:bg-surface-secondary"
            onClick={() => session.setStatus("About RoboticelaOS")}
          >
            About
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="space-y-3">
      <h1 className="text-[28px] font-semibold tracking-tight">Beautiful by default</h1>
      <p className="text-[15px] text-ink-secondary">
        RoboticelaOS shapes every label with RoboText: one glyph run per script, one face from the system
        pack, one pen.
      </p>
    </div>
  );
}

function InputLabBody({ session }: { session: DesktopController }) {
  const { telemetry } = session;

  return (
    <div className="space-y-5">
      <div>
        <h1 className="text-xl font-semibold tracking-tight">Input Lab</h1>
        <p className="mt-1 text-[14px] text-ink-secondary">
          Type anywhere, click controls, drag the title bar. Live state is below and in the menu bar.
        </p>
      </div>

      <section className="space-y-2">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-ink-secondary">Live telemetry</h2>
        <div className="grid gap-2 rounded-xl bg-surface-secondary p-3 font-mono text-xs text-ink sm:grid-cols-2">
          <div>key: {telemetry.lastKey}</div>
          <div>mouse: {telemetry.lastMouse}</div>
          <div>
            pointer: {telemetry.pointer.x},{telemetry.pointer.y} buttons={telemetry.pointer.buttons}
          </div>
          <div>focus: {telemetry.focusTarget}</div>
        </div>
      </section>

      <section className="space-y-2">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-ink-secondary">Text fields</h2>
        <input
          className="w-full rounded-lg border border-hairline bg-white px-3 py-2 text-[15px] outline-none transition focus:border-brand focus:ring-4 focus:ring-brand/20"
          placeholder="Empty — click and type"
          onFocus={() => session.setFocusTarget("field.empty")}
          onChange={(event) => session.setStatus(`field ${event.target.value || "(empty)"}`)}
        />
        <input
          className="w-full rounded-lg border-2 border-brand bg-white px-3 py-2 text-[15px] outline-none ring-4 ring-brand/20"
          defaultValue="Focused specimen"
          onFocus={() => session.setFocusTarget("field.focused")}
          onChange={(event) => session.setStatus(`focused ${event.target.value}`)}
        />
        <input
          disabled
          className="w-full rounded-lg border border-hairline bg-surface-tertiary px-3 py-2 text-[15px] text-ink-tertiary"
          defaultValue="Disabled"
        />
        <input
          aria-invalid
          className="w-full rounded-lg border border-danger bg-white px-3 py-2 text-[15px] outline-none"
          defaultValue="Needs a value"
          onFocus={() => session.setFocusTarget("field.error")}
        />
      </section>

      <section className="space-y-2">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-ink-secondary">Buttons</h2>
        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            className="rounded-full bg-brand px-5 py-2 text-[15px] font-medium text-white hover:bg-brand-hover active:bg-brand-active"
            onClick={() => session.setStatus("primary pressed")}
          >
            Primary
          </button>
          <button
            type="button"
            className="rounded-full border border-hairline px-5 py-2 text-[15px] font-medium text-ink hover:bg-surface-secondary"
            onClick={() => session.setStatus("secondary pressed")}
          >
            Secondary
          </button>
          <button
            type="button"
            className="rounded-full bg-brand-active px-5 py-2 text-[15px] font-medium text-white"
            onClick={() => session.setStatus("pressed state")}
          >
            Pressed
          </button>
          <button
            type="button"
            disabled
            className="rounded-full border border-hairline bg-surface-tertiary px-5 py-2 text-[15px] font-medium text-ink-tertiary"
          >
            Disabled
          </button>
        </div>
      </section>

      <section className="space-y-2">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-ink-secondary">Toggles</h2>
        <div className="flex items-center gap-4">
          <label className="inline-flex items-center gap-2 text-sm">
            <input type="checkbox" defaultChecked className="h-4 w-4 accent-brand" onChange={(e) => session.setStatus(`check ${e.target.checked}`)} />
            Checked
          </label>
          <label className="inline-flex items-center gap-2 text-sm">
            <input type="checkbox" className="h-4 w-4 accent-brand" onChange={(e) => session.setStatus(`check ${e.target.checked}`)} />
            Unchecked
          </label>
          <label className="inline-flex items-center gap-2 text-sm">
            <input type="checkbox" role="switch" defaultChecked className="h-4 w-8 accent-success" onChange={(e) => session.setStatus(`toggle ${e.target.checked}`)} />
            Switch on
          </label>
        </div>
        <input
          type="range"
          min={0}
          max={100}
          defaultValue={42}
          className="w-full accent-brand"
          onChange={(event) => session.setStatus(`slider ${event.target.value}`)}
        />
      </section>
    </div>
  );
}

export function InteractiveMenuBar({ session }: { session: DesktopController }) {
  return (
    <div className="fixed inset-x-0 top-0 z-50 flex h-7 items-center gap-4 border-b border-black/10 bg-white/80 px-4 text-[13px] backdrop-blur-xl">
      <button type="button" className="font-bold text-ink" onClick={session.openMenu}>
        Roboticela
      </button>
      <button type="button" className="font-semibold" onClick={session.openMenu}>
        Input Lab
      </button>
      <div className="hidden gap-3 text-ink sm:flex">
        <button type="button" onClick={session.openMenu}>
          File
        </button>
        <span>Edit</span>
        <span>View</span>
        <span>Window</span>
        <span>Help</span>
      </div>
      <div className="ml-auto flex items-center gap-4 text-ink-secondary">
        <span>
          {session.state.activeWorkspace} of {session.state.workspaces.length}
        </span>
        <span className="max-w-[14rem] truncate font-medium text-ink">{session.status}</span>
      </div>
    </div>
  );
}

export function InteractiveDock({ session }: { session: DesktopController }) {
  return (
    <div className="fixed bottom-4 left-1/2 z-50 flex -translate-x-1/2 items-end gap-2 rounded-2xl border border-black/10 bg-white/70 px-3 py-2 shadow-dock backdrop-blur-xl">
      {session.state.dock.map((item) => {
        const focused = item.id === session.state.windows.find((w) => w.id === session.focusedId)?.applicationId;
        return (
          <button
            key={item.id}
            type="button"
            aria-label={item.name}
            className="group flex w-12 flex-col items-center"
            onClick={() => session.launch(item.id)}
          >
            <span
              className={`h-9 w-9 rounded-[10px] border border-black/10 shadow-sm transition duration-200 ease-apple group-hover:-translate-y-1 group-hover:scale-110 ${
                focused ? "ring-2 ring-brand/40" : ""
              }`}
              style={{ background: item.tint ?? "#8E8E93" }}
            />
            {item.running ? <span className="mt-1 h-1 w-1 rounded-full bg-ink" /> : <span className="mt-1 h-1 w-1" />}
          </button>
        );
      })}
    </div>
  );
}

export function InteractiveMenu({ session }: { session: DesktopController }) {
  if (session.state.surface !== ShellSurface.ApplicationMenu) {
    return null;
  }

  return (
    <>
      <button type="button" className="fixed inset-0 z-40 cursor-default bg-transparent" aria-label="Dismiss menu" onClick={session.closeSurface} />
      <div className="fixed left-24 top-7 z-50 w-56 overflow-hidden rounded-xl border border-hairline bg-white/95 py-1 shadow-menu backdrop-blur-xl">
        {session.menu.map((item, index) =>
          item.separator ? (
            <div key={`sep-${index}`} className="my-1 h-px bg-hairline" />
          ) : (
            <button
              key={item.id}
              type="button"
              className="block w-full px-3 py-1.5 text-left text-[13px] hover:bg-brand/10"
              onClick={() => {
                session.setStatus(`menu ${item.label}`);
                session.closeSurface();
              }}
            >
              {item.label}
            </button>
          )
        )}
      </div>
    </>
  );
}

export function InteractiveDesktop({ session }: { session: DesktopController }) {
  return (
    <div className="relative h-screen w-screen overflow-hidden bg-[#B8C4D4]">
      <div className="pointer-events-none absolute inset-x-0 top-7 h-80 bg-gradient-to-b from-white/30 to-transparent" />
      <InteractiveMenuBar session={session} />
      <div className="absolute left-6 top-16 flex gap-6">
        {session.icons.map((icon) => (
          <button
            key={icon.id}
            type="button"
            className="flex w-24 flex-col items-center gap-2 text-center"
            onClick={() => session.launch(`systemui.${icon.id}`)}
          >
            <span className="h-11 w-11 rounded-xl border border-white/40 bg-white/80 shadow-sm" />
            <span className="text-[11px] font-medium text-ink drop-shadow-sm">{icon.name}</span>
          </button>
        ))}
      </div>
      <div className="absolute inset-0">
        {session.painted.map((window) => (
          <InteractiveWindow
            key={window.id}
            window={window}
            focused={window.id === session.focusedId}
            session={session}
          />
        ))}
      </div>
      <InteractiveDock session={session} />
      <InteractiveMenu session={session} />
    </div>
  );
}
