# RoboHTML Forms (Phase 8.14)

This document is the design record for Phase 8.14. It is normative for
`RoboHTMLForm.h` / `RoboHTMLForm.c` — the form-control model — and for what a
consumer may read from it.

Phase 8.11 already gave `form`, `label`, `input`, `button`, `textarea`,
`select` and `option` a tag identity, so the parser was building them before
this phase began — `input` was already void and `<textarea>` was already an
RCDATA text element. What was missing is everything *after* the element: a
control is only interesting because of the **state** its attributes declare.

```text
<form action="/Save" method="post" enctype="multipart/form-data">
     │
     ├── attributes    robo_html_element_form_attributes
     │    <input>      robo_html_element_input_attributes
     │    <button>     robo_html_element_button_attributes
     │    <textarea>   robo_html_element_textarea_attributes
     │    <select>     robo_html_element_select_attributes
     │    <option>     robo_html_element_option_attributes
     │
     ├── relationships  resolved through the live tree, never cached
     │    label → control   robo_html_element_label_target
     │    control → form    robo_html_element_form_owner
     │    select → option   select_option_count / option_index /
     │                      select_selected_option / option_value
     │
     └── state          declared (attribute) vs current (explicit override)
          value         robo_html_element_control_value
                        robo_html_element_set_control_value
                        robo_html_element_clear_control_value
          checked       robo_html_element_control_checked
                        robo_html_element_set_control_checked
                        robo_html_element_clear_control_checked
```

## 1. Declared and current are two different facts

This is the phase's central statement, and everything else follows from it. HTML
does not have one value per control; it has two:

| | What it is | Where it lives | Who restores it |
|---|---|---|---|
| **declared** | what the author wrote | the `value` attribute, or a textarea's children | a form reset |
| **current** | what the control is holding | an explicit override, if any | nothing |

```html
<input value="a">          <!-- declared = "a" -->
```

Setting the current value to `b` leaves the attribute saying `a` and the control
saying `b`. A model with a single value field cannot express that, so this phase
keeps the override in a per-document table beside the element that owns it and
always reports *which* answer was given:

- `robo_html_element_control_value(..., int *out_is_declared)` — the bytes, plus
  whether they are still the declared ones.
- `robo_html_element_set_control_value` — pin a current value.
- `robo_html_element_clear_control_value` — drop the override, so the value
  tracks the attribute again.

The declared value is always readable: `robo_html_element_input_attributes`'s
`value` field is the attribute verbatim, and setting a current value never
rewrites attributes.

The same shape applies to checkedness, and the two overrides are **independent**
— setting a value never disturbs checkedness and vice versa, because in HTML
they are separate dirty flags. Checkedness is modelled for `<input>` only, and
only when the input's *effective* type is checkable (checkbox, radio); every
other element reports `ROBO_NOT_SUPPORTED` rather than a default, so
`<input type="text" checked>` honestly has an attribute that is preserved and no
checkedness to report.

The consequence worth stating plainly: for a control that has *not* been
overridden, changing the `value` attribute changes the current value (it is
still tracking it), while for one that has, it does not. That is not an
inconsistency to be tidied away — it is the entire content of the dirty-value
flag, and the suites pin both halves.

The override is not a convenience cache: it is the only place a current value
lives, and it is released with the document, so a value cannot outlive the tree
that owns it.

All four state calls log at `DEBUG` (`robo_html: control value set / cleared`,
`control checked set / cleared`) with the document id, the control's tag and —
for a value — its byte length. Never the bytes: a password field's value is user
data, and a serial log is the last place it belongs. Length is the honest, safe
fact to record.

## 2. Nothing is cached, so nothing can go stale

8.14.15 asks that a mutation never leave a stale association, and the answer is
that there is nothing to keep in step. A form owner, a label's target, a
select's option order and a select's selected option are all **derived on demand
from the live tree**:

- move a control out of a form and its owner changes, because the next lookup
  walks the new ancestry;
- remove a form and a control's owner becomes its next ancestor form, not a
  dangling reference;
- retarget a label's `for` or remove the control it names and the next call
  describes the tree as it is now.

The only stored state is the two explicit overrides of section 1, and those
belong to one node *by identity* rather than to a relationship. No mutation hook
exists because no cache exists.

## 3. `present` versus `usable`, and `NONE` versus `UNKNOWN`

Attribute views borrow their bytes from the element and stay valid only while
the element's attributes are unchanged — the same rule as
`robo_html_element_attribute_value`, which these are typed views over.

Two distinctions carry the phase:

- **`present` vs `usable`.** `present` means the attribute record exists at all;
  `usable` means it names something once surrounding ASCII whitespace is
  ignored. So `<form action>` is present and unusable, `<form action="">` is
  present and unusable, and `<form>` is neither. That is why `action_usable` is
  reported rather than inferred from a non-zero length.
