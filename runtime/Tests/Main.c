/*
 * Unified Roboticela Runtime test executable (Goal 1.5).
 */

#include "RoboBase/RoboDebug.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"

void robo_test_register_base(void);
void robo_test_register_log(void);
void robo_test_register_error(void);
void robo_test_register_debug(void);
void robo_test_register_mem(void);
void robo_test_register_mem_debug(void);
void robo_test_register_mem_system(void);
void robo_test_register_arena(void);
void robo_test_register_pool(void);
void robo_test_register_array(void);
void robo_test_register_string(void);
void robo_test_register_unicode(void);
void robo_test_register_unicode_props(void);
void robo_test_register_unicode_norm(void);
void robo_test_register_unicode_grapheme(void);
void robo_test_register_unicode_case(void);
void robo_test_register_unicode_word(void);
void robo_test_register_unicode_sentence(void);
void robo_test_register_unicode_line(void);
void robo_test_register_unicode_bidi(void);
void robo_test_register_unicode_script(void);
void robo_test_register_unicode_emoji(void);
void robo_test_register_unicode_infra(void);
void robo_test_register_i18n(void);
void robo_test_register_i18n_cldr(void);
void robo_test_register_i18n_number(void);
void robo_test_register_i18n_plural(void);
void robo_test_register_i18n_datetime(void);
void robo_test_register_i18n_datetime_format(void);
void robo_test_register_i18n_interval(void);
void robo_test_register_i18n_relative(void);
void robo_test_register_i18n_datetime_arith(void);
void robo_test_register_i18n_collation(void);
void robo_test_register_i18n_display_names(void);
void robo_test_register_i18n_message(void);
void robo_test_register_i18n_message_format(void);
void robo_test_register_i18n_message_validate(void);
void robo_test_register_i18n_timezone(void);
void robo_test_register_i18n_timezone_display(void);
void robo_test_register_i18n_locale_service(void);
void robo_test_register_i18n_locale_negotiate(void);
void robo_test_register_i18n_ordinal(void);
void robo_test_register_i18n_unit(void);
void robo_test_register_i18n_measure(void);
void robo_test_register_i18n_currency(void);
void robo_test_register_i18n_parse(void);
void robo_test_register_i18n_datetime_parse(void);
void robo_test_register_i18n_search(void);
void robo_test_register_i18n_translit(void);
void robo_test_register_i18n_conformance(void);
void robo_test_register_i18n_phase4(void);
void robo_test_register_font(void);
void robo_test_register_font_sfnt(void);
void robo_test_register_font_tt(void);
void robo_test_register_font_ot(void);
void robo_test_register_font_cff(void);
void robo_test_register_font_cmap(void);
void robo_test_register_font_metrics(void);
void robo_test_register_font_outline(void);

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--child-panic") == 0) {
        robo_set_panic_handler(NULL, NULL);
        robo_panic("controlled child panic");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-assert") == 0) {
        robo_set_panic_handler(NULL, NULL);
        ROBO_ASSERT(0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-double-free") == 0) {
        void *p;
        robo_mem_debug_enable();
        robo_mem_debug_set_fault_handler(NULL, NULL);
        p = robo_malloc(16);
        robo_free(p);
        robo_free(p);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-invalid-free") == 0) {
        int local = 1;
        robo_mem_debug_enable();
        robo_mem_debug_set_fault_handler(NULL, NULL);
        robo_free(&local);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-underflow") == 0) {
        void *p;
        robo_mem_debug_enable();
        robo_mem_debug_set_fault_handler(NULL, NULL);
        p = robo_malloc(32);
        robo_mem_debug_corrupt_guard_for_test(p, 0);
        robo_free(p);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--child-mem-overflow") == 0) {
        void *p;
        robo_mem_debug_enable();
        robo_mem_debug_set_fault_handler(NULL, NULL);
        p = robo_malloc(32);
        robo_mem_debug_corrupt_guard_for_test(p, 1);
        robo_free(p);
        return 0;
    }

    robo_test_register_base();
    robo_test_register_log();
    robo_test_register_error();
    robo_test_register_debug();
    robo_test_register_mem();
    robo_test_register_mem_debug();
    robo_test_register_mem_system();
    robo_test_register_arena();
    robo_test_register_pool();
    robo_test_register_array();
    robo_test_register_string();
    robo_test_register_unicode();
    robo_test_register_unicode_props();
    robo_test_register_unicode_norm();
    robo_test_register_unicode_grapheme();
    robo_test_register_unicode_case();
    robo_test_register_unicode_word();
    robo_test_register_unicode_sentence();
    robo_test_register_unicode_line();
    robo_test_register_unicode_bidi();
    robo_test_register_unicode_script();
    robo_test_register_unicode_emoji();
    robo_test_register_unicode_infra();
    robo_test_register_i18n();
    robo_test_register_i18n_cldr();
    robo_test_register_i18n_number();
    robo_test_register_i18n_plural();
    robo_test_register_i18n_datetime();
    robo_test_register_i18n_datetime_format();
    robo_test_register_i18n_interval();
    robo_test_register_i18n_relative();
    robo_test_register_i18n_datetime_arith();
    robo_test_register_i18n_collation();
    robo_test_register_i18n_display_names();
    robo_test_register_i18n_message();
    robo_test_register_i18n_message_format();
    robo_test_register_i18n_message_validate();
    robo_test_register_i18n_timezone();
    robo_test_register_i18n_timezone_display();
    robo_test_register_i18n_locale_service();
    robo_test_register_i18n_locale_negotiate();
    robo_test_register_i18n_ordinal();
    robo_test_register_i18n_unit();
    robo_test_register_i18n_measure();
    robo_test_register_i18n_currency();
    robo_test_register_i18n_parse();
    robo_test_register_i18n_datetime_parse();
    robo_test_register_i18n_search();
    robo_test_register_i18n_translit();
    robo_test_register_i18n_conformance();
    robo_test_register_i18n_phase4();
    robo_test_register_font();
    robo_test_register_font_sfnt();
    robo_test_register_font_tt();
    robo_test_register_font_ot();
    robo_test_register_font_cff();
    robo_test_register_font_cmap();
    robo_test_register_font_metrics();
    robo_test_register_font_outline();
    return robo_test_main(argc, argv);
}
