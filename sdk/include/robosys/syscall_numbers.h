#ifndef ROBOSYS_SYSCALL_NUMBERS_H
#define ROBOSYS_SYSCALL_NUMBERS_H

/*
 * Roboticela OS x86_64 Syscall ABI v1 — numbers.
 *
 * These values are part of the public contract. Do not renumber.
 * Removed calls keep their number as ROBO_SYS_RESERVED_*.
 *
 *   0–31     core ABI v1
 *   32–99    reserved core
 *   100–199  process/thread (future)
 *   200–299  memory (future)
 *   300–399  IPC (future)
 *   400–499  filesystem (future)
 *   500+     experimental (never stable merely by use)
 */

#define ROBO_SYSCALL_ABI_MAJOR 1u
#define ROBO_SYSCALL_ABI_MINOR 0u

#define ROBO_SYS_ABI_INFO         0u
#define ROBO_SYS_EXIT             1u
#define ROBO_SYS_READ             2u
#define ROBO_SYS_WRITE            3u
#define ROBO_SYS_OPEN             4u
#define ROBO_SYS_CLOSE            5u
#define ROBO_SYS_MMAP             6u
#define ROBO_SYS_MUNMAP           7u
#define ROBO_SYS_PROCESS_CREATE   8u
#define ROBO_SYS_THREAD_CREATE    9u
#define ROBO_SYS_SLEEP           10u
#define ROBO_SYS_TIME            11u
#define ROBO_SYS_IPC             12u

#define ROBO_SYS_RESERVED_13     13u
#define ROBO_SYS_RESERVED_14     14u
#define ROBO_SYS_RESERVED_15     15u

#define ROBO_SYS_TABLE_SIZE      32u
#define ROBO_SYS_EXPERIMENTAL_BASE 500u

#define ROBO_PROT_READ  1u
#define ROBO_PROT_WRITE 2u
#define ROBO_PROT_EXEC  4u

#define ROBO_MAP_PRIVATE 1u
#define ROBO_MAP_ANON    2u
#define ROBO_MAP_FIXED   4u

#define ROBO_O_RDONLY 0u

#define ROBO_IPC_OP_SEND 1u
#define ROBO_IPC_OP_RECV 2u

#endif
