import { Link } from "react-router-dom";

const routes = [
  {
    to: "/trackb",
    title: "🆕 Track B Desktop",
    body: "macOS Sonoma clone — workspace switcher, Spotlight, Notification Centre, App Switcher, Settings, Control Centre, context menus, vibrancy, shadows. All of 14.24–14.48.",
    highlight: true,
  },
  {
    to: "/desktop",
    title: "Desktop",
    body: "Full interactive shell — menu bar, dock, windows, drag, focus, close.",
  },
  {
    to: "/window",
    title: "Window",
    body: "Single window chrome, traffic lights, and content surfaces.",
  },
  {
    to: "/controls",
    title: "Controls",
    body: "Input fields, buttons, toggles, slider — every interactive state.",
  },
  {
    to: "/dock",
    title: "Dock",
    body: "Apple-style dock tiles, running dots, launch behaviour.",
  },
  {
    to: "/menubar",
    title: "Menu bar",
    body: "System bar, application menu, live status from input.",
  },
  {
    to: "/input-lab",
    title: "Input Lab",
    body: "Keyboard, mouse, and focus telemetry in one focused window.",
  },
  {
    to: "/combined",
    title: "Combined",
    body: "Everything together — the same desk the guest scene exports.",
  },
  {
    to: "/background",
    title: "Background Engine",
    body: "Universal media adapter — video, GIF, SVG, static image. Fit modes, Low-Power, Space visibility.",
  },
  {
    to: "/folder",
    title: "Folder Icon",
    body: "Two-panel manila folder — 8 tints, system glyphs, content peeks, badges, interaction states.",
  },
];


export function IndexPage() {
  return (
    <div className="lab-shell">
      <div className="mx-auto max-w-3xl px-6 py-16">
        <p className="text-[12px] font-medium uppercase tracking-[0.08em] text-ink-secondary">RoboticelaOS</p>
        <h1 className="mt-3 text-5xl font-semibold tracking-tight text-ink">Desktop Lab</h1>
        <p className="mt-4 max-w-xl text-lg text-ink-secondary">
          Interactive TypeScript preview. Open a surface, click and type here — then rebuild the guest
          image when you want the same chrome in QEMU.
        </p>
        <div className="mt-10 grid gap-3 sm:grid-cols-2">
          {routes.map((route) => (
            <Link
              key={route.to}
              to={route.to}
              className={[
                "lab-link",
                (route as any).highlight
                  ? "ring-2 ring-[#0E7AFF]/40 bg-[#0E7AFF]/5"
                  : "",
              ].join(" ")}
            >
              <div className="text-[17px] font-semibold text-ink">{route.title}</div>
              <div className="mt-1 text-[14px] text-ink-secondary">{route.body}</div>
              <div className="mt-3 text-[13px] font-medium text-brand">{route.to}</div>
            </Link>
          ))}
        </div>
      </div>
    </div>
  );
}
