# RoboJS — Objects (16.5)

One struct, five kinds, one ordered property list, one element list, and a prototype pointer. `RoboJSObject.h` is
the contract; this document is what the shape buys and what it costs.

---

## 1. The struct

```c
typedef struct JsObject {
    uint32_t   kind;       /* RoboJsObjectPlain | Array | Function | Error | Host */
    uint32_t   flags;
    JsObject  *prototype;
    JsProperty *properties; int32_t propertyCount, propertyCapacity;
    JsValue   *elements;    int32_t length;         /* an array's own storage */
    JsFunction *fn;                                  /* a Function */
    uint32_t    hostKind; uint64_t hostId; void *hostData; const JsHostOps *hostOps;
} JsObject;
```

Five kinds and **one struct** rather than five structs with a common header. The reason is the interpreter: every
property access, every call and every array operation goes through one layout, so there is no "is this really an
array?" branch to forget. The cost is a few unused words per object, which `Memory.md` prices.

| Kind | What it is | Its own storage |
|---|---|---|
| `Plain` | an ordinary object, an object literal, a `new`-constructed instance | properties |
| `Array` | an array literal, `[]`, `Object.create`d arrays | `elements` + `length` |
| `Function` | a script function, a native binding, an arrow | `fn` (`RoboJSFunction.h`) |
| `Error` | one of the five error kinds | properties `name`, `message`, `line`, `column` |
| `Host` | a DOM wrapper or another host object | `hostKind`/`hostId`/`hostData`/`hostOps` |

`JsObjectKindIsKnown` bounds the set. A `Host` object is how a host adds a kind of its own without the module
learning what it is — `RoboJSDom.c`'s wrappers are `Host` objects with `JsDomWrapperHostKind` and a
`JsHostOps` pair.

## 2. Properties: one ordered list, three attributes

```c
typedef struct JsProperty {
    const char *key;   /* heap-owned */
    JsValue value;
    uint32_t attributes;
} JsProperty;

#define JsPropWritable 1u
#define JsPropEnumerable 2u
#define JsPropConfigurable 4u
#define JsPropAll (JsPropWritable | JsPropEnumerable | JsPropConfigurable)
```

An **ordered array**, not a hash table. For a page's script — tens of properties on a handful of objects — a scan
is faster than a hash and, more importantly, **deterministic**: `Object.keys` and a property enumeration cannot
come out in a hash order that changed between two runs, which is the property
`js.integration.determinism` and the module's own `deterministic` configuration exist to protect. The growth rate
is a doubling capped by `JsHeapMaxProperties`, so a hostile `while (true) { obj['k' + i] = i; }` is bounded.

Three doors, and the difference between them is the whole of the property model:

| Door | Behaviour |
|---|---|
| `JsObjectSet` | a **write**: consults `hostOps->set` for a host object, then `JsObjectDefine`, and refuses a non-writable property. This is what an assignment and a `var`-less global use |
| `JsObjectDefine` | define-or-update with default attributes; refuses an existing **non-writable** property with `RoboPermissionDenied` |
| `JsObjectDefineOwn` | define-or-update with **the caller's** attributes, replacing both — the specification's `DefineOwnProperty`, and the only door that can write a non-writable property. `JsEnvMirrorAttributes` uses it so a `const`'s own initialization can set the value while keeping the lock |

The read side is `JsObjectGet` (walks the prototype chain, then asks `hostOps->get` **last**, so a wrapper that
carries a real own property of a computed name answers from storage), `JsObjectGetOwn` (this object only),
`JsObjectHas`/`JsObjectHasOwn`, and `JsObjectDelete` (compacting the array so a scan cannot see a hole).

## 3. Arrays

An `Array` keeps its own `elements` and `length`, so `a[i]` is a bounds check rather than a property scan, and
`length` is a number rather than a count of properties. The doors are `JsArrayGet`, `JsArraySet`,
`JsArrayPush`, `JsArrayPop`, `JsArraySetLength` and `JsArrayIndexOfKey` (the last is how the interpreter decides
whether a key like `"3"` is an index or a name).

Two rules worth stating:

- **A non-index key on an array is an ordinary property.** `a.name = "x"` goes to the property list; `a[3]` goes to
  the elements. The split is what makes `for (i = 0; i < a.length; i++)` cheap and `a.name` still work.
- **The length is the authority.** Setting `a.length = 2` truncates; growing it leaves holes that read as
  `undefined`; a negative, fractional or out-of-range length is a `RangeError`. `JsArraySetLength` is the one door,
  so `push` and a direct `length` write cannot disagree.

There is no `Array.prototype.map`/`filter`/`reduce`/`forEach`/`sort`/`concat`/`splice`/`shift`/`unshift` in this
build; `push`, `pop`, `join`, `indexOf` and `slice` are what exists, plus `length`. `Limitations.md` carries the
row, and there is no `Array` global either — a literal is how an array is made.

## 4. `Object.keys`, `Object.create`, and the built-in object model

`Object.keys(o)` answers an array of the object's own keys, walked in insertion order — the C door behind it is
`JsObjectOwnKeys` (bounded by the caller's array) beside `JsObjectOwnPropertyCount`. `Object.create(p)` builds an
object whose prototype is `p` (or `null`). Those two, plus the small method set on each value kind's prototype,
are the entire reflective surface; there is no `Object.defineProperty`, no getter, no `Object.assign`, no `for…in`
(the parser does not accept it) and no `Proxy`/`Reflect`.

## 5. Host objects: the `JsHostOps` pair

```c
typedef struct JsHostOps {
    Result (*get)(JsObject *object, const char *key, JsValue *out);
    Result (*set)(JsObject *object, const char *key, const JsValue *value);
} JsHostOps;
```

Two callbacks, consulted **after** the own-property list and **before** the prototype chain on a write. The three
outcomes a host may give on a set are what makes the DOM binding honest:

| Answer | Meaning |
|---|---|
| `RoboOk` | the host handled the write |
| `RoboNotFound` | not a key the host knows — the object model stores it as an ordinary property |
| anything else | the host **recognised** the key and refused the write. Returned as-is, so a failed mutation is never hidden as a stored property (`DOMBindings.md` §2) |

The host's own failure reason survives to the caller: the object model does not overwrite a pending exception when
one is already set, which is why `RoboJSInterpreter.c`'s assignment arm checks `JsRuntimeHasException` before
raising its generic "a property write failed".

## 6. What is absent

Getters and setters, `defineProperty`/`getOwnPropertyDescriptor`, `Object.freeze`/`seal`, symbols as keys,
non-string keys, accessor properties, `Proxy`/`Reflect`, and enumeration order guarantees beyond "insertion, with
integer-like keys in the array's element order". Each is a row in `Limitations.md` where a consumer could be
surprised by it, and the property attribute model already has the three flags a later phase would need.
