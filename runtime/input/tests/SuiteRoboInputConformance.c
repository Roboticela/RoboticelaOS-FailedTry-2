/*
 * RoboInput conformance suite (Phase 12.1) — `input.conformance.*` and `input.model.dump`.
 *
 * Three cases about *promises*, none of them about behaviour:
 *
 *   `vocabulary`     the numbered vocabularies are asserted **positionally**, value by value, because they
 *                    are the numbers a backend writes into a record and a later phase reads back. A type
 *                    inserted in the middle of the enum would silently reinterpret every event already
 *                    produced, so the suite pins each value and the count that goes with it;
 *
 *   `capabilities`   every field of the capability record is asserted by name, the six that are 1 and the
 *                    eleven that are 0. An untested capability flag is a flag that stops being true without
 *                    anybody noticing, and the zeros are the half that matters: a reader of this build is
 *                    told what it does **not** do rather than left to infer it;
 *
 *   `model.dump`     the vocabulary as text, asserted whole -- the summary line, the line count per
 *                    vocabulary, a sample line from each, and byte-for-byte determinism across two calls.
 *                    This is the artifact a developer reads first, so it is the one that has to be right.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void test_conformance_vocabulary(void)
{
    /* Event types, in the phase brief's order. */
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_NONE, 0);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_KEY_DOWN, 1);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_KEY_UP, 2);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_POINTER_MOVE, 3);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_POINTER_DOWN, 4);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_POINTER_UP, 5);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_POINTER_CANCEL, 6);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_WHEEL, 7);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_TEXT_INPUT, 8);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_TOUCH_BEGIN, 9);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_TOUCH_MOVE, 10);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_TOUCH_END, 11);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_TOUCH_CANCEL, 12);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_EVENT_TYPE_COUNT, 13);

    /* Sources. */
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_UNKNOWN, 0);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_KEYBOARD, 1);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_MOUSE, 2);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_POINTER, 3);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_TOUCH, 4);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_TABLET, 5);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_GAME_CONTROLLER, 6);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_REMOTE, 7);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_VIRTUAL, 8);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SOURCE_COUNT, 9);

    /* Coordinate spaces. */
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SPACE_UNKNOWN, 0);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SPACE_SCREEN, 1);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SPACE_WINDOW, 2);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SPACE_APPLICATION, 3);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SPACE_DOCUMENT, 4);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_SPACE_COUNT, 5);

    /* Payload kinds and target kinds: the record-shape vocabularies. */
    ROBO_CHECK_EQ_INT(ROBO_INPUT_PAYLOAD_NONE, 0);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_PAYLOAD_KEYBOARD, 1);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_PAYLOAD_POINTER, 2);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_PAYLOAD_TEXT, 3);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_PAYLOAD_KIND_COUNT, 4);

    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_NONE, 0);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_WINDOW, 1);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_APPLICATION, 2);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_DOCUMENT, 3);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_KIND_COUNT, 4);

    /* Diagnostic domains: which boundary refused. */
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_NONE, 0);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_EVENT, 1);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_SPACE, 2);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_QUEUE, 3);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_TARGET, 4);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_BRIDGE, 5);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_DUMP, 6);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_DOMAIN_COUNT, 7);

    /* Every value of every vocabulary has a name, and no two types share one. The cleared value of the
     * *source*, *space*, *kind* and *domain* vocabularies is legitimately named "unknown", which is why the
     * check runs from 1 for those and from 0 for the event types. */
    {
        uint32_t index;

        for (index = 0u; index < ROBO_INPUT_EVENT_TYPE_COUNT; index++) {
            const char *name = robo_input_event_type_name((robo_input_event_type_t)index);

            ROBO_CHECK(name != NULL);
            ROBO_CHECK_STR_EQ(robo_input_event_type_name((robo_input_event_type_t)index), name);
        }
        ROBO_CHECK_STR_EQ(robo_input_event_type_name(ROBO_INPUT_EVENT_NONE), "None");
        ROBO_CHECK_STR_EQ(robo_input_event_type_name(ROBO_INPUT_EVENT_TOUCH_CANCEL), "TouchCancel");
        ROBO_CHECK_STR_EQ(robo_input_event_type_name((robo_input_event_type_t)13), "unknown");

        for (index = 1u; index < ROBO_INPUT_SOURCE_COUNT; index++) {
            ROBO_CHECK(strcmp(robo_input_source_name((robo_input_source_t)index), "unknown") != 0);
        }
        ROBO_CHECK_STR_EQ(robo_input_source_name(ROBO_INPUT_SOURCE_UNKNOWN), "unknown");
        ROBO_CHECK_STR_EQ(robo_input_source_name(ROBO_INPUT_SOURCE_GAME_CONTROLLER), "game-controller");

        for (index = 1u; index < ROBO_INPUT_SPACE_COUNT; index++) {
            ROBO_CHECK(strcmp(robo_input_space_name((robo_input_space_t)index), "unknown") != 0);
        }
        ROBO_CHECK_STR_EQ(robo_input_space_name(ROBO_INPUT_SPACE_DOCUMENT), "document");

        for (index = 1u; index < ROBO_INPUT_PAYLOAD_KIND_COUNT; index++) {
            ROBO_CHECK(strcmp(robo_input_payload_kind_name((robo_input_payload_kind_t)index),
                              "unknown") != 0);
        }
        for (index = 1u; index < ROBO_INPUT_TARGET_KIND_COUNT; index++) {
            ROBO_CHECK(strcmp(robo_input_target_kind_name((robo_input_target_kind_t)index), "unknown") !=
                       0);
        }
        for (index = 1u; index < ROBO_INPUT_DOMAIN_COUNT; index++) {
            ROBO_CHECK(strcmp(robo_input_domain_name((robo_input_diagnostic_domain_t)index), "unknown") !=
                       0);
        }
        ROBO_CHECK_STR_EQ(robo_input_domain_name(ROBO_INPUT_DOMAIN_SPACE), "space");
    }

    /* "Is it known?" agrees with the numbered range, at both ends and just outside. */
    ROBO_CHECK(robo_input_event_type_is_known(ROBO_INPUT_EVENT_NONE));
    ROBO_CHECK(robo_input_event_type_is_known(ROBO_INPUT_EVENT_TOUCH_CANCEL));
    ROBO_CHECK(!robo_input_event_type_is_known((robo_input_event_type_t)13));
    ROBO_CHECK(!robo_input_event_type_is_deliverable(ROBO_INPUT_EVENT_NONE));
    ROBO_CHECK(robo_input_event_type_is_deliverable(ROBO_INPUT_EVENT_KEY_DOWN));

    ROBO_CHECK(!robo_input_source_is_known(ROBO_INPUT_SOURCE_UNKNOWN));
    ROBO_CHECK(!robo_input_source_is_known((robo_input_source_t)9));
    ROBO_CHECK(!robo_input_space_is_known(ROBO_INPUT_SPACE_UNKNOWN));
    ROBO_CHECK(!robo_input_space_is_known((robo_input_space_t)5));
    ROBO_CHECK(!robo_input_target_kind_is_known((robo_input_target_kind_t)4));
    ROBO_CHECK(!robo_input_domain_is_known((robo_input_diagnostic_domain_t)7));

    /* Only three planes can be converted through: the application plane is named and never converted. */
    ROBO_CHECK(robo_input_space_is_convertible(ROBO_INPUT_SPACE_SCREEN));
    ROBO_CHECK(robo_input_space_is_convertible(ROBO_INPUT_SPACE_WINDOW));
    ROBO_CHECK(robo_input_space_is_convertible(ROBO_INPUT_SPACE_DOCUMENT));
    ROBO_CHECK(!robo_input_space_is_convertible(ROBO_INPUT_SPACE_APPLICATION));
    ROBO_CHECK(!robo_input_space_is_convertible(ROBO_INPUT_SPACE_UNKNOWN));
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

