/**
 * @file mbr_parser_aarch64.c
 * Bare metal MBR (Master Boot Record) parser for aarch64 Android devices
 * Adapted from UEFI MBR driver for direct hardware access
 * 
 * Features:
 * - MBR partition table parsing
 * - Extended partition support (EBR)
 * - Protective MBR detection (for GPT)
 * - Legacy partition type identification
 * - Validation and sanity checks
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
#define MBR_SIGNATURE 0xAA55
#define MAX_MBR_PARTITIONS 4
#define EXTENDED_DOS_PARTITION 0x05
#define EXTENDED_WINDOWS_PARTITION 0x0F
#define PMBR_GPT_PARTITION 0xEE

// Status codes
typedef enum {
    STATUS_SUCCESS = 0,
    STATUS_ERROR = -1,
    STATUS_NOT_FOUND = -2,
    STATUS_INVALID_PARAM = -3,
    STATUS_OUT_OF_MEMORY = -4,
    STATUS_MEDIA_CHANGED = -5,
    STATUS_NO_MEDIA = -6
} status_t;

// ============================================================================
// MBR Structures
// ============================================================================

typedef struct __attribute__((packed)) {
    u8  boot_indicator;     // 0x80 = bootable, 0x00 = not bootable
    u8  starting_chs[3];    // Starting CHS address (legacy)
    u8  os_indicator;       // Partition type
    u8  ending_chs[3];      // Ending CHS address (legacy)
    u32 starting_lba;       // Starting LBA
    u32 size_in_lba;        // Size in LBA sectors
} mbr_partition_entry_t;

typedef struct __attribute__((packed)) {
    u8 boot_code[440];
    u32 unique_mbr_signature;
    u16 unknown;
    mbr_partition_entry_t partition[MAX_MBR_PARTITIONS];
    u16 signature;  // Must be 0xAA55
} master_boot_record_t;

// Partition types
#define PARTITION_TYPE_EMPTY            0x00
#define PARTITION_TYPE_FAT12            0x01
#define PARTITION_TYPE_FAT16_SMALL      0x04
#define PARTITION_TYPE_EXTENDED         0x05
#define PARTITION_TYPE_FAT16            0x06
#define PARTITION_TYPE_NTFS             0x07
#define PARTITION_TYPE_FAT32            0x0B
#define PARTITION_TYPE_FAT32_LBA        0x0C
#define PARTITION_TYPE_FAT16_LBA        0x0E
#define PARTITION_TYPE_EXTENDED_LBA     0x0F
#define PARTITION_TYPE_LINUX_SWAP       0x82
#define PARTITION_TYPE_LINUX            0x83
#define PARTITION_TYPE_LINUX_EXTENDED   0x85
#define PARTITION_TYPE_LINUX_LVM        0x8E
#define PARTITION_TYPE_GPT_PROTECTIVE   0xEE
#define PARTITION_TYPE_EFI_SYSTEM       0xEF

// ============================================================================
// Partition Information
// ============================================================================

typedef struct {
    u64 start_lba;
    u64 end_lba;
    u64 size_sectors;
    u32 block_size;
    u8  partition_type;
    bool bootable;
    bool is_extended;
    u32 partition_number;
    char type_name[32];
} mbr_partition_info_t;

// ============================================================================
// Block Device Interface
// ============================================================================

typedef struct block_device {
    void *private_data;
    u64 total_sectors;
    u32 block_size;
    u32 media_id;
    
    status_t (*read_disk)(struct block_device *dev, u32 media_id,
                         u64 offset, u32 size, void *buffer);
    status_t (*write_disk)(struct block_device *dev, u32 media_id,
                          u64 offset, u32 size, const void *buffer);
} block_device_t;

// ============================================================================
// Memory/String Utilities
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

static void strcpy_s(char* dest, size_t dest_size, const char* src) {
    size_t i = 0;
    while (i < dest_size - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// ============================================================================
// Memory Allocation (implement based on your environment)
// ============================================================================

static void* alloc_pool(size_t size) {
    // TODO: Implement based on your memory allocator
    return NULL;
}

static void* alloc_zero_pool(size_t size) {
    void* ptr = alloc_pool(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static void free_pool(void* ptr) {
    // TODO: Implement based on your memory allocator
}

// ============================================================================
// Utility Functions
// ============================================================================

// Unpack 32-bit little-endian value
static inline u32 unpack_u32(const u8* data) {
    return (u32)data[0] | 
           ((u32)data[1] << 8) | 
           ((u32)data[2] << 16) | 
           ((u32)data[3] << 24);
}

// Get partition type name
static const char* get_partition_type_name(u8 type) {
    switch (type) {
        case PARTITION_TYPE_EMPTY:
            return "Empty";
        case PARTITION_TYPE_FAT12:
            return "FAT12";
        case PARTITION_TYPE_FAT16_SMALL:
        case PARTITION_TYPE_FAT16:
        case PARTITION_TYPE_FAT16_LBA:
            return "FAT16";
        case PARTITION_TYPE_EXTENDED:
        case PARTITION_TYPE_EXTENDED_LBA:
        case PARTITION_TYPE_LINUX_EXTENDED:
            return "Extended";
        case PARTITION_TYPE_NTFS:
            return "NTFS";
        case PARTITION_TYPE_FAT32:
        case PARTITION_TYPE_FAT32_LBA:
            return "FAT32";
        case PARTITION_TYPE_LINUX_SWAP:
            return "Linux Swap";
        case PARTITION_TYPE_LINUX:
            return "Linux";
        case PARTITION_TYPE_LINUX_LVM:
            return "Linux LVM";
        case PARTITION_TYPE_GPT_PROTECTIVE:
            return "GPT Protective";
        case PARTITION_TYPE_EFI_SYSTEM:
            return "EFI System";
        default:
            return "Unknown";
    }
}

// Check if partition type is extended
static bool is_extended_partition(u8 type) {
    return (type == PARTITION_TYPE_EXTENDED ||
            type == PARTITION_TYPE_EXTENDED_LBA ||
            type == PARTITION_TYPE_LINUX_EXTENDED);
}

// ============================================================================
// MBR Validation
// ============================================================================

static bool validate_mbr(const master_boot_record_t* mbr) {
    // Check MBR signature
    if (mbr->signature != MBR_SIGNATURE) {
        return false;
    }
    
    // Check if all partitions are valid
    for (u32 i = 0; i < MAX_MBR_PARTITIONS; i++) {
        const mbr_partition_entry_t* entry = &mbr->partition[i];
        
        // Skip empty partitions
        if (entry->os_indicator == PARTITION_TYPE_EMPTY) {
            continue;
        }
        
        // Validate boot indicator
        if (entry->boot_indicator != 0x00 && entry->boot_indicator != 0x80) {
            return false;
        }
        
        // Check for zero size
        if (entry->size_in_lba == 0) {
            return false;
        }
    }
    
    return true;
}

// Check if MBR is a protective MBR for GPT
static bool is_protective_mbr(const master_boot_record_t* mbr) {
    for (u32 i = 0; i < MAX_MBR_PARTITIONS; i++) {
        if (mbr->partition[i].boot_indicator == 0x00 &&
            mbr->partition[i].os_indicator == PMBR_GPT_PARTITION &&
            mbr->partition[i].starting_lba == 1) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Extended Partition Handling
// ============================================================================

typedef struct {
    u64 extended_base_lba;  // Base LBA of extended partition
    u64 current_ebr_lba;    // Current EBR LBA being processed
} extended_partition_context_t;

static status_t process_extended_partition(block_device_t* dev,
                                           u64 extended_base_lba,
                                           u64 ebr_lba,
                                           mbr_partition_info_t* partitions,
                                           u32* partition_count,
                                           u32 max_partitions) {
    master_boot_record_t* ebr = alloc_pool(dev->block_size);
    if (!ebr) {
        return STATUS_OUT_OF_MEMORY;
    }
    
    u64 current_ebr_lba = ebr_lba;
    u32 logical_partition_num = 5;  // Logical partitions start at 5
    
    // Process chain of EBRs
    while (current_ebr_lba != 0 && *partition_count < max_partitions) {
        // Read EBR
        u64 offset = current_ebr_lba * dev->block_size;
        status_t status = dev->read_disk(dev, dev->media_id, offset,
                                        dev->block_size, ebr);
        
        if (status != STATUS_SUCCESS) {
            free_pool(ebr);
            return status;
        }
        
        // Validate EBR signature
        if (ebr->signature != MBR_SIGNATURE) {
            break;
        }
        
        // First entry is the logical partition
        mbr_partition_entry_t* logical = &ebr->partition[0];
        if (logical->os_indicator != PARTITION_TYPE_EMPTY &&
            logical->size_in_lba > 0) {
            
            mbr_partition_info_t* part = &partitions[*partition_count];
            part->start_lba = current_ebr_lba + logical->starting_lba;
            part->size_sectors = logical->size_in_lba;
            part->end_lba = part->start_lba + part->size_sectors - 1;
            part->block_size = dev->block_size;
            part->partition_type = logical->os_indicator;
            part->bootable = (logical->boot_indicator == 0x80);
            part->is_extended = false;
            part->partition_number = logical_partition_num++;
            
            strcpy_s(part->type_name, sizeof(part->type_name),
                    get_partition_type_name(logical->os_indicator));
            
            (*partition_count)++;
        }
        
        // Second entry points to next EBR (if exists)
        mbr_partition_entry_t* next = &ebr->partition[1];
        if (next->os_indicator != PARTITION_TYPE_EMPTY &&
            is_extended_partition(next->os_indicator) &&
            next->size_in_lba > 0) {
            current_ebr_lba = extended_base_lba + next->starting_lba;
        } else {
            current_ebr_lba = 0;  // End of chain
        }
    }
    
    free_pool(ebr);
    return STATUS_SUCCESS;
}

// ============================================================================
// Main MBR Discovery Function
// ============================================================================

status_t discover_mbr_partitions(block_device_t* dev,
                                 mbr_partition_info_t* partitions,
                                 u32* num_partitions,
                                 u32 max_partitions) {
    master_boot_record_t* mbr = NULL;
    status_t result = STATUS_NOT_FOUND;
    u32 count = 0;
    
    // Validate inputs
    if (!dev || !partitions || !num_partitions || max_partitions == 0) {
        return STATUS_INVALID_PARAM;
    }
    
    // Validate block size
    if (dev->block_size < sizeof(master_boot_record_t)) {
        return STATUS_INVALID_PARAM;
    }
    
    // Allocate MBR buffer
    mbr = alloc_pool(dev->block_size);
    if (!mbr) {
        return STATUS_OUT_OF_MEMORY;
    }
    
    // Read MBR from LBA 0
    status_t status = dev->read_disk(dev, dev->media_id, 0, dev->block_size, mbr);
    if (status != STATUS_SUCCESS) {
        result = status;
        goto cleanup;
    }
    
    // Validate MBR
    if (!validate_mbr(mbr)) {
        result = STATUS_NOT_FOUND;
        goto cleanup;
    }
    
    // Check if this is a protective MBR for GPT
    if (is_protective_mbr(mbr)) {
        // This is a GPT disk, not pure MBR
        result = STATUS_NOT_FOUND;
        goto cleanup;
    }
    
    // Process primary partitions
    for (u32 i = 0; i < MAX_MBR_PARTITIONS && count < max_partitions; i++) {
        mbr_partition_entry_t* entry = &mbr->partition[i];
        
        // Skip empty partitions
        if (entry->os_indicator == PARTITION_TYPE_EMPTY || 
            entry->size_in_lba == 0) {
            continue;
        }
        
        // Check if this is an extended partition
        if (is_extended_partition(entry->os_indicator)) {
            // Process extended partition separately
            status = process_extended_partition(dev,
                                               entry->starting_lba,
                                               entry->starting_lba,
                                               partitions,
                                               &count,
                                               max_partitions);
            if (status != STATUS_SUCCESS) {
                // Continue even if extended partition fails
                continue;
            }
        } else {
            // Add primary partition
            mbr_partition_info_t* part = &partitions[count];
            part->start_lba = entry->starting_lba;
            part->size_sectors = entry->size_in_lba;
            part->end_lba = part->start_lba + part->size_sectors - 1;
            part->block_size = dev->block_size;
            part->partition_type = entry->os_indicator;
            part->bootable = (entry->boot_indicator == 0x80);
            part->is_extended = false;
            part->partition_number = i + 1;
            
            strcpy_s(part->type_name, sizeof(part->type_name),
                    get_partition_type_name(entry->os_indicator));
            
            count++;
        }
    }
    
    // Return success if we found at least one partition
    if (count > 0) {
        *num_partitions = count;
        result = STATUS_SUCCESS;
    } else {
        result = STATUS_NOT_FOUND;
    }
    
cleanup:
    if (mbr) {
        free_pool(mbr);
    }
    
    return result;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Find partition by number
static mbr_partition_info_t* find_partition_by_number(mbr_partition_info_t* partitions,
                                                      u32 num_partitions,
                                                      u32 partition_number) {
    for (u32 i = 0; i < num_partitions; i++) {
        if (partitions[i].partition_number == partition_number) {
            return &partitions[i];
        }
    }
    return NULL;
}

// Find bootable partition
static mbr_partition_info_t* find_bootable_partition(mbr_partition_info_t* partitions,
                                                     u32 num_partitions) {
    for (u32 i = 0; i < num_partitions; i++) {
        if (partitions[i].bootable) {
            return &partitions[i];
        }
    }
    return NULL;
}

// Find partition by type
static mbr_partition_info_t* find_partition_by_type(mbr_partition_info_t* partitions,
                                                    u32 num_partitions,
                                                    u8 partition_type) {
    for (u32 i = 0; i < num_partitions; i++) {
        if (partitions[i].partition_type == partition_type) {
            return &partitions[i];
        }
    }
    return NULL;
}

// Validate partition bounds
static bool validate_partition_bounds(const mbr_partition_info_t* partition,
                                     u64 total_sectors) {
    if (partition->start_lba >= total_sectors) {
        return false;
    }
    
    if (partition->end_lba >= total_sectors) {
        return false;
    }
    
    if (partition->start_lba > partition->end_lba) {
        return false;
    }
    
    return true;
}

// Check for partition overlaps
static bool check_partition_overlap(const mbr_partition_info_t* part1,
                                   const mbr_partition_info_t* part2) {
    if (part1->end_lba < part2->start_lba) {
        return false;  // part1 is completely before part2
    }
    
    if (part1->start_lba > part2->end_lba) {
        return false;  // part1 is completely after part2
    }
    
    return true;  // Partitions overlap
}

// ============================================================================
// MBR Writing (for creating/modifying partitions)
// ============================================================================

status_t write_mbr(block_device_t* dev, const master_boot_record_t* mbr) {
    if (!dev || !mbr) {
        return STATUS_INVALID_PARAM;
    }
    
    // Validate MBR before writing
    if (!validate_mbr(mbr)) {
        return STATUS_INVALID_PARAM;
    }
    
    // Write to LBA 0
    return dev->write_disk(dev, dev->media_id, 0, dev->block_size, mbr);
}

// Create a basic MBR with one partition
status_t create_simple_mbr(block_device_t* dev,
                          u64 partition_start_lba,
                          u64 partition_size_sectors,
                          u8 partition_type,
                          bool bootable) {
    master_boot_record_t* mbr = alloc_zero_pool(sizeof(master_boot_record_t));
    if (!mbr) {
        return STATUS_OUT_OF_MEMORY;
    }
    
    // Set signature
    mbr->signature = MBR_SIGNATURE;
    
    // Configure first partition
    mbr->partition[0].boot_indicator = bootable ? 0x80 : 0x00;
    mbr->partition[0].os_indicator = partition_type;
    mbr->partition[0].starting_lba = (u32)partition_start_lba;
    mbr->partition[0].size_in_lba = (u32)partition_size_sectors;
    
    // Write MBR
    status_t status = write_mbr(dev, mbr);
    
    free_pool(mbr);
    return status;
}

// ============================================================================
// Example Usage
// ============================================================================

/*
// Example: Complete MBR partition discovery on device

// 1. Implement block device interface
status_t disk_read(block_device_t* dev, u32 media_id,
                   u64 offset, u32 size, void* buffer) {
    // Read from storage device at byte offset
    return STATUS_SUCCESS;
}

status_t disk_write(block_device_t* dev, u32 media_id,
                    u64 offset, u32 size, const void* buffer) {
    // Write to storage device at byte offset
    return STATUS_SUCCESS;
}

// 2. Initialize block device
block_device_t storage_device = {
    .private_data = NULL,
    .total_sectors = 0x3A38000,  // Example: 30GB device
    .block_size = 512,
    .media_id = 1,
    .read_disk = disk_read,
    .write_disk = disk_write
};

// 3. Discover MBR partitions
mbr_partition_info_t partitions[16];
u32 num_partitions = 0;

status_t status = discover_mbr_partitions(&storage_device, partitions,
                                          &num_partitions, 16);

if (status == STATUS_SUCCESS) {
    // 4. Process discovered partitions
    for (u32 i = 0; i < num_partitions; i++) {
        mbr_partition_info_t* part = &partitions[i];
        
        // Use partition information
        u64 start_byte = part->start_lba * 512;
        u64 size_bytes = part->size_sectors * 512;
        
        // Check if bootable
        if (part->bootable) {
            // This is the bootable partition
        }
        
        // Check partition type
        if (part->partition_type == PARTITION_TYPE_FAT32) {
            // This is a FAT32 partition
        }
    }
    
    // 5. Find specific partitions
    mbr_partition_info_t* bootable = find_bootable_partition(partitions,
                                                             num_partitions);
    if (bootable) {
        // Load bootloader from bootable partition
    }
}

// 6. Create a new simple MBR (if needed)
status = create_simple_mbr(&storage_device,
                          2048,           // Start at LBA 2048 (1MB offset)
                          0x3A36000,      // Rest of disk
                          PARTITION_TYPE_FAT32,
                          true);          // Make it bootable
*/