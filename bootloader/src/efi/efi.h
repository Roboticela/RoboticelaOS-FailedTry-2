#ifndef RB_EFI_H
#define RB_EFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EFIAPI
#define EFI_SUCCESS 0ull
#define EFIERR(x) (0x8000000000000000ull | (unsigned long long)(x))
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_NOT_READY EFIERR(6)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_ABORTED EFIERR(21)

typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;
typedef uint64_t EFI_TPL;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Pad1;
    uint32_t Nanosecond;
    int16_t TimeZone;
    uint8_t Daylight;
    uint8_t Pad2;
} EFI_TIME;

typedef struct {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct {
    uint32_t KeyShiftState;
    uint8_t KeyToggleState;
} EFI_KEY_STATE;

typedef struct {
    EFI_INPUT_KEY Key;
    EFI_KEY_STATE KeyState;
} EFI_KEY_DATA;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
    EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
    EFI_STATUS (EFIAPI *SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);
    EFI_STATUS (EFIAPI *EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);
    void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_KEY_NOTIFY_FUNCTION)(EFI_KEY_DATA *KeyData);

typedef struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadKeyStrokeEx)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_DATA *KeyData);
    EFI_EVENT WaitForKeyEx;
    EFI_STATUS (EFIAPI *SetState)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, uint8_t *KeyToggleState);
    EFI_STATUS (EFIAPI *RegisterKeyNotify)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_DATA *KeyData,
                                           EFI_KEY_NOTIFY_FUNCTION Function, void **NotifyHandle);
    EFI_STATUS (EFIAPI *UnregisterKeyNotify)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, void *NotifyHandle);
} EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

typedef struct {
    uint32_t Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef enum {
    EfiResetCold,
    EfiResetWarm,
    EfiResetShutdown,
    EfiResetPlatformSpecific
} EFI_RESET_TYPE;

typedef struct {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    uint8_t Blue;
    uint8_t Green;
    uint8_t Red;
    uint8_t Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {
    EfiBltVideoFill,
    EfiBltVideoToBltBuffer,
    EfiBltBufferToVideo,
    EfiBltVideoToVideo,
    EfiGraphicsOutputBltOperationMax
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber,
                                   UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
    EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber);
    EFI_STATUS (EFIAPI *Blt)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
                             EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
                             UINTN SourceX, UINTN SourceY, UINTN DestinationX, UINTN DestinationY,
                             UINTN Width, UINTN Height, UINTN Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName,
                              uint64_t OpenMode, uint64_t Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Write)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL *This, uint64_t *Position);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL *This, uint64_t Position);
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Flush)(EFI_FILE_PROTOCOL *This);
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    uint64_t ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    uint64_t Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

#define EFI_FILE_MODE_READ 0x0000000000000001ull
#define EFI_FILE_MODE_WRITE 0x0000000000000002ull
#define EFI_FILE_MODE_CREATE 0x8000000000000000ull
#define EFI_FILE_DIRECTORY 0x0000000000000010ull
#define EFI_FILE_ARCHIVE 0x0000000000000020ull

#define EFI_SHIFT_STATE_VALID 0x80000000u
#define EFI_LEFT_CONTROL_PRESSED 0x00000001u
#define EFI_RIGHT_CONTROL_PRESSED 0x00000002u
#define EFI_LEFT_ALT_PRESSED 0x00000010u
#define EFI_RIGHT_ALT_PRESSED 0x00000020u
#define EFI_LEFT_SHIFT_PRESSED 0x00000004u
#define EFI_RIGHT_SHIFT_PRESSED 0x00000008u

#define SCAN_NULL 0x00
#define SCAN_UP 0x01
#define SCAN_DOWN 0x02
#define SCAN_RIGHT 0x03
#define SCAN_LEFT 0x04
#define SCAN_F8 0x12
#define SCAN_F10 0x14
#define SCAN_ESC 0x17

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (EFIAPI *GetTime)(EFI_TIME *Time, void *Capabilities);
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    EFI_STATUS (EFIAPI *GetVariable)(CHAR16 *VariableName, EFI_GUID *VendorGuid, uint32_t *Attributes,
                                     UINTN *DataSize, void *Data);
    EFI_STATUS (EFIAPI *GetNextVariableName)(UINTN *VariableNameSize, CHAR16 *VariableName, EFI_GUID *VendorGuid);
    EFI_STATUS (EFIAPI *SetVariable)(CHAR16 *VariableName, EFI_GUID *VendorGuid, uint32_t Attributes,
                                     UINTN DataSize, void *Data);
    void *GetNextHighMonotonicCount;
    void (EFIAPI *ResetSystem)(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, void *ResetData);
    void *UpdateCapsule;
    void *QueryCapsuleCapabilities;
    void *QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
                                                UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                                UINTN *MapKey, UINTN *DescriptorSize, uint32_t *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle, UINTN MapKey);

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, void **Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);
    void *CreateEvent;
    void *SetTimer;
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
    void *SignalEvent;
    void *CloseEvent;
    EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT Event);
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle, void *DevicePath,
                                   void *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN Timeout, uint64_t WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
    EFI_STATUS (EFIAPI *ConnectController)(EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle,
                                          void *RemainingDevicePath, BOOLEAN Recursive);
    void *DisconnectController;
    EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface,
                                      EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, uint32_t Attributes);
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(int SearchType, EFI_GUID *Protocol, void *SearchKey,
                                           UINTN *NoHandles, EFI_HANDLE **Buffer);
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x00000002u
#define EFI_LOCATE_ALL_HANDLES 0
#define EFI_LOCATE_BY_PROTOCOL 2

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
};

typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
extern EFI_HANDLE gImage;

#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_WRITE_PROTECTED EFIERR(8)

#define EFI_VARIABLE_NON_VOLATILE 0x00000001u
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002u
#define EFI_VARIABLE_RUNTIME_ACCESS 0x00000004u
#define LOAD_OPTION_ACTIVE 0x00000001u

#define MEDIA_DEVICE_PATH 0x04
#define MEDIA_FILEPATH_DP 0x04
#define END_DEVICE_PATH_TYPE 0x7F
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xFF

#pragma pack(1)
typedef struct {
    uint8_t Type;
    uint8_t SubType;
    uint8_t Length[2];
} EFI_DEVICE_PATH_PROTOCOL;
#pragma pack()

void efi_log(const char *msg);
void efi_init_lib(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);
int efi_platform_init(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);
void efi_install_boot_entry(void);
const struct rb_platform *efi_platform(void);
void efi_fb_size(int *w, int *h);
void efi_graphics_info(uint64_t *fb, uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp, uint32_t *pixfmt);
void efi_shutdown(void);
int efi_flag_selftest(void);
int efi_flag_menu(void);
int efi_flag_splash_ms(void);
const char *efi_last_boot(void);

#ifdef __cplusplus
}
#endif

#endif
