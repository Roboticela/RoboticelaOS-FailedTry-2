/*
 * RoboCSS property registry (Phase 9.7) — css.registry.*
 *
 * The registry is the phase's one piece of *data* rather than machinery, so this
 * suite is mostly a table check — and a table check is only worth writing if it
 * asserts the rows rather than the shape. "Every property has a classification"
 * would pass for a registry that classified all twenty-four properties the wrong
 * way round; what is asserted here is that `color` inherits and `width` does not,
 * by name, and that each property's initial value parses to the value the row
 * spells.
 *
 * The other half is the registration contract, because the registry is the first
 * RoboCSS object that can be built *by a caller* and then handed to a resolution:
 * a name it accepts and a table refuses, or an initial value that is stored
 * unparsed, would both surface much later as a mystery rather than here.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void TestCssRegistryLifecycle(void)
{
    CssPropertyRegistry *registry = NULL;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryCreate(&registry, NULL), RoboOk);
    RoboCheck(registry != NULL);
    /* An empty registry classifies nothing, and saying so is not an error. */
    RoboCheckEqInt(CssPropertyRegistryCount(registry), 0u);
    RoboCheck(CssPropertyRegistryFind(registry, "color") == NULL);
    RoboCheck(CssPropertyRegistryFind(NULL, "color") == NULL);
    RoboCheck(CssPropertyRegistryFind(registry, NULL) == NULL);
    RoboCheck(CssPropertyRegistryAt(registry, 0u) == NULL);
    RoboCheckEqInt(CssPropertyRegistryCount(NULL), 0u);

    /* Destroying twice, and destroying nothing, are both no-ops. */
    CssPropertyRegistryDestroy(registry);
    CssPropertyRegistryDestroy(NULL);
}

static void TestCssRegistryStandardSet(void)
{
    CssPropertyRegistry *registry = NULL;
    size_t index;
    size_t inherited = 0u;
    size_t nonInherited = 0u;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    RoboCheck(registry != NULL);
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(NULL, NULL),
                      RoboInvalidArgument);

    /* Every row is classified one way or the other, with a usable name and a
     * name/definition agreement no caller has to trust. */
    for (index = 0u; index < CssPropertyRegistryCount(registry); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryAt(registry, index);

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheck(definition->name != NULL);
        RoboCheck(definition->nameLength > 0u);
        RoboCheckEqInt((int)strlen(definition->name), (int)definition->nameLength);
        RoboCheck(CssPropertyInheritanceIsKnown(definition->inheritance));
        RoboCheck(definition->inheritance != RoboCssPropertyInheritanceUnknown);
        /* The definition carries the classification as an accessor too, and the
         * two cannot disagree. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition),
                          definition->inheritance == RoboCssPropertyInheritanceInherited);
        /* Every row names an initial value, and the text parses — asserted here
         * so a row added without one is caught rather than silently defaulting. */
        RoboCheck(definition->initialText != NULL);
        if (definition->initialText) {
            RoboCheckEqInt((int)strlen(definition->initialText),
                              (int)definition->initialTextLength);
            RoboCheck(CssPropertyDefinitionInitialValue(definition) != NULL);
            RoboCheck(!CssValueIsEmpty(
                CssPropertyDefinitionInitialValue(definition)));
        }
        if (CssPropertyDefinitionInherited(definition)) {
            inherited++;
        } else {
            nonInherited++;
        }
    }
    /* The set is exactly the one the phase names: the nine common inherited
     * properties and the eight common non-inherited ones, plus the handful of
     * font/text relatives that answer the same classification question, and
     * `background-color` (9.9) — the second property whose value is a colour,
     * added so that the contextual `currentColor` rule is reachable at all.
     *
     * 9.11 adds fourteen: the twelve box-model longhands (`margin-*`, `padding-*`,
     * `border-*-width`), `border-width` as their third shorthand, and `box-sizing`,
     * whose value model exists so a declaration survives to the phase that has a
     * layout rather than being dropped by the phase that does not.
     *
     * 9.13 adds five: the four inset properties (`top`, `right`, `bottom`, `left`),
     * which are the four sides under the names the side vocabulary already gives
     * them, and `z-index`. `position` was already a row — 9.13 moved it onto a
     * shape of its own the way 9.12 moved `display`, and a shape change is not a
     * new property. None of the five inherits.
     *
     * 9.14 adds five: the background longhands `background-image`, `background-position`,
     * `background-size`, `background-repeat` and `background-attachment`.
     * `background` and `background-color` were already rows — 9.14 *distributes* the
     * shorthand over its six longhands rather than registering it again, and
     * `background-color` keeps the colour shape 9.9 gave it. None of the five
     * inherits, because a background is painted per element.
     *
     * 9.15 adds fourteen: the eight remaining border longhands five of which were
     * already rows (`border-top-width` and its three siblings came with 9.11) — so
     * the four `border-*-style`, the four `border-*-color` — and the six shorthands
     * `border-style`, `border-color`, `border-top`, `border-right`, `border-bottom`
     * and `border-left`. `border-width` and `border` were already rows, which is the
     * same shape-change-not-new-property rule 9.13 recorded for `position`: a border
     * family is a family of properties, and the ones this phase taught new values to
     * were already in the table. None of the fourteen inherits, because a border is
     * drawn per element.
     *
     * A count is asserted rather than a lower bound so that a row added later is a
     * deliberate act rather than an accident. */
    RoboCheckEqInt((int)CssPropertyRegistryCount(registry), 99);
    RoboCheckEqInt((int)inherited, 16);
    /* 18.3's twelve flexbox rows and 18.4's twenty grid rows are all non-inherited,
     * which is why only this half of the count moved: a flex container's direction, a
     * wrap rule, an alignment, a factor, a basis and an order are the box's own, and a
     * grid container's flow, track template, line placements and gutters are too -- a
     * child that inherited a template would place itself into its parent's grid. */
    RoboCheckEqInt((int)nonInherited, 83);

    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

