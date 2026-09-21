#ifndef ROBO_EXEC_ERROR_H
#define ROBO_EXEC_ERROR_H

typedef enum {
    RoboExecOk = 0,
    RoboElfInvalidMagic,
    RoboElfInvalidClass,
    RoboElfInvalidEndian,
    RoboElfInvalidMachine,
    RoboElfInvalidVersion,
    RoboElfInvalidHeader,
    RoboElfInvalidProgramHeader,
    RoboElfInvalidSegment,
    RoboElfAddressOverflow,
    RoboElfAddressInvalid,
    RoboElfEntryInvalid,
    RoboElfUnsupportedType,
    RoboElfWx,
    RoboElfOverlap,
    RoboElfMemoryFailure,
    RoboElfFileFailure,
    RoboElfNoLoad
} ExecStatus;

static inline const char *ExecStatusName(ExecStatus s)
{
    switch (s) {
    case RoboExecOk: return "ok";
    case RoboElfInvalidMagic: return "Invalid ELF magic";
    case RoboElfInvalidClass: return "Invalid ELF class";
    case RoboElfInvalidEndian: return "Invalid ELF endianness";
    case RoboElfInvalidMachine: return "Unsupported machine";
    case RoboElfInvalidVersion: return "Invalid ELF version";
    case RoboElfInvalidHeader: return "Invalid ELF header";
    case RoboElfInvalidProgramHeader: return "Invalid program header";
    case RoboElfInvalidSegment: return "Invalid segment";
    case RoboElfAddressOverflow: return "Address overflow";
    case RoboElfAddressInvalid: return "Invalid virtual address";
    case RoboElfEntryInvalid: return "Entry point is not inside an executable user mapping.";
    case RoboElfUnsupportedType: return "Unsupported ELF type";
    case RoboElfWx: return "W^X violation";
    case RoboElfOverlap: return "Overlapping PT_LOAD segments";
    case RoboElfMemoryFailure: return "Unable to allocate physical page.";
    case RoboElfFileFailure: return "File read failed";
    case RoboElfNoLoad: return "No PT_LOAD segments";
    default: return "Unknown error";
    }
}

#endif
