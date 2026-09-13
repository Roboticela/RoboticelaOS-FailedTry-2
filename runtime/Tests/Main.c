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
    return robo_test_main(argc, argv);
}
