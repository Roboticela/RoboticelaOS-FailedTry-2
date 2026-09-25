# RoboJS — The interpreter (16.9)

`RoboJSInterpreter.c` is a tree-walking evaluator. It owns statement completion, the operator table, lvalue
writes, calls, and the step and depth budgets. It owns no socket, no file, no thread and no scheduler.

---

## 1. The doors

```c
Result JsInterpRunProgram(JsRuntime *runtime, JsNode *program, JsEnv *env, JsScriptResult *out);
Result JsInterpRunExpression(JsRuntime *runtime, JsNode *expression, JsEnv *env, JsValue *out);
int32_t JsInterpPropertyKey(const JsValue *value, char *buffer, size_t capacity);
int JsCompletionIsNormal(const JsCompletion *completion);
```

`JsInterpRunProgram` answers `RoboOk` when the program **ran to completion** — including a program whose last
statement was a `throw` an enclosing `try` caught. It answers a failure when a value was thrown *out* of the
program, with the runtime's pending exception holding it. That distinction is why a caller can tell "the script
finished" from "the script threw", and it is the one `JsRuntimeEvaluate` reports upward.

`JsInterpRunExpression` exists so a test can keep an expectation small, and so an integration layer can step a
script by hand (`js.integration.steps`).

## 2. Completion, not C control flow

```c
typedef struct JsCompletion { uint32_t kind; JsValue value; } JsCompletion;
```

`Normal`, `Break`, `Continue`, `Return`, `Throw`. Every statement arm returns one; a block's loop propagates
anything non-`Normal`; a `while` consumes its own `Break`/`Continue` and passes the rest up; a call node turns a
callee's pending exception into a `Throw` (`Exceptions.md` §5) and a function call's `Return` into a value.

This is what makes `finally` correct: the clause runs with the in-flight completion in hand, so a `return` inside
a `finally` can replace what was travelling and a `break` crossing a `finally` still runs it. A design that
unwound with C returns from the middle of a statement would need a second pass to run the `finally` blocks.

## 3. The operator table

One arm per node kind and one switch per operator class. `RoboJsNodeBinaryExpression` switches on `node->op`
between `+ - * / %`, the relational operators and `in`/`instanceof`; `RoboJsNodeLogicalExpression` on `&&`/`||`
(short-circuiting, which is why it is a separate *node kind* rather than a binary expression);
`RoboJsNodeAssignmentExpression` on `=` and the five compound forms; `RoboJsNodeUpdateExpression` on prefix and
postfix `++`/`--`.

Three rules the arms share:

- **`+` is not addition.** `JsValuePlusConcatenates` decides, then the arm concatenates or adds
  (`Values.md` §4).
- **A property key is computed once.** `JsInterpPropertyKey` is the one place a value becomes a key: a string stays
  as it is, a number becomes its canonical text, everything else goes through `JsValueToString`. A build with the
  conversion inline in each member-expression arm would have several answers to `a[1]` versus `a["1"]`.
- **An lvalue is written through one door.** `JsWriteLValue` handles an identifier (the environment, then the
  implicit-global rule, with `const` refused first) and a member (evaluate the target, then the key, then
  `JsObjectSet`). Assignment, compound assignment and update all go through it, so a build that guarded plain `=`
  and forgot `++` would fail `js.interpreter.declarations`.

## 4. Budgets

| Budget | Where it is spent | What runs out as |
|---|---|---|
| `maxSteps` (500 000) | a counter in `JsRuntimeSpendStep`, ticked per statement and per loop iteration | `RoboJsErrorLimit` at the current node's span |
| `maxCallDepth` (64) | incremented around a call, decremented after | `RangeError` — never a C stack overflow |
| `maxPropertiesPerObject` | the object model's append | `RangeError` |
| `maxStringLength` | `JsHeapString` | `RangeError` |

`while (true) {}` therefore stops with a located error rather than hanging the shell, and the runtime is usable
immediately afterwards (`Security.md` §4, `js.limit.budgets`).

## 5. Hoisting, and the one pass that makes the declarations real

Before a body runs, the interpreter walks its **statements** (not nested function bodies) and:

1. hoists a `var` into the neighbouring function link, tolerating a re-executed declaration;
2. hoists a `let` into the current link if it is not already there;
3. hoists a **`const` as an already-locked binding** — `JsEnvDeclareConst` — because the hoisting pass is the only
   place the declaration's kind can be recorded (`Closures.md` §3);
4. hoists a function declaration the same way a `var` is hoisted, with the function value.

Function declarations are hoisted **with their value**, so a function is callable before its textual position —
the observable half of hoisting, and the one `js.function.declaration` checks.

## 6. The tree is read, never written

An arm computes a value and returns; a compound assignment replaces a *binding* or a *property*, not a node; a
`for` loop's update evaluates its target. Nothing in the interpreter mutates a `JsNode`, so **one parse can be
evaluated many times** and `JsRuntimeParse` followed by repeated `JsRuntimeEvaluateProgram` is a supported shape —
which is what a host that wants a syntax check before running, or an evaluation under a smaller budget, wants.

## 7. What is absent

No bytecode, no IR, no JIT, no constant folding, no inlining, no fast path for a monomorphic call site, and no
tail-call elimination (so recursion is bounded by `maxCallDepth` rather than by the C stack). No `for…in`, no
`with`, no `eval`, no `arguments` object, no getters/setters, no spread or destructuring — each refused by the
lexer or the parser, so the interpreter cannot meet one (`LanguageSubset.md` §5).
