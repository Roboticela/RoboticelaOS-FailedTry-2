# RoboCSS Inheritance (9.7)

The cascade (9.5) answers *which declaration wins for this element*. Inheritance
answers the question underneath it: *and what about the properties this element
says nothing about?* For a property that inherits by default the answer is the
parent's resolved value; for one that does not, it is the property's own initial
value. The result is a **resolved style**: one entry per property the element has
an answer for, each carrying where the answer came from.

```
DOM element
    ↓  selector matching      9.4
    ↓  specificity            9.6
    ↓  cascade                9.5
inheritance                   9.7, here
    ↓  resolved style
computed values               9.8
```

Inheritance runs **after** the cascade and never competes with it. A value this
element declares always beats a value it could have inherited — including an
`!important` declaration on the *parent*, which has no way to reach a child,
because importance is a fact about a declaration on **this** element and
inheritance is not a declaration at all.

## The three sets, in the order they are walked

`robo_css_inherit_resolve` fills the output from three sources, and the order is
the order it reports them in:

1. **What the element declares.** Every property its own cascade declares, one
   entry each, taking the *winner* through the cascade's own resolver rather than
   re-deciding precedence. The CSS-wide keyword the winner carried is obeyed here.
2. **What the parent carries**, for the properties that inherit by default and
   that the element did not declare. This is what inheriting *is*.
3. **The initial value**, for every inherited-by-default property the registry
   knows that neither of the above supplied. This is where a root element's
   inherited properties come from, and it is what a caller who passes no parent
   is asking for.

Set 3 is deliberately **only** the inherited-by-default properties.
Materialising `width: auto` on every element because nobody declared it would put
a value in a resolution that no input produced; a later stage can reach the
registry's initial value for that. An element with no declarations at all
therefore still resolves to a complete style — defaults are answers, not gaps.

A property that does **not** inherit and that nothing declares has **no entry**.
The entry appears the moment something asks for it: a declaration, `initial`,
`unset`, or `inherit`.

## The CSS-wide keywords

`inherit`, `initial` and `unset` are instructions about *where to get* a value,
not values. They are recognised on a parsed value (`robo_css_wide_keyword_of`),
never on a string, and a keyword is a **single identifier and nothing else**:
`inherit` is one, `inherit inherit` is not, `red inherit` is not, and
`calc(inherit)` is not. The comparison is made on the decoded identifier, so
`\69 nherit` is the keyword — an escape changes which bytes spell a name, never
which name it is.

| Keyword | On an inherited property | On a non-inherited property |
|---|---|---|
| `inherit` | the parent's value | the parent's value (that is the point) |
| `initial` | the property's initial value | the property's initial value |
| `unset` | like `inherit` | like `initial` |
| `revert` | **recognised, not obeyed** — see below | same |

When `inherit` (or `unset` on an inherited property) has no parent value to take,
the answer is the property's initial value, because the parent's *computed* value
for an undeclared property is its initial value. That is not a failure.

### `revert` is carried, not obeyed

Rolling `revert` back means re-running the cascade with one origin's declarations
removed, which is a question about the cascade's **inputs** rather than about
inheritance. Answering it half-way — treating it as `unset` — would be wrong in
exactly the cases it exists for. So a `revert` declaration becomes a **deferred**
entry: the value is the keyword itself, the entry is counted, and `unsupported`
is set. It is never silently resolved into something that looks like an answer.
`revert-layer` is not recognised at all, because cascading layers do not exist in
this model.

## Where a value came from

Every entry records its provenance, because once a value is a copy the copy does
not remember:

| Source | Meaning |
|---|---|
| `DECLARED` | the element's own cascade produced it (an ordinary value) |
| `PARENT` | the parent's resolved style supplied it — by default inheritance or by a keyword |
| `DEFAULT` | the property's initial value supplied it |
| `DEFERRED` | the `revert` keyword, carried unresolved |

The keyword is recorded *beside* the source, so "the parent's colour because
`color` inherits" and "the parent's colour because someone wrote `color: inherit`"
stay distinguishable — the same value by two different routes.

## Custom properties

A custom property (`--x`) inherits **always**, whatever a registry says, and it is
compared byte-exact where an ordinary property name is compared case-insensitively
(`robo_css_is_custom_property_name` is the single naming rule both the registry and
this module consult). It is also the one family with **no initial value**: an unset
custom property is *invalid*, not `initial`, so one that neither the element nor
the parent declared simply has no entry and stays that way however deep the tree
goes. A custom property that neither the element nor the parent declared has no
entry at all. `var()` substitution is not performed — the declaration's value is
preserved as parsed.

## The property registry (9.7)

`RoboCSSPropertyRegistry.h` is the authoritative *metadata* about a property, and
it is deliberately **not** a second property table:

```
robo_css_property_table_t      identity, per caller, filled by the caller
robo_css_property_registry_t   classification + initial values, CSS's own
```

The registry issues no ids, resolves no names to identities and holds no
per-caller state. It answers the two questions inheritance asks about a property
it has already identified:

* **does it inherit by default?** — `INHERITED`, `NON_INHERITED`, or `UNKNOWN`;
* **what is its initial value?** — stored as text *and* as the component-value
  tree that text parses to, built at registration time through the same grammar a
  declaration goes through, so inheritance never reparses an initial value per
  element and an initial value cannot be a different dialect of CSS.

The standard set (`robo_css_property_registry_create_standard`) is the sixteen
inherited properties these phases have any business knowing (colour and the font
and text properties) and the common non-inherited ones (the box properties,
`display`, `position`). A text that does not parse is **refused at registration**,
not stored: a broken initial value is a defect in the registry and should surface
where it was written.

