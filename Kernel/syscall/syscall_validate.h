#ifndef ROBO_SYSCALL_VALIDATE_H
#define ROBO_SYSCALL_VALIDATE_H

#include "syscall/syscall_types.h"
#include <stddef.h>

int U64AddOkSys(uint64_t a, uint64_t b, uint64_t *out);
int UserRangeValid(RoboAddressSpace *as, uint64_t address,
                          uint64_t length, uint64_t access);
int UserStringValid(RoboAddressSpace *as, uint64_t address,
                           size_t maxLength, size_t *outLen);
int CopyFromUser(RoboAddressSpace *as, void *kernelDestination,
                        uint64_t userSource, size_t length);
int CopyToUser(RoboAddressSpace *as, uint64_t userDestination,
                      const void *kernelSource, size_t length);
int CopyUserString(RoboAddressSpace *as, char *kernelDestination,
                          uint64_t userSource, size_t maxLength);
int UserReturnOk(const struct Iframe *frame);

#endif
