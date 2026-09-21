/*
 * RoboInput keyboard suite (Phase 12.1) — `input.keyboard.*`.
 *
 * The payload is a *record of what a key did*: RoboWindow's two key identities plus the three things those
 * identities cannot carry (the action, the repeat flag, the text). These cases are about the three ways a
 * producer can get that wrong, since each one is a mistake a consumer would act on: an action that disagrees
 * with the event, a repeat flag invented for a release, or text on an event that produced none.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The payload                                                         */
/* ------------------------------------------------------------------ */

static void test_keyboard_payload(void)
{
    robo_input_keyboard_data_t data;

    robo_input_keyboard_data_init(&data);
    ROBO_CHECK_EQ_INT(data.physical, ROBO_PHYSICAL_KEY_UNKNOWN);
    ROBO_CHECK_EQ_INT(data.logical, ROBO_LOGICAL_KEY_UNKNOWN);
    ROBO_CHECK_EQ_INT(data.action, ROBO_KEY_ACTION_NONE);
    ROBO_CHECK_EQ_INT(data.repeat, 0);
    ROBO_CHECK_EQ_INT(data.text[0], '\0');
    /* A cleared payload is not a payload that happened: no action means the validator refuses it. */
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    robo_input_keyboard_data_init(NULL);

    /* A press of A, with the text the key produced. The *derived* flags are the interesting part: the
     * caller never sets `PHYSICAL_UNKNOWN` or `MODIFIER`, so they cannot forget them either. */
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                  ROBO_KEY_ACTION_DOWN, 0, "a"),
                      ROBO_OK);
    ROBO_CHECK(robo_input_keyboard_data_is_valid(&data));
    ROBO_CHECK_EQ_INT(data.flags, 0u);
    ROBO_CHECK_STR_EQ(data.text, "a");
    ROBO_CHECK_EQ_INT(data.scan_code, 0u);

    /* ... a modifier key reports itself as one, a numpad key reports its own flag, and both at once for a
     * numpad-located modifier is not a thing this table has. */
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_SHIFT_LEFT,
                                                  ROBO_LOGICAL_KEY_SHIFT, ROBO_KEY_ACTION_DOWN, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.flags, (uint32_t)ROBO_INPUT_KEY_FLAG_MODIFIER);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_NUMPAD_5,
                                                  ROBO_LOGICAL_KEY_NUMPAD_5, ROBO_KEY_ACTION_DOWN, 0, "5"),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.flags, (uint32_t)ROBO_INPUT_KEY_FLAG_NUMPAD);

    /* ... a key with no physical identity says so, and the flag it sets is the reason the record stays
     * valid: "I do not know which key this was" is a fact, not a missing field. */
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_UNKNOWN,
                                                  ROBO_LOGICAL_KEY_F5, ROBO_KEY_ACTION_DOWN, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.flags, (uint32_t)ROBO_INPUT_KEY_FLAG_PHYSICAL_UNKNOWN);
    ROBO_CHECK(robo_input_keyboard_data_is_valid(&data));

    /* A repeat is normalized to a flag, and a release can never be a repeat whatever the caller says. */
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                  ROBO_KEY_ACTION_DOWN, 7, "a"),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.repeat, 1);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                  ROBO_KEY_ACTION_UP, 7, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.repeat, 0);

    /* What is refused: no action, an action that is not down/up, and an identity outside the vocabulary.
     * Each leaves the payload cleared rather than half-filled. */
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                  ROBO_KEY_ACTION_NONE, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(data.action, ROBO_KEY_ACTION_NONE);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, (robo_physical_key_t)999,
                                                  ROBO_LOGICAL_KEY_A, ROBO_KEY_ACTION_DOWN, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A,
                                                  (robo_logical_key_t)999, ROBO_KEY_ACTION_DOWN, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(NULL, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                  ROBO_KEY_ACTION_DOWN, 0, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Text is bounded, and a fragment that does not fit is *refused*, not truncated: a truncated keystroke
     * is a different character. */
    {
        char long_text[ROBO_INPUT_TEXT_MAX + 4];
        size_t index;

        for (index = 0u; index < sizeof(long_text) - 1u; index++) {
            long_text[index] = 'x';
        }
        long_text[sizeof(long_text) - 1u] = '\0';
        ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                      ROBO_KEY_ACTION_DOWN, 0, long_text),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(data.text[0], '\0');
        ROBO_CHECK_EQ_INT(data.action, ROBO_KEY_ACTION_NONE);

        /* The longest text that *does* fit, for the bound's other side. */
        for (index = 0u; index + 1u < ROBO_INPUT_TEXT_MAX; index++) {
            long_text[index] = 'y';
        }
        long_text[ROBO_INPUT_TEXT_MAX - 1u] = '\0';
        ROBO_CHECK_EQ_INT(robo_input_keyboard_data_set(&data, ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                      ROBO_KEY_ACTION_DOWN, 0, long_text),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)strlen(data.text), (int)(ROBO_INPUT_TEXT_MAX - 1u));
    }
}

