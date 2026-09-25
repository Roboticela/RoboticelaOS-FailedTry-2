/*
 * The Desktop build's second half: render the shell, validate the result, and write the artifact the
 * runtime loads.
 *
 * ## What the runtime loads
 *
 * ## One quoting style, and why it is single
 *
 * React's static output quotes every attribute with `"`, and the document this build emits quotes
 * every attribute with `'`. The conversion is not cosmetic: the C side reads a class out of the markup
 * by looking for `class='`, which is how the scene suite's drift check and the shell's own element
 * scan agree on what a class *is*. A page that arrived with `class="…"` would lay out correctly and
 * count zero classes — which is exactly the failure this build hit the first time it emitted one, and
 * why the check below is now part of the validation rather than a comment.
 *
 * Three files and one C header:
 *
 *   `Build/Desktop/Ui/desktop.html`   the document the desktop window's content is
 *   `Build/Desktop/Ui/desktop.css`    the generated sheet (tokens → concrete values, one appearance)
 *   `Build/Desktop/Ui/desktop.js`     the page script `RoboDesktopScript` evaluates after attach
 *   `Runtime/Source/RoboDesktop/Generated/DesktopPage.h`   markup, sheet and script as C string literals
 *
 * The header is what the guest actually consumes: `RoboDesktopScene` builds its document from
 * `kDesktopPageMarkup`/`kDesktopPageStyle` and attaches `kDesktopPageScript` through
 * `DesktopScriptCreate`, so the page is compiled into the OS image rather than read
 * from a filesystem that the guest does not have yet. The `.html`/`.css`/`.js` trio beside it is the
 * same document for a reviewer and for the host preview, which is why they are written from one render.
 *
 * `Desktop/dist/` is deliberately **not** used, and this is the one place this project deviates from
 * the phase's example layout: the repository's rule is that `Build/` is generated and `Documentation/`
 * explains it, and a second generated tree under `Desktop/` would be a build output inside a source
 * folder. The build is still `npm run build` from `Desktop/`; only the output path differs.
 *
 * ## Why it validates, and not only writes
 *
 * Every check below is a failure this build can *see* and a reviewer cannot: a class the sheet has no
 * rule for lays out as an unstyled block and still looks like a page (that is the scene suite's drift
 * check, run here first so the build fails before the test does); an inline `style=` attribute is
 * dropped by the cascade, because this build's document bridge carries no inline origin; a
 * `border-radius` or a `transition` is a declaration the engine does not read. A build that emitted any
 * of them would produce an artifact that is silently less than it looks, which is exactly the class of
 * defect the phase's "validate the output" clause is for.
 */

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { createRequire } from "node:module";
import { buildSync } from "esbuild";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const repo = join(root, "..");
const outDir = join(repo, "Build", "Desktop", "Ui");
const generatedDir = join(repo, "Runtime", "Source", "RoboDesktop", "Generated");
const headerPath = join(generatedDir, "DesktopPage.h");
const bundlePath = join(outDir, "ssr.cjs");

/** Properties this build's CSS and layout do not read: a declaration of one is a silent no-op. */
const unsupportedProperties = [
  "border-radius",
  "box-shadow",
  "transition",
  "animation",
  "transform",
  "flex",
  "grid",
  "gap",
  "z-index",
  "overflow-x",
  "overflow-y",
];

mkdirSync(outDir, { recursive: true });
mkdirSync(generatedDir, { recursive: true });

buildSync({
  absWorkingDir: root,
  entryPoints: [join(root, "scripts", "ssr-entry.tsx")],
  bundle: true,
  platform: "node",
  format: "cjs",
  outfile: bundlePath,
  jsx: "automatic",
  loader: { ".ts": "ts", ".tsx": "tsx" },
  alias: {
    "@": join(root, "src"),
  },
});

const require = createRequire(pathToFileURL(bundlePath).href);
const { renderDesktopPage } = require(bundlePath);

