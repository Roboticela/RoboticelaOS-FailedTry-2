/*
 * Settings — Phase 14.40
 *
 * macOS System Settings clone: sidebar navigation + content panel.
 * Covers: Appearance (light/dark/auto), Reduced Motion, Display Scale,
 * Accessibility, General, Notifications, Sound, Network, Privacy.
 *
 * The appearance and reducedMotion flags come from DesktopState and flow back
 * via the bridge in production; the lab exercises the UI directly.
 */

import { useState } from "react";
import type { DesktopController } from "@/session/useDesktopSession";

type AppearanceValue = "light" | "dark" | "auto";

/* ── Section registry ──────────────────────────────────────────────────────── */

interface SettingsSection {
  id: string;
  icon: string;
  label: string;
  group?: string;
}

const SECTIONS: SettingsSection[] = [
  { id: "general", icon: "⚙️", label: "General", group: "SYSTEM" },
  { id: "appearance", icon: "🎨", label: "Appearance", group: "SYSTEM" },
  { id: "accessibility", icon: "♿", label: "Accessibility", group: "SYSTEM" },
  { id: "notifications", icon: "🔔", label: "Notifications", group: "SYSTEM" },
  { id: "sound", icon: "🔊", label: "Sound", group: "HARDWARE" },
  { id: "display", icon: "🖥️", label: "Displays", group: "HARDWARE" },
  { id: "battery", icon: "🔋", label: "Battery", group: "HARDWARE" },
  { id: "network", icon: "📶", label: "Network", group: "CONNECTIVITY" },
  { id: "bluetooth", icon: "🔵", label: "Bluetooth", group: "CONNECTIVITY" },
  { id: "privacy", icon: "🔒", label: "Privacy & Security", group: "SYSTEM" },
  { id: "users", icon: "👤", label: "Users & Groups", group: "SYSTEM" },
];

interface SettingsState {
  appearance: AppearanceValue;
  reducedMotion: boolean;
  reducedTransparency: boolean;
  increaseContrast: boolean;
  buttonShapes: boolean;
  displayScale: number;
  notificationsEnabled: boolean;
  soundVolume: number;
  voiceOverEnabled: boolean;
}

/* ── Root component ─────────────────────────────────────────────────────────── */

interface SettingsProps {
  session: DesktopController;
  onClose?: () => void;
}

export function Settings({ session, onClose: _onClose }: SettingsProps) {
  const [activeSection, setActiveSection] = useState("appearance");
  const [search, setSearch] = useState("");
  const [settings, setSettings] = useState<SettingsState>({
    appearance: session.state.appearance,
    reducedMotion: false,
    reducedTransparency: false,
    increaseContrast: false,
    buttonShapes: false,
    displayScale: 1,
    notificationsEnabled: true,
    soundVolume: 60,
    voiceOverEnabled: false,
  });

  const update = <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => {
    setSettings((prev) => ({ ...prev, [key]: value }));
    session.setStatus(`settings.${key}: ${value}`);
  };

  const groups = groupSections(SECTIONS, search);

  return (
    <div className="flex h-full bg-[#F2F2F7]">
      {/* Sidebar */}
      <div
        className="flex h-full w-64 flex-shrink-0 flex-col border-r border-black/[0.08]"
        style={{
          background: "rgba(246,246,246,0.95)",
          backdropFilter: "blur(20px)",
        }}
      >
        {/* Search */}
        <div className="p-3">
          <div className="flex items-center gap-2 rounded-lg bg-black/[0.07] px-2.5 py-1.5">
            <span className="text-[13px] text-[#8E8E93]">🔍</span>
            <input
              type="text"
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              placeholder="Search"
              className="flex-1 bg-transparent text-[13px] text-[#1D1D1F] placeholder-[#8E8E93] outline-none"
            />
          </div>
        </div>

        {/* Nav groups */}
        <div className="flex-1 overflow-y-auto px-2 pb-4">
          {groups.map(({ group, items }) => (
            <div key={group}>
              {group && (
                <p className="mb-1 mt-3 px-2 text-[10px] font-semibold uppercase tracking-wider text-[#8E8E93]">
                  {group}
                </p>
              )}
              {items.map((section) => (
                <button
                  key={section.id}
                  type="button"
                  onClick={() => setActiveSection(section.id)}
                  className={[
                    "flex w-full items-center gap-2.5 rounded-lg px-2.5 py-2 text-left transition-colors",
                    activeSection === section.id
                      ? "bg-[#0E7AFF] text-white"
                      : "text-[#1D1D1F] hover:bg-black/[0.07]",
                  ].join(" ")}
                >
                  <span className="text-[18px] leading-none">{section.icon}</span>
                  <span className="text-[13px] font-medium">{section.label}</span>
                </button>
              ))}
            </div>
          ))}
        </div>
      </div>

      {/* Content */}
      <div className="flex-1 overflow-y-auto">
        <div className="max-w-2xl p-8">
          {activeSection === "appearance" && (
            <AppearancePanel settings={settings} update={update} />
          )}
          {activeSection === "accessibility" && (
            <AccessibilityPanel settings={settings} update={update} />
          )}
          {activeSection === "notifications" && (
            <NotificationsPanel settings={settings} update={update} />
          )}
          {activeSection === "sound" && (
            <SoundPanel settings={settings} update={update} />
          )}
          {activeSection === "display" && (
            <DisplayPanel settings={settings} update={update} />
          )}
          {activeSection === "general" && (
            <GeneralPanel />
          )}
          {["network", "bluetooth", "privacy", "users", "battery"].includes(activeSection) && (
            <PlaceholderPanel
              section={SECTIONS.find((s) => s.id === activeSection)!}
            />
          )}
        </div>
      </div>
    </div>
  );
}

