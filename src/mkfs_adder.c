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
#include <sys/stat.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12

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

// Command line arguments structure
typedef struct {
    char *input_name;
    char *output_name;
    char *file_name;
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
    uint8_t tmp[INODE_SIZE]; 
    memcpy(tmp, ino, INODE_SIZE);
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

// Parsing command line arguments
int parse_args(int argc, char *argv[], cli_args_t *args) {
    int opt;
    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"file", required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };
    
    // Initialize args
    args->input_name = NULL;
    args->output_name = NULL;
    args->file_name = NULL;
    
    while ((opt = getopt_long(argc, argv, "i:o:f:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                args->input_name = optarg;
                break;
            case 'o':
                args->output_name = optarg;
                break;
            case 'f':
                args->file_name = optarg;
                break;
            default:
                return -1;
        }
    }
    
    // validating arguments
    if (!args->input_name || !args->output_name || !args->file_name) {
        fprintf(stderr, "Usage: mkfs_adder --input <file> --output <file> --file <file>\n");
        return -1;
    }
    
    return 0;
}

uint32_t find_free_inode(uint8_t *inode_bitmap, uint32_t max_inodes) {
    for (uint32_t i = 0; i < max_inodes; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_offset = i % 8;
        if (!(inode_bitmap[byte_index] & (1 << bit_offset))) {
            return i + 1; 
        }
    }
    return 0; 
}

// Finding first free data block
uint32_t find_free_data_block(uint8_t *data_bitmap, uint32_t max_blocks) {
    for (uint32_t i = 0; i < max_blocks; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_offset = i % 8;
        if (!(data_bitmap[byte_index] & (1 << bit_offset))) {
            return i; 
        }
    }
    return UINT32_MAX; 
}

// Bitmap editing
void set_bitmap_bit(uint8_t *bitmap, uint32_t bit_index) {
    uint32_t byte_index = bit_index / 8;
    uint32_t bit_offset = bit_index % 8;
    bitmap[byte_index] |= (1 << bit_offset);
}

// Root directory entries
uint32_t count_directory_entries(uint8_t *root_data_block) {
    uint32_t count = 0;
    dirent64_t *entries = (dirent64_t *)root_data_block;
    
    for (uint32_t i = 0; i < BS / sizeof(dirent64_t); i++) {
        if (entries[i].inode_no != 0) {
            count++;
        } else {
            break; 
        }
    }
    return count;
}

int find_free_dirent_slot(uint8_t *root_data_block) {
    dirent64_t *entries = (dirent64_t *)root_data_block;
    
    for (uint32_t i = 0; i < BS / sizeof(dirent64_t); i++) {
        if (entries[i].inode_no == 0) {
            return i;
        }
    }
    return -1; 
}

int main(int argc, char *argv[]) {
    crc32_init();
    
    // Parsing command line arguments
    cli_args_t args;
    if (parse_args(argc, argv, &args) < 0) {
        return 1;
    }
    
    struct stat file_stat;
    if (stat(args.file_name, &file_stat) != 0) {
        fprintf(stderr, "Error: File '%s' not found in working directory\n", args.file_name);
        return 1;
    }
    
    // Checking file type
    if (!S_ISREG(file_stat.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a regular file\n", args.file_name);
        return 1;
    }
    
    FILE *input_file = fopen(args.input_name, "rb");
    if (!input_file) {
        perror("Failed to open input image");
        return 1;
    }
    
    // Reading superblock
    uint8_t *block = calloc(1, BS);
    if (!block) {
        perror("Memory allocation failed");
        fclose(input_file);
        return 1;
    }
    
    if (fread(block, BS, 1, input_file) != 1) {
        perror("Failed to read superblock");
        free(block);
        fclose(input_file);
        return 1;
    }
    
    superblock_t *sb = (superblock_t *)block;
    
    // Magic number validation
    if (sb->magic != 0x4D565346) {
        fprintf(stderr, "Error: Invalid MiniVSFS image format\n");
        free(block);
        fclose(input_file);
        return 1;
    }
    
    printf("Loading MiniVSFS image: %lu blocks, %lu inodes\n", sb->total_blocks, sb->inode_count);
    
    // Reading inode bitmap
    uint8_t *inode_bitmap = calloc(1, BS);
    if (!inode_bitmap) {
        perror("Memory allocation failed");
        free(block);
        fclose(input_file);
        return 1;
    }
    
    fseek(input_file, sb->inode_bitmap_start * BS, SEEK_SET);
    if (fread(inode_bitmap, BS, 1, input_file) != 1) {
        perror("Failed to read inode bitmap");
        free(block);
        free(inode_bitmap);
        fclose(input_file);
        return 1;
    }
    
    // Reading data bitmap
    uint8_t *data_bitmap = calloc(1, BS);
    if (!data_bitmap) {
        perror("Memory allocation failed");
        free(block);
        free(inode_bitmap);
        fclose(input_file);
        return 1;
    }
    
    fseek(input_file, sb->data_bitmap_start * BS, SEEK_SET);
    if (fread(data_bitmap, BS, 1, input_file) != 1) {
        perror("Failed to read data bitmap");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        fclose(input_file);
        return 1;
    }
    
    // Reading root inode
    inode_t root_inode;
    fseek(input_file, sb->inode_table_start * BS, SEEK_SET);
    if (fread(&root_inode, sizeof(inode_t), 1, input_file) != 1) {
        perror("Failed to read root inode");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        fclose(input_file);
        return 1;
    }
    
    // Reading root directory data block
    uint8_t *root_data_block = calloc(1, BS);
    if (!root_data_block) {
        perror("Memory allocation failed");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        fclose(input_file);
        return 1;
    }
    
    fseek(input_file, root_inode.direct[0] * BS, SEEK_SET);
    if (fread(root_data_block, BS, 1, input_file) != 1) {
        perror("Failed to read root directory data");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(input_file);
        return 1;
    }
    
    uint32_t new_inode_num = find_free_inode(inode_bitmap, sb->inode_count);
    if (new_inode_num == 0) {
        fprintf(stderr, "Error: No free inodes available\n");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(input_file);
        return 1;
    }
    
    // Calculating required blocks
    uint64_t file_size = file_stat.st_size;
    uint32_t blocks_needed = (file_size + BS - 1) / BS; 
    
    if (blocks_needed > DIRECT_MAX) {
        fprintf(stderr, "Error: File too large (requires %u blocks, max %d supported)\n", 
                blocks_needed, DIRECT_MAX);
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(input_file);
        return 1;
    }
    
    uint32_t data_blocks[DIRECT_MAX];
    uint32_t found_blocks = 0;
    
    for (uint32_t i = 0; i < sb->data_region_blocks && found_blocks < blocks_needed; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_offset = i % 8;
        if (!(data_bitmap[byte_index] & (1 << bit_offset))) {
            data_blocks[found_blocks++] = i;
        }
    }
    
    if (found_blocks < blocks_needed) {
        fprintf(stderr, "Error: Not enough free data blocks (need %u, found %u)\n", 
                blocks_needed, found_blocks);
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(input_file);
        return 1;
    }
    
    int free_dirent_slot = find_free_dirent_slot(root_data_block);
    if (free_dirent_slot == -1) {
        fprintf(stderr, "Error: No free directory entry slots in root directory\n");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(input_file);
        return 1;
    }
    
    FILE *add_file = fopen(args.file_name, "rb");
    if (!add_file) {
        perror("Failed to open file to add");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(input_file);
        return 1;
    }
    
    fclose(input_file);
    
struct stat out_stat;
if (stat(args.output_name, &out_stat) == 0) {
    fprintf(stderr, "Error: output image '%s' already exists. Choose a different name or remove it.\n", args.output_name);
    free(block);
    free(inode_bitmap);
    free(data_bitmap);
    free(root_data_block);
    fclose(add_file);
    fclose(input_file);
    return 1;
}
    FILE *output_file = fopen(args.output_name, "wb");
    if (!output_file) {
        perror("Failed to create output image");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(add_file);
        return 1;
    }
    
    input_file = fopen(args.input_name, "rb");
    if (!input_file) {
        perror("Failed to reopen input image");
        free(block);
        free(inode_bitmap);
        free(data_bitmap);
        free(root_data_block);
        fclose(add_file);
        fclose(output_file);
        return 1;
    }
    
    uint8_t *copy_buffer = malloc(BS);
    for (uint64_t i = 0; i < sb->total_blocks; i++) {
        if (fread(copy_buffer, BS, 1, input_file) != 1) {
            perror("Failed to read block during copy");
            free(block);
            free(inode_bitmap);
            free(data_bitmap);
            free(root_data_block);
            free(copy_buffer);
            fclose(add_file);
            fclose(input_file);
            fclose(output_file);
            return 1;
        }
        if (fwrite(copy_buffer, BS, 1, output_file) != 1) {
            perror("Failed to write block during copy");
            free(block);
            free(inode_bitmap);
            free(data_bitmap);
            free(root_data_block);
            free(copy_buffer);
            fclose(add_file);
            fclose(input_file);
            fclose(output_file);
            return 1;
        }
    }
    free(copy_buffer);
    fclose(input_file);
    
    // Output image modification
    
    // Updating output inode bitmap
    set_bitmap_bit(inode_bitmap, new_inode_num - 1); 
    fseek(output_file, sb->inode_bitmap_start * BS, SEEK_SET);
    fwrite(inode_bitmap, BS, 1, output_file);
    
    // Updating output data bitmap
    for (uint32_t i = 0; i < blocks_needed; i++) {
        set_bitmap_bit(data_bitmap, data_blocks[i]);
    }
    fseek(output_file, sb->data_bitmap_start * BS, SEEK_SET);
    fwrite(data_bitmap, BS, 1, output_file);
    
    // Creating new inodes
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    
    new_inode.mode = 0100000;
    new_inode.links = 1;
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.size_bytes = file_size;
    
    time_t now = time(NULL);
    new_inode.atime = now;
    new_inode.mtime = now;
    new_inode.ctime = now;
    
    for (uint32_t i = 0; i < blocks_needed; i++) {
        new_inode.direct[i] = sb->data_region_start + data_blocks[i];
    }
    for (uint32_t i = blocks_needed; i < DIRECT_MAX; i++) {
        new_inode.direct[i] = 0;
    }
    
    new_inode.reserved_0 = 0;
    new_inode.reserved_1 = 0;
    new_inode.reserved_2 = 0;
    new_inode.proj_id = 1;
    new_inode.uid16_gid16 = 0;
    new_inode.xattr_ptr = 0;
    
    inode_crc_finalize(&new_inode);
    
    // Writing new inode to inode table
    uint64_t inode_offset = sb->inode_table_start * BS + (new_inode_num - 1) * sizeof(inode_t);
    fseek(output_file, inode_offset, SEEK_SET);
    fwrite(&new_inode, sizeof(inode_t), 1, output_file);
    
    // Updating root directory entry count
    root_inode.links++;
    root_inode.size_bytes += sizeof(dirent64_t);
    root_inode.mtime = now;
    inode_crc_finalize(&root_inode);
    
    // Writing updated root inode
    fseek(output_file, sb->inode_table_start * BS, SEEK_SET);
    fwrite(&root_inode, sizeof(inode_t), 1, output_file);
    
    // Adding directory entry for new file
    dirent64_t *entries = (dirent64_t *)root_data_block;
    entries[free_dirent_slot].inode_no = new_inode_num;
    entries[free_dirent_slot].type = 1;
    strncpy(entries[free_dirent_slot].name, args.file_name, 57);
    entries[free_dirent_slot].name[57] = '\0';
    dirent_checksum_finalize(&entries[free_dirent_slot]);
    
    // Writing updated root directory data
    fseek(output_file, root_inode.direct[0] * BS, SEEK_SET);
    fwrite(root_data_block, BS, 1, output_file);
    
    // Writing file data blocks
    uint8_t *file_buffer = malloc(BS);
    for (uint32_t i = 0; i < blocks_needed; i++) {
        memset(file_buffer, 0, BS);
        
        size_t bytes_to_read = BS;
        if (i == blocks_needed - 1) {
            bytes_to_read = file_size - (i * BS);
        }
        
        size_t bytes_read = fread(file_buffer, 1, bytes_to_read, add_file);
        if (bytes_read != bytes_to_read) {
            perror("Failed to read file data");
            free(block);
            free(inode_bitmap);
            free(data_bitmap);
            free(root_data_block);
            free(file_buffer);
            fclose(add_file);
            fclose(output_file);
            return 1;
        }
        
        fseek(output_file, (sb->data_region_start + data_blocks[i]) * BS, SEEK_SET);
        fwrite(file_buffer, BS, 1, output_file);
    }
    
// Updating superblock mtime and checksum after modifications
sb->mtime_epoch = now;
superblock_crc_finalize(sb);
fseek(output_file, 0, SEEK_SET);
fwrite(block, BS, 1, output_file);

    free(block);
    free(inode_bitmap);
    free(data_bitmap);
    free(root_data_block);
    free(file_buffer);
    fclose(add_file);
    fclose(output_file);
    
    printf("File '%s' added successfully to MiniVSFS image\n", args.file_name);
    printf("Allocated inode: %u\n", new_inode_num);
    printf("Allocated %u data blocks\n", blocks_needed);
    
    return 0;
}