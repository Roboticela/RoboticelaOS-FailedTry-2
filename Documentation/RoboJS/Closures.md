# RoboJS — Environments and closures (16.7)

An environment is a link in a chain: a kind, a parent, and a list of bindings. A closure is a function that points
at the link it was defined in. That is the whole mechanism, and `RoboJSEnvironment.h` is its contract.

---

## 1. The link

```c
typedef enum { RoboJsEnvGlobal = 0, RoboJsEnvFunction = 1, RoboJsEnvBlock = 2 } JsEnvKind;

typedef struct JsBinding { const char *name; JsValue value; uint32_t flags; } JsBinding;

typedef struct JsEnv {
    uint32_t kind;
    JsHeap  *heap;
    struct JsEnv *parent;
    JsBinding *bindings; int32_t bindingCount, bindingCapacity;
    JsObject *globalObject;   /* non-NULL only on the global link: the mirror's target */
} JsEnv;
```

`JsBindingMutable` is the flag that makes `const` real (below); `JsBindingGlobal` marks a binding that belongs to
the global link. A binding's array is grown by doubling under `JsHeapMaxProperties`, so a script cannot exhaust
memory by declaring names in a loop.

## 2. The six doors, and the rule each is

| Door | Rule |
|---|---|
| `JsEnvDeclare(heap, env, name, value)` | write **this** link — a parameter, a `let` |
| `JsEnvDeclareConst(heap, env, name, value)` | the same, with `JsBindingMutable` cleared — a `const` |
| `JsEnvDeclareOrSet(heap, env, name, value)` | write this link, defining it if absent — a `let` re-executed by a loop body, a function declaration re-executed by a block |
| `JsEnvSet(env, name, value)` | search this link and every parent; `RoboNotFound` when nothing declares it, `RoboPermissionDenied` when the binding is not writable |
| `JsEnvDeclareVar(heap, env, name, value)` | write the nearest **function** link — `var` hoisting, and function-declaration hoisting |
| `JsEnvGet(env, name, out)` | search upward, stop at the first hit — that is shadowing |

Three consequences that are easy to get wrong and are therefore stated:

- **`JsEnvDeclareVar` writes the function link by name, not through `JsEnvSet`.** `JsEnvSet` searches the chain, so
  it would write an *enclosing* function's (or the global) binding when this link's own `var` is what the script
  means. `var outer` inside a function must not overwrite a global `outer`.
- **`JsEnvGet` stops at the first hit.** A block's `let` shadows an enclosing `var` in both directions.
- **Nothing frees a link.** A link created by a call that has returned stays alive because a closure points at it,
  and it is released when the heap is (`Memory.md` §2).

## 3. `const` is a binding flag, not a check at the declaration

`JsEnvSet` refuses a binding whose `JsBindingMutable` bit is clear, and the interpreter turns that refusal into a
`TypeError`:

```text
const x = 1;  x = 2;          → TypeError: assignment to constant variable 'x'
```

Three places this has to be right, and each is a test case (`js.interpreter.declarations`):

1. **The hoisting pass records the kind.** A declaration's binding comes into existence when the scope is entered,
   so the *declaration keyword* is the only thing that can decide its writability. Hoisting a `const` through the
   ordinary `JsEnvDeclare` would create a writable binding and no later write would notice.
2. **A refused write must not become an implicit global.** The interpreter's identifier-assignment arm has two
   failure answers — "nothing declares it" (create a global; sloppy mode's rule) and "it is not writable" (throw).
   Treating the second as the first would turn the one write `const` exists to refuse into a *successful* one, in a
   new global. That is why `RoboPermissionDenied` is handled before the implicit-global fallback.
3. **The global object's mirror carries the attribute.** A global binding is also a property of the global object
   (below), so the mirror must be **non-writable** for a `const` — otherwise `this.x = 1` would be the hole in
   `const`-ness. Sloppy mode makes that refusal silent rather than a throw, which is why the test asserts the
   value is unchanged rather than that an error was raised.

## 4. The mirror: `foo` and `this.foo` are one value

Every write in the global link also writes the global object, through `JsEnvMirror`. The reason is that both
records are real — a script reads `foo`, a host reads `JsRuntimeGetGlobal`, `typeof window.foo` is a thing — and a
build where they disagreed would have two answers to one question.

Two rules fall out:

- **Only the link that owns the binding mirrors.** A local binding that shadows a global writes the local and
  leaves the global alone.
- **The mirror uses `JsObjectDefineOwn`, not `JsObjectDefine`.** A `const`'s *initialization* is a write to a
  binding that is already there and already locked, so the ordinary write door would refuse it (a `const` would
  fail at its own declaration), while widening the attributes back would lose `const`-ness. `DefineOwn` sets the
  value **and** the attributes, which is exactly the operation that is needed and the only one that can do it.

**A stated divergence:** in the specification a top-level `const` is a *global lexical* binding and never becomes a
property of the global object. In this build it does, as a non-writable property — because "a global binding lives
in both the object and the environment" is this runtime's one global rule, and a second kind of global binding
would be a second rule. `Limitations.md` carries the row.

## 5. Closures

A function expression or declaration stores the environment it was **defined** in, and a call runs its body in a
child of that environment. So a function returned from a function still sees the returned-from call's locals, and
a loop's `let` gets a fresh binding per iteration because the block link is created per iteration.

`js.closure.lifetime` is the case: a counter that survives the call that created it, two closures over one
environment seeing each other's writes, and a closure whose defining call's frame is gone. The mechanism is
`Memory.md`'s "nothing frees a link" plus `JsEnv`'s parent pointer — there is no separate capture list, no upvalue
box, and no copy of a captured variable.

## 6. What is absent

- **`arguments`** as an object, so there is no aliasing between a parameter and an index.
- **TDZ.** A `let`/`const` is hoisted to `undefined` rather than to an uninitialised state, so `x` before
  `let x` reads `undefined` instead of throwing. This is the one place the environment model is coarser than the
  specification, and a `const` makes it visible: `const x;` (no initializer) is legal here and would be a
  `SyntaxError` in the specification.
- **A redeclaration error.** `const x = 1` re-executed by a loop body re-initializes rather than throwing, because
  the hoisting pass answers `RoboAlreadyExists` and tolerates it — the same tolerance a re-executed `var` and a
  re-executed function declaration need.
- **`with`**, `eval`-scoped bindings, and a global object with a proxy-like `has` trap. `Security.md` §1 keeps
  `eval` absent.
