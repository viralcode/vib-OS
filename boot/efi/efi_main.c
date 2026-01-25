/*
 * UnixOS Kernel - EFI Stub Boot
 * 
 * UEFI boot stub that initializes hardware via EFI services
 * and then jumps to the main kernel.
 */

#include "efi.h"

/* ===================================================================== */
/* Global EFI pointers */
/* ===================================================================== */

static EFI_SYSTEM_TABLE *gST;
static EFI_BOOT_SERVICES *gBS;
static EFI_HANDLE gImageHandle;

/* ===================================================================== */
/* Console output helpers */
/* ===================================================================== */

static void efi_puts(const uint16_t *str)
{
    if (gST && gST->ConOut) {
        gST->ConOut->OutputString(gST->ConOut, (uint16_t *)str);
    }
}

static void efi_print(const char *str)
{
    uint16_t buf[256];
    int i;
    
    for (i = 0; str[i] && i < 255; i++) {
        buf[i] = str[i];
    }
    buf[i] = 0;
    
    efi_puts(buf);
}

static void efi_print_hex(uint64_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    
    buf[0] = '0';
    buf[1] = 'x';
    
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[18] = '\0';
    
    efi_print(buf);
}

/* ===================================================================== */
/* Memory map handling */
/* ===================================================================== */

static EFI_MEMORY_DESCRIPTOR *memory_map;
static uint64_t memory_map_size;
static uint64_t memory_map_key;
static uint64_t descriptor_size;
static uint32_t descriptor_version;

static EFI_STATUS get_memory_map(void)
{
    EFI_STATUS status;
    
    /* First call to get required size */
    memory_map_size = 0;
    status = gBS->GetMemoryMap(&memory_map_size, NULL, &memory_map_key,
                                &descriptor_size, &descriptor_version);
    
    /* Add space for extra entries (map may grow) */
    memory_map_size += 4 * descriptor_size;
    
    /* Allocate memory for map */
    status = gBS->AllocatePages(0, EfiLoaderData,
                                 (memory_map_size + 4095) / 4096,
                                 (EFI_PHYSICAL_ADDRESS *)&memory_map);
    if (status != EFI_SUCCESS) {
        efi_print("Failed to allocate memory for memory map\r\n");
        return status;
    }
    
    /* Get actual memory map */
    status = gBS->GetMemoryMap(&memory_map_size, memory_map, &memory_map_key,
                                &descriptor_size, &descriptor_version);
    if (status != EFI_SUCCESS) {
        efi_print("Failed to get memory map\r\n");
        return status;
    }
    
    return EFI_SUCCESS;
}

static void print_memory_map(void)
{
    efi_print("Memory Map:\r\n");
    
    uint64_t total_ram = 0;
    uint8_t *ptr = (uint8_t *)memory_map;
    uint64_t entries = memory_map_size / descriptor_size;
    
    for (uint64_t i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)ptr;
        
        if (desc->Type == EfiConventionalMemory) {
            total_ram += desc->NumberOfPages * 4096;
        }
        
        ptr += descriptor_size;
    }
    
    efi_print("Total conventional memory: ");
    efi_print_hex(total_ram);
    efi_print(" bytes (");
    efi_print_hex(total_ram / (1024 * 1024));
    efi_print(" MB)\r\n");
}

/* ===================================================================== */
/* Graphics initialization */
/* ===================================================================== */

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static EFI_PHYSICAL_ADDRESS framebuffer_base;
static uint64_t framebuffer_size;
static uint32_t screen_width;
static uint32_t screen_height;
static uint32_t pixels_per_scanline;

static EFI_STATUS init_graphics(void)
{
    /* For now, just use text mode */
    /* TODO: Locate GOP and set graphics mode */
    
    efi_print("Graphics output not yet implemented\r\n");
    
    return EFI_SUCCESS;
}

/* ===================================================================== */
/* Kernel loading */
/* ===================================================================== */

/* External kernel entry point */
extern void kernel_main(void *dtb);

/* Device tree blob location */
static void *device_tree_blob = NULL;

static EFI_STATUS find_device_tree(void)
{
    /* TODO: Find DTB from EFI configuration table */
    /* Apple Silicon provides DTB via special configuration table */
    
    efi_print("Looking for device tree...\r\n");
    
    /* For now, use NULL - kernel will use hardcoded values */
    device_tree_blob = NULL;
    
    return EFI_SUCCESS;
}

/* ===================================================================== */
/* Exit boot services */
/* ===================================================================== */

static EFI_STATUS exit_boot_services(void)
{
    EFI_STATUS status;
    
    /* Get final memory map */
    status = get_memory_map();
    if (status != EFI_SUCCESS) {
        return status;
    }
    
    /* Exit boot services - after this, EFI is gone */
    status = gBS->ExitBootServices(gImageHandle, memory_map_key);
    if (status != EFI_SUCCESS) {
        efi_print("Failed to exit boot services\r\n");
        
        /* Memory map may have changed, try again */
        status = get_memory_map();
        if (status == EFI_SUCCESS) {
            status = gBS->ExitBootServices(gImageHandle, memory_map_key);
        }
        
        if (status != EFI_SUCCESS) {
            return status;
        }
    }
    
    return EFI_SUCCESS;
}

/* ===================================================================== */
/* EFI Main Entry Point */
/* ===================================================================== */

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    
    /* Store global pointers */
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gImageHandle = ImageHandle;
    
    /* Clear screen and print banner */
    if (gST->ConOut) {
        gST->ConOut->OutputString(gST->ConOut, (uint16_t *)L"\r\n");
    }
    
    efi_print("UnixOS EFI Boot Stub\r\n");
    efi_print("====================\r\n\r\n");
    
    /* Print firmware info */
    efi_print("Firmware: ");
    if (gST->FirmwareVendor) {
        efi_puts(gST->FirmwareVendor);
    }
    efi_print("\r\n");
    
    /* Get memory map */
    efi_print("Getting memory map...\r\n");
    status = get_memory_map();
    if (status != EFI_SUCCESS) {
        efi_print("Failed to get memory map!\r\n");
        return status;
    }
    print_memory_map();
    
    /* Initialize graphics */
    efi_print("Initializing graphics...\r\n");
    init_graphics();
    
    /* Find device tree */
    efi_print("Finding device tree...\r\n");
    find_device_tree();
    
    /* Exit boot services */
    efi_print("Exiting boot services...\r\n");
    status = exit_boot_services();
    if (status != EFI_SUCCESS) {
        /* Can't print anymore - EFI might be partially exited */
        return status;
    }
    
    /* 
     * At this point, EFI boot services are gone.
     * We own the hardware now.
     * Jump to kernel!
     */
    
    kernel_main(device_tree_blob);
    
    /* Should never reach here */
    while (1) {
        asm volatile("wfi");
    }
    
    return EFI_SUCCESS;
}
