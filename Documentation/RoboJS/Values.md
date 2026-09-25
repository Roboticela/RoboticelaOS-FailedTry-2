# RoboJS — Values (16.4)

`JsValue` is the module's primitive, and it is a **by-value struct**: six kinds, a tag, and the payload beside it.
`RoboJSValue.h` is the whole contract.

---

## 1. The struct

```c
typedef struct JsValue {
    uint8_t  type;      /* RoboJsValue* */
    uint8_t  flags;
    double   number;    /* a Number */
    const char *string; int32_t stringLength;   /* a String, heap-owned */
    JsObject *object;   /* an Object, a function, an array, an error */
} JsValue;
```

Six kinds: `Undefined`, `Null`, `Boolean`, `Number`, `String`, `Object`. There is no `Symbol`, no `BigInt` and no
`Int32` — `TypedArray`/`Symbol` are in `JsCapabilities`' absent block, and a build that had a seventh kind would
have to say so there.

**Why by value.** An expression arm returns a `JsValue`, not a pointer with an owner. That is what makes the
interpreter a tree of `return JsValueNumber(x + y);` lines with no box allocation, no refcount, and no
question of who frees a temporary — a temporary is a stack slot. The cost is that a string is a (pointer, length)
pair rather than a header-prefixed buffer, so `JsValueString` is the only constructor and the bytes come from the
heap (`Memory.md`).

`JsValueObjectOrUndefined(NULL)` exists because "this host has no answer" and "this node is null" are different
values, and a wrapper door that collapsed them would make a missing parent look like a null one.

## 2. Predicates, not casts

`JsValueIsUndefined`, `…IsNull`, `…IsNullOrUndefined`, `…IsBoolean`, `…IsNumber`, `…IsString`, `…IsObject`,
`…IsFunction`, `…IsArray`, `…IsInteger`. Each takes a `const JsValue *` and answers `int`, so a caller reads
better than it writes: `if (JsValueIsNullOrUndefined(&args[0]))`.

`JsValueAsBool`/`…AsNumber` are the **unchecked** readers for a caller that has already tested the kind;
`JsValueToBoolean`/`…ToNumber`/`…ToString` are the specification's coercions and are total. The split matters:
the interpreter's `+` must call `ToNumber` (a string may coerce), while a native binding that wants the second
argument's number may test first and read unchecked.

## 3. The three conversions

| Door | Rule |
|---|---|
| `JsValueToBoolean` | `undefined`/`null`/`false`/`0`/`NaN`/`""` are false; **everything else is true**, including `"0"` and an empty array |
| `JsValueToNumber` | the specification's: `null → 0`, `undefined → NaN`, `"" → 0`, a non-numeric string → `NaN`, `true → 1`, a string with surrounding whitespace trimmed; an object goes through `valueOf`/`toString` when it has them |
| `JsValueToString` | total, and **truncating**: it writes what fits, NUL-terminates, and answers the bytes written. A caller that needs the whole string must size the buffer itself — which is why this door is for a diagnostic or a short rendering, not for a conversion a script can observe |

The *diagnostic* writers (`JsRuntimeDiagnosticAt`, the AST printer) are the ones that are complete-or-absent, because a half-written diagnostic is worse than none. `JsValueToString` is deliberately the other shape: it is called from a failure path that must not itself be able to fail.

`NaN` and `Infinity` are values of the `Number` kind, not globals the module installs: `JsMathNan()` and the
`Number`/`Math` builtins produce them. `JsValueIsInteger` is the predicate a native binding wants before treating
a number as an index, and it answers 0 for `NaN`, for a fraction, and for a magnitude outside `int32`.

## 4. Both equalities, and the one that concatenates

```c
int JsValueStrictEquals(const JsValue *a, const JsValue *b);   /* === */
int JsValueLooseEquals(const JsValue *a, const JsValue *b);    /* == */
int JsValuePlusConcatenates(const JsValue *a, const JsValue *b);
```

`===` compares the kind first and never coerces; a string is compared by its bytes *and* its length, and two
objects are equal only when they are the same pointer. `==` is the specification's abstract equality over the six
kinds, and it is the one that answers `0 == "0"`, `null == undefined` (true) and `NaN == NaN` (false).

`JsValuePlusConcatenates` is a separate door because `+` is not addition: it is *asked* whether the pair
concatenates (either operand a string after `ToPrimitive`) and then either concatenates or adds. Putting that
decision behind a named predicate is what keeps the interpreter's `+` arm one line and keeps `LanguageSubset.md`
§2's "both equalities are real and differ" a testable statement — `js.value.equality` and
`js.value.conversions` are the cases.

## 5. What is absent from the value model

- **No boxed primitives.** `new Number(1)` produces an object whose `valueOf` is the primitive; the module does not
  maintain a distinct boxed representation, which is why `JsValueAsNumber` on the *object* is not a thing.
- **No reference counting and no weak values.** `Memory.md` §2.
- **No tagged pointer or NaN-boxing.** The struct is larger than a `double` and that is the trade: a readable
  layout for a tree-walking interpreter, not a compact one for a JIT (`jit` is 0).
- **No `valueOf`/`toString` on every object.** An object's conversion goes through its prototype when the prototype
  has the method; without one, `ToString` answers `"[object Object]"` and `ToNumber` answers `NaN`, which is the
  specification's fallback rather than a special case here.
