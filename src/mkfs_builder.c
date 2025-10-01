
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <getopt.h>

#define BS 4096u               
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0; 


#pragma pack(push, 1)
typedef struct {
    uint32_t magic;              
    uint32_t version;          
    uint32_t block_size;        
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start; 
    uint64_t inode_bitmap_blocks; 
    uint64_t data_bitmap_start;  
    uint64_t data_bitmap_blocks;  
    uint64_t inode_table_start;   
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;       
    uint64_t mtime_epoch;
    uint32_t flags;             
    
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum;           
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode;          
    uint16_t links;        
    uint32_t uid;            
    uint32_t gid;            
    uint64_t size_bytes;    
    uint64_t atime;        
    uint64_t mtime;        
    uint64_t ctime;          
    uint32_t direct[12];     
    uint32_t reserved_0;    
    uint32_t reserved_1;    
    uint32_t reserved_2;    
    uint32_t proj_id;       
    uint32_t uid16_gid16;  
    uint64_t xattr_ptr;     

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc;  
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;     
    uint8_t type;           
    char name[58];         
    uint8_t checksum;     
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");

// Command line arguments 
typedef struct {
    char *image_name;
    uint32_t size_kib;
    uint32_t inode_count;
} cli_args_t;

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; 
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   
    de->checksum = x;
}

