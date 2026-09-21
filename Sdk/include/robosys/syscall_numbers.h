#ifndef ROBOSYS_SYSCALL_NUMBERS_H
#define ROBOSYS_SYSCALL_NUMBERS_H

/*
 * Roboticela OS x86_64 Syscall ABI v1 — numbers.
 *
 * These values are part of the public contract. Do not renumber.
 * Removed calls keep their number as ROBO_SYS_RESERVED_*.
 *
 *   0–31     core ABI v1
 *   32       SYS_THREAD (multiplexed thread/TLS ops)
 *   33–99    reserved core
 *   100–199  process/thread (future)
 *   200–299  memory (future)
 *   300–399  IPC (future)
 *   400–499  filesystem (future)
 *   500+     experimental (never stable merely by use)
 */

#define RoboSyscallAbiMajor 1u
#define RoboSyscallAbiMinor 0u

#define RoboSysAbiInfo         0u
#define RoboSysExit             1u
#define RoboSysRead             2u
#define RoboSysWrite            3u
#define RoboSysOpen             4u
#define RoboSysClose            5u
#define RoboSysMmap             6u
#define RoboSysMunmap           7u
#define RoboSysProcessCreate   8u
#define RoboSysThreadCreate    9u
#define RoboSysSleep           10u
#define RoboSysTime            11u
#define RoboSysIpc             12u
#define RoboSysReserved13     13u
#define RoboSysReserved14     14u
#define RoboSysReserved15     15u
#define RoboSysSeek            16u
#define RoboSysStat            17u
#define RoboSysMkdir           18u
#define RoboSysRmdir           19u
#define RoboSysUnlink          20u
#define RoboSysRename          21u

#define RoboSysWait            22u
#define RoboSysGetpid          23u
#define RoboSysDriverQuery    24u
#define RoboSysDisplay         25u
#define RoboSysSocket         26u
#define RoboSysReserved27    27u
#define RoboSysReserved28    28u
#define RoboSysReserved29    29u
#define RoboSysReserved30    30u
#define RoboSysReserved31    31u
#define RoboSysThread         32u

#define RoboSysTableSize      64u
#define RoboSysExperimentalBase 500u

#define RoboThreadOpYield   1u
#define RoboThreadOpExit    2u
#define RoboThreadOpJoin    3u
#define RoboThreadOpSetFs  4u
#define RoboThreadOpGettid  5u
#define RoboThreadOpGetppid 6u

#define RoboProtRead  1u
#define RoboProtWrite 2u
#define RoboProtExec  4u

#define RoboMapPrivate 1u
#define RoboMapAnon    2u
#define RoboMapFixed   4u

#define RoboORdonly 0u
#define RoboOWronly 1u
#define RoboORdwr   2u
#define RoboOCreat  0x40u
#define RoboOTrunc  0x200u

#define RoboSeekSet 0
#define RoboSeekCur 1
#define RoboSeekEnd 2

#define RoboWaitNohang 1u

#define RoboIpcOpSend       1u
#define RoboIpcOpRecv       2u
#define RoboIpcOpListen     3u
#define RoboIpcOpConnect    4u
#define RoboIpcOpClose      5u
#define RoboIpcOpShmCreate 6u
#define RoboIpcOpShmMap    7u
#define RoboIpcOpShmUnmap  8u

#define RoboIpcNowait 1u
#define RoboIpcAcceptMagic 0x41434354u

#define RoboIpcNameWindowServer "window-server"
#define RoboIpcNameCompositor    "compositor"

#endif