const page = renderDesktopPage();
const markup = page.markup.replace(/\s+/g, " ").trim().replace(/"/g, "'");
const style = page.style.replace(/\s+/g, " ").trim();

/* The classes the markup names, in order of first appearance, read from the *normalized* markup so
 * there is one representation of it rather than two. */
const classes = [...new Set([...markup.matchAll(/class='([^']+)'/g)].map((match) => match[1]))];

const problems = [];

/* 0 — the one quoting style the C side reads. */
if (!/class='/.test(markup)) {
  problems.push("the markup does not use single-quoted class attributes, which is the form the document scan reads");
}

/* 1 — every class the markup names has a rule. The same walk the scene suite does, at build time. */
const missing = classes.filter((name) => !style.includes(`.${name}`));
if (missing.length) {
  problems.push(`the sheet styles no rule for: ${missing.join(", ")}`);
}

/* 2 — no class attribute names two classes. The drift check reads the whole attribute as one name, so
 *     `class="a b"` would be looked up as `.a b` and never found; the components nest instead. */
const multi = classes.filter((name) => name.includes(" "));
if (multi.length) {
  problems.push(`class attributes naming more than one class: ${multi.join(" | ")}`);
}

/* 3 — the runtime is a static document: no inline style (dropped), no handler, no script. */
if (/style="/.test(markup)) {
  problems.push("the markup carries an inline style attribute, which this build's cascade does not read");
}
if (/on[a-z]+=/.test(markup)) {
  problems.push("the markup carries an event handler attribute; listeners belong in kDesktopPageScript");
}
if (/<script/.test(markup)) {
  problems.push("the markup carries a script element");
}
if (/node_modules|\/src\//.test(markup)) {
  problems.push("the markup references build-only paths");
}

/* 4 — the sheet writes no property the engine silently drops. */
for (const property of unsupportedProperties) {
  if (new RegExp(`(^|[;{\\s])${property}\\s*:`, "m").test(style)) {
    problems.push(`the sheet declares ${property}, which this build does not read`);
  }
}

if (problems.length) {
  console.error("desktop build validation failed:");
  for (const problem of problems) {
    console.error(`  - ${problem}`);
  }
  process.exit(1);
}

/** A C string literal, in chunks: MSVC refuses a single literal over 65535 bytes, and the sheet will
 *  get there. The split avoids a boundary inside an escape, which is the one way chunking corrupts. */
function cLiteral(text, chunk = 4096) {
  const escaped = text
    .replace(/\\/g, "\\\\")
    .replace(/"/g, '\\"')
    .replace(/\r\n/g, "\\n")
    .replace(/\n/g, "\\n")
    .replace(/\r/g, "\\n");
  const parts = [];
  let at = 0;

  while (at < escaped.length) {
    let end = Math.min(at + chunk, escaped.length);
    while (end > at && escaped[end - 1] === "\\") {
      end--;
    }
    if (end === at) {
      end = Math.min(at + chunk, escaped.length);
    }
    parts.push(`    "${escaped.slice(at, end)}"`);
    at = end;
  }

  return parts.length ? parts.join("\n") : '    ""';
}

const htmlOut = join(outDir, "desktop.html");
const cssOut = join(outDir, "desktop.css");
const jsOut = join(outDir, "desktop.js");
const metaOut = join(outDir, "build.json");

/*
 * The page script RoboJS evaluates after `JsDomHtmlAttach`. Kept ES5-shaped (`var`, `function`) to match
 * what the interpreter accepts today. Attribute writes are the mutation RoboHTML allows on a finalized
 * document; construction (`createElement`/`appendChild`) stays gated.
 */
const pageScript = [
  "(function () {",
  "  var root = document.getElementById(\"desktopRoot\");",
  "  if (!root) {",
  "    return;",
  "  }",
  "  root.setAttribute(\"data-js-ready\", \"1\");",
  "  root.addEventListener(\"click\", function (ev) {",
  "    var node = ev.target;",
  "    var hops = 0;",
  "    while (node && hops < 16) {",
  "      var command = node.getAttribute ? node.getAttribute(\"data-command\") : \"\";",
  "      if (command) {",
  "        root.setAttribute(\"data-last-command\", command);",
  "        break;",
  "      }",
  "      var icon = node.getAttribute ? node.getAttribute(\"data-icon\") : \"\";",
  "      if (icon) {",
  "        root.setAttribute(\"data-last-icon\", icon);",
  "        break;",
  "      }",
  "      node = node.parentNode;",
  "      hops = hops + 1;",
  "    }",
  "  });",
  "})();",
  "",
].join("\n");

writeFileSync(htmlOut, `<html><body>${markup}</body></html>\n`, "utf8");
writeFileSync(cssOut, `${style}\n`, "utf8");
writeFileSync(jsOut, pageScript, "utf8");

const header = `/* Generated by Desktop/scripts/export-page.mjs -- do not edit by hand. */
/*
 * The desktop document's three halves, compiled into the image.
 *
 * ${markup.length} bytes of markup, ${style.length} bytes of sheet and
 * ${pageScript.length} bytes of page script, rendered from \`Desktop/src\` at
 * build time: the markup is React's static output, the sheet is generated from
 * the design tokens for one appearance (${page.appearance}), and the script is
 * what \`DesktopScriptCreate\` evaluates after \`JsDomHtmlAttach\`.
 * ${page.windows} window${page.windows === 1 ? "" : "s"} and ${classes.length}
 * classes, every one of which the sheet styles -- the export script fails the
 * build otherwise.
 *
 * Each literal is split into chunks because a single C string literal
 * over 64 KiB is a compiler error rather than a long line.
 */
#ifndef ROBO_DESKTOP_PAGE_H
#define ROBO_DESKTOP_PAGE_H

static const char kDesktopPageMarkup[] =
${cLiteral(markup)};

static const char kDesktopPageStyle[] =
${cLiteral(style)};

static const char kDesktopPageScript[] =
${cLiteral(pageScript)};

#endif /* ROBO_DESKTOP_PAGE_H */
`;

writeFileSync(headerPath, header, "utf8");

const digest = (text) => createHash("sha256").update(text).digest("hex").slice(0, 16);
writeFileSync(
  metaOut,
  `${JSON.stringify(
    {
      artifact: "roboticela-desktop",
      appearance: page.appearance,
      windows: page.windows,
      classes: classes.length,
      markupBytes: markup.length,
      styleBytes: style.length,
      scriptBytes: pageScript.length,
      markupHash: digest(markup),
      styleHash: digest(style),
      scriptHash: digest(pageScript),
    },
    null,
    2
  )}\n`,
  "utf8"
);

console.log(
  `desktop page: ${page.appearance}, ${page.windows} window(s), ${classes.length} classes, ` +
    `${markup.length} + ${style.length} + ${pageScript.length} bytes`
);
console.log(`wrote ${headerPath}`);
console.log(`wrote ${htmlOut}`);
console.log(`wrote ${cssOut}`);
console.log(`wrote ${jsOut}`);
console.log(`wrote ${metaOut}`);