static void test_keyboard_validation(void)
{
    robo_input_keyboard_data_t data = input_key_data(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                     ROBO_KEY_ACTION_DOWN, 0, "a");

    ROBO_CHECK(robo_input_keyboard_data_is_valid(&data));
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(NULL));

    /* Undefined flag bits. */
    data.flags = (uint32_t)(1u << 20);
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.flags = 0u;

    /* The flag and the identity must agree in both directions. */
    data.flags = (uint32_t)ROBO_INPUT_KEY_FLAG_PHYSICAL_UNKNOWN;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.flags = 0u;
    data.physical = ROBO_PHYSICAL_KEY_UNKNOWN;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.flags = (uint32_t)ROBO_INPUT_KEY_FLAG_PHYSICAL_UNKNOWN;
    ROBO_CHECK(robo_input_keyboard_data_is_valid(&data));
    data.physical = ROBO_PHYSICAL_KEY_A;
    data.flags = 0u;

    /* An action that is not down/up, and a repeat that is neither 0 nor 1. */
    data.action = ROBO_KEY_ACTION_NONE;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.action = ROBO_KEY_ACTION_DOWN;
    data.repeat = 2;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.repeat = 0;

    /* A release that carried text, and a buffer that is not terminated inside its capacity. */
    data.action = ROBO_KEY_ACTION_UP;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.action = ROBO_KEY_ACTION_DOWN;
    {
        size_t index;

        for (index = 0u; index < ROBO_INPUT_TEXT_MAX; index++) {
            data.text[index] = 'z';
        }
        ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
        data.text[0] = '\0';
    }

    /* An identity outside the vocabulary is refused by the validator too, not only by the setter: a record
     * can arrive from anywhere. */
    data.physical = (robo_physical_key_t)999;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
    data.physical = ROBO_PHYSICAL_KEY_A;
    data.logical = (robo_logical_key_t)999;
    ROBO_CHECK(!robo_input_keyboard_data_is_valid(&data));
}

static void test_keyboard_modifiers(void)
{
    /* The four modifier groups report themselves and their side; the three locks report their lock. */
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_SHIFT_LEFT));
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_CONTROL_RIGHT));
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_ALT_LEFT));
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_META_RIGHT));
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_CAPS_LOCK));
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_NUM_LOCK));
    ROBO_CHECK(robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_SCROLL_LOCK));
    ROBO_CHECK(!robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_A));
    ROBO_CHECK(!robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_F5));
    ROBO_CHECK(!robo_input_physical_key_is_modifier(ROBO_PHYSICAL_KEY_UNKNOWN));

    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_SHIFT_LEFT),
                      ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_SHIFT_LEFT);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_SHIFT_RIGHT),
                      ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_SHIFT_RIGHT);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_CONTROL_LEFT),
                      ROBO_KEY_MOD_CTRL | ROBO_KEY_MOD_CTRL_LEFT);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_ALT_RIGHT),
                      ROBO_KEY_MOD_ALT | ROBO_KEY_MOD_ALT_RIGHT);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_META_LEFT),
                      ROBO_KEY_MOD_META | ROBO_KEY_MOD_META_LEFT);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_CAPS_LOCK),
                      ROBO_KEY_MOD_CAPS_LOCK);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_SCROLL_LOCK),
                      ROBO_KEY_MOD_SCROLL_LOCK);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_A), ROBO_KEY_MOD_NONE);
    ROBO_CHECK_EQ_INT(robo_input_physical_key_modifier(ROBO_PHYSICAL_KEY_UNKNOWN), ROBO_KEY_MOD_NONE);

    /* Every bit a key can produce is one the modifier vocabulary defines, which is what lets a state update
     * be a plain OR. */
    {
        int index;
        robo_input_modifiers_t produced = ROBO_INPUT_MODIFIERS_NONE;

        for (index = 0; index < (int)ROBO_PHYSICAL_KEY_COUNT; index++) {
            produced |= robo_input_physical_key_modifier((robo_physical_key_t)index);
        }
        ROBO_CHECK(robo_input_modifiers_are_known(produced));
        ROBO_CHECK(produced != ROBO_INPUT_MODIFIERS_NONE);
    }
}

