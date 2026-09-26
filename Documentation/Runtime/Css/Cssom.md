# RoboCSS and the Object Model (Phase 18.2)

This document is the authority for the second half of CSS that Phase 18 adds:
the **object model**. Every phase from 9.1 to 18.1 turned bytes into structure —
tokens, component values, declarations, selectors, rules, stylesheets — and
*read* that structure; not one of them turned it back into text, and not one of
them changed it. This phase does both.

The module is `Runtime/Source/RoboCSS/RoboCSSOM.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSOM.h`. It has two halves and no third:

```text
the model ──► cssText          CssOMWrite{Value,SelectorList,Declaration,Rule,RuleList,Stylesheet}
the model ◄──► a style declaration   CssStyleDeclaration{Create,ParseText,SetProperty,…}
a live sheet ◄─ rule editing    CssOMStylesheetInsertRule / …DeleteRule
```

## 1. `cssText` — the model as text

The writers answer one question: **what is this model object spelled as**. The
rendering is deliberately *not* the CSSOM specification's byte-exact
serialization. There is no number canonicalisation (`+01.0px` comes back as
`+01.0px`) and no colour normalisation (`#FF0000` is not rewritten to `red`),
because both would need a semantics this layer does not have and a second
implementer would disagree with. What the rendering *does* promise is that it is
**re-parseable**: feeding the output back through the parser yields the same
model. `css.cssom.round_trip` is that promise as a test — parse, write, parse
the written text, write again, and compare the two renderings byte for byte.

The rules, by node:

| Node | Rendering |
|---|---|
| TOKEN | its raw bytes — `10px`, `red`, `"a b"` (quotes and all) |
| FUNCTION | its name, a `(`, its children, a `)` |
| BLOCK | its opening bracket, its children, its closing bracket |
| LIST | its children with a single space where whitespace separated them, one space after a comma, and nothing where the author wrote nothing |
| selector list | complex selectors joined by `, `, compounds joined by the combinator's spelling (a descendant is a space) |
| simple selector | `.name`, `#name`, `[a="b"]`, `:pseudo(args)`, `::element`, `*`, `type` |
| declaration | `name: value;`, ` !important` before the `;` when the flag is set |
| style rule | `selectors { declarations }`, or `selectors { }` when the block is empty |
| at-rule | `@name prelude`, then `;` or `{ … }` |
| stylesheet | its rules joined by a newline |

Two details are load-bearing rather than tidy.

**Separators are preserved, not invented.** A `LIST` emits a space exactly where
whitespace separated two components (and one after a comma), so `calc(1px+2px)`
stays `calc(1px+2px)` and does not become a different expression, while
`rgb(1,2,3)` renders with CSS's own comma spacing as `rgb(1, 2, 3)`.

**A name that could be misread is escaped.** A selector name the parser resolved
(`.\66 oo` is the class `foo`) may hold a `:` or a space, and writing it back raw
would produce a *different* selector. `CssOMWriteSelectorList` escapes such a
byte as a hex escape, so `.\3a` style names round-trip.

**A style rule written straight from a parse renders its prelude.** The
stylesheet grammar keeps a style rule's prelude as a component-value tree and
leaves the selector list empty until 9.4 fills it. The writer prefers the parsed
selector list and falls back to the prelude's values, so a rule is printable the
moment it is parsed rather than only after a second step — and the two render the
same text.

### The buffer contract

Every writer is *measure first, then fit*: pass a `NULL` buffer to learn the
length, then a buffer of that length plus one. A buffer that is too small is
`RoboNotSupported`, `outLength` names the size a second attempt needs, and **the
buffer is left untouched** — the two-pass structure exists precisely so that a
truncated rendering is never left behind for a caller that cannot tell it from a
complete one.

## 2. `CssStyleDeclaration` — the mutable one

`CssStyleDeclaration` is CSSOM's `element.style`: a self-contained, mutable set of
properties that can be parsed, read, edited, removed and rendered.

It is a **second declaration container** rather than a mutable view of
`CssDeclarationList`, and the reason is ownership. A parsed declaration borrows
its name and its value's bytes from the source it was parsed out of — that is
what makes parsing cheap. `setProperty` has to work after that source is gone, so
each property here **copies its name's bytes, owns its value's bytes** (a private
one-property `CssSource`), and re-parses the value over them. The object is then
self-contained: nothing a caller passed in is still needed after the call
returns, and `CssStyleDeclarationParseText` says so explicitly — `text` is
borrowed for the duration of the call only.

The rules it implements, each of which was a place this could go wrong:

