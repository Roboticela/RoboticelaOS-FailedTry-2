# RoboJS — The parser (16.2)

`RoboJSParser.c` is a recursive-descent parser over the lexer's token stream. It owns the grammar, the precedence
climb, ASI at the edges, the parse-depth budget, and every "this is not in the subset" message that is about
*syntax* rather than about a character.

Grammar reference: `LanguageSubset.md` §3 (statements) and §4 (expressions). This document is the *mechanism*.

---

## 1. The door and the state

```c
Result JsParserInit(JsParser *parser, JsHeap *heap, const char *source, int32_t length);
Result JsParserParseProgram(JsParser *parser, JsNode **out);   /* the whole source */
int    JsParserAt(const JsParser *parser, JsTokenKind kind);
```

`JsParser` carries the lexer, the heap, the current token, the depth, and the diagnostic slot. `JsParserErrorOf`
reports the failure as a `JsDiag` (the accessor is named `…Of` because `JsParserError` is the *struct* — a
collision worth knowing about when reading the header).

**One token of lookahead, held in `parser->current`.** `JsParserNextToken` advances by *consuming* the current
token and lexing the next; a peek that lexed without consuming would leave the parser reading the token after the
one it peeked at, which is the single most expensive mistake to find in a recursive-descent parser (it does not
crash — it silently drops a token). `JsParserExpect` and `JsParserFail` both report against `current`, so a
diagnostic names the token the parser was actually looking at.

## 2. The depth budget

`maxParseDepth` (`RoboJsDefaultMaxParseDepth`, 64) is checked as the parser descends. `((((((…))))))` therefore
answers a `RangeError` at a located span rather than overflowing the C stack — which matters because the parser is
recursive and the source is untrusted. `Security.md` §4 lists it with the rest of the numbers.

## 3. Statements

`JsParseStatement` dispatches on the leading token: `{` a block, `var`/`let`/`const` a declaration, `if`/`while`/
`do`/`for`/`switch` the control forms, `break`/`continue`/`return`/`throw` the flows, `try` the exception form, and
`function` a declaration. Anything else is an expression statement.

| Rule | Detail |
|---|---|
| **`return` at the top level** | `SyntaxError` — the phase has no module wrapper to return from |
| **`throw;`** | `SyntaxError`: `throw` needs an expression |
| **a `try` with neither `catch` nor `finally`** | `SyntaxError` |
| **two `default` clauses** | `SyntaxError` |
| **`break`/`continue` with a label** | not parsed; a bare form only |
| **`for…in`, `for…of`** | not parsed — the `for` header expects `;` where the spec would accept `in`/`of`. The grammar is *narrower* than the token set, and the token set has `in` because it is a `for (a in b)` **operator** in `LanguageSubset.md` §2's relational row |
| **ASI** | `;` is optional before `}`, at end of input, and after a statement that ended with `}`; the parser does not implement the full restricted-production rules, and `Limitations.md` states it |

## 4. Expressions: one precedence climb

`JsParseExpression` → assignment → conditional → `||` → `&&` → equality → relational → additive → multiplicative →
unary → postfix → call/member → primary. Each level is one function, and the ordering is `LanguageSubset.md` §2's
table read top-down. The climb is what makes the operator set *checkable*: a level that forgot a token answers a
`SyntaxError` naming what it wanted, so an operator cannot be lexed and then be unreachable.

Three things the climb does that a naive one does not:

- **Assignment targets are verified.** `JsParseAssignment` checks the left side is an identifier or a member and
  reports *"an assignable target before the operator"* otherwise — so `1 = 2` is a parse error rather than an
  interpreter surprise.
- **`new` takes an argument list**, and its precedence is between call and member, which is what makes
  `new F().m` parse as `(new F()).m`.
- **A function body is parsed with `function` already consumed.** A recursive-descent parser's classic off-by-one:
  `JsParseFunctionRest` assumes the keyword is gone, and both call sites must advance first. Getting it wrong
  produces a parser that fails on every function and looks like a lexer bug.

## 5. Object literals and the one-key rule

An object literal accepts an identifier, a string, or a number as a key, with an optional trailing comma. A getter,
a setter, a shorthand property (`{ a }`), and a computed key (`{ [k]: v }`) are **not** parsed. The grammar is
deliberately the smallest one that can express a page's configuration object.

## 6. What the parser hands on

`JsRuntimeParse` runs the parser and answers a `JsNode *`; `JsParserParseProgram` is the same without the runtime's
diagnostic plumbing. A parse failure is `RoboError` with a `RoboJsErrorSyntax` diagnostic at the offending token's
span, and the partially-built tree is abandoned to the heap (`Ast.md` §4 — nothing frees a node).

`JsAstIsWellFormed` is the parser's contract with the interpreter: a tree the parser produced is a tree with an
evaluator arm for every node in it, and `js.parser.ast` checks it.

## 7. What is absent

No `for…in`/`for…of`, no labels, no `class`, no `import`/`export`, no `async`/`await`, no generators, no
destructuring, no rest/spread, no getters/setters, no computed keys, no optional chaining, no `??`, no tagged
templates, no `with`, no labelled `break`. Each is a token (or a token sequence) the parser reports it did not
want, which is what keeps `LanguageSubset.md`'s two lists the whole truth: a construct is either parsed and has an
evaluator arm, or it is a located `SyntaxError`.
