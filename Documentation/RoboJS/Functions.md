# RoboJS — Functions (16.6)

A script function and a native binding differ in **exactly one field**. That is the module's central claim about
functions, and `RoboJSFunction.h` is its contract.

---

## 1. One callable shape

```c
typedef Result (*JsNativeFn)(JsRuntime *runtime,
                             JsObject *thisObject,
                             const JsValue *args, int32_t argCount,
                             JsValue *out, void *userdata);

typedef struct JsFunction {
    uint32_t    kind;      /* JsFunctionKind */
    uint32_t    arity;     /* the declared parameter count, as `length` reports it */
    uint32_t    flags;     /* JsFunctionFlag* — an arrow is a flag, not a second struct */
    const char *name;      /* heap-owned, `""` when anonymous */
    const char **params; int32_t paramCount;   /* script function */
    JsNode     *body;      /* script function: the block to run */
    JsEnv      *closure;   /* script function: the defining environment */
    JsNativeFn  native; void *userdata;        /* native binding */
} JsFunction;
```

| Kind | Meaning |
|---|---|
| script | `body` + `closure` + `params`: a `JsNode` to run in an environment |
| native | `native` + `userdata`: a C callback |

`JsFunctionCall`/`JsFunctionConstruct` are the **only** doors that call anything, and they dispatch on `kind`. So
`Function.prototype.call` and `apply` accept a native, the DOM binding's `addEventListener` accepts either, and a
listener list needs no notion of which it holds. A build with one call door per kind would have two places for
`this`, the return value and an exception to be handled differently.

Three things the call door owns:

- **`this`.** `JsFunctionCall(runtime, callable, thisValue, args, argCount, out)` takes the receiver explicitly;
  a plain call passes NULL (meaning `undefined`), a method call passes the object. An arrow function **ignores**
  the receiver it is given and uses the one captured at creation (`Closures.md` §3), which is why the arrow-ness
  is a stored flag rather than something inferred from the node it was built from.
- **`arguments`.** Not implemented: a script function reads its declared parameters only. The receiver and the
  parameter list are the whole shape; `Limitations.md` carries the row, and the reason is that `arguments` is an
  object whose *aliasing* rules are a specification of their own.
- **The exception.** A `throw` inside a callee is **not** a `RoboError` on the return: the call answers the
  callee's own failure code (often `RoboError`) with the runtime's pending exception set, and it is the caller's
  job to propagate. That split is what makes `try`/`catch` work across a call boundary (`Exceptions.md` §4).

## 2. `new`, and the prototype it produces

`JsFunctionConstruct` calls the function with a **fresh plain object** as `this`, whose prototype is the
constructor's own `prototype` property (read off the function object, exactly as the specification does — a
function does not carry the instance prototype structurally). If the callee returns an object, that object is the
result; otherwise the fresh one is. There is no `new.target`, no `Reflect.construct` and no `Symbol.hasInstance`.

`js.function.declaration` asserts the observable parts: a `new`-constructed instance sees its methods through the
prototype, `instanceof` answers through the same chain, and a method on the instance shadows the prototype's.

## 3. Arity and `length`

`arity` is the declared parameter count, exposed as the function's `length` property — for a native, the number
the creator passed; for a script function, `paramCount`. A **missing** argument is `undefined` and an **extra** one
is ignored, which is the specification's rule and the reason a native binding tests `argCount` before reading
`args[i]` rather than assuming a count.

`JsFunctionCreateNative(heap, name, arity, native, userdata, prototype)` creates a native; the name is what
`toString` and the diagnostics use, and it is heap-owned.

## 4. What the interpreter does with a script function

Calling one:

1. builds an environment (`RoboJsEnvFunction`) whose parent is the function's `closure`, **not** the caller's
   environment — which is lexical scoping, and the whole of `Closures.md`;
2. declares each parameter in that link (`JsEnvDeclare`), then hoists the body's `var`s and declarations into the
   neighbouring function link (`JsEnvDeclareVar`);
3. runs the body and turns its completion into a value: `Return`'s value, or `undefined` for a body that fell off
   the end.

The call depth is counted (`maxCallDepth`, 64) and exhaustion is a `RangeError` rather than a C stack overflow —
the interpreter is recursive, so this is the same discipline as the parser's `maxParseDepth`.

## 5. Where functions live

`Function.prototype` is created by `JsRuntimeCreate` and is reachable through `JsRuntimeFunctionPrototype`. Its
methods are `call` and `apply`; there is no `bind`, no `length`-preserving partial application and no `arguments`
object. A native binding's prototype is the host's choice — the DOM binding puts its node methods on the node
wrapper objects directly.

## 6. What is absent

`arguments`, rest parameters, default parameter values, destructuring parameters, `bind`, generators, `async`
functions, `new.target`, getters/setters on a function, and function names as first-class
(`fn.name` is readable from the stored name for a declared function and empty for an anonymous expression). Each
is a row in `Limitations.md`; each *would* be a change to the one callable shape rather than a second one.