static void test_keyboard_action_and_text(void)
{
    robo_input_keyboard_data_t down = input_key_data(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                     ROBO_KEY_ACTION_DOWN, 0, "a");
    robo_input_keyboard_data_t up = input_key_data(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                   ROBO_KEY_ACTION_UP, 0, NULL);
    char text[ROBO_INPUT_TEXT_MAX];

    /* The action a type implies, and the agreement rule the event validator leans on. */
    ROBO_CHECK_EQ_INT(robo_input_keyboard_action_for_event(ROBO_INPUT_EVENT_KEY_DOWN),
                      ROBO_KEY_ACTION_DOWN);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_action_for_event(ROBO_INPUT_EVENT_KEY_UP), ROBO_KEY_ACTION_UP);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_action_for_event(ROBO_INPUT_EVENT_POINTER_MOVE),
                      ROBO_KEY_ACTION_NONE);
    ROBO_CHECK_EQ_INT(robo_input_keyboard_action_for_event(ROBO_INPUT_EVENT_TEXT_INPUT),
                      ROBO_KEY_ACTION_NONE);
    ROBO_CHECK(robo_input_keyboard_data_action_matches(&down, ROBO_INPUT_EVENT_KEY_DOWN));
    ROBO_CHECK(!robo_input_keyboard_data_action_matches(&down, ROBO_INPUT_EVENT_KEY_UP));
    ROBO_CHECK(robo_input_keyboard_data_action_matches(&up, ROBO_INPUT_EVENT_KEY_UP));
    ROBO_CHECK(!robo_input_keyboard_data_action_matches(NULL, ROBO_INPUT_EVENT_KEY_DOWN));

    /* Reading the text out: copied, terminated, and never longer than the destination. */
    ROBO_CHECK_EQ_INT((int)robo_input_keyboard_data_text(&down, text, sizeof(text)), 1);
    ROBO_CHECK_STR_EQ(text, "a");
    ROBO_CHECK_EQ_INT((int)robo_input_keyboard_data_text(&down, text, 1u), 0);
    ROBO_CHECK_STR_EQ(text, "");
    ROBO_CHECK_EQ_INT((int)robo_input_keyboard_data_text(NULL, text, sizeof(text)), 0);
    ROBO_CHECK_EQ_INT((int)robo_input_keyboard_data_text(&down, NULL, sizeof(text)), 0);
    ROBO_CHECK_EQ_INT((int)robo_input_keyboard_data_text(&down, text, 0u), 0);

    /* Equality is field-by-field, and the text is part of it. */
    ROBO_CHECK(robo_input_keyboard_data_equal(&down, &down));
    ROBO_CHECK(robo_input_keyboard_data_equal(NULL, NULL));
    ROBO_CHECK(!robo_input_keyboard_data_equal(&down, NULL));
    ROBO_CHECK(!robo_input_keyboard_data_equal(&down, &up));
    ROBO_CHECK(robo_input_keyboard_data_equal(&up, &up));
}

void robo_test_register_input_keyboard(void)
{
    robo_test_register("input.keyboard.payload", test_keyboard_payload);
    robo_test_register("input.keyboard.validation", test_keyboard_validation);
    robo_test_register("input.keyboard.modifiers", test_keyboard_modifiers);
    robo_test_register("input.keyboard.action_and_text", test_keyboard_action_and_text);
}
