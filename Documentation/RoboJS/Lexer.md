# RoboJS — The lexer (16.2)

`RoboJSLexer.c` turns bytes into tokens. It is small on purpose: the vocabulary is `RoboJsToken*` in
`RoboJSTypes.h`, the accepted characters are `LanguageSubset.md` §1, and **nothing here evaluates, allocates an
object, or knows what a token will become.**

---

## 1. The door

```c
Result JsLexerInit(JsLexer *lexer, JsHeap *heap, const char *source, int32_t length); /* < 0 → strlen */
Result JsLexerNext(JsLexer *lexer, JsToken *out);  /* one token; RoboOk at End, repeatedly */
Result JsLexerPeek(JsLexer *lexer, JsToken *out);  /* the next token, without consuming */
```

`JsLexerNext` at end of input answers `RoboJsTokenEnd` and `RoboOk` **forever** — a parser loop cannot spin on a
`RoboNotFound` that means "there is no more source", and an `End` token that had to be special-cased at every call
site would be the alternative.

The lexer takes the heap because a decoded string's bytes have to live somewhere; a *token* still borrows its
characters from the source rather than copying them.

## 2. What every token carries

| Field | Meaning |
|---|---|
| `kind` | one `RoboJsToken*`, tested with `JsTokenKindIsKnown` |
| `flags` | token-local marks (an identifier's, a number's) — never a second kind |
| `span` | `JsSpan`: 1-based `line`/`column`, 0-based `offset`, and the `end*` triple |
| `text` | the token's bytes, **borrowed from the source** — no copy |
| `length` | bytes in `text` |
| `number` | the value of a `RoboJsTokenNumber` |

Borrowing is what keeps a token a view into the source the caller already owns. The consequence a caller must know
is that **the source must outlive the tokens**, which is why the parser copies what it keeps (`RoboJSParser.c`
interns an identifier through `JsHeapString` when it builds a node).

`span` exists so a diagnostic can say *where*: every parse and lex failure in the module reports a 1-based line
and column, and `js.lexer.errors` asserts both — the start **and** the end, which is what makes `"a string
literal may not span a line"` point at the offending byte rather than at the token's first character.

## 3. Refusals are located and named

The lexer's failures are all `RoboJsErrorSyntax`, all with a span, and each names the byte it wanted:

| Input | Message |
|---|---|
| `` ` `` | *template literals are not supported by this subset* |
| `~` | *`~` is not supported by this JavaScript subset* |
| `&` / `\|` alone | *`&` alone is a bitwise operator this subset does not have* |
| `\xHH` with fewer than two hex digits | *`\x` needs two hexadecimal digits* |
| `\uHHHH` with fewer than four | *`\u` needs four hexadecimal digits* |
| an unknown escape | *unsupported escape `\%c`* |
| a string with a newline in it | *a string literal may not span a line* |
| an unterminated string or block comment | its own message |
| `0x` with no digits, or `.` with no digits | *a number needs at least one digit* |

**A refusal is not a fallback.** There is no "treat `~` as `-`" and no "stop the template literal at the first
`$`": a construct outside the subset produces a `SyntaxError` at its own byte, which is what makes
`LanguageSubset.md`'s list of absences checkable rather than aspirational. `js.lexer.errors` asserts the code, the
domain and the location for each.

## 4. The reserved words

An identifier whose bytes match a keyword is the keyword token, and there is no contextual lexing: `as`, `from`,
`of`, `get`, `set`, `static` and `async` are ordinary identifiers here because they have no meaning in the subset,
while `undefined` is a *keyword* so that `typeof undefined` is `"undefined"` without the global object having to
carry it. `let` and `const` are keywords with their own tokens, because `Closures.md` §4 (the scope chain) and
`LanguageSubset.md` §3 both need to know which declaration a script wrote — and the two differ in mutability and
in their binding attributes, which is a lexer-level distinction rather than a guess at the declaration site.

## 5. Numbers and strings

- **Numbers** are `int64`-wide doubles parsed by hand: decimal, fraction, exponent, and `0x` hexadecimal. There is
  no `0o`, no `0b`, no numeric separator, no `BigInt` suffix and no `Infinity`/`NaN` literal — those names are the
  globals `Math`/`Number` provide, not keywords.
- **Strings** are single or double quoted with the escape set in `LanguageSubset.md` §1. The lexer **does not
  decode**: `text` is the raw bytes including the quotes, and the string's *value* is built by the parser's
  literal path. That split is deliberate — a lexer that built values would need the heap, and it is the parser
  that already owns one.

## 6. What is absent

No regular-expression literal, no template literal, no `#!` shebang, no ASI (a missing `;` is the *parser's*
decision, not the lexer's), and no comment-preserving token stream. Each is a row in `Limitations.md` where it
matters, and the lexer's job stays one pass with one token out.
