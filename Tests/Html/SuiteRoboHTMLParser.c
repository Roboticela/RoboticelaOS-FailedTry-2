/*
 * RoboHTML Phase 8.1 parser / source / node-identity suite:
 * parser lease, source kinds and lifetime, stale handles.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboTest.h"

#include <string.h>

static void HtmlParserFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void HtmlParserFixtureEnd(void)
{
    HtmlShutdown();
}

static void TestHtmlSourceKinds(void)
{
    HtmlSource src;
    uint8_t buf[8];
    static const uint8_t bytes[] = "hello";

    HtmlSourceInit(&src);
    RoboCheckEqInt(src.kind, RoboHtmlSourceKindNone);
    RoboCheck(HtmlSourceIsEmpty(&src));

    /* NULL source is refused for every configuration call. */
    RoboCheckEqInt(HtmlSourceEmpty(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(HtmlSourceMemory(NULL, bytes, 5u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlSourceString(NULL, "x", NULL),
                      RoboInvalidArgument);

    /* Empty source. */
    RoboCheckEqInt(HtmlSourceEmpty(&src, NULL), RoboOk);
    RoboCheckEqInt(src.kind, RoboHtmlSourceKindEmpty);
    RoboCheckEqInt(HtmlSourceLength(&src), 0u);
    RoboCheck(HtmlSourceIsEmpty(&src));

    /* Memory source: borrow view with explicit length. */
    RoboCheckEqInt(HtmlSourceMemory(&src, bytes, 5u, NULL), RoboOk);
    RoboCheckEqInt(src.kind, RoboHtmlSourceKindMemory);
    RoboCheckEqInt(HtmlSourceLength(&src), 5u);
    RoboCheck(!HtmlSourceIsEmpty(&src));
    RoboCheckEqInt(HtmlSourceCopyBytes(&src, buf, sizeof(buf)),
                      RoboOk);
    RoboCheck(memcmp(buf, "hello", 5) == 0);
    /* Copy that cannot hold the bytes is refused, never truncated. */
    RoboCheckEqInt(HtmlSourceCopyBytes(&src, buf, 3u),
                      RoboNotSupported);
    /* NULL bytes with non-zero length is a boundary failure. */
    RoboCheckEqInt(HtmlSourceMemory(&src, NULL, 4u, NULL),
                      RoboInvalidArgument);
    /* NULL bytes with zero length is accepted as empty-equivalent. */
    RoboCheckEqInt(HtmlSourceMemory(&src, NULL, 0u, NULL), RoboOk);
    RoboCheck(HtmlSourceIsEmpty(&src));

    /* String source: length derived from the C string. */
    RoboCheckEqInt(HtmlSourceString(&src, "ab", NULL), RoboOk);
    RoboCheckEqInt(src.kind, RoboHtmlSourceKindString);
    RoboCheckEqInt(HtmlSourceLength(&src), 2u);
    RoboCheckEqInt(HtmlSourceString(&src, NULL, NULL), RoboOk);
    RoboCheck(HtmlSourceIsEmpty(&src));

    /* Encoding label metadata. */
    RoboCheckEqInt(HtmlSourceSetDeclaredEncoding(&src, "UTF-8",
                                                             NULL),
                      RoboOk);
    RoboCheckStrEq(HtmlSourceDeclaredEncoding(&src), "UTF-8");
    RoboCheckEqInt(HtmlSourceSetDeclaredEncoding(&src, "",
                                                             NULL),
                      RoboParseError);
    {
        const char bad[] = { (char)0xFF, '\0' };
        RoboCheckEqInt(HtmlSourceSetDeclaredEncoding(&src, bad,
                                                                 NULL),
                          RoboParseError);
    }
    RoboCheckEqInt(HtmlSourceSetDeclaredEncoding(&src, NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(HtmlSourceDeclaredEncoding(&src), "");

    /* Name metadata: bounded copy. */
    RoboCheckEqInt(HtmlSourceSetName(&src, "robo://app/main", NULL),
                      RoboOk);
    RoboCheckStrEq(HtmlSourceName(&src), "robo://app/main");
    {
        char huge[RoboHtmlUrlMax + 8u];
        memset(huge, 'a', sizeof(huge));
        huge[sizeof(huge) - 1u] = '\0';
        RoboCheckEqInt(HtmlSourceSetName(&src, huge, NULL),
                          RoboNotSupported);
    }
}

static void TestHtmlSourceLifetime(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlSource src;
    uint8_t stackBytes[16];

    HtmlParserFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);

    /*
     * Lifetime contract: the source borrows stack bytes; RoboHTML copies
     * only metadata (kind/length/name), never the bytes. This test
     * documents the boundary: after attach, RoboHTML makes no further
     * reads of the borrow (no parse in 8.1), so overwriting the borrow
     * afterwards cannot affect the document.
     */
    memcpy(stackBytes, "OLD", 4);
    RoboCheckEqInt(HtmlSourceMemory(&src, stackBytes, 3u, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlSourceSetName(&src, "borrow", NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);
    memcpy(stackBytes, "NEW", 4); /* reuse the borrow: allowed after attach */
    RoboCheckStrEq(HtmlDocumentSourceName(id), "borrow");
    RoboCheckEqInt(HtmlDocumentDetachSource(id, NULL), RoboOk);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlParserFixtureEnd();
}

static void TestHtmlParserLease(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlDocumentId other = RoboHtmlDocumentIdNone;
    HtmlParser *p = NULL;
    HtmlParser *p2 = NULL;
    HtmlParserState st;
    HtmlParserMode mode;
    HtmlDocumentLifecycle life;
    HtmlSource src;

    HtmlParserFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreate(&other, NULL), RoboOk);

    /* Argument / state validation. */
    RoboCheckEqInt(HtmlParserCreate(id, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlParserCreate(RoboHtmlDocumentIdNone, &p,
                                              NULL),
                      RoboNotFound);
    RoboCheckEqInt(p == NULL, 1);

    RoboCheckEqInt(HtmlParserCreate(id, &p, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserDocument(p), id);
    RoboCheckEqInt(HtmlParserGetState(p, &st), RoboOk);
    RoboCheckEqInt(st, RoboHtmlParserStateIdle);
    RoboCheckEqInt(HtmlParserGetMode(p, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlParserModeNone);

    /* Exclusive lease: a second parser on the same document is refused. */
    RoboCheckEqInt(HtmlParserCreate(id, &p2, NULL), RoboBusy);
    /* A parser on another document is fine. */
    RoboCheckEqInt(HtmlParserCreate(other, &p2, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserDocument(p2), other);

    /* Attach drives the document into LOADING and selects the mode. */
    RoboCheckEqInt(HtmlSourceMemory(&src, (const uint8_t *)"<p>",
                                              3u, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlParserAttachSource(p, &src, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserGetState(p, &st), RoboOk);
    RoboCheckEqInt(st, RoboHtmlParserStateFeeding);
    RoboCheckEqInt(HtmlParserGetMode(p, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlParserModeComplete);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleLoading);

    /* Re-attach while feeding is refused. */
    RoboCheckEqInt(HtmlParserAttachSource(p, &src, NULL), RoboBusy);
    /* Direct document attachment while a parser holds the lease is also
     * refused: the document is LOADING (source attached → BUSY). */
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboBusy);

    /*
     * Detach returns the document to INTERACTIVE and clears the lease.
     * Detaching abandons the input rather than finishing it, so the
     * document is interactive and not complete.
     */
    RoboCheckEqInt(HtmlParserDetach(p, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserGetState(p, &st), RoboOk);
    RoboCheckEqInt(st, RoboHtmlParserStateDetached);
    RoboCheckEqInt(HtmlParserDocument(p),
                      RoboHtmlDocumentIdNone);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleInteractive);
    /* Detach is idempotent. */
    RoboCheckEqInt(HtmlParserDetach(p, NULL), RoboOk);

    /* After detach the lease is free again. */
    {
        HtmlParser *p3 = NULL;
        RoboCheckEqInt(HtmlParserCreate(id, &p3, NULL), RoboOk);
        HtmlParserDestroy(p3);
    }

    /* Destroy-while-leased parser (destroy detaches first). p2 never
     * attached a source, so its document stays CREATED. */
    HtmlParserDestroy(p2);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(other, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleCreated);
    HtmlParserDestroy(p);
    HtmlParserDestroy(NULL); /* safe no-op */

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentDestroy(other, NULL), RoboOk);
    HtmlParserFixtureEnd();
}

static void TestHtmlParserDocumentDestroyLease(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlParser *p = NULL;
    HtmlParserState st;

    HtmlParserFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserCreate(id, &p, NULL), RoboOk);

    /*
     * Ownership rule: parsers never outlive their document. Document
     * destroy fails the lease; the parser object is still owned by the
     * caller (which must destroy it), but is detached and inert.
     */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserGetState(p, &st), RoboOk);
    RoboCheckEqInt(st, RoboHtmlParserStateDetached);
    RoboCheckEqInt(HtmlParserDocument(p),
                      RoboHtmlDocumentIdNone);
    HtmlParserDestroy(p);
    HtmlParserFixtureEnd();
}

static void TestHtmlParserFailsDocumentDestroy(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlParser *p = NULL;

    HtmlParserFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserCreate(id, &p, NULL), RoboOk);
    /* Destroy succeeds and silently releases the lease (see internal
     * contract); parser object remains caller-owned but detached. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlParserStateIsKnown(
                          RoboHtmlParserStateDetached),
                      1);
    HtmlParserDestroy(p);
    HtmlParserFixtureEnd();
}

void TestRegisterHtmlParser(void)
{
    TestRegister("html.source.kinds", TestHtmlSourceKinds);
    TestRegister("html.source.lifetime", TestHtmlSourceLifetime);
    TestRegister("html.parser.lease", TestHtmlParserLease);
    TestRegister("html.parser.destroy_lease",
                       TestHtmlParserDocumentDestroyLease);
    TestRegister("html.parser.detach_on_destroy",
                       TestHtmlParserFailsDocumentDestroy);
}
