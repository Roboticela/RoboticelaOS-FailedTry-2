/**
 * GenerativeSource — ambient gradient-drift live scene.
 *
 * Uses requestAnimationFrame + a time-offset trick so that resume()
 * continues from the exact visual state where pause() halted —
 * the sine/noise functions use (elapsedMs) not wall-clock time,
 * meaning the scene never "jumps" after a resume.
 *
 * The scene is rendered to a <canvas> so it works for getPosterFrame().
 */

import { BackgroundSource, type FitMode } from "../BackgroundSource";

export interface GenerativeConfig {
  /** Primary hue (0-360). Defaults to a cool indigo. */
  hue1?: number;
  /** Secondary hue. Defaults to a warm violet. */
  hue2?: number;
  /** Tertiary hue for the third orb. Defaults to cyan. */
  hue3?: number;
  /** Speed multiplier. 1.0 = default. */
  speed?: number;
}

export class GenerativeSource extends BackgroundSource {
  private readonly config: Required<GenerativeConfig>;
  private canvas: HTMLCanvasElement | null = null;
  private ctx: CanvasRenderingContext2D | null = null;
  private rafId: number | null = null;
  private elapsedMs = 0;
  private lastTimestamp: number | null = null;
  private _paused = false;
  private resizeObserver: ResizeObserver | null = null;

  constructor(config: GenerativeConfig = {}) {
    super();
    this.config = {
      hue1: config.hue1 ?? 235,
      hue2: config.hue2 ?? 280,
      hue3: config.hue3 ?? 185,
      speed: config.speed ?? 1.0,
    };
  }

  async mount(container: HTMLElement): Promise<void> {
    const canvas = document.createElement("canvas");
    canvas.className = "bg-source bg-source--generative";
    container.appendChild(canvas);
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");

    this.syncSize(container);
    this.resizeObserver = new ResizeObserver(() => this.syncSize(container));
    this.resizeObserver.observe(container);

    if (!this._paused) {
      this.startLoop();
    } else {
      this.drawFrame();
    }
  }

  unmount(): void {
    this.stopLoop();
    this.resizeObserver?.disconnect();
    this.canvas?.remove();
    this.canvas = null;
    this.ctx = null;
  }

  pause(): void {
    this._paused = true;
    this.stopLoop();
    // lastTimestamp is nulled so the next resume() doesn't count the gap
    this.lastTimestamp = null;
  }

  resume(): void {
    this._paused = false;
    this.startLoop();
  }

  setFitMode(_mode: FitMode): void {
    // Canvas always fills the container; fit mode is irrelevant for generative.
  }

  async getPosterFrame(): Promise<string> {
    this.drawFrame();
    return this.canvas?.toDataURL("image/webp", 0.85) ?? "";
  }

  isAnimated(): boolean {
    return true;
  }

  // ── Private ────────────────────────────────────────────────────────────────

  private syncSize(container: HTMLElement): void {
    if (!this.canvas) return;
    this.canvas.width = container.clientWidth || 1280;
    this.canvas.height = container.clientHeight || 800;
    this.drawFrame();
  }

  private startLoop(): void {
    const tick = (timestamp: number) => {
      if (this._paused) return;

      if (this.lastTimestamp !== null) {
        this.elapsedMs += (timestamp - this.lastTimestamp) * this.config.speed;
      }
      this.lastTimestamp = timestamp;

      this.drawFrame();
      this.rafId = requestAnimationFrame(tick);
    };
    this.rafId = requestAnimationFrame(tick);
  }

  private stopLoop(): void {
    if (this.rafId !== null) {
      cancelAnimationFrame(this.rafId);
      this.rafId = null;
    }
  }

  private drawFrame(): void {
    const canvas = this.canvas;
    const ctx = this.ctx;
    if (!canvas || !ctx) return;

    const { width: W, height: H } = canvas;
    const t = this.elapsedMs / 1000; // seconds
    const { hue1, hue2, hue3 } = this.config;

    // Animated orb positions — smooth sine/cosine drifts
    const ox1 = 0.5 + 0.35 * Math.sin(t * 0.17 + 0.0);
    const oy1 = 0.5 + 0.30 * Math.cos(t * 0.13 + 1.0);
    const ox2 = 0.5 + 0.38 * Math.cos(t * 0.11 + 2.1);
    const oy2 = 0.5 + 0.32 * Math.sin(t * 0.19 + 0.7);
    const ox3 = 0.5 + 0.28 * Math.sin(t * 0.23 + 3.5);
    const oy3 = 0.5 + 0.36 * Math.cos(t * 0.09 + 1.8);

    // Black background base
    ctx.fillStyle = `hsl(${hue1}, 30%, 6%)`;
    ctx.fillRect(0, 0, W, H);

    const drawOrb = (ox: number, oy: number, hue: number, radiusRatio: number, alpha: number) => {
      const x = ox * W;
      const y = oy * H;
      const r = Math.max(W, H) * radiusRatio;
      const grad = ctx.createRadialGradient(x, y, 0, x, y, r);
      grad.addColorStop(0, `hsla(${hue}, 90%, 65%, ${alpha})`);
      grad.addColorStop(0.4, `hsla(${hue}, 80%, 50%, ${alpha * 0.4})`);
      grad.addColorStop(1, `hsla(${hue}, 70%, 30%, 0)`);
      ctx.fillStyle = grad;
      ctx.fillRect(0, 0, W, H);
    };

    // Compose three drifting colour orbs
    drawOrb(ox1, oy1, hue1, 0.75, 0.55);
    drawOrb(ox2, oy2, hue2, 0.65, 0.45);
    drawOrb(ox3, oy3, hue3, 0.55, 0.35);

    // Subtle dark vignette
    const vig = ctx.createRadialGradient(W / 2, H / 2, 0, W / 2, H / 2, Math.max(W, H) * 0.75);
    vig.addColorStop(0, "rgba(0,0,0,0)");
    vig.addColorStop(1, "rgba(0,0,0,0.45)");
    ctx.fillStyle = vig;
    ctx.fillRect(0, 0, W, H);

    // Grain overlay — low-frequency noise grid, cheap and effective
    const grainAlpha = 0.025 + 0.015 * Math.sin(t * 7.3);
    ctx.fillStyle = `rgba(255,255,255,${grainAlpha})`;
    const cellSize = 3;
    for (let gy = 0; gy < H; gy += cellSize * 4) {
      for (let gx = 0; gx < W; gx += cellSize * 4) {
        if (Math.random() < 0.12) {
          ctx.fillRect(
            gx + Math.random() * cellSize,
            gy + Math.random() * cellSize,
            cellSize,
            cellSize
          );
        }
      }
    }
  }
}