static void TestCssRegistryInheritedClassification(void)
{
    /* The nine the phase names, plus the font/text relatives the standard set
     * carries. Spelled out by name: a registry that classified everything the
     * same way would pass a loop over the file and fail this. */
    static const char *const inherited[] = {
        "color", "font-family", "font-size", "font-style", "font-weight",
        "line-height", "text-align", "visibility", "white-space",
        "cursor", "direction", "font-variant", "letter-spacing", "text-indent",
        "text-transform", "word-spacing"
    };
    /* The eight the phase names as the things that must NOT propagate, plus the
     * box model's fourteen (9.11) — every one of which is the *opposite* of an
     * inherited property: a `margin` that propagated would give a child its
     * parent's spacing, which is the single most visible way to be wrong. */
    static const char *const nonInherited[] = {
        "width", "height", "margin", "padding", "border", "background",
        "display", "position", "background-color",
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width", "border-width", "box-sizing",
        "background-image", "background-position", "background-size",
        "background-repeat", "background-attachment",
        /* 9.15's eight longhands and five shorthands. `border-color` on a *parent*
         * that a child inherited would be the colour of a border the child does not
         * have — and it would not even be a wrong colour, it would be a reference to
         * the wrong element's `color`. */
        "border-top-style", "border-right-style", "border-bottom-style",
        "border-left-style", "border-style",
        "border-top-color", "border-right-color", "border-bottom-color",
        "border-left-color", "border-color",
        "border-top", "border-right", "border-bottom", "border-left"
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);

    for (index = 0u; index < sizeof(inherited) / sizeof(inherited[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, inherited[index]);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 1);
        }
    }
    for (index = 0u;
         index < sizeof(nonInherited) / sizeof(nonInherited[0]);
         index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, nonInherited[index]);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        }
    }
    /* A property nothing registered is UNKNOWN, which is a different answer from
     * NON_INHERITED and must not be conflated with it. */
    RoboCheck(CssPropertyRegistryFind(registry, "totally-unknown") == NULL);

    CssPropertyRegistryDestroy(registry);
}