/* ── Appearance panel ───────────────────────────────────────────────────────── */

function AppearancePanel({
  settings,
  update,
}: {
  settings: SettingsState;
  update: <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => void;
}) {
  return (
    <div className="space-y-6">
      <h1 className="text-[22px] font-semibold text-[#1D1D1F]">Appearance</h1>

      {/* Mode selector */}
      <SettingsCard title="Appearance">
        <div className="flex gap-3">
          {(["light", "dark", "auto"] as const).map((mode) => (
            <button
              key={mode}
              type="button"
              onClick={() => update("appearance", mode)}
              className={[
                "flex flex-col items-center gap-2 rounded-xl border-2 p-3 transition-all",
                settings.appearance === mode
                  ? "border-[#0E7AFF] bg-[#0E7AFF]/5"
                  : "border-transparent bg-black/5 hover:bg-black/8",
              ].join(" ")}
              style={{ minWidth: 80 }}
            >
              <div
                className={[
                  "h-10 w-10 rounded-full",
                  mode === "light"
                    ? "bg-white border border-black/10"
                    : mode === "dark"
                    ? "bg-[#1C1C1E] border border-white/20"
                    : "bg-gradient-to-br from-white to-[#1C1C1E]",
                ].join(" ")}
              />
              <span className="text-[12px] font-medium text-[#1D1D1F] capitalize">{mode}</span>
            </button>
          ))}
        </div>
      </SettingsCard>

      {/* Accent color */}
      <SettingsCard title="Accent Colour">
        <div className="flex gap-2">
          {["#0E7AFF", "#FF3B30", "#FF9F0A", "#30D158", "#AF52DE", "#FF2D55", "#636366"].map(
            (color) => (
              <button
                key={color}
                type="button"
                className="h-7 w-7 rounded-full border-2 border-transparent transition hover:scale-110"
                style={{
                  background: color,
                  borderColor: color === "#0E7AFF" ? "rgba(14,122,255,0.5)" : "transparent",
                }}
                onClick={() => update("appearance", settings.appearance)}
              />
            )
          )}
        </div>
      </SettingsCard>

      {/* Highlight */}
      <SettingsCard title="Highlight Colour">
        <p className="text-[13px] text-[#6E6E73]">Blue (default)</p>
      </SettingsCard>
    </div>
  );
}

/* ── Accessibility panel ────────────────────────────────────────────────────── */