// Command line arguments parsing
int parse_args(int argc, char *argv[], cli_args_t *args) {
    int opt;
    static struct option long_options[] = {
        {"image", required_argument, 0, 'i'},
        {"size-kib", required_argument, 0, 's'},
        {"inodes", required_argument, 0, 'n'},
        {0, 0, 0, 0}
    };
    
    args->image_name = NULL;
    args->size_kib = 0;
    args->inode_count = 0;
    
    while ((opt = getopt_long(argc, argv, "i:s:n:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                args->image_name = optarg;
                break;
            case 's':
                args->size_kib = atoi(optarg);
                break;
            case 'n':
                args->inode_count = atoi(optarg);
                break;
            default:
                return -1;
        }
    }
    
    // Validation
    if (!args->image_name || args->size_kib == 0 || args->inode_count == 0) {
        fprintf(stderr, "Usage: mkfs_builder --image <file> --size-kib <180..4096> --inodes <128..512>\n");
        return -1;
    }
    
    if (args->size_kib < 180 || args->size_kib > 4096 || args->size_kib % 4 != 0) {
        fprintf(stderr, "Error: size-kib must be between 180-4096 and multiple of 4\n");
        return -1;
    }
    
    if (args->inode_count < 128 || args->inode_count > 512) {
        fprintf(stderr, "Error: inode count must be between 128-512\n");
        return -1;
    }
    
    return 0;
}

// Superblock creation
void create_superblock(superblock_t *sb, uint32_t size_kib, uint32_t inode_count) {
    memset(sb, 0, sizeof(superblock_t));
    
    uint64_t total_blocks = (size_kib * 1024) / BS;
    uint64_t inode_table_blocks = (inode_count * INODE_SIZE + BS - 1) / BS; 
    
    sb->magic = 0x4D565346;
    sb->version = 1;
    sb->block_size = BS;
    sb->total_blocks = total_blocks;
    sb->inode_count = inode_count;
    sb->inode_bitmap_start = 1;
    sb->inode_bitmap_blocks = 1;
    sb->data_bitmap_start = 2;
    sb->data_bitmap_blocks = 1;
    sb->inode_table_start = 3;
    sb->inode_table_blocks = inode_table_blocks;
    sb->data_region_start = 3 + inode_table_blocks;
    sb->data_region_blocks = total_blocks - (3 + inode_table_blocks);
    sb->root_inode = ROOT_INO;
    sb->mtime_epoch = time(NULL);
    sb->flags = 0;
}

// Root directory creation
void create_root_inode(inode_t *root_inode, uint64_t data_region_start, uint32_t proj_id) {
    memset(root_inode, 0, sizeof(inode_t));
    
    root_inode->mode = 0040000; 
    root_inode->links = 2; 
    root_inode->uid = 0;
    root_inode->gid = 0;
    root_inode->size_bytes = 2 * sizeof(dirent64_t); 
    
    time_t now = time(NULL);
    root_inode->atime = now;
    root_inode->mtime = now;
    root_inode->ctime = now;
    
    // Root directory first block assignment
    root_inode->direct[0] = data_region_start;
    for (int i = 1; i < 12; i++) {
        root_inode->direct[i] = 0; 
    }
    
    root_inode->reserved_0 = 0;
    root_inode->reserved_1 = 0;
    root_inode->reserved_2 = 0;
    root_inode->proj_id = 1;
    root_inode->uid16_gid16 = 0;
    root_inode->xattr_ptr = 0;
}

// Creating root directory entries
void create_root_directory_entries(dirent64_t *entries) {
    // Create . entry
    memset(&entries[0], 0, sizeof(dirent64_t));
    entries[0].inode_no = ROOT_INO;
    entries[0].type = 2; 
    strcpy(entries[0].name, ".");
    
    memset(&entries[1], 0, sizeof(dirent64_t));
    entries[1].inode_no = ROOT_INO;
    entries[1].type = 2;
    strcpy(entries[1].name, "..");
    
    dirent_checksum_finalize(&entries[0]);
    dirent_checksum_finalize(&entries[1]);
}

// Bitmap set
void set_bitmap_bit(uint8_t *bitmap, uint32_t bit_index) {
    uint32_t byte_index = bit_index / 8;
    uint32_t bit_offset = bit_index % 8;
    bitmap[byte_index] |= (1 << bit_offset);
}

int main(int argc, char *argv[]) {
    crc32_init();
    
    // Command line parsing
    cli_args_t args;
    if (parse_args(argc, argv, &args) < 0) {
        return 1;
    }
    
    // Calculating filesystem parameters
    uint64_t total_blocks = (args.size_kib * 1024) / BS;
    uint64_t inode_table_blocks = (args.inode_count * INODE_SIZE + BS - 1) / BS;
    uint64_t data_region_start = 3 + inode_table_blocks;
    
    // Size validation
    if (data_region_start >= total_blocks) {
        fprintf(stderr, "Error: Filesystem too small for given parameters\n");
        return 1;
    } 
    FILE *img_file = fopen(args.image_name, "wb");
    if (!img_file) {
        perror("Failed to create image file");
        return 1;
    }
    
    // Superblock writing
    uint8_t *block = calloc(1, BS);
    if (!block) {
        perror("Memory allocation failed");
        fclose(img_file);
        return 1;
    }
    
    superblock_t *sb = (superblock_t *)block;
    create_superblock(sb, args.size_kib, args.inode_count);
    superblock_crc_finalize(sb);
    
    if (fwrite(block, BS, 1, img_file) != 1) {
        perror("Failed to write superblock");
        free(block);
        fclose(img_file);
        return 1;
    }
    
    // Inode bitmap creation
    memset(block, 0, BS);
    set_bitmap_bit(block, 0); 
    
    if (fwrite(block, BS, 1, img_file) != 1) {
        perror("Failed to write inode bitmap");
        free(block);
        fclose(img_file);
        return 1;
    }
    
    // Data bitmap creation
    memset(block, 0, BS);
    set_bitmap_bit(block, 0); 
    
    if (fwrite(block, BS, 1, img_file) != 1) {
        perror("Failed to write data bitmap");
        free(block);
        fclose(img_file);
        return 1;
    }
    
    // Inode table creation
    for (uint64_t i = 0; i < inode_table_blocks; i++) {
        memset(block, 0, BS);
        
        if (i == 0) {
            inode_t *root_inode = (inode_t *)block;
            create_root_inode(root_inode, data_region_start, 1);
            inode_crc_finalize(root_inode);
        }

        // Zero filling
        
        if (fwrite(block, BS, 1, img_file) != 1) {
            perror("Failed to write inode table");
            free(block);
            fclose(img_file);
            return 1;
        }
    }
    
    // Data block writing
    uint64_t data_region_blocks = total_blocks - data_region_start;
    
    for (uint64_t i = 0; i < data_region_blocks; i++) {
        memset(block, 0, BS);
        
        if (i == 0) {
            // Data block root directory entries
            dirent64_t *entries = (dirent64_t *)block;
            create_root_directory_entries(entries);
        }
        if (fwrite(block, BS, 1, img_file) != 1) {
            perror("Failed to write data block");
            free(block);
            fclose(img_file);
            return 1;
        }
    }
    
    free(block);
    fclose(img_file);
    
    printf("MiniVSFS image '%s' created successfully\n", args.image_name);
    printf("Size: %u KiB (%lu blocks)\n", args.size_kib, total_blocks);
    printf("Inodes: %u\n", args.inode_count);
    
    return 0;
}