| Case | The answer |
|---|---|
| a property set twice | keeps its first **position**, takes the new value — a replace, not an append |
| `setProperty(name, "")` | a removal (CSSOM's rule); removing an absent property is not an error |
| a name compared | ASCII case-insensitively — CSS property names do — and stored as written |
| a custom property (`--brand`) | a property like any other: stored, read, rendered |
| a value that parses to nothing (`"   "`, `color:`) | ignored — not a property with an empty value |
| a priority other than `""`/`important` | refused rather than stored |
| `getPropertyValue` on `var(--x)` | `var(--x)` — a CSSOM value is the **specified** one; substitution is 18.1's and happens at computed-value time |

An INVALID declaration in a parsed list is skipped: a style declaration holds
properties, and a malformed record is not one.

## 3. Rule editing

`CssOMStylesheetInsertRule` and `CssOMStylesheetDeleteRule` are CSSOM's
`insertRule`/`deleteRule`. The sheet is a live object whose top-level rule list
can be spliced.

`insertRule` parses its text as a stylesheet and **requires exactly one rule** — a
stylesheet body, or a second rule, is refused rather than half-inserted, because
an insertion that consumed only part of its text would make the sheet's rules
disagree with the sheet's source. An index past the end appends, which is what
makes `insertRule(text, cssRules.length)` — the idiom every caller writes — a
single call.

**The rule text is borrowed and must outlive the rule.** A rule's slices point
into the bytes it was parsed from, exactly as they do for a parsed stylesheet,
and a stylesheet does not take ownership of a caller's buffer
(`CssStylesheetSetSourceMemory` states the same contract). A caller that wants
the insertion to own its text copies it first.

What it deliberately does not do: the stylesheet is not re-parsed, no style
context is notified, and a rule a cascade had already collected is not
re-collected. A caller holding a live context must treat an edit as a document
change.

## 4. The boundary this phase names

The CSSOM in a browser is more than this, and the rest is named rather than
implied:

- **No document binding.** There is no `document.styleSheets`, no `ownerNode`, no
  `getComputedStyle`. Those need a DOM, and the document layer is a peer of
  RoboCSS (`DEPENDENCIES.md`), not a part of it.
- **No evaluated at-rule objects.** An at-rule is a name, a prelude and a block;
  there is no `CSSMediaRule.media` with a parsed media-query list, no
  `CSSImportRule.styleSheet`.
- **No parent chain.** `CSSStyleRule.parentStyleSheet` does not exist: a rule is
  a value inside a sheet, and a sheet is addressed by id.
- **No live style context.** Editing a sheet does not invalidate or re-collect a
  resolution; the phase that owns invalidation will.
- **No live `classList`/`style` splice back to a sheet.** RoboCSS is handed text
  and a model comes back; turning a `style` attribute into a declaration list is
  9.1's inline boundary, and this phase's declaration is the self-contained one
  a caller edits, not an attribute store.

## 5. Capabilities and version

Phase 18.2 adds one flag to `CssCapabilities` — `cssom` — and one diagnostic
domain (`RoboCssDomainCssom`), and takes the module to **0.20.0**. The flag is
asserted field by field in `SuiteRoboCssConformance.c` and read again in
`SuiteRoboCssCssom.c`.

## 6. Tests

`Tests/Css/SuiteRoboCssCssom.c` (`css.cssom.*`, 14 cases) is the contract:

- the capability flag, and the flags and boundaries beside it;
- `CssOMWriteValue` over built values — a dimension, a multi-part list, a
  function with commas, a `calc()` — and the buffer discipline (measure, refuse
  one byte short and leave the buffer untouched, then fit);
- `CssOMWriteSelectorList` across combinators, lists, attribute selectors,
  `:not()`, and a name that must be escaped to round-trip;
- rule and whole-stylesheet text, a stale handle answering `RoboNotFound`, and
  the round trip (parse → write → parse → write, byte for byte equal);
- the declaration: parse, read name/value/priority, `cssText`, its own buffer
  discipline, set, replace-in-place, custom property, refused name and priority,
  a value that parses to nothing, removal returning the old value, and
  `var()` answered as the specified value;
- `insertRule` (front, append, reject-not-one-rule) and `deleteRule`;
- determinism: two parses of one source render identically.

```
pwsh -NoProfile -File Scripts/Test/TestCss.ps1 -Target Host
```

An object-model change is not done until `TestCss.ps1 -Target Host` passes and
then `TestLayout`, `TestPaint`, `TestHtml` and `TestDesktop` (`-Target Host`)
pass, and `Build.ps1 -Module Css -Verify` reports the module's
Chromium-independence scan `OK`.