static void test_conformance_capabilities(void)
{
    robo_input_capabilities_t caps;

    ROBO_CHECK_EQ_INT(robo_input_get_capabilities(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_get_capabilities(&caps), ROBO_OK);

    /* What exists. */
    ROBO_CHECK_EQ_INT(caps.event_model, 1);
    ROBO_CHECK_EQ_INT(caps.coordinate_spaces, 1);
    ROBO_CHECK_EQ_INT(caps.target_identity, 1);
    ROBO_CHECK_EQ_INT(caps.event_queue, 1);
    ROBO_CHECK_EQ_INT(caps.debug_dump, 1);
    ROBO_CHECK_EQ_INT(caps.window_bridge, 1);

    /* What deliberately does not, each one named so that a reader cannot mistake the model for a system. */
    ROBO_CHECK_EQ_INT(caps.device_reading, 0);
    ROBO_CHECK_EQ_INT(caps.platform_adapters, 0);
    ROBO_CHECK_EQ_INT(caps.dispatch, 0);
    ROBO_CHECK_EQ_INT(caps.focus, 0);
    ROBO_CHECK_EQ_INT(caps.text_editing, 0);
    ROBO_CHECK_EQ_INT(caps.text_composition, 0);
    ROBO_CHECK_EQ_INT(caps.clipboard, 0);
    ROBO_CHECK_EQ_INT(caps.propagation, 0);
    ROBO_CHECK_EQ_INT(caps.gestures, 0);
    ROBO_CHECK_EQ_INT(caps.event_loop, 0);
    ROBO_CHECK_EQ_INT(caps.multiprocess_input, 0);

    ROBO_CHECK_EQ_INT(caps.event_type_count, ROBO_INPUT_EVENT_TYPE_COUNT);
    ROBO_CHECK_EQ_INT(caps.source_count, ROBO_INPUT_SOURCE_COUNT);
    ROBO_CHECK_EQ_INT(caps.space_count, ROBO_INPUT_SPACE_COUNT);
    ROBO_CHECK_STR_EQ(caps.version, ROBO_INPUT_VERSION_STRING);
    ROBO_CHECK_STR_EQ(caps.version, robo_input_version_string());

    /* The flag constants and the door agree: `model_is_implemented` is the same fact as `event_model`. */
    ROBO_CHECK_EQ_INT(robo_input_model_is_implemented(), caps.event_model);
    ROBO_CHECK_EQ_INT(robo_input_platform_adapters_are_implemented(), caps.platform_adapters);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_API_FROZEN, 0);

    /* Version components: numeric and string forms of one version. */
    {
        robo_i32 major = -1;
        robo_i32 minor = -1;
        robo_i32 patch = -1;

        ROBO_CHECK_EQ_INT(robo_input_get_version(&major, &minor, &patch), ROBO_OK);
        ROBO_CHECK_EQ_INT(major, ROBO_INPUT_VERSION_MAJOR);
        ROBO_CHECK_EQ_INT(minor, ROBO_INPUT_VERSION_MINOR);
        ROBO_CHECK_EQ_INT(patch, ROBO_INPUT_VERSION_PATCH);
        /* NULL means "do not want that one", which is a legitimate request rather than an error. */
        ROBO_CHECK_EQ_INT(robo_input_get_version(NULL, NULL, NULL), ROBO_OK);
    }
}

