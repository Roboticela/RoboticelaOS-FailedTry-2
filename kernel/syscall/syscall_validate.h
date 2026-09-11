#ifndef ROBO_SYSCALL_VALIDATE_H
#define ROBO_SYSCALL_VALIDATE_H

#include "syscall/syscall_types.h"
#include <stddef.h>

int robo_u64_add_ok_sys(uint64_t a, uint64_t b, uint64_t *out);
int robo_user_range_valid(RoboAddressSpace *as, uint64_t address,
                          uint64_t length, uint64_t access);
int robo_user_string_valid(RoboAddressSpace *as, uint64_t address,
                           size_t max_length, size_t *out_len);
int robo_copy_from_user(RoboAddressSpace *as, void *kernel_destination,
                        uint64_t user_source, size_t length);
int robo_copy_to_user(RoboAddressSpace *as, uint64_t user_destination,
                      const void *kernel_source, size_t length);
int robo_copy_user_string(RoboAddressSpace *as, char *kernel_destination,
                          uint64_t user_source, size_t max_length);
int robo_user_return_ok(const struct robo_iframe *frame);

#endif
