/*
 * RoboCSS Phase 9.1 declaration suite:
 * property identity, the declaration record, declaration lists and the
 * inline-style parse boundary.
 */

#include "RoboCssTestSupport.h"

static void TestCssPropertyTableLifecycle(void)
{
    CssPropertyTable *table = NULL;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
    RoboCheck(table != NULL);
    RoboCheckEqInt(CssPropertyTableCount(table), 0u);
    /* A fresh table knows no property: that is the honest state of a build with
     * no property families implemented. */
    RoboCheckEqInt(CssPropertyTableLookup(table, "color"),
                      RoboCssPropertyIdNone);
    RoboCheck(!CssPropertyTableIsRegistered(table, 1u));

    CssPropertyTableDestroy(table);
    CssPropertyTableDestroy(NULL);
    RoboCheckEqInt(CssPropertyTableCreate(NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableCount(NULL), 0u);
    RoboCheckEqInt(CssPropertyTableLookup(NULL, "color"),
                      RoboCssPropertyIdNone);
    CssPropertyTableReset(NULL);
}

static void TestCssPropertyRegisterAndLookup(void)
{
    CssPropertyTable *table = NULL;
    CssPropertyId color = RoboCssPropertyIdNone;
    CssPropertyId again = RoboCssPropertyIdNone;
    CssPropertyId padding = RoboCssPropertyIdNone;
    CssPropertyInfo info;
    CssDiag diag;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);

    RoboCheckEqInt(CssPropertyTableRegister(
                          table, "color", RoboCssPropertyFlagInherited, &color,
                          NULL),
                      RoboOk);
    RoboCheck(color != RoboCssPropertyIdNone);
    RoboCheckEqInt(CssPropertyTableCount(table), 1u);

    /* Lookup is ASCII case-insensitive and works from a slice as well as a
     * string: a declaration's name is a slice. */
    RoboCheckEqInt(CssPropertyTableLookup(table, "color"), color);
    RoboCheckEqInt(CssPropertyTableLookup(table, "COLOR"), color);
    RoboCheckEqInt(CssPropertyTableLookup(table, "CoLoR"), color);
    RoboCheckEqInt(CssPropertyTableLookupSlice(
                          table, CssSliceFromText("Color")),
                      color);
    RoboCheckEqInt(CssPropertyTableLookup(table, "colour"),
                      RoboCssPropertyIdNone);
    RoboCheckEqInt(CssPropertyTableLookup(table, ""),
                      RoboCssPropertyIdNone);
    RoboCheckEqInt(CssPropertyTableLookupSlice(table, CssSliceEmpty()),
                      RoboCssPropertyIdNone);

    /* Registering a name that is already present answers ALREADY_EXISTS with the
     * existing id, and the stored flags are the first registration's. */
    CssDiagReset(&diag);
    RoboCheckEqInt(CssPropertyTableRegister(table, "COLOR",
                                                       RoboCssPropertyFlagShorthand,
                                                       &again, &diag),
                      RoboAlreadyExists);
    RoboCheckEqInt(again, color);
    RoboCheckEqInt(diag.domain, RoboCssDomainProperty);
    RoboCheckEqInt(CssPropertyTableCount(table), 1u);
    RoboCheckEqInt(CssPropertyTableInfo(table, color, &info, NULL), RoboOk);
    RoboCheck(CssPropertyFlagsInherited(info.flags));
    RoboCheck(!CssPropertyFlagsShorthand(info.flags));

    /* The name comes back as written, not lowercased: normalising case is the
     * tokenizer's job, and a caller reading back what it registered must see
     * what it registered. */
    RoboCheckStrEq(info.name, "color");
    RoboCheckEqInt(info.nameLength, 5u);
    RoboCheck(CssPropertyInfoIsSet(&info));
    RoboCheckEqInt(info.id, color);

    RoboCheckEqInt(CssPropertyTableRegister(
                          table, "padding", RoboCssPropertyFlagShorthand, &padding,
                          NULL),
                      RoboOk);
    RoboCheck(padding != color);
    RoboCheckEqInt(CssPropertyTableCount(table), 2u);
    RoboCheckEqInt(CssPropertyTableAt(table, 0u, &info, NULL), RoboOk);
    RoboCheckEqInt(info.id, color);
    RoboCheckEqInt(CssPropertyTableAt(table, 1u, &info, NULL), RoboOk);
    RoboCheckEqInt(info.id, padding);
    RoboCheckEqInt(CssPropertyTableAt(table, 2u, &info, NULL), RoboNotFound);
    RoboCheckEqInt(CssPropertyTableInfo(table, (CssPropertyId)99,
                                                   &info, NULL),
                      RoboNotFound);
    RoboCheckEqInt(CssPropertyTableInfo(table, RoboCssPropertyIdNone,
                                                   &info, NULL),
                      RoboNotFound);
    RoboCheckEqInt(CssPropertyTableInfo(table, color, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableAt(table, 0u, NULL, NULL),
                      RoboInvalidArgument);

    CssPropertyTableDestroy(table);
}

static void TestCssPropertyRejectsBadNames(void)
{
    CssPropertyTable *table = NULL;
    CssPropertyId id = RoboCssPropertyIdNone;
    char tooLong[RoboCssPropertyNameMax + 8];
    size_t i;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);

    RoboCheckEqInt(CssPropertyTableRegister(table, "", 0u, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableRegister(table, NULL, 0u, &id, NULL),
                      RoboInvalidArgument);
    /* A name that could not be a property name is refused rather than stored. */
    RoboCheckEqInt(CssPropertyTableRegister(table, "text-align:center", 0u,
                                                       &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableRegister(table, "a b", 0u, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableRegister(table, "border.radius", 0u,
                                                       &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableCount(table), 0u);
    RoboCheckEqInt(id, RoboCssPropertyIdNone);

    for (i = 0u; i + 1u < sizeof(tooLong); i++) {
        tooLong[i] = 'x';
    }
    tooLong[sizeof(tooLong) - 1u] = '\0';
    RoboCheckEqInt(CssPropertyTableRegister(table, tooLong, 0u, &id, NULL),
                      RoboInvalidArgument);

    /* A vendor prefix is a name like any other, and a custom property is not
     * special-cased in this phase — it registers as a plain name. */
    RoboCheckEqInt(CssPropertyTableRegister(table, "-webkit-line-clamp", 0u,
                                                       &id, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "--brand", 0u, &id, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyTableCount(table), 2u);

    RoboCheckEqInt(CssPropertyTableRegister(NULL, "color", 0u, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPropertyTableRegister(table, "color", 0u, NULL, NULL),
                      RoboInvalidArgument);

    CssPropertyTableDestroy(table);
}

static void TestCssPropertyIdsNeverReissued(void)
{
    CssPropertyTable *table = NULL;
    CssPropertyId before = RoboCssPropertyIdNone;
    CssPropertyId after = RoboCssPropertyIdNone;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);

    RoboCheckEqInt(CssPropertyTableRegister(table, "color", 0u, &before,
                                                       NULL),
                      RoboOk);
    CssPropertyTableReset(table);
    RoboCheckEqInt(CssPropertyTableCount(table), 0u);
    /* An id issued before a reset must not name whatever is registered next. */
    RoboCheck(!CssPropertyTableIsRegistered(table, before));
    RoboCheckEqInt(CssPropertyTableRegister(table, "padding", 0u, &after,
                                                       NULL),
                      RoboOk);
    RoboCheck(after != before);

    CssPropertyTableDestroy(table);
}

static void TestCssDeclarationModel(void)
{
    CssDeclaration declaration = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    CssDeclarationInit(&declaration);
    RoboCheckEqInt(declaration.state, RoboCssDeclarationStateNone);
    RoboCheck(!CssDeclarationIsUsable(&declaration));
    RoboCheck(!CssDeclarationHasValue(&declaration));
    RoboCheck(CssSliceIsEmpty(CssDeclarationName(&declaration)));

    RoboCheckEqInt(CssDeclarationMake(&declaration,
                                                CssSliceFromText("color"),
                                                RoboCssDeclarationStateParsed, NULL),
                      RoboOk);
    RoboCheck(CssDeclarationIsUsable(&declaration));
    RoboCheck(CssSliceEqualsAscii(CssDeclarationName(&declaration),
                                           "color"));
    RoboCheckEqInt(declaration.property, RoboCssPropertyIdNone);
    RoboCheckEqInt(declaration.important, 0);

    /* A declaration cannot be made in the NONE state — that state means "no
     * declaration" and is what clearing produces, not something to build. */
    CssDiagReset(&diag);
    RoboCheckEqInt(CssDeclarationMake(&declaration,
                                                CssSliceFromText("x"),
                                                RoboCssDeclarationStateNone, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainDeclaration);
    RoboCheck(CssDeclarationIsUsable(&declaration));

    /* A PARSED declaration needs a name; an INVALID one is exactly what an
     * empty name looks like, so it is allowed there. */
    RoboCheckEqInt(CssDeclarationMake(&declaration, CssSliceEmpty(),
                                                RoboCssDeclarationStateParsed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDeclarationMake(&declaration, CssSliceEmpty(),
                                                RoboCssDeclarationStateInvalid, NULL),
                      RoboOk);
    RoboCheckEqInt(declaration.state, RoboCssDeclarationStateInvalid);
    RoboCheck(!CssDeclarationIsUsable(&declaration));

    RoboCheckEqInt(CssDeclarationMake(NULL, CssSliceFromText("a"),
                                                RoboCssDeclarationStateParsed, NULL),
                      RoboInvalidArgument);
    CssDeclarationDispose(&declaration);
    CssDeclarationDispose(NULL);
    RoboCheckEqInt(CssDeclarationStateIsKnown(declaration.state), 1);
    RoboCheckStrEq(CssDeclarationStateName(
                          RoboCssDeclarationStateInvalid),
                      "invalid");
    RoboCheck(!CssDeclarationStateIsKnown((CssDeclarationState)9));
    RoboCheckStrEq(CssDeclarationStateName((CssDeclarationState)9),
                      "unknown");
    RoboCheck(CssDeclarationValue(NULL) == NULL);
}

static void TestCssDeclarationValueAndImportance(void)
{
    CssSource source = RoboCssCleared;
    CssDeclaration declaration = RoboCssCleared;
    CssValue value = RoboCssCleared;
    CssToken token = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "12px !important");
    CssDeclarationMake(&declaration, CssSliceFromText("padding-left"),
                              RoboCssDeclarationStateParsed, NULL);

    RoboCheckEqInt(CssDeclarationSetImportant(&declaration, 1, NULL),
                      RoboOk);
    RoboCheckEqInt(declaration.important, 1);
    RoboCheckEqInt(CssDeclarationSetImportant(&declaration, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(declaration.important, 0);
    /* Importance is normalised to 0/1 rather than stored as the caller's int. */
    RoboCheckEqInt(CssDeclarationSetImportant(&declaration, 42, NULL),
                      RoboOk);
    RoboCheckEqInt(declaration.important, 1);

    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenDimension, &source,
                                          CssRangeSpan(0u, 4u), NULL),
                      RoboOk);
    RoboCheckEqInt(CssValueMakeToken(&value, &token, NULL), RoboOk);
    RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value, NULL),
                      RoboOk);
    RoboCheck(CssValueIsEmpty(&value));
    RoboCheck(CssDeclarationHasValue(&declaration));
    RoboCheck(CssSliceEqualsAscii(
                   CssValueTokenText(CssDeclarationValue(&declaration)),
                   "12px"));

    /* Setting a second value replaces the first and disposes it: one owner. */
    {
        CssValue function = RoboCssCleared;

        RoboCheckEqInt(CssValueMakeFunction(&function,
                                                       CssSliceFromText("calc"),
                                                       CssRangeAt(0u), NULL),
                          RoboOk);
        RoboCheckEqInt(CssDeclarationSetValue(&declaration, &function, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(CssDeclarationValue(&declaration)->kind,
                      RoboCssValueKindFunction);

    RoboCheckEqInt(CssDeclarationSetValue(&declaration, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDeclarationSetImportant(NULL, 1, NULL),
                      RoboInvalidArgument);

    /* A declaration that has not been made refuses a value. */
    {
        CssDeclaration empty = RoboCssCleared;
        CssValue list = RoboCssCleared;

        CssDeclarationInit(&empty);
        RoboCheckEqInt(CssValueMakeList(&list, NULL), RoboOk);
        RoboCheckEqInt(CssDeclarationSetValue(&empty, &list, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(list.kind, RoboCssValueKindList);
        CssValueDispose(&list);
    }

    CssDeclarationDispose(&declaration);
    CssSourceDispose(&source);
}

static void TestCssDeclarationResolveProperty(void)
{
    CssPropertyTable *table = NULL;
    CssPropertyId color = RoboCssPropertyIdNone;
    CssDeclaration declaration = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "color", 0u, &color,
                                                       NULL),
                      RoboOk);

    CssDeclarationMake(&declaration, CssSliceFromText("Color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssDeclarationResolveProperty(&declaration, table),
                      color);
    RoboCheckEqInt(declaration.property, color);
    /* The name slice is untouched by resolution: the declaration still carries
     * what the author wrote. */
    RoboCheck(CssSliceEqualsAscii(CssDeclarationName(&declaration),
                                           "Color"));

    /* An unknown property is a result, not a failure: NONE, no error. */
    CssDeclarationMake(&declaration, CssSliceFromText("unknown-prop"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssDeclarationResolveProperty(&declaration, table),
                      RoboCssPropertyIdNone);
    RoboCheckEqInt(declaration.property, RoboCssPropertyIdNone);

    RoboCheckEqInt(CssDeclarationResolveProperty(&declaration, NULL),
                      RoboCssPropertyIdNone);
    RoboCheckEqInt(CssDeclarationResolveProperty(NULL, table),
                      RoboCssPropertyIdNone);

    CssDeclarationDispose(&declaration);
    CssPropertyTableDestroy(table);
}

static void TestCssDeclarationRanges(void)
{
    CssDeclaration declaration = RoboCssCleared;

    CssFixture();
    CssDeclarationMake(&declaration, CssSliceFromText("color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssDeclarationSetRanges(&declaration,
                                                      CssRangeSpan(3u, 12u),
                                                      CssRangeSpan(3u, 5u),
                                                      CssRangeSpan(10u, 3u),
                                                      NULL),
                      RoboOk);
    RoboCheckEqInt(CssRangeLength(declaration.range), 12u);
    RoboCheckEqInt(declaration.nameRange.begin, 3u);
    RoboCheckEqInt(declaration.nameRange.end, 8u);
    RoboCheckEqInt(declaration.valueRange.begin, 10u);
    RoboCheckEqInt(CssDeclarationSetRanges(NULL, CssRangeAt(0u),
                                                      CssRangeAt(0u),
                                                      CssRangeAt(0u), NULL),
                      RoboInvalidArgument);
    CssDeclarationDispose(&declaration);
}

static void TestCssDeclarationList(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssDeclaration declaration = RoboCssCleared;
    CssPropertyTable *table = NULL;
    CssPropertyId color = RoboCssPropertyIdNone;
    CssPropertyId padding = RoboCssPropertyIdNone;
    size_t index = 99u;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "color", 0u, &color,
                                                       NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "padding", 0u, &padding,
                                                       NULL),
                      RoboOk);

    CssDeclarationListInit(&list);
    RoboCheck(CssDeclarationListIsEmpty(&list));
    RoboCheckEqInt(CssDeclarationListCount(&list), 0u);
    RoboCheck(CssDeclarationListAt(&list, 0u) == NULL);

    /* Source order is preserved, duplicates included: dropping a duplicate here
     * would be implementing the cascade's last-one-wins rule. */
    {
        static const char *names[] = {"color", "padding", "color", "color"};
        int i;

        for (i = 0; i < 4; i++) {
            CssDeclarationMake(&declaration, CssSliceFromText(names[i]),
                                      RoboCssDeclarationStateParsed, NULL);
            (void)CssDeclarationResolveProperty(&declaration, table);
            RoboCheckEqInt(CssDeclarationListAppend(&list, &declaration,
                                                               NULL),
                              RoboOk);
            /* The appended declaration was moved: the local is empty again. */
            RoboCheckEqInt(declaration.state, RoboCssDeclarationStateNone);
        }
    }
    RoboCheckEqInt(CssDeclarationListCount(&list), 4u);
    RoboCheck(!CssDeclarationListIsEmpty(&list));
    RoboCheckEqInt(CssDeclarationListCountOf(&list, color), 3u);
    RoboCheckEqInt(CssDeclarationListCountOf(&list, padding), 1u);
    RoboCheckEqInt(CssDeclarationListCountOf(&list,
                                                         RoboCssPropertyIdNone), 0u);
    RoboCheck(CssDeclarationListFindFirst(&list, color, &index));
    RoboCheckEqInt(index, 0u);
    RoboCheck(CssDeclarationListFindLast(&list, color, &index));
    RoboCheckEqInt(index, 3u);
    RoboCheck(CssDeclarationListFindFirst(&list, padding, &index));
    RoboCheckEqInt(index, 1u);
    /* Both ends of a repeated property are visible; which one wins is 9.5's
     * decision and this module does not make it. */
    RoboCheck(!CssDeclarationListFindFirst(&list,
                                                     RoboCssPropertyIdNone, &index));
    RoboCheck(!CssDeclarationListFindLast(&list, (CssPropertyId)99,
                                                    &index));
    RoboCheck(!CssDeclarationListFindFirst(NULL, color, &index));

    /* The borrowed accessors agree with the order. */
    RoboCheck(CssSliceEqualsAscii(
                   CssDeclarationName(CssDeclarationListAt(&list, 1u)),
                   "padding"));
    RoboCheckEqInt(CssDeclarationListAt(&list, 1u)->property, padding);
    RoboCheck(CssDeclarationListAt(&list, 4u) == NULL);

    CssDeclarationListClear(&list);
    RoboCheckEqInt(CssDeclarationListCount(&list), 0u);
    /* Clearing keeps the allocation, so the list is still usable. */
    CssDeclarationMake(&declaration, CssSliceFromText("color"),
                              RoboCssDeclarationStateParsed, NULL);
    RoboCheckEqInt(CssDeclarationListAppend(&list, &declaration, NULL),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 1u);

    CssDeclarationListDispose(&list);
    RoboCheckEqInt(CssDeclarationListCount(&list), 0u);
    CssDeclarationListDispose(&list);
    CssDeclarationListDispose(NULL);
    CssDeclarationListClear(NULL);
    CssDeclarationListInit(NULL);
    RoboCheck(CssDeclarationListIsEmpty(NULL));
    RoboCheckEqInt(CssDeclarationListCount(NULL), 0u);
    RoboCheckEqInt(CssDeclarationListAppend(NULL, &declaration, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDeclarationListAppend(&list, NULL, NULL),
                      RoboInvalidArgument);

    /* A declaration that was never made is refused by the list. */
    {
        CssDeclaration unmade = RoboCssCleared;

        CssDeclarationInit(&unmade);
        RoboCheckEqInt(CssDeclarationListAppend(&list, &unmade, NULL),
                          RoboInvalidArgument);
    }

    CssPropertyTableDestroy(table);
}

static void TestCssDeclarationListLifetimeBalances(void)
{
    int weEnabled;
    size_t baseline;

    CssFixture();
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    {
        int i;

        for (i = 0; i < 24; i++) {
            CssDeclarationList list = RoboCssCleared;
            CssDeclaration declaration = RoboCssCleared;
            CssValue value = RoboCssCleared;
            int j;

            CssDeclarationListInit(&list);
            for (j = 0; j < 5; j++) {
                CssDeclarationMake(&declaration,
                                          CssSliceFromText("color"),
                                          RoboCssDeclarationStateParsed, NULL);
                RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
                RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value,
                                                                 NULL),
                                  RoboOk);
                RoboCheckEqInt(CssDeclarationListAppend(&list, &declaration,
                                                                   NULL),
                                  RoboOk);
            }
            RoboCheckEqInt(CssDeclarationListCount(&list), 5u);
            CssDeclarationListDispose(&list);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
}

static void TestCssInlineParseTextBoundary(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const char *text = "color: red; padding: 10px";
    const CssDeclaration *declaration;

    CssFixture();
    /* The boundary is filled as of 9.3, and it parses: the DOM keeps the `style`
     * attribute, hands RoboCSS its text, and gets declarations back. */
    RoboCheckEqInt(CssDeclarationListParseText(text, strlen(text),
                                                           RoboCssOriginInline,
                                                           &list, &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 2u);
    /* A clean fragment is not a problem: the call succeeded and the error record
     * says so rather than being left untouched. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheck(!CssErrorIsError(&error));

    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    if (declaration) {
        RoboCheckEqInt(declaration->state, RoboCssDeclarationStateParsed);
        RoboCheck(CssSliceEqualsAscii(CssDeclarationName(declaration),
                                               "color"));
        RoboCheckEqInt(declaration->important, 0);
        RoboCheckEqInt(CssValueChildCount(CssDeclarationValue(declaration)),
                          1u);
        RoboCheck(CssSliceEqualsAscii(
            CssValueTokenText(
                CssValueChild(CssDeclarationValue(declaration), 0u)),
            "red"));
    }
    declaration = CssDeclarationListAt(&list, 1u);
    RoboCheck(declaration != NULL);
    if (declaration) {
        RoboCheck(CssSliceEqualsAscii(CssDeclarationName(declaration),
                                               "padding"));
        RoboCheckEqInt(CssValueChildCount(CssDeclarationValue(declaration)),
                          1u);
        {
            const CssValue *component =
                CssValueChild(CssDeclarationValue(declaration), 0u);

            RoboCheckEqInt(component->token.type, RoboCssTokenDimension);
            RoboCheck(component->token.number == 10.0);
            RoboCheck(CssSliceEqualsAscii(component->token.unit, "px"));
        }
    }
    /* The bytes are borrowed, never copied, so the list must be usable and
     * disposable while the caller's buffer is alive — and it is. */
    CssDeclarationListDispose(&list);

    /* Empty text is not an error either: there are no declarations, and the
     * parse says so rather than refusing. */
    RoboCheckEqInt(CssDeclarationListParseText("", 0u,
                                                           RoboCssOriginInline, &list,
                                                           &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 0u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    CssDeclarationListDispose(&list);

    /* Malformed text is recovered from, not refused: the bad declaration is
     * kept as an INVALID record and the good one after it is still parsed. The
     * first problem is reported, and the call still succeeded. The `;` matters:
     * the specification's recovery consumes the remnants of a bad declaration up
     * to the `;` that ends it, so without one there is no boundary to recover
     * at and everything after it is remnants too. */
    RoboCheckEqInt(CssDeclarationListParseText(
                          "broken; padding: 10px", 21u, RoboCssOriginInline, &list,
                          &error),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 2u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedDeclaration);
    RoboCheck(CssErrorKindIsRecoverable(error.kind));
    RoboCheck(error.message != NULL);
    RoboCheckEqInt(CssDeclarationListAt(&list, 0u)->state,
                      RoboCssDeclarationStateInvalid);
    RoboCheckEqInt(CssDeclarationListAt(&list, 1u)->state,
                      RoboCssDeclarationStateParsed);
    RoboCheck(!CssDeclarationIsUsable(CssDeclarationListAt(&list, 0u)));
    RoboCheck(CssDeclarationIsUsable(CssDeclarationListAt(&list, 1u)));
    CssDeclarationListDispose(&list);

    /* A NULL list, or text that does not exist, is a caller mistake. */
    RoboCheckEqInt(CssDeclarationListParseText(text, strlen(text),
                                                           RoboCssOriginInline, NULL,
                                                           &error),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDeclarationListParseText(NULL, 4u,
                                                           RoboCssOriginInline, &list,
                                                           &error),
                      RoboInvalidArgument);
    /* An origin outside the closed vocabulary is refused by both entry points. */
    RoboCheckEqInt(CssDeclarationListParseText(text, strlen(text),
                                                           (CssOrigin)99,
                                                           &list, &error),
                      RoboInvalidArgument);
    /* A NULL error record is optional: the result code is authoritative. */
    RoboCheckEqInt(CssDeclarationListParseText(text, strlen(text),
                                                           RoboCssOriginInline, &list,
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 2u);
    CssDeclarationListDispose(&list);

    CssErrorInit(&error);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheck(!CssErrorIsError(&error));
    RoboCheckStrEq(CssErrorMessage(&error), "");
    CssErrorInit(NULL);
}

void TestRegisterCssDeclaration(void);

void TestRegisterCssDeclaration(void)
{
    TestRegister("css.property.table_lifecycle",
                       TestCssPropertyTableLifecycle);
    TestRegister("css.property.register_and_lookup",
                       TestCssPropertyRegisterAndLookup);
    TestRegister("css.property.rejects_bad_names",
                       TestCssPropertyRejectsBadNames);
    TestRegister("css.property.ids_never_reissued",
                       TestCssPropertyIdsNeverReissued);
    TestRegister("css.declaration.model", TestCssDeclarationModel);
    TestRegister("css.declaration.value_and_importance",
                       TestCssDeclarationValueAndImportance);
    TestRegister("css.declaration.resolve_property",
                       TestCssDeclarationResolveProperty);
    TestRegister("css.declaration.ranges", TestCssDeclarationRanges);
    TestRegister("css.declaration.list", TestCssDeclarationList);
    TestRegister("css.declaration.list_lifetime_balances",
                       TestCssDeclarationListLifetimeBalances);
    TestRegister("css.inline.parse_text_boundary",
                       TestCssInlineParseTextBoundary);
}