/* ------------------------------------------------------------------ */
/* The model dump                                                      */
/* ------------------------------------------------------------------ */

static void test_model_dump(void)
{
    char first[ROBO_INPUT_MODEL_DUMP_MAX];
    char second[ROBO_INPUT_MODEL_DUMP_MAX];
    size_t first_length = 0u;
    size_t second_length = 0u;
    size_t index;
    int type_lines = 0;
    int source_lines = 0;
    int space_lines = 0;
    int summary_lines = 0;

    ROBO_CHECK_EQ_INT(robo_input_write_model(NULL, sizeof(first), &first_length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_write_model(first, 0u, &first_length, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_write_model(first, sizeof(first), NULL, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_input_write_model(first, sizeof(first), &first_length, NULL), ROBO_OK);
    ROBO_CHECK(first_length > 0u);
    /* The stated constant is the *ceiling*: a model dump that needed more than it would mean a caller who
     * sized a buffer from the header was misled. */
    ROBO_CHECK(first_length < (size_t)ROBO_INPUT_MODEL_DUMP_MAX);
    ROBO_CHECK_EQ_INT((int)strlen(first), (int)first_length);

    ROBO_CHECK_EQ_INT(robo_input_write_model(second, sizeof(second), &second_length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)second_length, (int)first_length);
    ROBO_CHECK(strcmp(first, second) == 0);

    /* The summary line, whole, and first. */
    ROBO_CHECK(strstr(first, "input 0.1.0 model=1 adapters=0 types=13 sources=9 spaces=5\n") == first);

    /* One line per vocabulary entry, and no line of one vocabulary masquerading as another. */
    for (index = 0u; index + 1u < first_length; index++) {
        if (index == 0u || first[index] == '\n') {
            const char *line = first + (index == 0u ? index : index + 1u);

            if (strncmp(line, "type ", 5) == 0) {
                type_lines++;
            } else if (strncmp(line, "source ", 7) == 0) {
                source_lines++;
            } else if (strncmp(line, "space ", 6) == 0) {
                space_lines++;
            } else if (strncmp(line, "input ", 6) == 0) {
                summary_lines++;
            }
        }
    }
    ROBO_CHECK_EQ_INT(type_lines, (int)ROBO_INPUT_EVENT_TYPE_COUNT);
    ROBO_CHECK_EQ_INT(source_lines, (int)ROBO_INPUT_SOURCE_COUNT);
    ROBO_CHECK_EQ_INT(space_lines, (int)ROBO_INPUT_SPACE_COUNT);
    ROBO_CHECK_EQ_INT(summary_lines, 1);

    /* Sample lines: the shape of one from each vocabulary, with the facts a reader reads them for. */
    ROBO_CHECK(strstr(first, "\ntype KeyDown payload=keyboard position=0 release=0 key=1 touch=0\n") != NULL);
    ROBO_CHECK(strstr(first, "\ntype PointerMove payload=pointer position=1 release=0 key=0 touch=0\n") !=
               NULL);
    ROBO_CHECK(strstr(first, "\ntype TouchCancel payload=pointer position=1 release=1 key=0 touch=1\n") !=
               NULL);
    ROBO_CHECK(strstr(first, "\ntype None payload=none position=0 release=0 key=0 touch=0\n") != NULL);
    ROBO_CHECK(strstr(first, "\nsource mouse positioning=1\n") != NULL);
    ROBO_CHECK(strstr(first, "\nsource keyboard positioning=0\n") != NULL);
    ROBO_CHECK(strstr(first, "\nsource game-controller positioning=1\n") != NULL);
    ROBO_CHECK(strstr(first, "\nspace window convertible=1\n") != NULL);
    ROBO_CHECK(strstr(first, "\nspace application convertible=0\n") != NULL);

    /* A buffer one byte short of the text reports the length it needed, and stores a `?` rather than a
     * prefix that reads as a whole dump. */
    {
        char small[8];
        size_t needed = 0u;

        ROBO_CHECK_EQ_INT(robo_input_write_model(small, sizeof(small), &needed, NULL), ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT((int)needed, (int)first_length);
        ROBO_CHECK_STR_EQ(small, "?");
    }
}

void robo_test_register_input_conformance(void)
{
    robo_test_register("input.conformance.vocabulary", test_conformance_vocabulary);
    robo_test_register("input.conformance.capabilities", test_conformance_capabilities);
    robo_test_register("input.model.dump", test_model_dump);
}
