# RoboJS — The language subset

What this build lexes, parses and evaluates — and, in the same document, what it refuses. The refusals matter as
much as the support: a subset that half-lexes `class` produces a `SyntaxError` pointing at the wrong place, and a
subset that silently accepts `let` with `var` semantics is worse than one that says no.

`JsCapabilities::languageSubset` carries this document's first sentence as one line a caller may print.

---

## 1. Lexical grammar

| Kind | What is accepted |
|---|---|
| whitespace | space, tab, CR, LF, form feed, vertical tab; `\r\n` counts as one line |
| comments | `// …` to end of line; `/* … */`, unterminated → `SyntaxError` |
| identifiers | `[A-Za-z_$][A-Za-z0-9_$]*` |
| reserved words | `var let const function return if else while do for break continue throw try catch finally switch case default new this typeof delete void in instanceof true false null undefined` |
| numbers | decimal, with fraction and exponent; `0x` hexadecimal; a leading `.` is allowed |
| strings | `'…'` and `"…"`, with `\\ \' \" \n \r \t \b \f \v \0 \xHH \uHHHH`; a literal may not span a line |
| punctuation | `( ) { } [ ] . , ; : ? =>` and the operator set in §2 |

**Refused by name, at the offending byte:**

| Input | Answer |
|---|---|
| `` ` `` | *template literals are not supported by this subset* |
| `~` | *`~` is not supported by this JavaScript subset* |
| `&`, `\|` alone | *`&` alone is a bitwise operator this subset does not have* |
| `\x` / `\u` with too few digits | *`\x` needs two hexadecimal digits* / *`\u` needs four* |
| an unterminated string, comment, or number with no digits | its own message |

There are no bitwise operators (`& | ^ ~ << >> >>>`) and no `**` in this build. `&&`, `||`, `!` and `??`'s
absence are separate facts: the first three exist, the last does not.

**Keywords are not reserved contextually.** `undefined` is a keyword token rather than a global name, which is
why `typeof undefined === "undefined"` and why assigning to it is refused rather than silently accepted.

---

## 2. Operators, by precedence

Highest first. Every level is one arm of the parser's precedence climb, and the interpreter's operator table has
an entry per row — so an operator cannot be parsed and then be unimplemented.

| Precedence | Operators | Associativity |
|---|---|---|
| 18 | `(` call, `[` member, `.` member | left |
| 17 | `new` | right (argument list) |
| 15 | `!` `-` `+` `typeof` `void` `delete` | prefix, right |
| 14 | `++` `--` | postfix |
| 13 | `*` `/` `%` | left |
| 12 | `+` `-` | left |
| 11 | `<` `>` `<=` `>=` `in` `instanceof` | left |
| 10 | `==` `!=` `===` `!==` | left |
| 9 | `&&` | left |
| 8 | `\|\|` | left |
| 4 | `?:` | right |
| 3 | `=` `+=` `-=` `*=` `/=` `%=` | right |
| 1 | `,` (sequence) | left |

**Both equalities are real and differ.** `===` compares kind first and never coerces; `==` is the specification's
abstract equality over the six kinds. `js.value.equality` asserts the pairs where they disagree (`0 == "0"` is
true, `0 === "0"` is false, `null == undefined` is true, `NaN == NaN` is false).

---

## 3. Statements

| Statement | Notes |
|---|---|
| `var` / `let` / `const` | `let` and `const` parse and *do* have block-level scope in the environment chain; `const` is enforced on assignment |
| an expression statement | `;` optional after `}` and at end of input; required otherwise |
| a block | `{ … }` — its own scope |
| `if` / `else`, `while`, `do … while`, `for` | `for (init; test; update) body`; `init` may be a declaration, `test` and `update` any expression |
| `break` / `continue` | with an optional label is **not** supported; a bare `break`/`continue` only |
| `return` | top-level `return` is a `SyntaxError` |
| `throw` | `throw;` is a `SyntaxError` |
| `try { } catch (e) { } finally { }` | `catch` and `finally` are each optional but not both absent |
| `switch (x) { case …: … default: … }` | fall-through works; duplicate `default` is a `SyntaxError` |
| `function` declaration / expression | see `Functions.md` |
| an arrow function | `(a, b) => expr`, `a => expr`, `() => { … }` |
| `;` | an empty statement |

**Not parsed, and therefore refused by the parser** with a located message naming what it wanted: `for…in`,
`for…of`, labels, `class`, `import`/`export`, `async`/`await`, `yield`, destructuring patterns, rest/spread, a
getter/setter in an object literal, and a computed property name other than a literal or identifier.

---

## 4. Expressions

| Form | Notes |
|---|---|
| number, string, `true`, `false`, `null`, `undefined` | literals |
| `this` | the receiver of the current call; a top-level `this` is the global object |
| an array literal | `[a, b, c]`, holes are **not** supported |
| an object literal | `{ key: value, "key": value, 7: value, }` — a trailing comma is allowed |
| a function expression, an arrow function | `Functions.md` |
| a call | `f(a, b)`, including a method call with a receiver |
| `new` | `new F(args)` — `F` is called with a fresh object as `this` |
| a member | `a.b`, `a["b"]`, `a[0]` |
| assignment | `a = b`, and the five compound forms, on a name or a member |
| a conditional | `a ? b : c`, right-associative |
| a sequence | `a, b, c` |
| a postfix/prefix update | `a++`, `++a`, on a name or a member |

There is **no** optional chaining (`a?.b`), no nullish coalescing (`a ?? b`), no spread, and no tagged template.
Each is refused rather than approximated.

---

## 5. The evaluated set is the parsed set

The interpreter has one arm per node kind and one entry per operator row, so a construct cannot be parsed and then
quietly do nothing. `js.parser.ast` walks the shape table and `js.interpreter.expressions` / `js.interpreter.loops`
exercise the arms; a construct that parses but has no arm fails one of the two.

---

## 6. What a caller may print as the subset

`JsGetCapabilities(&caps)` fills `caps.languageSubset` with one line naming the grammar above, and the absent block
with `0` for each thing §3 and §4 refuse. A consumer decides what to do about the absences *at build time* rather
than discovering them at runtime, which is the whole reason the table has a second half.

---

## 7. Where a change belongs

| Add | Files |
|---|---|
| a token | `RoboJSTypes.h` (`RoboJsToken*`, and `JsTokenKindIsKnown`), `RoboJSLexer.c`, `languageSubset` |
| a node kind | `RoboJSTypes.h` (`RoboJsNode*`), `RoboJSAst.c` (the shape table and printer), the parser |
| a statement | `RoboJSParser.c`'s statement switch, `RoboJSInterpreter.c`'s statement switch |
| an operator | the precedence table here, the parser's climb, `RoboJSInterpreter.c`'s operator table |
| a refresh of the absences | `RoboJSVersion.h`'s second block, `RoboJSVersion.c`, `js.runtime.capabilities` |

A change to any of those is not done until `TestRoboJS.ps1 -Target Host` passes and this document matches the
build — and, for anything a document page may contain, `TestDesktop.ps1 -Target Host`.
