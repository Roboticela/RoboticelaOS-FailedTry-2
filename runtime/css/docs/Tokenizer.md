# RoboCSSTokenizer (Phase 9.2)

The real machine, where 9.1 shipped the record it fills. `RoboCSSTokenizer.c`
turns CSS source bytes into the token vocabulary of `RoboCSSToken.h`: the CSS
Syntax state machine — whitespace, comments, CDO/CDC, identifiers, functions,
at-keywords, hashes, strings, urls, numbers, percentages, dimensions,
punctuation, EOF. Written independently, with no Chromium code or tables.

```text
robo_css_source_t            bytes                       (9.1)
      │
      ▼
robo_css_tokenizer_t         the stream machine          (9.2)
      │
      ▼
robo_css_token_t             one token, borrowed         (9.1 + 9.2 value)
      │
      ▼
the stylesheet grammar                                   (9.3, not here)
```

## 1. Three decisions that shape everything

### 1.1 The stream is a cursor the caller owns, not a heap object

RoboHTML's tokenizer is created and destroyed because it retains a bounded
diagnostic list. RoboCSS's is a plain by-value record with `init`, `dispose` and
`reset`, exactly like `robo_css_parser_t` — the thing it will be embedded in.
**There is no allocation anywhere in this subsystem.** `init` cannot fail for an
allocation reason, nothing leaks, and a tokenizer is as cheap to put on the stack
as it is to keep in a parser.

### 1.2 The caller's bytes are never rewritten

The specification's input preprocessing maps CR, CRLF and FF to LF and NUL to
U+FFFD. This tokenizer does not, because a token's range is a byte range into the
caller's own buffer and rewriting would make that range a lie. The same effect is
reached by **classification** instead:

| Preprocessing would | This build instead |
|---|---|
| CR → LF | CR, LF and FF read as one newline; a CRLF pair as one |
| (nothing) | a hex escape's trailing whitespace swallows a whole CRLF pair |
| newline in string → LF | a newline inside a string makes it a **bad-string** |
| NUL → U+FFFD | NUL is a **delimiter**, reported as such |

The NUL case is the one place classification cannot substitute. The
specification would have made NUL an identifier character by turning it into
U+FFFD; claiming that would mean the token's value said something the caller's
bytes do not contain, so this build reports the delimiter it actually saw.

Ill-formed UTF-8 is preserved and reported as an `INVALID` token one byte at a
time — the same "one ill-formed position" rule the source model uses — never
repaired and never silently folded into a neighbouring name.

### 1.3 A token is raw; its value is computed

The tokenizer fills every field of the 9.1 record — `number`, `repr`, `unit`, the
hash classification — but leaves `text` as the **raw bytes**, so `text` and
`range` can never disagree. The token's *value* (escapes resolved, delimiters
stripped) is reached through the `robo_css_token_value_*` accessors, which work
from `text` alone.

Consequences: a tokenizer never hands out a token whose text points into scratch
storage, no token is invalidated by asking for the next one, and a token can be
copied, stored and compared for as long as its source lives.

## 2. Token categories

The vocabulary is 9.1's; 9.2 adds no token type. What 9.2 adds is a producer for
each of them.

| Group | Types | Notes |
|---|---|---|
| Trivia | `WHITESPACE`, `COMMENT` | A whitespace *run* is one token; a comment includes its delimiters |
| Names | `IDENT`, `FUNCTION`, `AT_KEYWORD` | `--x` is an ident; an ident followed by `(` becomes `FUNCTION` |
| Hashes | `HASH_ID`, `HASH_UNRESTRICTED` | Id-shaped (`#foo`) vs unrestricted (`#123`) |
| Quoted | `STRING`, `BAD_STRING` | A newline in a string ends it as `BAD_STRING` |
| Urls | `URL`, `BAD_URL` | Value excludes `url(` and `)`; escapes resolved |
| Numeric | `NUMBER`, `PERCENTAGE`, `DIMENSION` | `unit` carries `px`, `deg`, `ms`, …; `repr` carries the source spelling |
| Literals | `DELIM`, `COLON`, `SEMICOLON`, `COMMA`, `CDO`, `CDC` | `<!--` and `-->` are their own tokens |
| Brackets | `OPEN_PAREN/CLOSE_PAREN`, `OPEN_SQUARE/CLOSE_SQUARE`, `OPEN_CURLY/CLOSE_CURLY` | `robo_css_token_type_matching_close` pairs them |
| Terminal | `EOF` | A real token with an empty range at the end offset |
| Failure | `INVALID` | Ill-formed UTF-8, one byte at a time |

