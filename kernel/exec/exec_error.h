#ifndef ROBO_EXEC_ERROR_H
#define ROBO_EXEC_ERROR_H

typedef enum {
    ROBO_EXEC_OK = 0,
    ROBO_ELF_INVALID_MAGIC,
    ROBO_ELF_INVALID_CLASS,
    ROBO_ELF_INVALID_ENDIAN,
    ROBO_ELF_INVALID_MACHINE,
    ROBO_ELF_INVALID_VERSION,
    ROBO_ELF_INVALID_HEADER,
    ROBO_ELF_INVALID_PROGRAM_HEADER,
    ROBO_ELF_INVALID_SEGMENT,
    ROBO_ELF_ADDRESS_OVERFLOW,
    ROBO_ELF_ADDRESS_INVALID,
    ROBO_ELF_ENTRY_INVALID,
    ROBO_ELF_UNSUPPORTED_TYPE,
    ROBO_ELF_WX,
    ROBO_ELF_OVERLAP,
    ROBO_ELF_MEMORY_FAILURE,
    ROBO_ELF_FILE_FAILURE,
    ROBO_ELF_NO_LOAD
} robo_exec_status_t;

static inline const char *robo_exec_status_name(robo_exec_status_t s)
{
    switch (s) {
    case ROBO_EXEC_OK: return "ok";
    case ROBO_ELF_INVALID_MAGIC: return "Invalid ELF magic";
    case ROBO_ELF_INVALID_CLASS: return "Invalid ELF class";
    case ROBO_ELF_INVALID_ENDIAN: return "Invalid ELF endianness";
    case ROBO_ELF_INVALID_MACHINE: return "Unsupported machine";
    case ROBO_ELF_INVALID_VERSION: return "Invalid ELF version";
    case ROBO_ELF_INVALID_HEADER: return "Invalid ELF header";
    case ROBO_ELF_INVALID_PROGRAM_HEADER: return "Invalid program header";
    case ROBO_ELF_INVALID_SEGMENT: return "Invalid segment";
    case ROBO_ELF_ADDRESS_OVERFLOW: return "Address overflow";
    case ROBO_ELF_ADDRESS_INVALID: return "Invalid virtual address";
    case ROBO_ELF_ENTRY_INVALID: return "Entry point is not inside an executable user mapping.";
    case ROBO_ELF_UNSUPPORTED_TYPE: return "Unsupported ELF type";
    case ROBO_ELF_WX: return "W^X violation";
    case ROBO_ELF_OVERLAP: return "Overlapping PT_LOAD segments";
    case ROBO_ELF_MEMORY_FAILURE: return "Unable to allocate physical page.";
    case ROBO_ELF_FILE_FAILURE: return "File read failed";
    case ROBO_ELF_NO_LOAD: return "No PT_LOAD segments";
    default: return "Unknown error";
    }
}

#endif
