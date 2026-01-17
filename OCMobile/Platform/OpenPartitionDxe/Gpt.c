/**
 * @file gpt_parser_aarch64.c
 * Bare metal GPT (GUID Partition Table) parser for aarch64 Android devices
 * Adapted from UEFI GPT driver for direct hardware access
 * 
 * Features:
 * - GPT header validation with CRC32 checking
 * - Partition entry validation
 * - Primary and backup GPT table support
 * - Automatic GPT table restoration
 * - Protective MBR validation
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
#define PRIMARY_PART_HEADER_LBA 1
#define MAX_MBR_PARTITIONS 4
#define PMBR_GPT_PARTITION 0xEE

// Status codes
typedef enum {
    STATUS_SUCCESS = 0,
    STATUS_ERROR = -1,
    STATUS_NOT_FOUND = -2,
    STATUS_CRC_ERROR = -3,
    STATUS_INVALID_PARAM = -4,
    STATUS_OUT_OF_MEMORY = -5,
    STATUS_OVERLAP = -6,
    STATUS_OUT_OF_RANGE = -7
} status_t;

// ============================================================================
// GUID Structure
// ============================================================================

typedef struct __attribute__((packed)) {
    u32 data1;
    u16 data2;
    u16 data3;
    u8  data4[8];
} guid_t;

// Well-known GUIDs
static const guid_t GUID_UNUSED = {
    0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static const guid_t GUID_EFI_SYSTEM = {
    0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}
};

// Android-specific partition type GUIDs
static const guid_t GUID_ANDROID_BOOT = {
    0x49A4D17F, 0x93A3, 0x45C1, {0xA0, 0xDE, 0xF5, 0x0B, 0x2E, 0xBE, 0x25, 0x99}
};

static const guid_t GUID_ANDROID_SYSTEM = {
    0x97409AC0, 0xBDBE, 0x4B38, {0xAF, 0xC7, 0x8B, 0x4F, 0xAE, 0x85, 0x7E, 0xF8}
};

static const guid_t GUID_ANDROID_USERDATA = {
    0x0BB7E6ED, 0x4424, 0x49C0, {0x9C, 0x72, 0xE8, 0xB2, 0x4F, 0x4E, 0x6C, 0x1E}
};

// ============================================================================
// GPT Structures
// ============================================================================

#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL  // "EFI PART"

typedef struct __attribute__((packed)) {
    u64 signature;              // "EFI PART"
    u32 revision;               // GPT revision (usually 0x00010000)
    u32 header_size;            // Header size in bytes (usually 92)
    u32 header_crc32;           // CRC32 of header
    u32 reserved;               // Must be zero
    u64 my_lba;                 // LBA of this header
    u64 alternate_lba;          // LBA of alternate header
    u64 first_usable_lba;       // First usable LBA for partitions
    u64 last_usable_lba;        // Last usable LBA for partitions
    guid_t disk_guid;           // Disk GUID
    u64 partition_entry_lba;    // Starting LBA of partition entries
    u32 num_partition_entries;  // Number of partition entries
    u32 partition_entry_size;   // Size of each partition entry
    u32 partition_array_crc32;  // CRC32 of partition array
    // Remainder of block is reserved and must be zero
} gpt_header_t;

typedef struct __attribute__((packed)) {
    guid_t partition_type_guid; // Partition type GUID
    guid_t unique_guid;         // Unique partition GUID
    u64 starting_lba;           // First LBA of partition
    u64 ending_lba;             // Last LBA of partition (inclusive)
    u64 attributes;             // Partition attributes
    u16 partition_name[36];     // Partition name (UTF-16LE)
} gpt_partition_entry_t;

// Partition entry status flags
typedef struct {
    bool out_of_range;
    bool overlap;
    bool os_specific;
} partition_entry_status_t;

// ============================================================================
// MBR Structure (for protective MBR validation)
// ============================================================================

typedef struct __attribute__((packed)) {
    u8  boot_indicator;
    u8  starting_chs[3];
    u8  os_indicator;
    u8  ending_chs[3];
    u32 starting_lba;
    u32 size_in_lba;
} mbr_partition_t;

typedef struct __attribute__((packed)) {
    u8 boot_code[440];
    u32 unique_mbr_signature;
    u16 unknown;
    mbr_partition_t partition[4];
    u16 signature;  // 0xAA55
} master_boot_record_t;

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

static bool compare_guid(const guid_t* g1, const guid_t* g2) {
    return memcmp(g1, g2, sizeof(guid_t)) == 0;
}

static void copy_guid(guid_t* dest, const guid_t* src) {
    memcpy(dest, src, sizeof(guid_t));
}

// Convert UTF-16LE to ASCII
static void utf16_to_ascii(char* dest, const u16* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1 && src[i] != 0) {
        dest[i] = (src[i] < 128) ? (char)src[i] : '?';
        i++;
    }
    dest[i] = '\0';
}

// ============================================================================
// Memory Allocation (Simple - implement based on your environment)
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
// CRC32 Implementation
// ============================================================================

static const u32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
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
// CRC Validation Functions
// ============================================================================

static bool check_header_crc(u32 max_size, u32 size, gpt_header_t* header) {
    if (size == 0) {
        return false;
    }
    
    if (max_size != 0 && size > max_size) {
        return false;
    }
    
    u32 original_crc = header->header_crc32;
    header->header_crc32 = 0;
    
    u32 calculated_crc = calculate_crc32(header, size);
    
    header->header_crc32 = calculated_crc;
    
    return (original_crc == calculated_crc);
}

static void set_header_crc(u32 size, gpt_header_t* header) {
    header->header_crc32 = 0;
    u32 crc = calculate_crc32(header, size);
    header->header_crc32 = crc;
}

// ============================================================================
// GPT Validation Functions
// ============================================================================

static bool validate_gpt_entry_array_crc(block_device_t* dev, 
                                         gpt_header_t* header) {
    u32 entries_size = header->num_partition_entries * header->partition_entry_size;
    u8* entries = alloc_pool(entries_size);
    
    if (!entries) {
        return false;
    }
    
    u64 offset = header->partition_entry_lba * dev->block_size;
    status_t status = dev->read_disk(dev, dev->media_id, offset, 
                                     entries_size, entries);
    
    if (status != STATUS_SUCCESS) {
        free_pool(entries);
        return false;
    }
    
    u32 calculated_crc = calculate_crc32(entries, entries_size);
    free_pool(entries);
    
    return (header->partition_array_crc32 == calculated_crc);
}

static bool validate_gpt_table(block_device_t* dev, u64 lba, 
                               gpt_header_t* header_out) {
    u32 block_size = dev->block_size;
    gpt_header_t* header = alloc_zero_pool(block_size);
    
    if (!header) {
        return false;
    }
    
    // Read GPT header
    u64 offset = lba * block_size;
    status_t status = dev->read_disk(dev, dev->media_id, offset, 
                                     block_size, header);
    
    if (status != STATUS_SUCCESS) {
        free_pool(header);
        return false;
    }
    
    // Validate signature
    if (header->signature != GPT_HEADER_SIGNATURE) {
        free_pool(header);
        return false;
    }
    
    // Validate header CRC
    if (!check_header_crc(block_size, header->header_size, header)) {
        free_pool(header);
        return false;
    }
    
    // Validate LBA
    if (header->my_lba != lba) {
        free_pool(header);
        return false;
    }
    
    // Validate partition entry size
    if (header->partition_entry_size < sizeof(gpt_partition_entry_t)) {
        free_pool(header);
        return false;
    }
    
    // Check for overflow in partition entries calculation
    if (header->num_partition_entries > (0xFFFFFFFFFFFFFFFFULL / header->partition_entry_size)) {
        free_pool(header);
        return false;
    }
    
    // Copy header to output
    memcpy(header_out, header, sizeof(gpt_header_t));
    
    // Validate partition entry array CRC
    if (!validate_gpt_entry_array_crc(dev, header_out)) {
        free_pool(header);
        return false;
    }
    
    free_pool(header);
    return true;
}

// ============================================================================
// GPT Entry Validation
// ============================================================================

static void check_gpt_entries(gpt_header_t* header, 
                              gpt_partition_entry_t* entries,
                              partition_entry_status_t* entry_status) {
    for (u32 i = 0; i < header->num_partition_entries; i++) {
        gpt_partition_entry_t* entry = 
            (gpt_partition_entry_t*)((u8*)entries + i * header->partition_entry_size);
        
        // Skip unused entries
        if (compare_guid(&entry->partition_type_guid, &GUID_UNUSED)) {
            continue;
        }
        
        u64 start_lba = entry->starting_lba;
        u64 end_lba = entry->ending_lba;
        
        // Check if partition is within valid range
        if (start_lba > end_lba ||
            start_lba < header->first_usable_lba ||
            start_lba > header->last_usable_lba ||
            end_lba < header->first_usable_lba ||
            end_lba > header->last_usable_lba) {
            entry_status[i].out_of_range = true;
            continue;
        }
        
        // Check if OS-specific partition (bit 1 set)
        if (entry->attributes & (1ULL << 1)) {
            entry_status[i].os_specific = true;
        }
        
        // Check for overlaps with other partitions
        for (u32 j = i + 1; j < header->num_partition_entries; j++) {
            gpt_partition_entry_t* other_entry = 
                (gpt_partition_entry_t*)((u8*)entries + j * header->partition_entry_size);
            
            if (compare_guid(&other_entry->partition_type_guid, &GUID_UNUSED)) {
                continue;
            }
            
            // Check if partitions overlap
            if (other_entry->ending_lba >= start_lba && 
                other_entry->starting_lba <= end_lba) {
                entry_status[i].overlap = true;
                entry_status[j].overlap = true;
            }
        }
    }
}

// ============================================================================
// GPT Restoration
// ============================================================================

static bool restore_gpt_table(block_device_t* dev, gpt_header_t* header) {
    u32 block_size = dev->block_size;
    gpt_header_t* new_header = alloc_zero_pool(block_size);
    
    if (!new_header) {
        return false;
    }
    
    // Determine new partition entry LBA
    u64 new_entry_lba;
    if (header->my_lba == PRIMARY_PART_HEADER_LBA) {
        new_entry_lba = header->last_usable_lba + 1;
    } else {
        new_entry_lba = PRIMARY_PART_HEADER_LBA + 1;
    }
    
    // Copy and update header
    memcpy(new_header, header, sizeof(gpt_header_t));
    new_header->my_lba = header->alternate_lba;
    new_header->alternate_lba = header->my_lba;
    new_header->partition_entry_lba = new_entry_lba;
    
    // Update CRC
    set_header_crc(new_header->header_size, new_header);
    
    // Write new header
    u64 offset = new_header->my_lba * block_size;
    status_t status = dev->write_disk(dev, dev->media_id, offset, 
                                      block_size, new_header);
    
    if (status != STATUS_SUCCESS) {
        free_pool(new_header);
        return false;
    }
    
    // Read and write partition entries
    u32 entries_size = header->num_partition_entries * header->partition_entry_size;
    u8* entries = alloc_pool(entries_size);
    
    if (!entries) {
        free_pool(new_header);
        return false;
    }
    
    // Read original entries
    offset = header->partition_entry_lba * block_size;
    status = dev->read_disk(dev, dev->media_id, offset, entries_size, entries);
    
    if (status != STATUS_SUCCESS) {
        free_pool(entries);
        free_pool(new_header);
        return false;
    }
    
    // Write to new location
    offset = new_entry_lba * block_size;
    status = dev->write_disk(dev, dev->media_id, offset, entries_size, entries);
    
    free_pool(entries);
    free_pool(new_header);
    
    return (status == STATUS_SUCCESS);
}

// ============================================================================
// Partition Information Structure
// ============================================================================

typedef struct {
    guid_t type_guid;
    guid_t unique_guid;
    u64 start_lba;
    u64 end_lba;
    u64 size_sectors;
    u64 attributes;
    char name[128];
    u32 partition_number;
    bool is_system;
    bool is_bootable;
} gpt_partition_info_t;

// ============================================================================
// Main GPT Discovery Function
// ============================================================================

status_t discover_gpt_partitions(block_device_t* dev,
                                 gpt_partition_info_t* partitions,
                                 u32* num_partitions,
                                 u32 max_partitions) {
    u32 block_size = dev->block_size;
    u64 last_block = dev->total_sectors - 1;
    master_boot_record_t* mbr = NULL;
    gpt_header_t* primary_header = NULL;
    gpt_header_t* backup_header = NULL;
    gpt_partition_entry_t* entries = NULL;
    partition_entry_status_t* entry_status = NULL;
    status_t result = STATUS_NOT_FOUND;
    u32 count = 0;
    
    // Validate block size
    if (block_size < sizeof(master_boot_record_t)) {
        return STATUS_INVALID_PARAM;
    }
    
    // Read and validate protective MBR
    mbr = alloc_pool(block_size);
    if (!mbr) {
        return STATUS_OUT_OF_MEMORY;
    }
    
    if (dev->read_disk(dev, dev->media_id, 0, block_size, mbr) != STATUS_SUCCESS) {
        result = STATUS_ERROR;
        goto cleanup;
    }
    
    // Verify protective MBR
    bool has_gpt_partition = false;
    for (u32 i = 0; i < MAX_MBR_PARTITIONS; i++) {
        if (mbr->partition[i].boot_indicator == 0x00 &&
            mbr->partition[i].os_indicator == PMBR_GPT_PARTITION &&
            mbr->partition[i].starting_lba == 1) {
            has_gpt_partition = true;
            break;
        }
    }
    
    if (!has_gpt_partition) {
        goto cleanup;
    }
    
    // Allocate GPT headers
    primary_header = alloc_zero_pool(sizeof(gpt_header_t));
    backup_header = alloc_zero_pool(sizeof(gpt_header_t));
    
    if (!primary_header || !backup_header) {
        result = STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    
    // Validate primary and backup GPT
    bool primary_valid = validate_gpt_table(dev, PRIMARY_PART_HEADER_LBA, primary_header);
    bool backup_valid = false;
    
    if (!primary_valid) {
        // Try backup GPT
        backup_valid = validate_gpt_table(dev, last_block, backup_header);
        
        if (backup_valid) {
            // Restore primary from backup
            restore_gpt_table(dev, backup_header);
            primary_valid = validate_gpt_table(dev, 
                                                       backup_header->alternate_lba,
                                                       primary_header);
        }
    } else {
        // Primary valid, check backup
        backup_valid = validate_gpt_table(dev, primary_header->alternate_lba, 
                                         backup_header);
        
        if (!backup_valid) {
            // Restore backup from primary
            restore_gpt_table(dev, primary_header);
            backup_valid = validate_gpt_table(dev, primary_header->alternate_lba,
                                             backup_header);
        }
    }
    
    if (!primary_valid && !backup_valid) {
        result = STATUS_NOT_FOUND;
        goto cleanup;
    }
    
    // Use primary header for partition enumeration
    gpt_header_t* header = primary_valid ? primary_header : backup_header;
    
    // Read partition entries
    u32 entries_size = header->num_partition_entries * header->partition_entry_size;
    entries = alloc_pool(entries_size);
    
    if (!entries) {
        result = STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    
    u64 offset = header->partition_entry_lba * block_size;
    if (dev->read_disk(dev, dev->media_id, offset, entries_size, entries) != STATUS_SUCCESS) {
        result = STATUS_ERROR;
        goto cleanup;
    }
    
    // Allocate entry status array
    entry_status = alloc_zero_pool(header->num_partition_entries * 
                                   sizeof(partition_entry_status_t));
    
    if (!entry_status) {
        result = STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    
    // Check partition entries
    check_gpt_entries(header, entries, entry_status);
    
    // Process partition entries
    for (u32 i = 0; i < header->num_partition_entries && count < max_partitions; i++) {
        gpt_partition_entry_t* entry = 
            (gpt_partition_entry_t*)((u8*)entries + i * header->partition_entry_size);
        
        // Skip unused, invalid, or OS-specific partitions
        if (compare_guid(&entry->partition_type_guid, &GUID_UNUSED) ||
            entry_status[i].out_of_range ||
            entry_status[i].overlap ||
            entry_status[i].os_specific) {
            continue;
        }
        
        gpt_partition_info_t* part = &partitions[count];
        
        copy_guid(&part->type_guid, &entry->partition_type_guid);
        copy_guid(&part->unique_guid, &entry->unique_guid);
        part->start_lba = entry->starting_lba;
        part->end_lba = entry->ending_lba;
        part->size_sectors = entry->ending_lba - entry->starting_lba + 1;
        part->attributes = entry->attributes;
        part->partition_number = i + 1;
        
        // Check if system partition
        part->is_system = compare_guid(&entry->partition_type_guid, &GUID_EFI_SYSTEM);
        
        // Check bootable flag (legacy BIOS bootable attribute)
        part->is_bootable = (entry->attributes & (1ULL << 2)) != 0;
        
        // Convert partition name from UTF-16LE to ASCII
        utf16_to_ascii(part->name, entry->partition_name, sizeof(part->name));
        
        count++;
    }
    
    *num_partitions = count;
    result = STATUS_SUCCESS;
    
cleanup:
    if (mbr) free_pool(mbr);
    if (primary_header) free_pool(primary_header);
    if (backup_header) free_pool(backup_header);
    if (entries) free_pool(entries);
    if (entry_status) free_pool(entry_status);
    
    return result;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Get partition name by GUID
static const char* get_partition_type_name(const guid_t* type) {
    if (compare_guid(type, &GUID_EFI_SYSTEM)) {
        return "EFI System";
    } else if (compare_guid(type, &GUID_ANDROID_BOOT)) {
        return "Android Boot";
    } else if (compare_guid(type, &GUID_ANDROID_SYSTEM)) {
        return "Android System";
    } else if (compare_guid(type, &GUID_ANDROID_USERDATA)) {
        return "Android Userdata";
    }
    return "Unknown";
}

// Find partition by name
static gpt_partition_info_t* find_partition_by_name(gpt_partition_info_t* partitions,
                                                    u32 num_partitions,
                                                    const char* name) {
    for (u32 i = 0; i < num_partitions; i++) {
        size_t j = 0;
        while (name[j] && partitions[i].name[j] && 
               name[j] == partitions[i].name[j]) {
            j++;
        }
        if (name[j] == '\0' && partitions[i].name[j] == '\0') {
            return &partitions[i];
        }
    }
    return NULL;
}

// Find partition by type GUID
static gpt_partition_info_t* find_partition_by_type(gpt_partition_info_t* partitions,
                                                    u32 num_partitions,
                                                    const guid_t* type) {
    for (u32 i = 0; i < num_partitions; i++) {
        if (compare_guid(&partitions[i].type_guid, type)) {
            return &partitions[i];
        }
    }
    return NULL;
}

// ============================================================================
// Example Usage
// ============================================================================

/*
// Example: Complete GPT partition discovery on Android device

// 1. Implement block device interface
status_t emmc_read_disk(block_device_t* dev, u32 media_id,
                        u64 offset, u32 size, void* buffer) {
    // Read from eMMC at byte offset
    return STATUS_SUCCESS;
}

status_t emmc_write_disk(block_device_t* dev, u32 media_id,
                         u64 offset, u32 size, const void* buffer) {
    // Write to eMMC at byte offset
    return STATUS_SUCCESS;
}

// 2. Initialize block device
block_device_t emmc_device = {
    .private_data = NULL,
    .total_sectors = 0x1D1C0000,  // Example: 238GB device
    .block_size = 512,
    .media_id = 1,
    .read_disk = emmc_read_disk,
    .write_disk = emmc_write_disk
};

// 3. Discover GPT partitions
gpt_partition_info_t partitions[128];
u32 num_partitions = 0;

status_t status = discover_gpt_partitions(&emmc_device, partitions, 
                                          &num_partitions, 128);

if (status == STATUS_SUCCESS) {
    // 4. Find specific Android partitions
    gpt_partition_info_t* boot = find_partition_by_name(partitions, 
                                                        num_partitions, "boot");
    gpt_partition_info_t* system = find_partition_by_name(partitions,
                                                          num_partitions, "system");
    
    // 5. Use partition information
    if (boot) {
        // Load kernel from boot partition
        u64 boot_start_byte = boot->start_lba * 512;
        u64 boot_size_bytes = boot->size_sectors * 512;
    }
}
*/