static void TestCssRegistryInitialValues(void)
{
    /* Property -> the shape of its registered initial value. */
    static const struct {
        const char *name;
        const char *shape;
    } expected[] = {
        {"color", "black"},
        {"font-family", "serif"},
        {"font-size", "medium"},
        {"font-style", "normal"},
        {"font-weight", "normal"},
        {"line-height", "normal"},
        {"text-align", "start"},
        {"visibility", "visible"},
        {"white-space", "normal"},
        {"text-indent", "0"},
        {"text-transform", "none"},
        {"direction", "ltr"},
        {"cursor", "auto"},
        {"letter-spacing", "normal"},
        {"word-spacing", "normal"},
        {"font-variant", "normal"},
        {"width", "auto"},
        {"height", "auto"},
        {"display", "inline"},
        {"position", "static"},
        {"margin", "0"},
        {"padding", "0"},
        {"background", "transparent"},
        {"background-color", "transparent"},
        /* 9.14's five, in the registry's own order, and two of them are worth
         * reading twice: `background-position` starts at `0% 0%` — the top-left as
         * a *percentage*, so it stays relative to whatever box it lands on — and
         * `background-size` at `auto`, which is not a length. */
        {"background-image", "none"},
        {"background-position", "0%,_,0%"},
        {"background-size", "auto"},
        {"background-repeat", "repeat"},
        {"background-attachment", "scroll"},
        {"border", "medium,_,none,_,currentcolor"},
        /* 9.11's box model: each longhand's own initial, and `border-width`
         * starting where its four longhands start rather than at zero. */
        {"margin-top", "0"},
        {"margin-right", "0"},
        /* 11.9's one row, and its initial value is the reason it is in this table at
         * all: every other property here starts at the zero of its own vocabulary, and
         * an `opacity` that did would draw nothing. */
        {"opacity", "1"},
        {"margin-bottom", "0"},
        {"margin-left", "0"},
        {"padding-top", "0"},
        {"padding-right", "0"},
        {"padding-bottom", "0"},
        {"padding-left", "0"},
        {"border-top-width", "medium"},
        {"border-right-width", "medium"},
        {"border-bottom-width", "medium"},
        {"border-left-width", "medium"},
        {"border-width", "medium"},
        /* 9.15's border family. `border-style` and its four longhands start at
         * `none` — a decided value, not an absent border. Every colour starts at
         * `currentcolor`, the one initial in the table that is a *reference*: a
         * border whose colour had been resolved at registration time would be a
         * border nobody could see and no author could explain. The five composites
         * carry the concatenation of what they distribute. */
        {"border-style", "none"},
        {"border-top-style", "none"},
        {"border-right-style", "none"},
        {"border-bottom-style", "none"},
        {"border-left-style", "none"},
        {"border-color", "currentcolor"},
        {"border-top-color", "currentcolor"},
        {"border-right-color", "currentcolor"},
        {"border-bottom-color", "currentcolor"},
        {"border-left-color", "currentcolor"},
        {"border-top", "medium,_,none,_,currentcolor"},
        {"border-right", "medium,_,none,_,currentcolor"},
        {"border-bottom", "medium,_,none,_,currentcolor"},
        {"border-left", "medium,_,none,_,currentcolor"},
        {"box-sizing", "content-box"},
        /* 9.13's positioning family. An inset starts at `auto` and not at zero —
         * the one initial value in this table whose wrong answer would look
         * plausible: `0` would pin every absolutely positioned box to the corner
         * of its containing block. `z-index: auto` is likewise not `0`. */
        {"top", "auto"},
        {"right", "auto"},
        {"bottom", "auto"},
        {"left", "auto"},
        {"z-index", "auto"},
        /* 10.13's overflow family -- the rows a *layout* phase added rather than one
         * of the 9.x value phases, and the whole of what it needed: all three rows
         * take the closed overflow vocabulary, so a document written either as the
         * shorthand or as the two longhands reaches a value RoboLayout can classify.
         * `visible` is the initial for all three, and it is the initial a box that
         * declares nothing gets, which is why a cleared axis record means "nobody
         * wrote anything" rather than "an author asked to clip". */
        {"overflow", "visible"},
        {"overflow-x", "visible"},
        {"overflow-y", "visible"},
        /* 18.3's flexbox family. Three of the initials are worth reading twice:
         * `align-self` starts at `auto` rather than at the `normal` its three
         * siblings start at, `flex-grow` starts at `0` while `flex-shrink` starts at
         * `1`, and the two shorthands carry the concatenation of their halves as a
         * PRESERVED token list -- the same shape `background` and `border` carry. */
        {"align-content", "normal"},
        {"align-items", "normal"},
        {"align-self", "auto"},
        {"flex", "0,_,1,_,auto"},
        {"flex-basis", "auto"},
        {"flex-direction", "row"},
        {"flex-flow", "row,_,nowrap"},
        {"flex-grow", "0"},
        {"flex-shrink", "1"},
        {"flex-wrap", "nowrap"},
        {"justify-content", "normal"},
        {"order", "0"},
        /* 18.4's grid family. Two of the twenty take a shape of the family -- the flow
         * (`row`) and the gutters (`normal`, which is a keyword and not zero) -- and the
         * rest are carried declarations, so their initial shape is the author's own
         * tokens PRESERVED. */
        {"column-gap", "normal"},
        {"gap", "normal"},
        {"grid", "none"},
        {"grid-area", "auto"},
        {"grid-auto-columns", "auto"},
        {"grid-auto-flow", "row"},
        {"grid-auto-rows", "auto"},
        {"grid-column", "auto"},
        {"grid-column-end", "auto"},
        {"grid-column-gap", "normal"},
        {"grid-column-start", "auto"},
        {"grid-row", "auto"},
        {"grid-row-end", "auto"},
        {"grid-row-gap", "normal"},
        {"grid-row-start", "auto"},
        {"grid-template", "none"},
        {"grid-template-areas", "none"},
        {"grid-template-columns", "none"},
        {"grid-template-rows", "none"},
        {"row-gap", "normal"}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[256];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }
    /* Every row was reached, so a property renamed in the file would fail here
     * rather than silently reducing the coverage of the loop above. */
    RoboCheckEqInt((int)index, (int)CssPropertyRegistryCount(registry));

    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Naming                                                              */
/* ------------------------------------------------------------------ */

static void TestCssRegistryCustomPropertyNames(void)
{
    RoboCheckEqInt(
        CssIsCustomPropertyName(CssSliceFromText("--x")), 1);
    RoboCheckEqInt(
        CssIsCustomPropertyName(CssSliceFromText("--theme-color")), 1);
    /* `--` alone is not a custom property: CSS requires at least one more byte. */
    RoboCheckEqInt(CssIsCustomPropertyName(CssSliceFromText("--")), 0);
    RoboCheckEqInt(
        CssIsCustomPropertyName(CssSliceFromText("-x")), 0);
    RoboCheckEqInt(
        CssIsCustomPropertyName(CssSliceFromText("color")), 0);
    RoboCheckEqInt(CssIsCustomPropertyName(CssSliceFromText("")), 0);
    /* A slice with no bytes is different from an empty one, and neither is a
     * custom property. */
    RoboCheckEqInt(CssIsCustomPropertyName(CssSliceEmpty()), 0);
}

static void TestCssRegistryFind(void)
{
    CssPropertyRegistry *registry = NULL;
    const CssPropertyDefinition *definition;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    /* Ordinary property names are ASCII case-insensitive, as CSS says. */
    RoboCheck(CssPropertyRegistryFind(registry, "COLOR") != NULL);
    RoboCheck(CssPropertyRegistryFind(registry, "CoLoR") != NULL);
    RoboCheck(CssPropertyRegistryFind(registry, "color") != NULL);
    definition = CssPropertyRegistryFind(registry, "COLOR");
    RoboCheckStrEq(definition ? definition->name : "", "color");
    /* A name nothing knows, and an absent slice, are both misses. */
    RoboCheck(CssPropertyRegistryFind(registry, "colours") == NULL);
    RoboCheck(CssPropertyRegistryFind(registry, "") == NULL);
    RoboCheck(CssPropertyRegistryFindSlice(registry, CssSliceEmpty()) ==
               NULL);
    /* A registered custom property is compared byte-exactly, because CSS
     * custom property names are case-sensitive where ordinary ones are not. */
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "--theme", RoboCssPropertyInheritanceInherited,
                          "red", 0u, NULL),
                      RoboOk);
    RoboCheck(CssPropertyRegistryFind(registry, "--theme") != NULL);
    RoboCheck(CssPropertyRegistryFind(registry, "--THEME") == NULL);

    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestCssRegistryRegister(void)
{
    CssPropertyRegistry *registry = NULL;
    const CssPropertyDefinition *definition;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreate(&registry, NULL), RoboOk);

    RoboCheckEqInt(CssPropertyRegistryRegister(
                          NULL, "color", RoboCssPropertyInheritanceInherited,
                          "black", 0u, NULL),
                      RoboInvalidArgument);
    /* A name the property table would refuse is refused here too, so a registry
     * can always be applied to a table. */
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "", RoboCssPropertyInheritanceInherited, NULL,
                          0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "two words", RoboCssPropertyInheritanceInherited,
                          NULL, 0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "a:b", RoboCssPropertyInheritanceInherited, NULL,
                          0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "a.b", RoboCssPropertyInheritanceInherited, NULL,
                          0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryRegister(registry, NULL,
                                                         RoboCssPropertyInheritanceInherited,
                                                         NULL, 0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryCount(registry), 0u);

    /* UNKNOWN is never stored: a lookup that succeeded while classifying nothing
     * would be strictly worse than not finding the property. */
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "color", RoboCssPropertyInheritanceUnknown, NULL,
                          0u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "color", (CssPropertyInheritance)99, NULL,
                          0u, NULL),
                      RoboInvalidArgument);

    /* An initial value that does not parse is refused rather than stored: a
     * broken row should fail where it is written. */
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "color", RoboCssPropertyInheritanceInherited,
                          "red } {", 0u, NULL),
                      RoboInvalidArgument);

    /* With no initial text the property is classified and has no default — a
     * real state, and readable. */
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "color", RoboCssPropertyInheritanceInherited,
                          NULL, 0u, NULL),
                      RoboOk);
    definition = CssPropertyRegistryFind(registry, "color");
    RoboCheck(definition != NULL);
    if (definition) {
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 1);
        RoboCheck(definition->initialText == NULL);
        RoboCheck(CssPropertyDefinitionInitialValue(definition) == NULL);
    }

    /* Registering the same name again is reported and changes nothing: first
     * registration wins, so a classification cannot move under a reader. */
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "COLOR", RoboCssPropertyInheritanceNonInherited,
                          "blue", 0u, NULL),
                      RoboAlreadyExists);
    definition = CssPropertyRegistryFind(registry, "color");
    RoboCheck(definition != NULL);
    if (definition) {
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 1);
    }
    RoboCheckEqInt(CssPropertyRegistryCount(registry), 1u);

    CssPropertyRegistryDestroy(registry);
}