The two objects meet through `robo_css_property_registry_apply_to_table`, which
registers every classified property into a caller's table with the right
`ROBO_CSS_PROPERTY_FLAG_INHERITED` bit — the supported way to build a table, and
the reason the classification and the table's flag cannot drift. A caller that
fills its own table by hand is still heard: a property the registry has never
heard of inherits when the table's flag is set. The precedence is stated once, in
the resolution path:

1. a custom property inherits;
2. otherwise the registry's classification, when it has one;
3. otherwise the caller's table flag;
4. otherwise it does **not** inherit.

Step 4 is a decision rather than a silence: CSS drops a declaration for a property
it does not recognise, and propagating a value for a property nothing understands
would spread it into every descendant.

## Ownership

A resolved style owns everything it reports: each entry's name is a copy and each
entry's value is a **deep copy** of whatever it came from (`robo_css_value_copy`).
The parent's style is read and never modified, the cascade is read and never
modified, and a style can outlive every sheet and cascade it was resolved from.
Copying rather than borrowing is deliberate — an inherited value that aliased its
parent's would be freed through the parent's disposal and the two styles would
stop being independent objects.

One dependency remains and is documented rather than hidden: a value carries
slices into the bytes that declared it, so an entry's *name* and *value* are
copied (freeing them from the sheet's and the parent's lifetimes) but the bytes
inside that value still belong to the stylesheet or source the declaration came
from. A resolved style therefore stays valid as long as the sheets it was resolved
from — a lifetime the caller already owns because it owns them.

## The primitive door and the chain door

`robo_css_inherit_resolve` takes its parent as an **argument**. This module never
walks a tree looking for a parent of its own accord and never keeps a global
"current style", so a resolution is a pure function of its inputs, testable one
element at a time, and free of caches that would need invalidating. Nothing is
stored between calls, so nothing can go stale.

`robo_css_style_context_resolve_inherited_style` is the door a caller normally
wants. It walks from the element **up** to the root through the host's `parent`
callback, then resolves **top down**, so each level has its own *resolved parent*
in hand rather than the nearest ancestor that happened to declare something. The
walk is bounded by `ROBO_CSS_INHERIT_MAX_DEPTH` (64) and hitting the bound is
*reported* (`unsupported`), because the top of a capped chain is not a root and
pretending it was would produce a wrong answer that looked right. A cycle among
parents is caught by the same bound rather than by detecting it — this module only
ever asks for one element's parent.

A context with no element host, or one whose `parent` callback is `NULL`, is still
usable: the walk finds no ancestor and the element resolves as a root. That is not
a failure — a host that cannot answer "what is your parent" has told this module
the one thing it needs to know.

## Invalidation

There is no invalidation engine in this phase, and the boundary is explicit:
because nothing is cached, the rule is **re-resolve**. Changes to a parent's
cascaded style, a parent's inherited properties, or a child's declarations may
change descendants, and the caller re-resolves. 9.8 kept that shape exactly: it
computes a resolved style per level as it walks down, and caches nothing, so its
invalidation boundary is the same sentence.

## What is deliberately absent

* **No computed values.** `10px` is still `10px` and `red` is still `red`.
  Percentages, relative lengths, `calc()` and colour normalisation are the next
  stage's (`docs/ComputedValues.md`); the box model is later still.
* **No `var()` substitution.** Custom properties cascade and inherit as values;
  their contents are not substituted.
* **No `revert` semantics.** Carried and reported, not obeyed.
* **No inheritance engine over a DOM.** The caller supplies the parent; this
  module never touches a document.

## API

`RoboCSSInherit.h`:

* `robo_css_wide_keyword_of` / `_name` / `_is_known` — classify a value.
* `robo_css_inherited_source_name` / `_is_known` — name a provenance.
* `robo_css_inherit_resolve` — the primitive: `(context, cascade, parent, out)`.
* `robo_css_inherited_style_init` / `_dispose` / `_count` / `_at` / `_find` /
  `_find_property` / `_has` / `_is_complete` / `_element` / `_has_parent`.
* `robo_css_inherited_property_name` / `_value`.
* `robo_css_style_context_resolve_inherited_style` (RoboCSSStyle.h) — the chain
  door.

`RoboCSSPropertyRegistry.h`:

* `robo_css_property_registry_create` / `_create_standard` / `_destroy`.
* `robo_css_property_registry_register` / `_find` / `_find_slice` / `_at` /
  `_count`.
* `robo_css_property_registry_apply_to_table`.
* `robo_css_property_definition_inherited` / `_initial_value`.
* `robo_css_property_inheritance_is_known` / `_name`.
* `robo_css_is_custom_property_name`.

## How the tests are run

* `scripts/robo test-runtime` — the whole suite.
* `./Out/RoboTest.host.exe --filter "css.inherit."` — the inheritance suites.
* `./Out/RoboTest.host.exe --filter "css.registry."` — the registry suite.

### A note on stylesheet lifetime in the suites

The module keeps a **fixed table of live stylesheets**
(`ROBO_CSS_MAX_STYLESHEETS`). A suite that made a sheet per test and never
released it would exhaust that table partway through a combined run and start
failing for a reason with nothing to do with what it tests. The cascade fixture
therefore **owns what it makes and releases it in its `end`**
(`css_cascade_begin` / `css_cascade_sheet` / `css_cascade_end` in
`RoboCSS-test-support`), and a test that destroys a sheet itself must do so
*before* the fixture's end.
