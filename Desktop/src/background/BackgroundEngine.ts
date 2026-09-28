/**
 * BackgroundEngine — the singleton per-surface manager.
 *
 * Responsibilities:
 *   • Only ONE BackgroundSource is ever mounted at a time per surface.
 *   • Switching sources: unmount old → mount new (with crossfade at the DOM level).
 *   • Low-power mode: pause all animated sources system-wide.
 *   • Space visibility: pause/resume without unmounting.
 *
 * Usage:
 *   const engine = new BackgroundEngine(containerEl);
 *   await engine.setSource(new GenerativeSource());
 *   engine.setLowPowerMode(true);   // pauses animated sources
 *   engine.setVisible(false);       // pauses (not unmounts) on hide
 */

import { type BackgroundSource, type FitMode } from "./BackgroundSource";

export class BackgroundEngine {
  private container: HTMLElement;
  private current: BackgroundSource | null = null;
  private fitMode: FitMode = "fill";
  private lowPower = false;
  private visible = true;

  constructor(container: HTMLElement) {
    this.container = container;
  }

  /** Mount a new source, unmounting the previous one first. */
  async setSource(next: BackgroundSource): Promise<void> {
    const old = this.current;
    this.current = next;

    // Apply current settings to the incoming source before mounting
    // (so it initialises in the right state, not fill→fit flash)
    next.setFitMode(this.fitMode);

    await next.mount(this.container);

    // If the engine is paused (low-power or invisible), pause immediately
    if ((this.lowPower || !this.visible) && next.isAnimated()) {
      next.pause();
    }

    // Unmount old AFTER new is mounted to avoid a blank frame gap.
    // (Both exist for one paint; then old is removed.)
    if (old) {
      old.pause(); // Stop old before removing
      old.unmount();
    }
  }

  /** Swap to a different source with a crossfade. */
  async transitionTo(next: BackgroundSource, durationMs = 400): Promise<void> {
    const old = this.current;

    // Prepare both layers
    const oldLayer = this.container.querySelector<HTMLElement>(".bg-source");
    next.setFitMode(this.fitMode);
    await next.mount(this.container);

    const newLayer = this.container.querySelector<HTMLElement>(".bg-source:last-child");

    if (oldLayer && newLayer && oldLayer !== newLayer) {
      // Start new layer transparent, fade it in
      newLayer.style.opacity = "0";
      newLayer.style.transition = `opacity ${durationMs}ms ease`;

      // Force reflow
      void newLayer.offsetWidth;
      newLayer.style.opacity = "1";

      await new Promise<void>((resolve) => setTimeout(resolve, durationMs));

      // Old layer fades out
      oldLayer.style.transition = `opacity ${durationMs}ms ease`;
      oldLayer.style.opacity = "0";
      await new Promise<void>((resolve) => setTimeout(resolve, durationMs));
    }

    // Now pause+unmount old
    if (old) {
      old.pause();
      old.unmount();
    }

    this.current = next;

    // Honour current engine state
    if ((this.lowPower || !this.visible) && next.isAnimated()) {
      next.pause();
    }
  }

  setFitMode(mode: FitMode): void {
    this.fitMode = mode;
    this.current?.setFitMode(mode);
  }

  /**
   * Low-Power Mode toggle (wired from Settings).
   * Pauses any isAnimated() source and replaces the live view with
   * the poster frame; resumes when disabled.
   */
  setLowPowerMode(enabled: boolean): void {
    this.lowPower = enabled;
    this.applyPauseState();
  }

  /**
   * Visibility toggle — called by Spaces navigation.
   * Pauses rather than unmounts so resuming a Space feels instant.
   */
  setVisible(isVisible: boolean): void {
    this.visible = isVisible;
    this.applyPauseState();
  }

  async getPosterFrame(): Promise<string> {
    return this.current?.getPosterFrame() ?? "";
  }

  isAnimated(): boolean {
    return this.current?.isAnimated() ?? false;
  }

  unmountAll(): void {
    this.current?.pause();
    this.current?.unmount();
    this.current = null;
  }

  private applyPauseState(): void {
    if (!this.current?.isAnimated()) return;
    if (this.lowPower || !this.visible) {
      this.current.pause();
    } else {
      this.current.resume();
    }
  }
}
