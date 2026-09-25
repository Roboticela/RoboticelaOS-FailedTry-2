# RoboJS — Exceptions (16.8)

An exception is a **value in a slot**, not a C `longjmp`, and the runtime survives one. `RoboJSException.h` is the
contract; the five error kinds are `RoboJsError*` in `RoboJSTypes.h`.

---

## 1. The seven refusal codes and the five error kinds

| Code | Script-visible as | Where it comes from |
|---|---|---|
| `RoboJsErrorNone` | — | nothing pending |
| `RoboJsErrorGeneral` | `Error` | a script's own `throw new Error(…)`, and the engine's generic failures |
| `RoboJsErrorType` | `TypeError` | a bad operand, a call of a non-callable, a write to a `const` |
| `RoboJsErrorReference` | `ReferenceError` | reading a name nothing declares |
| `RoboJsErrorRange` | `RangeError` | a budget (`Security.md` §4), an array length, a string length |
| `RoboJsErrorSyntax` | `SyntaxError` | the lexer and the parser |
| `RoboJsErrorInternal` | `Error` | **the engine's own failure, never a script's** — a NULL argument, an exhausted table |
| `RoboJsErrorLimit` | `RangeError` in script terms | a resource limit reached, kept distinct so a host can tell "the script was too big" from "the script was wrong" |

The split between `Internal` and `Limit` is a host-facing distinction: both reach a script as an `Error`-family
value, and a shell that wants to say *why* a page failed reads the code rather than parsing a message.

## 2. The pending slot

```c
void    JsRuntimeThrow(JsRuntime *runtime, JsErrorKind kind, const JsSpan *span, const char *format, ...);
void    JsRuntimeSetException(JsRuntime *runtime, const JsValue *value);
int     JsRuntimeHasException(JsRuntime *runtime);
Result  JsRuntimePeekException(JsRuntime *runtime, JsValue *out);   /* does not clear */
Result  JsRuntimeTakeException(JsRuntime *runtime, JsValue *out);   /* clears */
void    JsRuntimeClearException(JsRuntime *runtime);
```

**One slot.** A second `JsRuntimeThrow` before the first is read **replaces** it, which is why the module is
careful about who raises: a host's specific reason must not be overwritten by a framework line that says less.
`JsRuntimeHasException` is the door a caller uses to ask "has someone already said why?", and the interpreter's
property-assignment arm and the DOM binding's text-write arm both use it before raising their own generic message.

`JsRuntimeThrow` builds an error object and formats the message through `vsnprintf` into a fixed buffer; if the
object could not be built (memory), the pending value is the plain string, so *"there is an exception"* stays true
at the memory limit — the case a caller is most likely to be debugging.

## 3. The error object

`JsErrorCreateAt` builds an object of kind `Error` with its prototype from the matching constructor's prototype,
and its properties are exactly the four a script reads:

| Property | Value |
|---|---|
| `name` | `JsErrorKindName(kind)` — `"TypeError"`, `"RangeError"`, … |
| `message` | the formatted text |
| `line`, `column` | **only when a span was supplied** — so `catch (e) { e.line }` answers the line a runtime error happened on, and `undefined` for an error a script built by hand |

A `throw` of a non-error value is legal and stores that value: `throw "boom"` makes the pending value a string, and
`catch (e) { e }` reads `"boom"`. There is no `stack` property, no `cause`, no `AggregateError`, no `error.code`,
and no subclassing — `class` is not lexed, so an error's kind is one of the five rather than a prototype chain.

## 4. Statements: `throw`, `try`, `catch`, `finally`

`throw` is a **completion**, not a C failure: the interpreter's statement loop returns
`JsCompletion{kind = Throw, value}` up the block chain until a `try` catches it. That is why `catch` sees the
value the script threw rather than a code, and why a `throw` inside a loop body does not have to unwind anything
the interpreter owns.

`try`/`catch`/`finally`:

| Case | Answer |
|---|---|
| the body throws, `catch (e)` present | the clause runs with `e` bound in **its own** environment link |
| the body throws, no `catch` | `finally` runs, then the throw continues |
| the body throws and `finally` also throws | `finally`'s throw wins (the specification's rule) |
| `finally` has a `return` | the return wins |
| the body returns, `finally` present | `finally` runs, then the return continues |
| a `break`/`continue` crossing a `finally` | `finally` runs first |

## 5. A throw across a call

This is the one place the completion model needs help. A `throw` inside a **callee** cannot travel as a completion
— `JsFunctionCall` answers a `Result` to a caller that is not the interpreter's statement loop. So the rule is:

> A failure inside a call **is** the pending exception, and the interpreter's call arm asks
> `JsRuntimeHasException` before it reports anything of its own.

One helper is shared by both boundaries — the statement loop and the call arm — and it turns "a call returned
non-Ok" into "a completion of kind `Throw` **if** the runtime has a pending exception", reporting a genuine
internal failure only when nothing is pending. That predicate is §2's slot read as a question. Without it,
`try { f(); } catch (e)` would miss a throw from inside `f`, which is the shape a page's whole error handling
takes.

## 6. The runtime survives

After an uncaught `throw`, after a step-limit stop, after a parse failure: the pending exception is readable and
clearable, the globals are intact, and the next evaluation runs. `js.exception.handling` and
`js.integration.steps` (step 25) both assert it, because a runtime that had to be recreated after an error would
make a page's own error handling pointless.

## 7. What is absent

`Error.captureStackTrace`, stack traces of any kind, `AggregateError`, `cause`, custom error subclasses, and
`finally` interacting with a generator or an `async` function (neither exists). `Security.md` §1 keeps `eval` out,
which is also why there is no "syntax error at runtime" path.
