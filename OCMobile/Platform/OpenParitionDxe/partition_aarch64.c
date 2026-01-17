/**
 * @file partition_aarch64.c
 * Bare metal partition driver for aarch64 Android devices
 * Adapted from OpenCore UEFI partition driver for direct hardware access
 * 
 * Supports: GPT, MBR partition schemes
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// Type Definitions
// ============================================================================

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#define SECTOR_SIZE 512
#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL  // "EFI PART"
#define MBR_SIGNATURE 0xAA55

// Status codes
typedef enum {
    STATUS_SUCCESS = 0,
    STATUS_ERROR = -1,
    STATUS_NO_MEDIA = -2,
    STATUS_INVALID_PARAM = -3,
    STATUS_OUT_OF_RESOURCES = -4,
    STATUS_NOT_FOUND = -5
} status_t;

// Partition types
typedef enum {
    PART_TYPE_UNKNOWN = 0,
    PART_TYPE_GPT,
    PART_TYPE_MBR
} partition_type_t;

// ============================================================================
// Data Structures
// ============================================================================

// GUID structure (128-bit)
typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8  data4[8];
} guid_t;

// GPT Header
typedef struct __attribute__((packed)) {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 current_lba;
    u64 backup_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    guid_t disk_guid;
    u64 partition_entry_lba;
    u32 num_partition_entries;
    u32 partition_entry_size;
    u32 partition_array_crc32;
} gpt_header_t;

// GPT Partition Entry
typedef struct __attribute__((packed)) {
    guid_t type_guid;
    guid_t unique_guid;
    u64 first_lba;
    u64 last_lba;
    u64 attributes;
    u16 name[36];  // UTF-16LE partition name
} gpt_entry_t;

// MBR Partition Entry
typedef struct __attribute__((packed)) {
    u8  status;
    u8  first_chs[3];
    u8  partition_type;
    u8  last_chs[3];
    u32 first_lba;
    u32 num_sectors;
} mbr_entry_t;

// MBR
typedef struct __attribute__((packed)) {
    u8 boot_code[440];
    u32 disk_signature;
    u16 reserved;
    mbr_entry_t partitions[4];
    u16 signature;
} mbr_t;

// Partition Information
typedef struct {
    partition_type_t type;
    u64 start_lba;
    u64 end_lba;
    u64 size_sectors;
    u32 block_size;
    guid_t type_guid;
    guid_t unique_guid;
    char name[128];
    bool bootable;
    u8 mbr_type;
} partition_info_t;

// Block Device Interface
typedef struct block_device {
    void *private_data;
    u64 total_sectors;
    u32 block_size;
    
    // Function pointers for device operations
    status_t (*read_blocks)(struct block_device *dev, u64 lba, u32 count, void *buffer);
    status_t (*write_blocks)(struct block_device *dev, u64 lba, u32 count, const void *buffer);
    status_t (*flush)(struct block_device *dev);
} block_device_t;

// Partition Device (logical block device)
typedef struct {
    block_device_t block_dev;
    block_device_t *parent;
    partition_info_t info;
} partition_device_t;

// ============================================================================
// Memory Management (Simple allocator - implement based on your environment)
// ============================================================================

static void* simple_malloc(size_t size) {
    // TODO: Implement based on your bare metal memory allocator
    // This is a placeholder - you need to implement actual allocation
    return NULL;
}

static void simple_free(void* ptr) {
    // TODO: Implement based on your bare metal memory allocator
}

// ============================================================================
// String/Memory Utilities
// ============================================================================

static void* memcpy(void* dest, const void* src, size_t n) {
    u8* d = (u8*)dest;
    const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static void* memset(void* s, int c, size_t n) {
    u8* p = (u8*)s;
    while (n--) *p++ = (u8)c;
    return s;
}

static int memcmp(const void* s1, const void* s2, size_t n) {
    const u8* p1 = (const u8*)s1;
    const u8* p2 = (const u8*)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

// Convert UTF-16LE to ASCII (simple version)
static void utf16_to_ascii(char* dest, const u16* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1 && src[i] != 0) {
        dest[i] = (src[i] < 128) ? (char)src[i] : '?';
        i++;
    }
    dest[i] = '\0';
}

// ============================================================================
// CRC32 Calculation (for GPT validation)
// ============================================================================

static const u32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    // ... (full table omitted for brevity - include full 256 entries)
};

static u32 calculate_crc32(const void* data, size_t length) {
    u32 crc = 0xFFFFFFFF;
    const u8* p = (const u8*)data;
    
    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// GPT Partition Detection
// ============================================================================

static status_t detect_gpt_partitions(block_device_t* device, 
                                      partition_info_t* partitions,
                                      u32* num_partitions,
                                      u32 max_partitions) {
    u8 sector_buf[SECTOR_SIZE];
    gpt_header_t* gpt_hdr;
    gpt_entry_t* entries;
    u32 count = 0;
    
    // Read GPT header from LBA 1
    if (device->read_blocks(device, 1, 1, sector_buf) != STATUS_SUCCESS) {
        return STATUS_ERROR;
    }
    
    gpt_hdr = (gpt_header_t*)sector_buf;
    
    // Validate GPT signature
    if (gpt_hdr->signature != GPT_HEADER_SIGNATURE) {
        return STATUS_NOT_FOUND;
    }
    
    // Validate header CRC
    u32 orig_crc = gpt_hdr->header_crc32;
    gpt_hdr->header_crc32 = 0;
    u32 calc_crc = calculate_crc32(gpt_hdr, gpt_hdr->header_size);
    
    if (orig_crc != calc_crc) {
        return STATUS_ERROR;
    }
    
    // Allocate buffer for partition entries
    u32 entries_size = gpt_hdr->num_partition_entries * gpt_hdr->partition_entry_size;
    entries = (gpt_entry_t*)simple_malloc(entries_size);
    if (!entries) {
        return STATUS_OUT_OF_RESOURCES;
    }
    
    // Read partition entries
    u32 sectors_needed = (entries_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (device->read_blocks(device, gpt_hdr->partition_entry_lba, 
                           sectors_needed, entries) != STATUS_SUCCESS) {
        simple_free(entries);
        return STATUS_ERROR;
    }
    
    // Parse partition entries
    for (u32 i = 0; i < gpt_hdr->num_partition_entries && count < max_partitions; i++) {
        gpt_entry_t* entry = (gpt_entry_t*)((u8*)entries + i * gpt_hdr->partition_entry_size);
        
        // Skip empty entries (all zeros in type GUID)
        bool is_empty = true;
        for (int j = 0; j < 16; j++) {
            if (((u8*)&entry->type_guid)[j] != 0) {
                is_empty = false;
                break;
            }
        }
        
        if (is_empty) continue;
        
        partition_info_t* part = &partitions[count];
        part->type = PART_TYPE_GPT;
        part->start_lba = entry->first_lba;
        part->end_lba = entry->last_lba;
        part->size_sectors = entry->last_lba - entry->first_lba + 1;
        part->block_size = device->block_size;
        memcpy(&part->type_guid, &entry->type_guid, sizeof(guid_t));
        memcpy(&part->unique_guid, &entry->unique_guid, sizeof(guid_t));
        utf16_to_ascii(part->name, entry->name, sizeof(part->name));
        part->bootable = (entry->attributes & 0x4) != 0;  // Legacy BIOS bootable flag
        
        count++;
    }
    
    simple_free(entries);
    *num_partitions = count;
    return STATUS_SUCCESS;
}

// ============================================================================
// MBR Partition Detection
// ============================================================================

static status_t detect_mbr_partitions(block_device_t* device,
                                      partition_info_t* partitions,
                                      u32* num_partitions,
                                      u32 max_partitions) {
    u8 sector_buf[SECTOR_SIZE];
    mbr_t* mbr;
    u32 count = 0;
    
    // Read MBR from LBA 0
    if (device->read_blocks(device, 0, 1, sector_buf) != STATUS_SUCCESS) {
        return STATUS_ERROR;
    }
    
    mbr = (mbr_t*)sector_buf;
    
    // Validate MBR signature
    if (mbr->signature != MBR_SIGNATURE) {
        return STATUS_NOT_FOUND;
    }
    
    // Parse primary partitions
    for (int i = 0; i < 4 && count < max_partitions; i++) {
        mbr_entry_t* entry = &mbr->partitions[i];
        
        // Skip empty partitions
        if (entry->partition_type == 0 || entry->num_sectors == 0) {
            continue;
        }
        
        partition_info_t* part = &partitions[count];
        part->type = PART_TYPE_MBR;
        part->start_lba = entry->first_lba;
        part->end_lba = entry->first_lba + entry->num_sectors - 1;
        part->size_sectors = entry->num_sectors;
        part->block_size = device->block_size;
        part->bootable = (entry->status & 0x80) != 0;
        part->mbr_type = entry->partition_type;
        
        // Generate a simple name
        char type_str[20];
        if (entry->partition_type == 0x0C || entry->partition_type == 0x0B) {
            memcpy(part->name, "FAT32", 6);
        } else if (entry->partition_type == 0x83) {
            memcpy(part->name, "Linux", 6);
        } else if (entry->partition_type == 0xEE) {
            memcpy(part->name, "GPT_Protective", 15);
        } else {
            memcpy(part->name, "Unknown", 8);
        }
        
        count++;
    }
    
    *num_partitions = count;
    return STATUS_SUCCESS;
}

// ============================================================================
// Main Partition Discovery
// ============================================================================

status_t discover_partitions(block_device_t* device,
                             partition_info_t* partitions,
                             u32* num_partitions,
                             u32 max_partitions) {
    status_t status;
    
    // Try GPT first (UEFI spec order)
    status = detect_gpt_partitions(device, partitions, num_partitions, max_partitions);
    if (status == STATUS_SUCCESS) {
        return STATUS_SUCCESS;
    }
    
    // Fall back to MBR
    status = detect_mbr_partitions(device, partitions, num_partitions, max_partitions);
    if (status == STATUS_SUCCESS) {
        return STATUS_SUCCESS;
    }
    
    return STATUS_NOT_FOUND;
}

// ============================================================================
// Partition Block Device Operations
// ============================================================================

static status_t partition_read_blocks(block_device_t* dev, u64 lba, 
                                     u32 count, void* buffer) {
    partition_device_t* part_dev = (partition_device_t*)dev;
    u64 parent_lba = part_dev->info.start_lba + lba;
    
    // Bounds check
    if (parent_lba + count - 1 > part_dev->info.end_lba) {
        return STATUS_INVALID_PARAM;
    }
    
    return part_dev->parent->read_blocks(part_dev->parent, parent_lba, count, buffer);
}

static status_t partition_write_blocks(block_device_t* dev, u64 lba,
                                      u32 count, const void* buffer) {
    partition_device_t* part_dev = (partition_device_t*)dev;
    u64 parent_lba = part_dev->info.start_lba + lba;
    
    // Bounds check
    if (parent_lba + count - 1 > part_dev->info.end_lba) {
        return STATUS_INVALID_PARAM;
    }
    
    return part_dev->parent->write_blocks(part_dev->parent, parent_lba, count, buffer);
}

static status_t partition_flush(block_device_t* dev) {
    partition_device_t* part_dev = (partition_device_t*)dev;
    return part_dev->parent->flush(part_dev->parent);
}

// ============================================================================
// Create Partition Device
// ============================================================================

partition_device_t* create_partition_device(block_device_t* parent,
                                           partition_info_t* info) {
    partition_device_t* part_dev = (partition_device_t*)simple_malloc(sizeof(partition_device_t));
    if (!part_dev) {
        return NULL;
    }
    
    memset(part_dev, 0, sizeof(partition_device_t));
    
    part_dev->parent = parent;
    memcpy(&part_dev->info, info, sizeof(partition_info_t));
    
    part_dev->block_dev.total_sectors = info->size_sectors;
    part_dev->block_dev.block_size = info->block_size;
    part_dev->block_dev.read_blocks = partition_read_blocks;
    part_dev->block_dev.write_blocks = partition_write_blocks;
    part_dev->block_dev.flush = partition_flush;
    part_dev->block_dev.private_data = part_dev;
    
    return part_dev;
}

// ============================================================================
// Example Usage
// ============================================================================

/*
// Example: How to use this driver with your hardware

// 1. Implement your hardware-specific block device
status_t mmc_read_blocks(block_device_t* dev, u64 lba, u32 count, void* buffer) {
    // Your MMC/eMMC/SD card read implementation
    return STATUS_SUCCESS;
}

status_t mmc_write_blocks(block_device_t* dev, u64 lba, u32 count, const void* buffer) {
    // Your MMC/eMMC/SD card write implementation
    return STATUS_SUCCESS;
}

status_t mmc_flush(block_device_t* dev) {
    // Your flush implementation
    return STATUS_SUCCESS;
}

// 2. Initialize the hardware device
block_device_t mmc_device = {
    .private_data = NULL,
    .total_sectors = 0x1D1C0000,  // Example: 238GB
    .block_size = 512,
    .read_blocks = mmc_read_blocks,
    .write_blocks = mmc_write_blocks,
    .flush = mmc_flush
};

// 3. Discover partitions
partition_info_t partitions[32];
u32 num_partitions = 0;

if (discover_partitions(&mmc_device, partitions, &num_partitions, 32) == STATUS_SUCCESS) {
    // 4. Create partition devices
    for (u32 i = 0; i < num_partitions; i++) {
        partition_device_t* part = create_partition_device(&mmc_device, &partitions[i]);
        // Use partition device...
    }
}
*/