- **`NONE` vs `UNKNOWN`.** `NONE` is "no attribute", `UNKNOWN` is "an attribute
  naming something this runtime does not recognise". Both would submit as GET in
  a browser, but that defaulting is a *submission* rule, and submission is not
  this phase, so the model reports what the document said and lets a later phase
  apply the default. An empty or absent value is `NONE`, never `UNKNOWN`.

Classification is case-insensitive and whitespace-tolerant, because the HTML
rule ignores surrounding whitespace, and the raw bytes are still exposed for
anything the classification discards.

**Reading the wrong element is a refusal, not an empty view.** Asking
`robo_html_element_form_attributes` for a `<div>` is `ROBO_NOT_SUPPORTED`, not a
form with no action — otherwise a caller could mistake "not a form" for "a form
with nothing to do".

## 4. The vocabularies degrade safely

An `<input type>` this phase does not model classifies as `UNKNOWN` and
*degrades* to `TEXT` through `robo_html_input_type_effective`, which is the HTML
rule for an unrecognised type. `<button type>` does the same, degrading to
`SUBMIT`. Nothing is destroyed by the degradation: the element keeps its
attributes, its name and its place in the tree, and behaves as the default
control. The view reports both facts — `type_declared`, `type_effective` and
`type_recognised` — so a caller can tell "the author said `text`" from "the
author said `date` and we treat it as text".

`TEXT` and `SUBMIT` are `0` on purpose. A zeroed view is therefore a text input
and a submit button, which is the HTML default, so the zero value and the
language default agree instead of needing a special case.

The input types modelled are `text`, `password`, `checkbox`, `radio`, `number`,
`email`, `submit`, `button`, `reset`, `search` and `hidden`. `range`, `color`,
`date`, `time`, `file` and anything future degrade to `text`.

## 5. Numbers are parsed, not compared

`min`, `max` and `step` carry the same present/valid split the image module's
declared dimensions do, and it is what lets a caller tell `<input min>` from
`<input min="abc">` from `<input>`:

- `_present` — the attribute record exists;
- `_valid` — and it parsed as a number this model accepts;
- `_value` — meaningful only when `_valid`.

An invalid number is **reported rather than dropped**: a document that says
`min="ten"` has said something, and silently discarding it would hide a real
authoring error from every later phase, including the one that finally
implements validation. Nothing is compared, so `<input min="10" max="1">` is
reported faithfully rather than flagged.

`rows`/`cols` on a textarea are non-negative integers bounded by
`ROBO_HTML_FORM_MAX_DECLARED_LINES`; a sign, a fraction, an empty value or
anything non-numeric is invalid rather than defaulted, so a typo cannot silently
resize a control.

Boolean attributes are presence tests: `<input checked>`,
`<input checked="checked">` and `<input checked="">` all report `1`, which is
the HTML rule and the reason those fields are `int` and not pointers.

## 6. `<textarea>`, `<select>` and `<option>`

**A textarea has no `value` attribute.** Its declared value is its own child
text (reachable with `robo_html_node_subtree_text`) and its current value is
`robo_html_element_control_value`, which reads the children when no override is
in effect. The attributes view therefore has no `value` field, because modelling
one would invent something the element does not have.

**Options are counted and ordered by a document-order walk of the subtree**, not
by inspecting direct children, so an option nested in an `<optgroup>` still
counts and the index `robo_html_element_option_index` reports always agrees with
the count `robo_html_element_select_option_count` reports.

**An option's value falls back to its text.** `robo_html_element_option_value`
returns the `value` attribute when one is present and the option's own subtree
text otherwise — the real HTML rule, and the reason an option with no `value`
still means something. `out_is_explicit` says which of the two answered, so a
caller never has to guess whether the text *is* the value or merely looks like
it. The text is taken verbatim, not whitespace-folded: folding is a
text-processing decision that belongs with the code that renders or submits the
value, not with the code that reads it.

**The selected option is determined, not guessed**, in this order:

1. the first option carrying the `selected` attribute;
2. otherwise the first option that is not `disabled`;
3. otherwise there is none (a valid, empty select).

Step 2 is the HTML "ask for a reset" default expressed as a plain rule: a select
with no `selected` shows its first selectable option, and picking a `disabled`
one would be a state a user could never reach. A `multiple` select reports its
first selection here rather than a set — this phase models selectedness, not a
selection list. An `<option>`'s *current* selectedness is not modelled as an
override; it follows the attribute.

## 7. A label resolves to the control it names

`robo_html_element_label_target` is deliberately this small:

1. a usable `for` attribute naming an id → the first element in document order
   with that id, *when that element is labelable*
   (`robo_html_tag_is_labelable`);
