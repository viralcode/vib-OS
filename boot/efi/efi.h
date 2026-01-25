/*
 * UnixOS Kernel - EFI Stub Header
 * 
 * UEFI boot support for ARM64 Apple Silicon
 */

#ifndef _BOOT_EFI_H
#define _BOOT_EFI_H

#include "types.h"

/* ===================================================================== */
/* EFI Types */
/* ===================================================================== */

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;

/* EFI Status codes */
#define EFI_SUCCESS             0
#define EFI_LOAD_ERROR          1
#define EFI_INVALID_PARAMETER   2
#define EFI_UNSUPPORTED         3
#define EFI_BAD_BUFFER_SIZE     4
#define EFI_BUFFER_TOO_SMALL    5
#define EFI_NOT_READY           6
#define EFI_DEVICE_ERROR        7
#define EFI_WRITE_PROTECTED     8
#define EFI_OUT_OF_RESOURCES    9
#define EFI_NOT_FOUND           14

/* ===================================================================== */
/* EFI Memory Types */
/* ===================================================================== */

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

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* ===================================================================== */
/* EFI Table Header */
/* ===================================================================== */

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

/* ===================================================================== */
/* EFI Simple Text Output Protocol */
/* ===================================================================== */

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint16_t *String);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    void *ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
};

/* ===================================================================== */
/* EFI Boot Services */
/* ===================================================================== */

typedef struct {
    EFI_TABLE_HEADER Hdr;
    
    /* Task Priority Services */
    void *RaiseTPL;
    void *RestoreTPL;
    
    /* Memory Services */
    EFI_STATUS (*AllocatePages)(uint32_t Type, EFI_MEMORY_TYPE MemoryType,
                                 uint64_t Pages, EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, uint64_t Pages);
    EFI_STATUS (*GetMemoryMap)(uint64_t *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                uint64_t *MapKey, uint64_t *DescriptorSize,
                                uint32_t *DescriptorVersion);
    void *AllocatePool;
    void *FreePool;
    
    /* Event & Timer Services */
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    
    /* Protocol Handler Services */
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    
    /* Image Services */
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, uint64_t MapKey);
    
    /* Misc Services */
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    
    /* Driver Support Services */
    void *ConnectController;
    void *DisconnectController;
    
    /* Open/Close Protocol Services */
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    
    /* Library Services */
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    void *LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    
    /* CRC Services */
    void *CalculateCrc32;
    
    /* Misc Services */
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

/* ===================================================================== */
/* EFI System Table */
/* ===================================================================== */

typedef struct {
    EFI_TABLE_HEADER Hdr;
    uint16_t *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    uint64_t NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ===================================================================== */
/* EFI Graphics Output Protocol */
/* ===================================================================== */

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
    uint64_t SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    uint64_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

/* Graphics Output Protocol GUID */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * efi_main - EFI entry point
 * @ImageHandle: Handle to the loaded image
 * @SystemTable: Pointer to EFI system table
 * 
 * Return: EFI status code
 */
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

#endif /* _BOOT_EFI_H */