function AccessibilityPanel({
  settings,
  update,
}: {
  settings: SettingsState;
  update: <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => void;
}) {
  return (
    <div className="space-y-6">
      <h1 className="text-[22px] font-semibold text-[#1D1D1F]">Accessibility</h1>

      <SettingsCard title="Vision">
        <div className="space-y-3">
          <ToggleRow
            label="VoiceOver"
            sublabel="Enable the screen reader that describes what is on screen"
            value={settings.voiceOverEnabled}
            onChange={(v) => update("voiceOverEnabled", v)}
          />
          <div className="h-px bg-black/[0.06]" />
          <ToggleRow
            label="Reduce Motion"
            sublabel="Limit the motion of the user interface"
            value={settings.reducedMotion}
            onChange={(v) => update("reducedMotion", v)}
          />
          <div className="h-px bg-black/[0.06]" />
          <ToggleRow
            label="Reduce Transparency"
            sublabel="Reduce the transparency and blurs in the user interface"
            value={settings.reducedTransparency}
            onChange={(v) => update("reducedTransparency", v)}
          />
          <div className="h-px bg-black/[0.06]" />
          <ToggleRow
            label="Increase Contrast"
            sublabel="Increase the contrast of controls and other interface elements"
            value={settings.increaseContrast}
            onChange={(v) => update("increaseContrast", v)}
          />
          <div className="h-px bg-black/[0.06]" />
          <ToggleRow
            label="Button Shapes"
            sublabel="Show a shape behind buttons to help identify them"
            value={settings.buttonShapes}
            onChange={(v) => update("buttonShapes", v)}
          />
        </div>
      </SettingsCard>
    </div>
  );
}

/* ── Notifications panel ────────────────────────────────────────────────────── */

function NotificationsPanel({
  settings,
  update,
}: {
  settings: SettingsState;
  update: <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => void;
}) {
  return (
    <div className="space-y-6">
      <h1 className="text-[22px] font-semibold text-[#1D1D1F]">Notifications</h1>
      <SettingsCard title="Notification Centre">
        <ToggleRow
          label="Allow Notifications"
          sublabel="When on, apps can send notifications"
          value={settings.notificationsEnabled}
          onChange={(v) => update("notificationsEnabled", v)}
        />
      </SettingsCard>
      <SettingsCard title="Application Notifications">
        {["Finder", "Settings", "Input Lab", "Messages"].map((app) => (
          <div key={app} className="flex items-center justify-between py-2">
            <span className="text-[13px] font-medium text-[#1D1D1F]">{app}</span>
            <span className="text-[12px] text-[#0E7AFF]">Banners ▸</span>
          </div>
        ))}
      </SettingsCard>
    </div>
  );
}

/* ── Sound panel ────────────────────────────────────────────────────────────── */

function SoundPanel({
  settings,
  update,
}: {
  settings: SettingsState;
  update: <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => void;
}) {
  return (
    <div className="space-y-6">
      <h1 className="text-[22px] font-semibold text-[#1D1D1F]">Sound</h1>
      <SettingsCard title="Output Volume">
        <div className="flex items-center gap-3">
          <span className="text-[14px] text-[#8E8E93]">🔈</span>
          <input
            type="range"
            min={0}
            max={100}
            value={settings.soundVolume}
            onChange={(e) => update("soundVolume", Number(e.target.value))}
            className="flex-1 accent-[#0E7AFF]"
          />
          <span className="text-[14px] text-[#8E8E93]">🔊</span>
          <span className="w-8 text-right text-[13px] text-[#1D1D1F]">{settings.soundVolume}%</span>
        </div>
      </SettingsCard>
    </div>
  );
}

/* ── Display panel ──────────────────────────────────────────────────────────── */

