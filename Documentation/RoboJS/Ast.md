# RoboJS — The AST (16.3)

`RoboJSAst.h` is one node struct, one shape table, and a printer a test can diff. Three claims it makes:

1. **One struct, not one struct per kind.** A `JsNode` is `kind` + `flags` + `span` + `op` + a number, a string, a
   boolean, a parameter list, three named children (`first`/`second`/`third`) and one variable-length `list`.
2. **The shape is data.** `JsNodeShapeOf(kind)` answers how many children a kind has and whether it uses `list`;
   the printer, the validator and the interpreter all read that one table instead of switching per kind.
3. **A tree the interpreter cannot run is a tree the parser cannot build** — `JsAstIsWellFormed` is the door that
   makes that a checked fact rather than an intention.

---

## 1. The node

```c
typedef struct JsNode {
    uint32_t kind;      /* JsNodeKind */
    uint32_t flags;
    JsSpan span;        /* where it came from — every diagnostic reads this */
    JsTokenKind op;     /* the operator token for a Unary/Binary/Logical/Assignment/Update node */
    int32_t intValue;   /* an index, a count, a case count */
    double number;      /* a NumberLiteral */
    const char *text;   /* heap-owned: an identifier, a literal, a key */
    int32_t textLength;
    int boolean;        /* true/false, and the declared kind for a VariableDeclaration */
    const char **params; int32_t paramCount;   /* a function's parameters */
    struct JsNode *first, *second, *third;
    struct JsNode **list; int32_t count, capacity;
} JsNode;
```

Three things the layout buys:

- **`span` on every node** means a runtime error raised while evaluating a node can name the line and column the
  node came from, without a side table mapping nodes to positions.
- **`op` as a token kind** means the interpreter has *one* arithmetic arm for `+ - * / %`, one comparison arm, and
  one assignment arm, each switching on the operator — instead of a node kind per operator. That is why the parser's
  precedence climb and the interpreter's operator table can be read side by side against `LanguageSubset.md` §2.
- **`list` for the variable-arity kinds** (a program, a block, a parameter list, an argument list, an array/object
  literal, a switch's cases) keeps the fixed-arity kinds free of a second indirection.

The kinds are `RoboJsNode*` in `RoboJSTypes.h` (42 of them), and `JsNodeKindIsKnown` bounds the table so
`JsGetCapabilities` can report `nodeKindCount` without hard-coding it.

## 2. The printer, and why it exists

```c
int32_t JsAstPrint(const JsNode *node, char *buffer, size_t capacity);
int32_t JsAstWrite(const JsNode *node, char *buffer, size_t capacity, int indent);
```

Both are **complete-or-absent**: they write a NUL-terminated line when it fits and leave the buffer empty when it
does not, answering the length a complete line needs. A test that diffed a truncated tree would be diffing the
buffer size rather than the parse, which is the failure this shape prevents.

`js.parser.ast` prints a program that exercises every statement and expression form in `LanguageSubset.md` and
compares the text. That is what makes "the parser builds the tree the document describes" a diffable fact rather
than a claim — and it is why a new node kind is not done until the printer knows it.

`JsAstWrite`'s `indent` is a budget, not a promise: nested lists are broken across lines while the budget lasts
and flattened once it runs out, so a hostile tree cannot make the printer allocate without bound.

## 3. The shape table

```c
typedef struct JsNodeShape {
    const char *name;
    int childCount;   /* how many of first/second/third are meaningful */
    int usesList;     /* whether `list` is the child list */
} JsNodeShape;
```

`name` is what a diagnostic and the printer both use, so a node kind's spelling exists once. `childCount` and
`usesList` are what `JsAstIsWellFormed` walks: a node whose `childCount` disagrees with its kind is malformed. The
validator answers about a whole subtree under `JsAstCount`'s bound, which is the pair a test uses to say "this
parse produced a tree the interpreter can run".

## 4. Ownership

A node, its `text`, its `params` entries and its `list` all come from the runtime's heap (`RoboJSHeap.h`), so **a
tree is released when the heap is** and nothing in the module frees a node. `JsNodeAppend` grows a `list` with
`JsHeapRealloc`, which means an append is `RoboOutOfMemory` at the runtime's limits rather than a reallocation
failure the caller has to interpret.

`JsRuntimeParse` is the door that hands a tree to a caller, and `JsRuntimeEvaluateProgram` is the one that
evaluates it — so a host may parse once and evaluate many times, or parse and never evaluate (which is what a
syntax check is).

## 5. What is absent

No bytecode, no IR, no optimisation pass, no constant folding and no scope analysis beyond what the interpreter
does as it runs. A `class`, a template literal or a destructuring pattern has no node kind because the lexer
refuses the token first (`Lexer.md` §3) — the absence is one layer up, and `LanguageSubset.md` §3 and §4 are the
list. `JsAstIsWellFormed` is therefore a *closure* property: it can only be satisfied while every kind in the
table has an evaluator arm, which is what makes "parsed but not runnable" impossible rather than unlikely.
