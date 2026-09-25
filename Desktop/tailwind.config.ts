import type { Config } from "tailwindcss";

/** Apple-leaning tokens for the interactive Vite preview (not the guest RoboCSS sheet). */
export default {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        brand: {
          DEFAULT: "#0E7AFF",
          hover: "#0A6AE8",
          active: "#0757C7",
        },
        ink: {
          DEFAULT: "#1D1D1F",
          secondary: "#6E6E73",
          tertiary: "#86868B",
        },
        surface: {
          DEFAULT: "#FFFFFF",
          secondary: "#F5F5F7",
          tertiary: "#EDEDEF",
        },
        hairline: "#D2D2D7",
        danger: "#FF3B30",
        warning: "#FF9F0A",
        success: "#30D158",
        traffic: {
          close: "#FF5F57",
          minimize: "#FEBC2E",
          zoom: "#28C840",
        },
      },
      fontFamily: {
        sans: [
          "-apple-system",
          "BlinkMacSystemFont",
          "SF Pro Display",
          "SF Pro Text",
          "Helvetica Neue",
          "Arial",
          "sans-serif",
        ],
      },
      boxShadow: {
        window: "0 12px 40px rgba(0,0,0,0.18), 0 2px 8px rgba(0,0,0,0.08)",
        dock: "0 8px 32px rgba(0,0,0,0.22)",
        menu: "0 8px 28px rgba(0,0,0,0.16)",
      },
      transitionTimingFunction: {
        apple: "cubic-bezier(0.28, 0.11, 0.32, 1)",
      },
    },
  },
  plugins: [],
} satisfies Config;