## 3. Source ranges

Every emitted token carries `range` (start/end byte offsets) into the caller's
source, plus the line/column start resolved through the source model's position
rules (LF, lone CR, CRLF, scalars). The range is the interesting part for several
classes:

- a whitespace run is **one** token covering many bytes;
- a comment's range **includes** `/*` and `*/`;
- a function token's range includes the `(`;
- a URL token's range includes `url(` and `)`, while its *value* excludes them;
- EOF has `start == end == length`.

`robo_css_tokenizer_position` resolves the cursor's own (offset, line, column).

## 4. Malformed input

Errors follow the parser's rule exactly: the **first** problem is kept, later
ones are not recorded over it, and every problem is a **value**. Malformed CSS
never crashes, never terminates the caller, and always yields a defined token
stream. A malformed style sheet is not a failed call — it is a token plus a
recorded error (`robo_css_tokenizer_error`, `take_error`, `has_error`).

Deterministic outcomes for the cases the suite pins:

| Input | Outcome |
|---|---|
| `"unterminated` | `BAD_STRING` to EOF, error recorded |
| `/* unterminated` | One `COMMENT` to EOF (CSS closes it implicitly), error recorded |
| `10.` | `NUMBER` `10`, then `DELIM` `.` |
| `url("unterminated)` | `BAD_URL` |
| ill-formed UTF-8 | One `INVALID` per ill-formed byte |
| NUL | `DELIM` |

## 5. Token stream API

```c
robo_css_tokenizer_t t;
robo_css_tokenizer_init(&t, &source, diag);      /* never allocates */

robo_css_token_t token;
while (robo_css_tokenizer_next(&t, &token, diag) == ROBO_OK &&
       token.type != ROBO_CSS_TOKEN_EOF) {
    /* token borrows source: text/range stay valid while source lives */
}
```

| Call | Purpose |
|---|---|
| `init` / `dispose` / `reset` | Bind a source; forget it; rewind keeping the source |
| `next` | Produce and consume |
| `peek` | Produce without consuming; repeated peeks read the same token |
| `skip` | Drop the next token without producing it |
| `skip_trivia` | Step over whitespace and comments, leaving the next real token peeked |
| `at_end`, `offset`, `remaining`, `source`, `has_peek`, `tokens_produced`, `position` | Cursor state |
| `record_error`, `fail`, `has_error`, `error`, `take_error` | The first error |

`peek` and `skip_trivia` share one `peeked` slot, so `peek` twice reads the same
token twice rather than tokenizing it twice. Trailing EOF is a **value**: asking
again keeps returning that EOF token, never advances the cursor and never touches
the source, so a loop that terminates only on EOF terminates on a value.

## 6. Ownership and lifetime

| Object | Owner | Lifetime |
|---|---|---|
| `robo_css_source_t` | Caller | Must outlive every tokenizer pointed at it |
| `robo_css_tokenizer_t` | Caller | By value; `init` → `dispose`; frees nothing |
| `robo_css_token_t` | Caller | Plain value, copyable; `text`/`range` borrow the source |
| `const char *` from `value_*` | Caller (copy) | `robo_css_token_value_copy` writes into caller storage |
| Recorded error | Tokenizer | Valid until `reset`/`dispose`/`take_error` |

No ownership cycles, no hidden global state, no Chromium ownership model. The
tokenizer holds no reference to any DOM node, stylesheet or parser.

## 7. How to run the tokenizer tests

```bash
# Everything (host suite)
pwsh -NoProfile -File runtime/Tools/Build.ps1 -Configuration Debug -Test

# The CSS suite alone
./runtime/Out/RoboTest.host.exe --filter=css.

# The tokenizer alone
./runtime/Out/RoboTest.host.exe --filter=css.tokenize

# One suite
./runtime/Out/RoboTest.host.exe --filter=css.tokenize.number
```

`--list` prints every registered name. See `docs/Tests.md` for the suite map and
`docs/Architecture.md` for ownership rules shared with the rest of RoboCSS.
