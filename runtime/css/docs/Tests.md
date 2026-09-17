# RoboCSS Test Organisation (Phase 9.1–9.2)

Fourteen suites, 151 tests, one framework. These phases add no test runner, no
reporter, and no assertion vocabulary: everything here goes through
`runtime/Tests/Framework` (`ROBO_CHECK*` and `robo_test_register`), the same as
every other subsystem.

The split is by **phase, then by token class**. The 9.1 suites pin the model
(does a record own what it says it owns?). The 9.2 suites pin the machine (does a
byte sequence produce the vocabulary 9.1 defined?), one suite per family of token
classes, so a failure names both the class and the rule.

## 1. Suite map

### 9.1 — the model

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCss.c` | 14 | Version, module init/shutdown, slot table bounds, shared types, diagnostics, capabilities, default-stylesheet boundary, the cleared-record contract |
| `SuiteRoboCssSource.c` | 11 | Source kinds, copy ownership, reconfiguration, argument rejection, labels, positions (LF / CRLF / lone CR / scalars), ranges, UTF-8 description, Unicode preservation |
| `SuiteRoboCssToken.c` | 8 | Token vocabulary, classification, bracket pairing, hash types, range binding, EOF, lifetime |
| `SuiteRoboCssValue.c` | 8 | Value kinds, tokens, function/block, nesting, move-in append, argument rejection, ownership transfer, lifetime balance |
| `SuiteRoboCssDeclaration.c` | 11 | Declaration model, value and importance, property resolution, ranges, lists, list lifetime, inline parse boundary |
| `SuiteRoboCssSelector.c` | 8 | Simple, attribute, pseudo-argument, compound, complex and list selectors, lifetime balance |
| `SuiteRoboCssSheet.c` | 12 | Stylesheet lifecycle, origin/state/name, source attachment and ownership, rule storage and nesting, the parse boundary, the parser cursor and its first-error rule |
| `SuiteRoboCssStyle.c` | 8 | Element view, its classes and attributes, the style context, borrowed callbacks, the unsupported match/compute seams, computed-style lifetime |
| `SuiteRoboCssRobust.c` | 8 | Empty, whitespace-only, large, incomplete and malformed input; repeated churn; deep rule nesting; wide structures; deep value trees; Unicode through every model |

### 9.2 — the machine

| Suite | Tests | Subject |
|---|---|---|
| `SuiteRoboCssTokenizer.c` | 13 | Stream API and lifetime: init/dispose/reset, `next`/`peek`/`skip`/`skip_trivia`, peek sharing one slot, EOF as a value and EOF repeats, cursor state, first-error rule, the cleared-record regression, tokenizer-fits-in-a-parser |
| `SuiteRoboCssTokenizeCore.c` | 13 | Whitespace runs, comments (empty, multiline, Unicode, unterminated), CDO/CDC, punctuation, delimiters, bracket pairing, EOF range; NUL and ill-formed UTF-8 — the two ends of the byte spectrum where the no-preprocessing decision is visible |
| `SuiteRoboCssTokenizeIdent.c` | 12 | Identifiers (simple, hyphenated, underscore, `--custom`, Unicode, escaped), functions, at-keywords, hashes (`HASH_ID` vs `HASH_UNRESTRICTED`), urls (bare, quoted, escapes, malformed) |
| `SuiteRoboCssTokenizeLiteral.c` | 13 | Strings (quote forms, escapes, hex escapes, line continuations, Unicode, empty, malformed), numbers (integer, decimal, leading point, signs, exponents, extremes), percentages, dimensions and their `unit` |
| `SuiteRoboCssTokenizeSequence.c` | 12 | Whole snippets lexed end to end (`color: red;`, `margin: 10px 20px;`, the `@media` block), range/position audit, malformed inputs, streamed large-input robustness |

## 2. The cleared-record contract test

`css.records.cleared_state` exists because §3 of `docs/Architecture.md` — "a
record must be cleared before its first use" — is a precondition the compiler
cannot check, and every caller of `ROBO_CSS_CLEARED` depends on the cleared state
being *exactly* what the init functions produce.

If a cleared record were ever not the same as an init'd one, every caller of
`ROBO_CSS_CLEARED` would be releasing fields nobody set. So the test asserts the
equivalence field by field, for a value, a source, a declaration, a selector, a
compound, a complex, a selector list, a rule, a rule list, an error and a parser:
the kind enumerator, the child array pointer and capacity, the absence of slices
and ranges, the derived counts (depth, tree count, child count), the names, the
error message, and the parser's source and offset.

It then goes one step further and asserts that each cleared record is a *usable*
starting point rather than an inert one: each can be configured or released
without a trip through its init function, release is idempotent, and release is
safe on a record that was cleared and never used.

This test is the reason the rule is a contract rather than a comment. It was
written *after* a real crash: a `robo_css_value_t` local reached
`robo_css_value_make_list` uninitialized, the release step walked a `count` of
stack garbage and freed a pointer that was never an allocation.

## 3. What the CSS suites do and do not assert

They assert what the **model** does with bytes — kinds, ownership, ranges,
slices, invariants, lifetimes — and what the **tokenizer** does with a byte
sequence: which token type, what value, which range. They never assert that
something was *parsed*, matched or computed, because in these phases nothing is.

Where a boundary answers `ROBO_NOT_SUPPORTED`, the suites assert the whole shape
of the refusal, not just the code: nothing changed, the state did not advance, the
reason is recorded on the object as well as returned, the message names the goal
that will implement it, and a NULL out-parameter is tolerated.

`css.capabilities` asserts that `parser`, `source_model`, `selectors`, `cascade`,
`computed_values` and the rest report what this build *does*. A pass does not get
to round those flags up to what the phase list intends.

## 4. Fixtures

`RoboCssTestSupport.h` is shared by all nine suites. It is not a framework; it
holds the three things a suite would otherwise copy:

* `css_fixture()` — brings RoboBase and RoboCSS up, and a `css_source_of_string`
  that builds a source from a literal without the caller tracking the length.
* `css_samples[]` — the thirteen script families every "does it preserve text"
  test walks, in one place so two tests cannot disagree about what "Arabic" means.
* Builders (`css_value_token_at`, `css_value_function_with_token`,
  `css_declaration_append`, `css_complex_selector_of`), the lexing pair
  `css_lex` / `css_lex_text` with `css_token_log_t`, `css_token_at`,
  `css_count_type`, `css_kind_label` and `css_log_kinds`, the streaming pair
  `css_stream_audit_t` / `css_audit_stream`, and the allocator measurement pair
  `css_allocator_ensure_enabled` / `css_allocator_restore`, so a lifetime test
  never leaves the debug allocator on for the next suite.

The audit pair exists for a specific reason. The framework's expectation log is
bounded, so a test that logs one line per token fails by *overflowing the
reporter* on a several-thousand-token input — it reports nothing about the thing
it was written to check. `css_audit_stream` walks a stream counting tokens by
type and total instead, so a large-input test asserts a shape rather than a list.

Everything in the header is `static inline`. That is not decoration: a plain
`static` function in a header that one suite does not use is an unused-function
error under this project's `-Wall -Wextra -Werror`, and `static inline` is how
the framework's own headers solve the same problem. The header also casts its
UTF-8 samples carefully — `"\xC3\x9Cber"` is a single out-of-range escape
(`\x9Cb`), so the German sample splits its literal to keep the escape honest.

## 5. Running

```bash
scripts/robo test-css                     # the 151 css.* tests only
scripts/robo test-runtime                 # the whole runtime suite
scripts/robo test-css -Verbose            # per-test lines
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test
```

A focused run builds the same host binary and filters on the `css.` prefix, so a
failure in another subsystem cannot be reported against this one. The tokenizer
subtree is `--filter=css.tokenize` (63 tests); one suite is
`--filter=css.tokenize.number`.

`--list` prints every registered name, which is how a documented test name can be
checked against the binary instead of trusted.

## 6. Sanitizers

```bash
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test -Sanitize
```

The flag probes the toolchain and enables AddressSanitizer and
UndefinedBehaviorSanitizer for the host test binary; it is documented in
`docs/Build.md`. The result for this phase:

* **AddressSanitizer + UndefinedBehaviorSanitizer: clean.** 1558 tests, 0
  failures, zero sanitizer findings across the whole runtime suite.

One real defect was found by the sanitizers during 9.1 and fixed rather than
suppressed:

> `robo_css_stylesheet_parse` dereferenced its `out_error` parameter
> unconditionally when reporting the not-yet-implemented parse, even though
> `robo_css_error_set` tolerates a NULL destination and the header promises the
> record is optional. ASan reported the access at `0x0` when
> `css.sheet.parse_boundary` called it as `robo_css_stylesheet_parse(id, NULL,
> NULL)`. Fixed by building the record locally and copying it to the caller only
> when one was given — the stylesheet keeps its copy either way.

No sanitizer finding was suppressed, and no test was weakened to reach a clean
run.

Two more real defects surfaced during 9.2, both from tests rather than from the
sanitizers:

> **The hex-escape terminator rule.** `\41 b` is the ident `Ab`: the whitespace
> after a hexadecimal escape is part of the escape and must be swallowed. The
> scanner consumed it but the *value* accessor did not re-apply the same rule, so
> `robo_css_token_value_*` returned `A b` for a token whose `repr` said `Ab`.
> The accessor now applies the identical rule — the two layers are tested against
> each other so they cannot drift again.
>
> **Line continuations in strings, again across the two layers.** A backslash
> before a newline inside a string is a continuation that produces no character.
> The tokenizer classified it correctly; the value accessor wrote both the
> backslash and the newline into the resolved text.

A third finding was a **contract violation in the new suites themselves**: several
records reached `dispose`/`copy` without being `ROBO_CSS_CLEARED` first. The
first symptom was not a failed assertion but an invalid-free, which is exactly the
failure mode that contract exists to prevent — hence the regression test it
gained in `SuiteRoboCssTokenizer.c`.

`SuiteRoboCssTokenizeSequence.c` also streams its large-input audit rather than
logging every token, because the framework's expectation log is bounded and a
several-thousand-token snippet would exhaust it — a test that fails by
overflowing the reporter reports nothing useful.
