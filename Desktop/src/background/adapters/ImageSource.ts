/**
 * ImageSource — adapter for static images (jpg / png / webp / avif).
 *
 * Rendering path: CSS background-image on a <div>.
 * pause() / resume() are no-ops.  isAnimated() → false.
 */

import { BackgroundSource, type FitMode } from "../BackgroundSource";

const FIT_SIZE: Record<FitMode, string> = {
  fill: "cover",
  fit: "contain",
  stretch: "100% 100%",
  center: "auto",
  tile: "auto",
};

const FIT_REPEAT: Record<FitMode, string> = {
  fill: "no-repeat",
  fit: "no-repeat",
  stretch: "no-repeat",
  center: "no-repeat",
  tile: "repeat",
};

const FIT_POSITION: Record<FitMode, string> = {
  fill: "center",
  fit: "center",
  stretch: "center",
  center: "center",
  tile: "0 0",
};

export class ImageSource extends BackgroundSource {
  private readonly url: string;
  private el: HTMLDivElement | null = null;
  private fitMode: FitMode = "fill";

  constructor(url: string) {
    super();
    this.url = url;
  }

  async mount(container: HTMLElement): Promise<void> {
    const div = document.createElement("div");
    div.className = "bg-source bg-source--image";
    this.applyFit(div, this.fitMode);
    div.style.backgroundImage = `url(${JSON.stringify(this.url)})`;
    container.appendChild(div);
    this.el = div;

    // Preload so the first paint is instant.
    await new Promise<void>((resolve) => {
      const img = new Image();
      img.onload = () => resolve();
      img.onerror = () => resolve(); // still resolve; let CSS show what it can
      img.src = this.url;
    });
  }

  unmount(): void {
    this.el?.remove();
    this.el = null;
  }

  pause(): void { /* no-op for static */ }
  resume(): void { /* no-op for static */ }

  setFitMode(mode: FitMode): void {
    this.fitMode = mode;
    if (this.el) this.applyFit(this.el, mode);
  }

  async getPosterFrame(): Promise<string> {
    return this.url;
  }

  isAnimated(): boolean {
    return false;
  }

  private applyFit(el: HTMLDivElement, mode: FitMode): void {
    el.style.backgroundSize = FIT_SIZE[mode]!;
    el.style.backgroundRepeat = FIT_REPEAT[mode]!;
    el.style.backgroundPosition = FIT_POSITION[mode]!;
    if (mode === "fit") {
      el.style.backgroundColor = "var(--bg-primary, #000)";
    } else {
      el.style.backgroundColor = "";
    }
  }
}
