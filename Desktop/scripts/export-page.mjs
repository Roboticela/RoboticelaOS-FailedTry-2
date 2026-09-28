/*
 * The Desktop build's second half: render the shell, validate the result, and write the artifact the
 * runtime loads.
 *
 * Three files and one C header:
 *
 *   `Build/Desktop/Ui/desktop.html`   Overview document (primary)
 *   `Build/Desktop/Ui/desktop.css`    the generated sheet
 *   `Build/Desktop/Ui/desktop.js`     the page script `RoboDesktopScript` evaluates after attach
 *   `Runtime/Source/RoboDesktop/Generated/DesktopPage.h`   markup(s), sheet and script as C literals
 *
 * Native chrome owns the desk. Exported markup is **content-only** application bodies (Overview +
 * Input Lab). Flex longhands are allowed; silent no-ops stay banned.
 */

import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
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
  "grid",
  "gap",
  "row-gap",
  "column-gap",
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
const normalize = (text) => text.replace(/\s+/g, " ").trim().replace(/"/g, "'");
const overview = normalize(page.overview);
const inputLab = normalize(page.inputLab);
const markup = overview;
const style = page.style.replace(/\s+/g, " ").trim();

const allMarkup = `${overview} ${inputLab}`;
const classes = [...new Set([...allMarkup.matchAll(/class='([^']+)'/g)].map((match) => match[1]))];

const problems = [];

if (!/class='/.test(overview) || !/class='/.test(inputLab)) {
  problems.push("each app markup must use single-quoted class attributes");
}

const missing = classes.filter((name) => !style.includes(`.${name}`));
if (missing.length) {
  problems.push(`the sheet styles no rule for: ${missing.join(", ")}`);
}

const multi = classes.filter((name) => name.includes(" "));
if (multi.length) {
  problems.push(`class attributes naming more than one class: ${multi.join(" | ")}`);
}

for (const doc of [overview, inputLab]) {
  if (/style="/.test(doc) || /style='/.test(doc)) {
    problems.push("markup carries an inline style attribute");
  }
  if (/on[a-z]+=/.test(doc)) {
    problems.push("markup carries an event handler attribute; listeners belong in kDesktopPageScript");
  }
  if (/<script/.test(doc)) {
    problems.push("markup carries a script element");
  }
}

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
 * Page script: ES5-shaped for RoboJS. Attribute/class writes are allowed on a finalized document;
 * construction is gated. Visible updates need DesktopDocumentRefresh after Pump.
 */
const pageScript = [
  "(function () {",
  "  function markReady(root, badgeId) {",
  "    if (!root) { return; }",
  "    root.setAttribute(\"data-js-ready\", \"1\");",
  "    var badge = document.getElementById(badgeId);",
  "    if (badge) { badge.setAttribute(\"class\", \"jsReady\"); }",
  "  }",
  "  function onClick(root, statusId) {",
  "    return function (ev) {",
  "      var node = ev.target;",
  "      var hops = 0;",
  "      while (node && hops < 16) {",
  "        var command = node.getAttribute ? node.getAttribute(\"data-command\") : \"\";",
  "        if (command) {",
  "          root.setAttribute(\"data-last-command\", command);",
  "          var status = document.getElementById(statusId);",
  "          if (status) { status.setAttribute(\"class\", \"appStatusActive\"); }",
  "          if (command === \"lab.primary\") {",
  "            var btn = document.getElementById(\"labPrimary\");",
  "            if (btn) { btn.setAttribute(\"class\", \"buttonActive\"); }",
  "          }",
  "          break;",
  "        }",
  "        node = node.parentNode;",
  "        hops = hops + 1;",
  "      }",
  "    };",
  "  }",
  "  var overview = document.getElementById(\"overviewRoot\");",
  "  if (overview) {",
  "    markReady(overview, \"overviewJsBadge\");",
  "    overview.addEventListener(\"click\", onClick(overview, \"overviewStatus\"));",
  "  }",
  "  var lab = document.getElementById(\"inputLabRoot\");",
  "  if (lab) {",
  "    markReady(lab, \"labJsBadge\");",
  "    lab.addEventListener(\"click\", onClick(lab, \"labStatus\"));",
  "  }",
  "})();",
  "",
].join("\n");

writeFileSync(htmlOut, `<html><body>${overview}</body></html>\n`, "utf8");
writeFileSync(join(outDir, "inputlab.html"), `<html><body>${inputLab}</body></html>\n`, "utf8");
writeFileSync(cssOut, `${style}\n`, "utf8");
writeFileSync(jsOut, pageScript, "utf8");

const header = `/* Generated by Desktop/scripts/export-page.mjs -- do not edit by hand. */
/*
 * Desktop application documents, compiled into the image.
 *
 * Overview ${overview.length} bytes, Input Lab ${inputLab.length} bytes,
 * sheet ${style.length} bytes, page script ${pageScript.length} bytes.
 * Appearance: ${page.appearance}. ${page.windows} native window(s) seeded from the model.
 * ${classes.length} classes, every one styled — the export fails otherwise.
 *
 * Native chrome owns the desk; these markups are content-only app bodies.
 */
#ifndef ROBO_DESKTOP_PAGE_H
#define ROBO_DESKTOP_PAGE_H

/** Primary document (System Overview) — also what DesktopSceneMarkup() reports. */
static const char kDesktopPageMarkup[] =
${cLiteral(markup)};

/** Input Lab application body. */
static const char kDesktopDocInputLabMarkup[] =
${cLiteral(inputLab)};

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
      inputLabBytes: inputLab.length,
      styleBytes: style.length,
      scriptBytes: pageScript.length,
      markupHash: digest(markup),
      inputLabHash: digest(inputLab),
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
    `overview ${overview.length} + lab ${inputLab.length} + style ${style.length} + script ${pageScript.length} bytes`
);
console.log(`wrote ${headerPath}`);
console.log(`wrote ${htmlOut}`);
console.log(`wrote ${cssOut}`);
console.log(`wrote ${jsOut}`);
console.log(`wrote ${metaOut}`);