2. otherwise the first labelable descendant of the label, in document order;
3. otherwise there is none, which is a valid state: a `<label>` wrapping only
   text labels nothing.

This is a stated simplification of the HTML algorithm rather than a claim to
implement it. The spec's labelable test also excludes `<input type=hidden>`, and
the id search here is the document walk rather than a maintained id index; both
differences are visible in the result rather than hidden behind it.

`for` is not the start of a fallback chain in the other direction: when a usable
`for` names an element that is **not** labelable, steps 2 and 3 are not
consulted, because the author named something and the honest answer is that the
name does not resolve to a control. `out_from_attribute` says which step
answered.

## 8. A control's form owner is derived, and the source is reported

`robo_html_element_form_owner` applies:

1. a usable `form` attribute naming an id → the first element in document order
   with that id, when that element is a `<form>`;
2. otherwise the nearest ancestor `<form>`;
3. otherwise there is no owner — a valid state, and what this reports rather
   than an error.

Step 2 is a *fallback*: when a `form` attribute names something missing or not a
form, this model resolves through ancestry instead of refusing. Refusing would
silently detach a control the author clearly meant to belong to the enclosing
form, and this phase has no submission to be misled by. `out_source` records
which rule answered — `ATTRIBUTE` (a deliberate author reference that survives a
move) or `ANCESTOR` (a positional fact a move changes) — so "why is this input in
that form?" stays answerable from the outside.

Only form-associated elements (`robo_html_tag_is_form_associated`) are accepted:
a `<div>` inside a `<form>` is not owned by it, and asking would be a category
error rather than a lookup that happens to fail.

## 9. What a consumer reads, and what the picture used to report

A consumer reads a control's *state* through this API — whether a checkbox is
checked, what a text field is holding, how many options a select has — and never
re-derives it from the markup, which is what makes a report of the model a fact
about the DOM rather than a second opinion about it.

Phase 8.2's visual demo adapter was the consumer that showed this in pixels: it
painted a control as a field block whose shape differed by state (a muted colour
for a placeholder-only or disabled control, a brand mark inside a checked box, a
chevron on a select). That adapter was deleted in Phase 11.13 along with the rest
of the demo layer, so the *rules* below are recorded here as what the model
offers a painter, and the painter is now whoever consumes a display list:

- a value-holding control has a declared value and a current one, and the pair is
  what "placeholder" is distinguishable from;
- an `<option>` is not a separate thing to paint; a select's options are its own
  state, and the selected one is what a consumer reports;
- `disabled` is a property of the control, not a rendering instruction — a
  consumer decides what it looks like;
- a control's identity (its tag, name and attributes) survives every state read,
  which is the seam the model suite pins.

## 10. What this phase is not

Submission, HTTP, encodings, form data, validation, constraint checking, a file
input, a date/time/range/color control, user interaction, `input` / `change` /
`submit` / `reset` events, JavaScript bindings, CSS, layout, an accessibility
tree, `form.elements` / named access, `checkValidity`, or a form-reset
*operation* (the reset rule is what the declared value is *for*, and this phase
only guarantees that the declared value survives). A value changes only because
an API call changed it.

## 11. Test coverage

| Suite | Tests |
|---|---|
| `SuiteRoboHTMLForm.c` | `html.form.parser_element`, `form_attributes`, `method_enctype_vocabulary`, `non_form_refused`, `input_types`, `input_attributes`, `input_value_state`, `input_checked_state`, `input_numeric_constraints`, `button`, `textarea`, `select_options`, `option_value_fallback`, `label_target`, `owner_nested`, `owner_attribute`, `owner_not_supported`, `parser_malformed`, `void_input`, `mutation`, `mutation_options`, `lifecycle`, `destroyed_state`, `structure`, `unicode` |
| `desktop/tests/SuiteRoboDesktopScene.c` | `desktop.scene.system_face`, `desktop.scene.open` — a document built from markup reaches a frame through the real pipeline, which is where a consumer's reading of the model becomes a picture |

## 12. Deliberate boundaries worth remembering

- **The declared/current split is per node identity, not per relationship.** The
  overrides live in a per-document table keyed by the element, so moving a
  control — even to another form — never loses or transfers them, and destroying
  a document releases every entry with the tree.
- **Setting an override on a closed document is allowed.** The document
  lifecycle gates *construction*, not state; a parsed page is exactly the state a
  control is mutated in.
- **Nothing resolves a URL.** `action` is read verbatim and never fetched; a
  reference's meaning belongs to `RoboHTMLAnchor.h`'s resolver, and duplicating
  it here would be a second, worse parser.
- **The adapter's field style is temporary.** Like 8.13's link styling, it is
  adapter behaviour that exists to make the model visible; styling a control is
  CSS's job in a later phase, and no other part of the runtime may infer a visual
  contract from it.