function DisplayPanel({
  settings,
  update,
}: {
  settings: SettingsState;
  update: <K extends keyof SettingsState>(key: K, value: SettingsState[K]) => void;
}) {
  return (
    <div className="space-y-6">
      <h1 className="text-[22px] font-semibold text-[#1D1D1F]">Displays</h1>
      <SettingsCard title="Resolution">
        <div className="flex gap-2">
          {[1, 1.5, 2].map((scale) => (
            <button
              key={scale}
              type="button"
              onClick={() => update("displayScale", scale)}
              className={[
                "rounded-lg px-4 py-2 text-[13px] font-medium transition-colors",
                settings.displayScale === scale
                  ? "bg-[#0E7AFF] text-white"
                  : "bg-black/8 text-[#1D1D1F] hover:bg-black/12",
              ].join(" ")}
            >
              {scale === 1 ? "Default" : `${scale}×`}
            </button>
          ))}
        </div>
      </SettingsCard>
    </div>
  );
}

/* ── General panel ──────────────────────────────────────────────────────────── */

function GeneralPanel() {
  return (
    <div className="space-y-6">
      <h1 className="text-[22px] font-semibold text-[#1D1D1F]">General</h1>
      <SettingsCard title="About This Mac">
        <div className="space-y-1 text-[13px]">
          <InfoRow label="System" value="RoboticelaOS 1.0" />
          <InfoRow label="Kernel" value="RoboKernel 0.1" />
          <InfoRow label="Runtime" value="Phase 15.19 (Compositor landed)" />
          <InfoRow label="Memory" value="4 GB" />
          <InfoRow label="Architecture" value="x86-64" />
        </div>
      </SettingsCard>
    </div>
  );
}

function InfoRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-start gap-2">
      <span className="w-28 flex-shrink-0 text-[#6E6E73]">{label}</span>
      <span className="text-[#1D1D1F]">{value}</span>
    </div>
  );
}

/* ── Placeholder ────────────────────────────────────────────────────────────── */

function PlaceholderPanel({ section }: { section: SettingsSection }) {
  return (
    <div className="flex flex-col items-center justify-center py-20 text-center">
      <span className="mb-3 text-5xl">{section.icon}</span>
      <h2 className="text-[18px] font-semibold text-[#1D1D1F]">{section.label}</h2>
      <p className="mt-2 text-[14px] text-[#6E6E73]">This panel is reserved for {section.label.toLowerCase()} settings.</p>
    </div>
  );
}

/* ── Shared UI atoms ────────────────────────────────────────────────────────── */

function SettingsCard({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="overflow-hidden rounded-xl bg-white shadow-sm border border-black/[0.05]">
      <div className="border-b border-black/[0.06] bg-[#F9F9F9] px-4 py-2.5">
        <h3 className="text-[12px] font-semibold uppercase tracking-wide text-[#6E6E73]">
          {title}
        </h3>
      </div>
      <div className="p-4">{children}</div>
    </div>
  );
}

function ToggleRow({
  label,
  sublabel,
  value,
  onChange,
}: {
  label: string;
  sublabel: string;
  value: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <div className="flex items-start gap-4 py-1">
      <div className="flex-1">
        <p className="text-[13px] font-medium text-[#1D1D1F]">{label}</p>
        <p className="text-[12px] text-[#6E6E73]">{sublabel}</p>
      </div>
      <button
        type="button"
        role="switch"
        aria-checked={value}
        onClick={() => onChange(!value)}
        className={[
          "relative mt-0.5 flex-shrink-0 h-[22px] w-[40px] rounded-full transition-colors duration-200",
          value ? "bg-[#30D158]" : "bg-black/25",
        ].join(" ")}
      >
        <span
          className={[
            "absolute top-0.5 h-[18px] w-[18px] rounded-full bg-white shadow-sm transition-transform duration-200",
            value ? "translate-x-[19px]" : "translate-x-0.5",
          ].join(" ")}
        />
      </button>
    </div>
  );
}

/* ── Helpers ────────────────────────────────────────────────────────────────── */

function groupSections(
  sections: SettingsSection[],
  search: string
): { group: string; items: SettingsSection[] }[] {
  const q = search.trim().toLowerCase();
  const filtered = q ? sections.filter((s) => s.label.toLowerCase().includes(q)) : sections;
  const map = new Map<string, SettingsSection[]>();
  for (const section of filtered) {
    const key = section.group ?? "OTHER";
    if (!map.has(key)) map.set(key, []);
    map.get(key)!.push(section);
  }
  return [...map.entries()].map(([group, items]) => ({ group, items }));
}