static void TestCssRegistryDefinitionIsStable(void)
{
    CssPropertyRegistry *registry = NULL;
    const CssPropertyDefinition *definition;
    const CssValue *initial;
    char shape[128];
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreate(&registry, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyRegistryRegister(
                          registry, "color", RoboCssPropertyInheritanceInherited,
                          "black", 0u, NULL),
                      RoboOk);
    definition = CssPropertyRegistryFind(registry, "color");
    initial = CssPropertyDefinitionInitialValue(definition);
    RoboCheck(definition != NULL);
    RoboCheck(initial != NULL);

    /* Register a hundred more: a borrowed definition and its initial value must
     * not move, because entries are individually allocated and addressed through
     * a pointer array. This is the property a caller relies on when it holds a
     * definition across a registration. */
    for (index = 0u; index < 100u; index++) {
        char name[32];

        (void)snprintf(name, sizeof(name), "generated-%d", (int)index);
        RoboCheckEqInt(CssPropertyRegistryRegister(
                              registry, name, RoboCssPropertyInheritanceNonInherited,
                              "auto", 0u, NULL),
                          RoboOk);
    }
    RoboCheckEqInt((int)CssPropertyRegistryCount(registry), 101);
    CssShape(CssPropertyDefinitionInitialValue(definition), shape,
              sizeof(shape));
    RoboCheckStrEq(shape, "black");
    RoboCheck(CssPropertyDefinitionInitialValue(definition) == initial);

    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Applying to a property table                                        */
/* ------------------------------------------------------------------ */

static void TestCssRegistryApplyToTable(void)
{
    CssPropertyRegistry *registry = NULL;
    CssPropertyTable *table = NULL;
    CssPropertyInfo info =    RoboCssCleared;
    size_t registered = 0u;
    CssPropertyId colorId;
    CssPropertyId widthId;
    CssPropertyId displayId;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);

    RoboCheckEqInt(CssPropertyRegistryApplyToTable(NULL, table, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyRegistryApplyToTable(registry, NULL, NULL, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(CssPropertyRegistryApplyToTable(registry, table,
                                                                &registered, NULL),
                      RoboOk);
    RoboCheckEqInt((int)registered, (int)CssPropertyRegistryCount(registry));

    /* The flags a resolution reads are the registry's classifications: this is
     * what keeps the table and the registry from disagreeing. */
    colorId = CssPropertyTableLookup(table, "color");
    widthId = CssPropertyTableLookup(table, "width");
    displayId = CssPropertyTableLookup(table, "display");
    RoboCheck(colorId != RoboCssPropertyIdNone);
    RoboCheck(widthId != RoboCssPropertyIdNone);
    RoboCheck(displayId != RoboCssPropertyIdNone);

    RoboCheckEqInt(CssPropertyTableInfo(table, colorId, &info, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyFlagsInherited(info.flags), 1);
    RoboCheckEqInt(CssPropertyTableInfo(table, widthId, &info, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyFlagsInherited(info.flags), 0);
    RoboCheckEqInt(CssPropertyTableInfo(table, displayId, &info, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyFlagsInherited(info.flags), 0);
    /* A shorthand's flag survives the trip too, so 9.9 has something to read. */
    RoboCheckEqInt(CssPropertyFlagsShorthand(
                          CssPropertyTableInfo(table,
                                                       CssPropertyTableLookup(table,
                                                                                      "margin"),
                                                       &info, NULL) == RoboOk
                              ? info.flags
                              : 0u),
                      1);

    /* Applying twice registers nothing the second time and is not an error: the
     * caller's numbering is its own. */
    registered = 99u;
    RoboCheckEqInt(CssPropertyRegistryApplyToTable(registry, table,
                                                                &registered, NULL),
                      RoboOk);
    RoboCheckEqInt((int)registered, 0);
    RoboCheckEqInt((int)CssPropertyTableCount(table),
                      (int)CssPropertyRegistryCount(registry));

    CssPropertyTableDestroy(table);
    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static void TestCssRegistryClassificationNames(void)
{
    RoboCheckStrEq(CssPropertyInheritanceName(
                          RoboCssPropertyInheritanceUnknown),
                      "unknown");
    RoboCheckStrEq(CssPropertyInheritanceName(
                          RoboCssPropertyInheritanceInherited),
                      "inherited");
    RoboCheckStrEq(CssPropertyInheritanceName(
                          RoboCssPropertyInheritanceNonInherited),
                      "non-inherited");
    RoboCheckStrEq(CssPropertyInheritanceName(
                          (CssPropertyInheritance)77),
                      "unknown");
    RoboCheckEqInt(CssPropertyInheritanceIsKnown(
                          RoboCssPropertyInheritanceUnknown),
                      1);
    RoboCheckEqInt(CssPropertyInheritanceIsKnown(
                          RoboCssPropertyInheritanceNonInherited),
                      1);
    RoboCheckEqInt(CssPropertyInheritanceIsKnown(
                          (CssPropertyInheritance)77),
                      0);
    RoboCheckEqInt(RoboCssPropertyInheritanceCount, 3);
    /* The accessors tolerate NULL the way the rest of the model does. */
    RoboCheckEqInt(CssPropertyDefinitionInherited(NULL), 0);
    RoboCheck(CssPropertyDefinitionInitialValue(NULL) == NULL);
}

/* ------------------------------------------------------------------ */
/* Ownership                                                           */
/* ------------------------------------------------------------------ */

static void TestCssRegistryReleasesEverything(void)
{
    int enabled;
    long baseline;

    CssFixture();
    enabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();

    /* Names, initial texts and their parsed value trees all belong to the
     * registry, so dropping it must return the allocator to where it started. A
     * registry whose definitions leaked their initial value's children would
     * show up here and nowhere else. */
    {
        CssPropertyRegistry *registry = NULL;

        RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                          RoboOk);
        RoboCheckEqInt(CssPropertyRegistryRegister(
                              registry, "--custom", RoboCssPropertyInheritanceInherited,
                              "calc(100% - 12px)", 0u, NULL),
                          RoboOk);
        /* A refused registration must not leak the parts it built first. The
         * text is an unterminated block: a value the grammar cannot finish is a
         * value it cannot return, which is the case the registry refuses. */
        RoboCheckEqInt(CssPropertyRegistryRegister(
                              registry, "broken", RoboCssPropertyInheritanceInherited,
                              "{", 0u, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssPropertyRegistryRegister(
                              registry, "color", RoboCssPropertyInheritanceInherited,
                              "black", 0u, NULL),
                          RoboAlreadyExists);
        CssPropertyRegistryDestroy(registry);
    }

    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(enabled);
}

void TestRegisterCssPropertyRegistry(void);

void TestRegisterCssPropertyRegistry(void)
{
    TestRegister("css.registry.lifecycle", TestCssRegistryLifecycle);
    TestRegister("css.registry.standard_set", TestCssRegistryStandardSet);
    TestRegister("css.registry.inherited_classification",
                       TestCssRegistryInheritedClassification);
    TestRegister("css.registry.initial_values", TestCssRegistryInitialValues);
    TestRegister("css.registry.custom_property_names",
                       TestCssRegistryCustomPropertyNames);
    TestRegister("css.registry.find", TestCssRegistryFind);
    TestRegister("css.registry.register", TestCssRegistryRegister);
    TestRegister("css.registry.definition_is_stable",
                       TestCssRegistryDefinitionIsStable);
    TestRegister("css.registry.apply_to_table", TestCssRegistryApplyToTable);
    TestRegister("css.registry.classification_names",
                       TestCssRegistryClassificationNames);
    TestRegister("css.registry.releases_everything",
                       TestCssRegistryReleasesEverything);
}
