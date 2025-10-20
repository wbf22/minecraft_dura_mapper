#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#define NBT_IMPLEMENTATION
#include "dependencies/nbt.h"  // Make sure nbt.h is in your include path
#include "dependencies/Map.h"
#include "dependencies/cJSON.h"
#define STB_IMAGE_IMPLEMENTATION
#include "dependencies/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "dependencies/stb_image_write.h"
#include <sys/stat.h> // for stat
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


#define SECTION_SIZE 16
#define BLOCKS_PER_SECTION (SECTION_SIZE*SECTION_SIZE*SECTION_SIZE)

char* ANGLE = "SW";

char* RESET = "\033[0m";
char* RED = "\033[252;3;3m";
char* YELLOW = "\033[252;186;3m";


// DUMPERS

typedef struct {
    uint8_t *data;
    size_t size;
    size_t offset;
} nbt_mem_reader_t;

size_t nbt_read_mem(void *userdata, uint8_t *buf, size_t size) {
    nbt_mem_reader_t *r = (nbt_mem_reader_t *)userdata;
    if (r->offset + size > r->size) size = r->size - r->offset;
    memcpy(buf, r->data + r->offset, size);
    r->offset += size;
    return size;
}

void dump_compound(nbt_tag_t *compound, int indent) {
    if (!compound || compound->type != NBT_TYPE_COMPOUND) return;

    for (size_t i = 0; i < compound->tag_compound.size; i++) {
        nbt_tag_t *child = compound->tag_compound.value[i];
        if (!child) continue;

        for (int j = 0; j < indent; j++) printf("  ");
        printf("%s (type %d)\n", 
               child->name ? child->name : "(no name)", 
               child->type);

        if (child->type == NBT_TYPE_COMPOUND) {
            dump_compound(child, indent + 1);
        }
        else if (child->type == NBT_TYPE_LIST) {
            printf("  (list of %zu items, subtype=%d)\n", 
                child->tag_list.size, child->tag_list.type);

            for (size_t k = 0; k < child->tag_list.size; k++) {
                nbt_tag_t *elem = child->tag_list.value[k];
                if (!elem) continue;

                for (int j = 0; j < indent + 1; j++) printf("  ");
                printf("[%zu] type=%d", k, elem->type);

                if (elem->type == NBT_TYPE_STRING) {
                    printf(" value=\"%.*s\"", (int)elem->tag_string.size, elem->tag_string.value);
                }
                else if (elem->type == NBT_TYPE_INT) {
                    printf(" value=%d", elem->tag_int.value);
                }
                else if (elem->type == NBT_TYPE_COMPOUND) {
                    printf(" (compound)\n");
                    dump_compound(elem, indent + 2);
                    continue;
                }
                // add other types if you want (BYTE, SHORT, LONG, etc.)

                printf("\n");
            }
        }
    }
}


// Helper to read a bitfield from the packed long array
static inline uint64_t get_bits(uint64_t *data, size_t bit_index, size_t bits_per_block, size_t data_len) {
    size_t start_long = bit_index / 64;
    size_t start_bit = bit_index % 64;
    uint64_t val = data[start_long] >> start_bit;

    // Check if the value spans two longs
    if (start_bit + bits_per_block > 64 && start_long + 1 < data_len) {
        val |= data[start_long + 1] << (64 - start_bit);
    }

    uint64_t mask = (1ULL << bits_per_block) - 1;
    return val & mask;
}

void make_dirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            mkdir(tmp, 0755); // ignore if exists
            *p = '/';
        }
    }
}

// Decode BlockStates long array into 16x16x16 palette indices
void decode_block_states(int64_t *data, size_t data_len, size_t palette_size, uint8_t out[SECTION_SIZE][SECTION_SIZE][SECTION_SIZE]) {
    if (palette_size == 0 || !data) return;

    size_t bits_per_block = (size_t)ceil(log2(palette_size));
    if (bits_per_block < 1) bits_per_block = 1; // minimum 1 bit

    for (size_t i = 0; i < BLOCKS_PER_SECTION; i++) {
        uint64_t idx = get_bits((uint64_t*)data, i * bits_per_block, bits_per_block, data_len);
        if (idx >= palette_size) idx = 0; // safety check
        // Convert linear index to 3D coords
        size_t y = i / (SECTION_SIZE*SECTION_SIZE);
        size_t z = (i / SECTION_SIZE) % SECTION_SIZE;
        size_t x = i % SECTION_SIZE;
        out[x][y][z] = (uint8_t)idx;
    }
}

// Print a 16x16x16 section using the palette names
void print_section_palette_names(uint8_t blocks[16][16][16], nbt_tag_t *palette) {
    if (!palette) return;

    printf("Section printout:\n");
    for (size_t y = 0; y < 16; y++) {
        printf("Y=%zu:\n", y);
        for (size_t z = 0; z < 16; z++) {
            for (size_t x = 0; x < 16; x++) {
                uint8_t idx = blocks[x][y][z];
                nbt_tag_t *block_tag = nbt_tag_list_get(palette, idx);
                nbt_tag_t *name_tag = nbt_tag_compound_get(block_tag, "Name");
                const char *name = name_tag ? name_tag->tag_string.value : "unknown";
                // print first letter of block name for simplicity
                printf("%c", name[0]);
            }
            printf("\n");
        }
        printf("\n");
    }
}

// Print a 16x16x16 section using the palette names (full names)
void print_section(uint8_t blocks[16][16][16], nbt_tag_t *palette) {
    if (!palette) return;

    printf("Section printout:\n");
    for (size_t y = 0; y < 16; y++) {
        printf("Y=%zu:\n", y);
        for (size_t z = 0; z < 16; z++) {
            for (size_t x = 0; x < 16; x++) {
                uint8_t idx = blocks[x][y][z];
                nbt_tag_t *block_tag = nbt_tag_list_get(palette, idx);
                nbt_tag_t *name_tag = block_tag ? nbt_tag_compound_get(block_tag, "Name") : NULL;
                const char *name = name_tag ? name_tag->tag_string.value : "unknown";

                // Print block name padded/truncated to 16 chars for alignment
                printf("%-24.24s ", name);
            }
            printf("\n");
        }
        printf("\n");
    }
}

void print_section_to_file(uint8_t blocks[16][16][16], nbt_tag_t *palette, const char *filename) {
    if (!palette) return;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to open file");
        return;
    }

    fprintf(fp, "Section printout:\n");
    for (size_t y = 0; y < 16; y++) {
        fprintf(fp, "Y=%zu:\n", y);
        for (size_t z = 0; z < 16; z++) {
            for (size_t x = 0; x < 16; x++) {
                uint8_t idx = blocks[x][y][z];
                nbt_tag_t *block_tag = nbt_tag_list_get(palette, idx);
                nbt_tag_t *name_tag = block_tag ? nbt_tag_compound_get(block_tag, "Name") : NULL;
                const char *name = name_tag ? name_tag->tag_string.value : "unknown";

                nbt_tag_t *props = nbt_tag_compound_get(block_tag, "Properties");
                if (props && props->type == NBT_TYPE_COMPOUND) {
                    for (size_t i = 0; i < props->tag_compound.size; i++) {
                        nbt_tag_t *p = props->tag_compound.value[i];
                        if (p->type == NBT_TYPE_STRING) {
                            fprintf(fp, "[%s=%s]", p->name, p->tag_string.value);
                        }
                    }
                }

                fprintf(fp, "%-24.24s ", name);
            }
            fprintf(fp, "\n");
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
}

void print_nbt(nbt_tag_t *tag, int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
    printf("%s (%d)", tag->name ? tag->name : "", tag->type);

    switch (tag->type) {
        case NBT_TYPE_BYTE:    printf(" = %d\n", tag->tag_byte.value); break;
        case NBT_TYPE_SHORT:   printf(" = %d\n", tag->tag_short.value); break;
        case NBT_TYPE_INT:     printf(" = %d\n", tag->tag_int.value); break;
        case NBT_TYPE_LONG:    printf(" = %lld\n", tag->tag_long.value); break;
        case NBT_TYPE_FLOAT:   printf(" = %f\n", tag->tag_float.value); break;
        case NBT_TYPE_DOUBLE:  printf(" = %f\n", tag->tag_double.value); break;
        case NBT_TYPE_STRING:  printf(" = %s\n", tag->tag_string.value); break;

        case NBT_TYPE_LIST:
            printf(" [list size %zu]\n", tag->tag_list.size);
            for (size_t i = 0; i < tag->tag_list.size; i++)
                print_nbt(tag->tag_list.value[i], depth+1);
            break;

        case NBT_TYPE_COMPOUND:
            printf(" [compound size %zu]\n", tag->tag_compound.size);
            for (size_t i = 0; i < tag->tag_compound.size; i++)
                print_nbt(tag->tag_compound.value[i], depth+1);
            break;

        case NBT_TYPE_BYTE_ARRAY:
            printf(" [byte array size %zu]\n", tag->tag_byte_array.size); break;
        case NBT_TYPE_INT_ARRAY:
            printf(" [int array size %zu]\n", tag->tag_int_array.size); break;
        case NBT_TYPE_LONG_ARRAY:
            printf(" [long array size %zu]\n", tag->tag_long_array.size); break;

        default: printf("\n"); break;
    }
}

int print_region_to_file(const char *region_file_path, const char *file_name) {

    if (mkdir("dump", 0755) == 0) {
        printf("Directory created: %s\n", "dump");
    } else {
        perror("mkdir failed");
    }


    FILE *fp = fopen(region_file_path, "rb");
    if (!fp) { perror("fopen"); return 1; }

    // Read offsets table
    uint8_t offsets[1024*4];
    fread(offsets, 1, sizeof(offsets), fp);

    // Choose chunk (0,0) in region
    int cx = 0, cz = 0;
    int index = (cx & 31) + (cz & 31) * 32;
    int sector_offset = ((offsets[index*4] << 16) | (offsets[index*4+1] << 8) | offsets[index*4+2]);
    int sectors = offsets[index*4 + 3];

    if (sector_offset == 0) { printf("Chunk not generated\n"); return 0; }

    fseek(fp, sector_offset * 4096, SEEK_SET);

    // Read chunk length and compression type
    uint32_t length;
    fread(&length, 4, 1, fp);
    uint8_t compression_type;
    fread(&compression_type, 1, 1, fp);

    uint8_t *compressed = malloc(length - 1);
    fread(compressed, 1, length - 1, fp);
    fclose(fp);

    // Decompress with zlib
    uLongf uncompressed_size = 128*1024; // Adjust if needed
    uint8_t *nbt_data = malloc(uncompressed_size);
    int res = uncompress(nbt_data, &uncompressed_size, compressed, length - 1);
    if (res != Z_OK) { fprintf(stderr, "Decompression failed: %d\n", res); return 1; }

    // Parse NBT
    nbt_mem_reader_t reader = { nbt_data, uncompressed_size, 0 };
    nbt_reader_t nbt_reader = { nbt_read_mem, &reader };
    nbt_tag_t *chunk_tag = nbt_parse(nbt_reader, NBT_PARSE_FLAG_USE_RAW);
    if (!chunk_tag) { fprintf(stderr, "NBT parse failed\n"); return 1; }


    nbt_tag_t *sections = nbt_tag_compound_get(chunk_tag, "sections");
    if (!sections) {
        fprintf(stderr, "No sections found!\n");
        return 1;
    }

    for (size_t i = 0; i < sections->tag_list.size; ++i) {
        nbt_tag_t *section = nbt_tag_list_get(sections, i);


        nbt_tag_t *biomes = nbt_tag_compound_get(section, "biomes");
        nbt_tag_t *biomes_palette = nbt_tag_compound_get(biomes, "palette");


        nbt_tag_t *bs = nbt_tag_compound_get(section, "block_states");
        nbt_tag_t *palette = nbt_tag_compound_get(bs, "palette");
        nbt_tag_t *data = nbt_tag_compound_get(bs, "data");


        int y = nbt_tag_compound_get(section, "Y")->tag_byte.value;

        if (data != NULL) {
            printf(
                "Section Y=%d, palette size=%zu, data size=%zu\n",
                y,
                palette->tag_list.size,
                data->tag_long_array.size
            );
        }
        else {
            printf(
                "Section Y=%d, palette size=%zu\n",
                y,
                palette->tag_list.size
            );

        }

        if (bs) {
            nbt_tag_t *palette = nbt_tag_compound_get(bs, "palette");
            nbt_tag_t *data = nbt_tag_compound_get(bs, "data");

            if (data) {
                uint8_t blocks[16][16][16];
                decode_block_states(data->tag_long_array.value, data->tag_long_array.size, palette->tag_list.size, blocks);

                // blocks[x][y][z] now contains palette indices
                // Use palette->tag_list[blocks[x][y][z]] to get block name and properties
                char buffer[100];
                sprintf(buffer, "dump/y%d-%s", y, file_name);
                print_section_to_file(blocks, palette, buffer);
            }
            else {
                printf("    - No blocks section Y=%d\n", y);
            }
        }

    }


    nbt_free_tag(chunk_tag);
    free(nbt_data);
    free(compressed);

    return 0;
}

int rip_textures_from_minecraft_jar(const char *jar_path, const char *out_dir) {


    if (mkdir(out_dir, 0755) != 0) {
        perror("mkdir failed");
    }


    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, jar_path, 0)) {
        printf("Failed to open jar: %s\n", jar_path);
        return 1;
    }

    int file_count = (int)mz_zip_reader_get_num_files(&zip);
    printf("Archive has %d files\n", file_count);

    for (int i = 0; i < file_count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            printf("Failed to get file stat for index %d\n", i);
            continue;
        }

        const char *name = stat.m_filename;

        // Only extract block textures
        if (strstr(name, "assets/minecraft/textures/block/") &&
            strstr(name, ".png")) {

            // printf("Extracting %s...\n", name);

            // Build output path
            char out_path[1024];
            snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);

            make_dirs(out_path);

            if (!mz_zip_reader_extract_to_file(&zip, i, out_path, 0)) {
                printf("Failed to extract %s\n", name);
            }
        }
    }

    mz_zip_reader_end(&zip);

    return 0;
}

int rip_block_states_from_minecraft_jar(const char *jar_path, const char *out_dir) {


    if (mkdir(out_dir, 0755) != 0) {
        perror("mkdir failed");
    }


    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, jar_path, 0)) {
        printf("Failed to open jar: %s\n", jar_path);
        return 1;
    }

    int file_count = (int)mz_zip_reader_get_num_files(&zip);
    printf("Archive has %d files\n", file_count);

    for (int i = 0; i < file_count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            printf("Failed to get file stat for index %d\n", i);
            continue;
        }

        const char *name = stat.m_filename;

        // Only extract block textures
        if (strstr(name, "assets/minecraft/blockstates/") &&
            strstr(name, ".json")) {

            // printf("Extracting %s...\n", name);

            // Build output path
            char out_path[1024];
            snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);

            make_dirs(out_path);

            if (!mz_zip_reader_extract_to_file(&zip, i, out_path, 0)) {
                printf("Failed to extract %s\n", name);
            }
        }
    }

    mz_zip_reader_end(&zip);

    return 0;
}

int rip_json_files_from_minecraft_jar(const char *jar_path, const char *json_files_jar_folder, const char *out_dir) {


    if (mkdir(out_dir, 0755) != 0) {
        perror("mkdir failed");
    }


    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, jar_path, 0)) {
        printf("Failed to open jar: %s\n", jar_path);
        return 1;
    }

    int file_count = (int)mz_zip_reader_get_num_files(&zip);
    printf("Archive has %d files\n", file_count);

    for (int i = 0; i < file_count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            printf("Failed to get file stat for index %d\n", i);
            continue;
        }

        const char *name = stat.m_filename;

        // Only extract block textures
        if (strstr(name, json_files_jar_folder) &&
            strstr(name, ".json")) {

            // printf("Extracting %s...\n", name);

            // Build output path
            char out_path[1024];
            snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);

            make_dirs(out_path);

            if (!mz_zip_reader_extract_to_file(&zip, i, out_path, 0)) {
                printf("Failed to extract %s\n", name);
            }
        }
    }

    mz_zip_reader_end(&zip);

    return 0;
}



// ARRAYS

/**
 * Adds the element to the last index in the array. If last_index is -1, then the array will be walked until
 * the first NULL element. 
 * 
 * If the array is full, a new array is made with double the size. The current array will then be freed and replaced with the
 * new array.
 */
void push(void** array, void* element, long array_length, long* last_index) {

    // determine last_index if not provided
    if (*last_index == -1) {
        *last_index = 0;
        while(array[*last_index] != NULL && last_index < array_length) {
            *last_index++;
        }
    }

    // double if full
    if (last_index == array_length) {
        void** new_array = calloc(array_length * 2, sizeof(element));
        memcpy(new_array, array, sizeof(array));
        free(array);
        array = new_array;
    }

    // set last element
    array[*last_index] = element;
    *last_index++;
}

void free_array_of_pointers(void** array) {
    if (array[0] != NULL) {

    }
}

void resize_if_needed(void*** array, int new_size, int *capacity, size_t element_size) {

    if (new_size >= *capacity) { // directly use *capacity, no need for cp
        *capacity *= 2;
        void **tmp = realloc(*array, (*capacity) * element_size);
        if (!tmp) {
            fprintf(stderr, "Failed resizing an array. Ran out of memory probably\n");
            exit(-1);
        }
        *array = tmp;
    }
}



// STRINGS

/**
 * Add the second string onto the end of the buffer.
 * 
 * The buffer string may be doubled in length if an overflow will occur.
 */
int bcat(char* buffer, long* buffer_len, char* str2) {
    size_t len = strlen(buffer) + strlen(str2) + 1;

    if (len >= *buffer_len-1) {
        char *result = malloc(len * 2 * sizeof(char));

        if (result == NULL) {
            perror("malloc failed");
            return 1;
        }

        strcpy(result, buffer);   // copy str1 into result
        strcat(result, str2);   // append str2

        free(buffer);
        buffer = result;
    }
    else {
        strcat(buffer, str2);   // append str2
    }

    return 0;
}



char* CAT(char* str, ...) {
    
    
    if (!str) str = "";  // handle NULL as empty string

    size_t total_len = strlen(str);

    // First pass: compute total length
    va_list args;
    va_start(args, str);

    char* next;
    while ((next = va_arg(args, char*)) != NULL) {
        if (!next) next = "";  // treat NULL as empty
        total_len += strlen(next);
    }

    va_end(args);

    // Allocate result string
    char* result = malloc(total_len + 1);
    if (!result) return NULL;

    // Copy strings into result
    strcpy(result, str);

    va_start(args, str);
    while ((next = va_arg(args, char*)) != NULL) {
        if (!next) next = "";
        strcat(result, next);
    }
    va_end(args);

    return result;
}

/**
 * Combines the two strings. creating a new string. Neither input string is freed.
 */
char* cat(char* str, char* app) {
    // if (!str) str = "";  // treat NULL as empty string
    // if (!app) app = "";
    
    // size_t len = strlen(str) + strlen(app) + 1;
    // char *result = malloc(len);
    // if (!result) return NULL;

    // strcpy(result, str);
    // strcat(result, app);

    // return result;

    return CAT(str, app, NULL);
}


int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;

    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    if (len_suffix > len_str) return 0;

    // Compare the end of str with suffix
    return strcmp(str + len_str - len_suffix, suffix) == 0;
}

/**
 * Splits a string returned the amount of splits created in 'count'
 */
char** split(char* str, char* delim, int* count) {
    // copy strings since literals can't be processed by strtok
    char* strn = strdup(str);

    // split
    *count = 0;
    char** splits = malloc(2*sizeof(char*));
    int array_size = 2;
    
    char *token = strtok(strn, delim);
    while (token != NULL) {
        
        resize_if_needed(&splits, *count+1, &array_size, sizeof(char*));

        splits[*count] = strdup(token);
        (*count)++;
        token = strtok(NULL, delim);
    }
    free(strn);

    return splits;
}


// ARG PARSER

#define len(x) (sizeof(x) / sizeof((x)[0]))

typedef enum { ARG_BOOL, ARG_INT, ARG_STRING } ArgType;
typedef struct {
    const char *long_name;   // e.g. "file"
    char short_name;         // e.g. 'f'
    ArgType type;            // ARG_BOOL / ARG_INT / ARG_STRING
    const char *help;        // help text
    void *value;             // where to store the parsed value
} ArgOption;

// Auto-help printer
void print_help(const char *prog, ArgOption *options, int count) {
    printf("Usage: %s [options]\n\nOptions:\n", prog);
    for (int i = 0; i < count; i++) {
        printf("  -%c, --%-10s %s\n",
               options[i].short_name,
               options[i].long_name,
               options[i].help);
    }
}

// Generic parse function
void parse_args(int argc, char *argv[], ArgOption *options, int options_size) {
    struct option long_opts[options_size + 2]; // +2 for help and terminator

    for (int i = 0; i < options_size; i++) {
        long_opts[i].name    = options[i].long_name;
        long_opts[i].has_arg = (options[i].type == ARG_BOOL) ? no_argument : required_argument;
        long_opts[i].flag    = 0;
        long_opts[i].val     = options[i].short_name;
    }
    // Add built-in help
    long_opts[options_size].name = "help";
    long_opts[options_size].has_arg = no_argument;
    long_opts[options_size].flag = 0;
    long_opts[options_size].val = 'h';
    long_opts[options_size+1].name = 0;

    char short_opts[2*options_size + 3]; // e.g. "f:n:"
    int pos = 0;
    for (int i = 0; i < options_size; i++) {
        short_opts[pos++] = options[i].short_name;
        if (options[i].type != ARG_BOOL) {
            short_opts[pos++] = ':';
        }
    }
    short_opts[pos++] = 'h';
    short_opts[pos] = '\0';

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        if (opt == 'h') {
            print_help(argv[0], options, options_size);
            exit(0);
        }
        for (int i = 0; i < options_size; i++) {
            if (opt == options[i].short_name) {
                switch (options[i].type) {
                    case ARG_BOOL:
                        *(int*)options[i].value = 1;
                        break;
                    case ARG_INT:
                        *(int*)options[i].value = atoi(optarg);
                        break;
                    case ARG_STRING:
                        *(char**)options[i].value = optarg;
                        break;
                }
            }
        }
    }
}




// MAIN METHODS



// STRUCTS

typedef struct {
    char* key;
    char* value;
} Property;

void free_property(Property* property) {
    free(property->key);
    free(property->value);
}

typedef struct {
    char* name;                   // Full block name, e.g., "minecraft:grass_block"
    int tint_index;               // -1 if none
    size_t num_properties;
    Property* properties;         // Array of properties like "facing=north", "half=top"
    char* model;                  // Optional reference to a block model JSON
    char* biome;                  // Optional biome name for color calculations
} Block;

void free_block(Block* block) {
    free(block->name);
    free(block->model);
    free(block->biome);
    for(size_t i = 0; i < block->num_properties; ++i) {
        free_property(&block->properties[i]);
    }
}


typedef struct {
    uint8_t pixels[16*16*4]; // 16x16 pixel image representing a block (side view kind of thing)
} RenderedBlock;



// UTILITY METHODS

int extract_jar(const char *jar_path, const char *out_dir) {
    
    if (mkdir(out_dir, 0755) != 0) {
        perror("mkdir failed");
    }


    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, jar_path, 0)) {
        printf("Failed to open jar: %s\n", jar_path);
        return 1;
    }

    int file_count = (int)mz_zip_reader_get_num_files(&zip);
    printf("Archive has %d files\n", file_count);

    for (int i = 0; i < file_count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            printf("Failed to get file stat for index %d\n", i);
            continue;
        }

        const char *name = stat.m_filename;

        // Only extract block textures
        if (strstr(name, ".json") || strstr(name, ".png")) {

            // printf("Extracting %s...\n", name);

            // Build output path
            char out_path[1024];
            snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);

            make_dirs(out_path);

            if (!mz_zip_reader_extract_to_file(&zip, i, out_path, 0)) {
                printf("Failed to extract %s\n", name);
            }
        }
    }

    mz_zip_reader_end(&zip);

    return 0;
}

int is_regular_file(const char *dir, const char *name) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, name);

    struct stat st;
    if (stat(fullpath, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

char **collect_files(const char *path, int *out_count) {
    char **files = NULL;
    int count = 0;
    int capacity = 10;

    files = malloc(capacity * sizeof(char*));
    if (!files) {
        perror("malloc");
        *out_count = 0;
        return NULL;
    }

#ifdef _WIN32
    // Use wide chars for Windows API
    wchar_t wpath[MAX_PATH];
    mbstowcs(wpath, path, MAX_PATH);

    wchar_t search_path[MAX_PATH];
    swprintf(search_path, MAX_PATH, L"%ls\\*", wpath);

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        free(files);
        *out_count = 0;
        return NULL;
    }

    do {
        // Skip "." and ".."
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        // Only regular files
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) 
            continue;

        // Grow array if needed
        resize_if_needed(&files, count, &capacity, sizeof(char*));
        
        // Convert wchar_t filename to UTF-8
        size_t len_name = wcstombs(NULL, findData.cFileName, 0);
        if (len_name == (size_t)-1) continue; // conversion failed
        char *filename = malloc(len_name + 1);
        if (!filename) break;
        wcstombs(filename, findData.cFileName, len_name + 1);

        // Build full path: path + "\\" + filename
        size_t len_path = strlen(path);
        size_t len_full = len_path + 1 + len_name + 1; // path + '\' + filename + '\0'
        char *fullpath = malloc(len_full);
        if (!fullpath) {
            free(filename);
            break;
        }
        snprintf(fullpath, len_full, "%s\\%s", path, filename);

        free(filename); // free intermediate name, we only keep fullpath

        files[count++] = fullpath;

    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

#else
    // POSIX implementation
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        free(files);
        *out_count = 0;
        return NULL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' &&
           (entry->d_name[1] == '\0' ||
           (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        if (!is_regular_file(path, entry->d_name)) continue;

        resize_if_needed(&files, count, &capacity, sizeof(char*));

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        files[count++] = strdup(fullpath);
    }

    closedir(dir);
#endif

    *out_count = count;
    return files;
}

char* read_file(const char* filename) {
    FILE* fp = fopen(filename, "rb");  // open binary to avoid issues with line endings
    if (!fp) {
        perror("fopen");
        return NULL;
    }

    // Seek to end to get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, fp);
    fclose(fp);

    if (read_bytes != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';  // null-terminate
    return buffer;
}


void substr(char* dest, char* src, int start_inclusive, int end_exclusive) {
    int len = end_exclusive - start_inclusive;
    strncpy(dest, src + start_inclusive, len);
    dest[len] = '\0';
}

void extract_mca_region_coordinates(const char* mca_path, long long* x, long long* z) {
    int p_len = strlen(mca_path);

    int i = p_len - 1;
    int done = 0;
    while(mca_path[i] != '.') i--;
    int end_z = i;
    i--;
    while(mca_path[i] != '.') i--;
    int start_z = i + 1;
    int end_x = i;
    i--;
    while(mca_path[i] != '.') i--;
    int start_x = i + 1;

    char x_str[256];
    substr(x_str, mca_path, start_x, end_x);
    printf("%s\n", x_str);
    *x = strtoll(x_str, NULL, 10);

    char z_str[256];
    substr(z_str, mca_path, start_z, end_z);
    *z = strtoll(z_str, NULL, 10);

}

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

int compare_mca_paths(const void *a, const void *b) {
    const char* first = *(const char**)a;
    const char* second = *(const char**)b;

    long long first_x = 0;
    long long first_z = 0;
    long long second_x = 0;
    long long second_z = 0;

    extract_mca_region_coordinates(first, &first_x, &first_z);
    extract_mca_region_coordinates(second, &second_x, &second_z);

    
    if (strcmp(ANGLE, "NE") == 0) { // + +
        long long max_x = MAX(first_x, second_x);
        long long max_z = MAX(first_z, second_z);
        long long max_coor = MAX(max_x, max_z);

        long long dist_first = llabs(max_coor - first_x) + llabs(max_coor - first_z);
        long long dist_second = llabs(max_coor - second_x) + llabs(max_coor - second_z);
        return dist_first - dist_second;
    }
    else if (strcmp(ANGLE, "SE") == 0) {  // + -
        long long max_x = MAX(first_x, second_x);
        long long min_z = MIN(first_z, second_z);
        long long max_coor = MAX(llabs(max_x), llabs(min_z));

        long long dist_first = llabs(max_coor - first_x) + llabs(-max_coor - first_z);
        long long dist_second = llabs(max_coor - second_x) + llabs(-max_coor - second_z);
        return dist_first - dist_second;
    }
    else if (strcmp(ANGLE, "SW") == 0) {  // - -
        long long min_x = MIN(first_x, second_x);
        long long min_z = MIN(first_z, second_z);
        long long min_coor = MIN(min_x, min_z);

        long long dist_first = llabs(min_coor - first_x) + llabs(min_coor - first_z);
        long long dist_second = llabs(min_coor - second_x) + llabs(min_coor - second_z);
        return dist_first - dist_second;

    }
    else if (strcmp(ANGLE, "NW") == 0) {  // - +
        long long min_x = MIN(first_x, second_x);
        long long max_z = MAX(first_z, second_z);
        long long max_coor = MAX(llabs(min_x), llabs(max_z));

        long long dist_first = llabs(-max_coor - first_x) + llabs(max_coor - first_z);
        long long dist_second = llabs(-max_coor - second_x) + llabs(max_coor - second_z);
        return dist_first - dist_second;
    }
    else {
        printf("'%s' isn't a valid angle.\n", ANGLE);
        exit(-1);
    }
}

char* make_block_tag() {

}


int is_directory(const char *dir, const char *entry_name) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, entry_name);

    struct stat statbuf;
    if (stat(fullpath, &statbuf) != 0) return 0;
    return S_ISDIR(statbuf.st_mode);
}

char **collect_folders(const char *path, int *out_count) {
    char **dirs = NULL;
    int count = 0;
    int capacity = 10;

    dirs = malloc(capacity * sizeof(char*));
    if (!dirs) {
        perror("malloc");
        *out_count = 0;
        return NULL;
    }

#ifdef _WIN32
    wchar_t wpath[MAX_PATH];
    mbstowcs(wpath, path, MAX_PATH);

    wchar_t search_path[MAX_PATH];
    swprintf(search_path, MAX_PATH, L"%ls\\*", wpath);

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        free(dirs);
        *out_count = 0;
        return NULL;
    }

    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue; // Only directories

        resize_if_needed(&dirs, count, &capacity, sizeof(char*));

        size_t len_name = wcstombs(NULL, findData.cFileName, 0);
        if (len_name == (size_t)-1) continue;

        char *filename = malloc(len_name + 1);
        if (!filename) break;
        wcstombs(filename, findData.cFileName, len_name + 1);

        size_t len_path = strlen(path);
        size_t len_full = len_path + 1 + len_name + 1;
        char *fullpath = malloc(len_full);
        if (!fullpath) {
            free(filename);
            break;
        }

        snprintf(fullpath, len_full, "%s\\%s", path, filename);
        free(filename);

        dirs[count++] = fullpath;

    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

#else
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        free(dirs);
        *out_count = 0;
        return NULL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' &&
           (entry->d_name[1] == '\0' ||
           (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        if (!is_directory(path, entry->d_name)) continue;

        resize_if_needed(&dirs, count, &capacity, sizeof(char*));

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        dirs[count++] = strdup(fullpath);

        // recurse
        int rec_count;
        char **rec_dirs = collect_folders(fullpath, &rec_count);
        for (int r = 0; r < rec_count; r++) {
            resize_if_needed(&dirs, count, &capacity, sizeof(char*));
            dirs[count++] = rec_dirs[r];
        }
        free(rec_dirs); // free array
    }

    closedir(dir);
#endif

    *out_count = count;
    return dirs;
}

char* BLOCKS_PATH = "dump/jar/assets/minecraft/models/block/";
char* TEXTURE_PATH = "dump/jar/assets/minecraft/textures/";
void find_block_models() {

    int count;
    char **dirs = collect_folders("dump/jar", &count);
    for (int i = 0; i < count; i++) {
        if (ends_with(dirs[i], "models/block")) {
            printf("%s\n", dirs[i]);
            BLOCKS_PATH = cat(dirs[i], "/");
        }
        else if (ends_with(dirs[i], "textures/block")) {
            printf("%s\n", dirs[i]);
            TEXTURE_PATH = cat(dirs[i], "/");
        }
        free(dirs[i]); // free each path
    }
    free(dirs); // free array
}

// Converts the x y of a pixel to an index in the array (assumes 4 channel pixels)
int pixel_index(int x, int y, int image_width) {
    return (y * image_width + x) * 4;
}

// swaps two values
void swap(int* a, int* b) {
    int a_i = *a;
    int b_i = *b;
    *a = b_i;
    *b = a_i;
}

// get's the value on the oppposite side of the flip value
void flip(int* a, int flip_value) {
    int dist = flip_value - *a;
    *a = flip_value + dist;
}


/**
 * Convert model coordinates to texture coordinates
 * 
 * for top faces you'll want to input x and z
 * 
 * For side faces you'll do x and y or z and y
 */
void model_coor_to_texture_x_y(int c_1, int c_2, int* t_x, int* t_y) {
    *t_x = c_1;
    *t_y = c_2;
}

void model_x_y_z_to_image_x_y(int x, int y, int z, int* image_x, int* image_y) {
    /*

    slope x = 1/2
    slope z = -1/2


    image_x = 1/2x + 1/2z
    image_y = -1/4x + 11 + 1/4z - 1/2y

    TESTS:
    - 15,8,0
        image_x = 7.5
        image_y = -7.5 + 11 - 4

    - 0,0,0
        image_x = 0
        image_y = 11
    - 16,0,0
        image_x = 0
        image_y = 3
    - 16,0,16
        image_x = 16
        image_y = 11
    - 0,0,16
        image_x = 8
        image_y = 15
    */
    
    *image_x = round(0.5*x + 0.5*z);
    *image_y = round(-0.25*x + 11 + 0.25*z - 0.5*y);
}


void apply_side_rotation(int side, int* x_start, int* x_end, int* z_start, int* z_end) {

    if (side == 0) {
        // no change
    }
    else if (side == 1) {
        // 8, 8, 0 -> 16, 8, 8
        // 16, 16, 16 -> 0, 16, 16
        // swap x and z for both
        // flip x for both
        
        swap(x_start, z_start);
        swap(x_end, z_end);

        flip(x_start, 8);
        flip(x_end, 8);
    }
    else if (side == 2) {
        // 8, 8, 0 -> 8, 8, 16
        // 16, 16, 16 -> 0, 16, 0
        // flip z for both
        // flip x for both 
        
        flip(z_start, 8);
        flip(z_end, 8);

        flip(x_start, 8);
        flip(x_end, 8);
    }
    else if (side == 3) {
        // 8, 8, 0 -> 0, 8, 8
        // 16, 16, 16 -> 16, 16, 0
        // swap x and z for both
        // flip z for both

        swap(x_start, z_start);
        swap(x_end, z_end);

        flip(z_start, 8);
        flip(z_end, 8);
    }
}

void set_top_square(uint8_t* image, int side, uint8_t* top_texture, cJSON* from, cJSON* to) {

    // DETERMINE indices
    int model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
    int model_x_end = cJSON_GetArrayItem(to, 0)->valueint;
    int model_y = cJSON_GetArrayItem(to, 1)->valueint;
    int model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
    int model_z_end = cJSON_GetArrayItem(to, 2)->valueint;

    apply_side_rotation(side, &model_x_start, &model_x_end, &model_z_start, &model_z_end);


    // FOR EACH square walk x y of square on texture converting to image pixels
    int x_step = (model_x_start <= model_x_end)? 1 : -1;
    int z_step = (model_z_start <= model_z_end)? 1 : -1;

    printf("\n");
    for (int x = model_x_start; x != model_x_end; x+=x_step) {
        for (int z = model_z_start; z != model_z_end; z+=z_step) {
            if (x == model_x_end) continue;
            if (z == model_z_end) continue;
            
            // convert to image coordinates
            int image_x, image_y;
            model_x_y_z_to_image_x_y(x, model_y, z, &image_x, &image_y);

            // skip out of bounds stuff
            if (image_x == 16 || image_y == 16 || image_x == -1 || image_y == -1) continue;


            // if pixel set by other coordinates skip
            int i = pixel_index(image_x, image_y, 16);
            if (image[i] == 0) {
                printf("x: %d, y: %d\n", image_x, image_y);

                // otherwise set it
                int tex_x, tex_y;
                model_coor_to_texture_x_y(x, z, &tex_x, &tex_y);
                int t = pixel_index(tex_x, tex_y, 16);

                image[i] = top_texture[t];
                image[i+1] = top_texture[t+1];
                image[i+2] = top_texture[t+2];
                image[i+3] = top_texture[t+3];
                // stbi_write_jpg("test.jpg", 16, 16, 4, image, 96);
            }

        }
    }
}

void set_left_square(uint8_t* image, int side, uint8_t* top_texture, cJSON* from, cJSON* to) {

    // DETERMINE indices
    int model_x_start;
    int model_x_end;
    int model_y_start;
    int model_y_end;
    int model_z_start;
    int model_z_end;
    if (side == 0) {
        model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
        model_x_end = model_x_start;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
        model_z_end = cJSON_GetArrayItem(to, 2)->valueint;
    }
    else if (side == 1) {
        model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
        model_x_end = cJSON_GetArrayItem(to, 0)->valueint;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(to, 2)->valueint;
        model_z_end = model_z_start;
    }
    else if (side == 2) {
        model_x_start = cJSON_GetArrayItem(to, 0)->valueint;
        model_x_end = model_x_start;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
        model_z_end = cJSON_GetArrayItem(to, 2)->valueint;
    }
    else if (side == 3) {
        model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
        model_x_end = cJSON_GetArrayItem(to, 0)->valueint;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
        model_z_end = model_z_start;
    }
    apply_side_rotation(side, &model_x_start, &model_x_end, &model_z_start, &model_z_end);


    // FOR EACH square walk x y of square on texture converting to image pixels
    int x_step = (model_x_start <= model_x_end)? 1 : -1;
    int y_step = (model_y_start <= model_y_end)? 1 : -1;
    int z_step = (model_z_start <= model_z_end)? 1 : -1;

    int model_h_start = (model_x_start == model_x_end)? model_z_start : model_x_start;
    int model_h_end = (model_x_start == model_x_end)? model_z_end : model_x_end;
    int h_step = (model_x_start == model_x_end)? z_step : x_step;

    printf("\n");
    for (int y = model_y_start; y != model_y_end; y+=y_step) {
        for (int h = model_h_start; h != model_h_end; h+=h_step) {
            if (h == model_h_end) continue;
            if (y == model_y_end) continue;
            
            // convert to image coordinates
            int image_x, image_y;
            if (model_x_start == model_x_end) {
                model_x_y_z_to_image_x_y(model_x_start, y, h, &image_x, &image_y);
            }
            else {
                model_x_y_z_to_image_x_y(h, y, model_z_start, &image_x, &image_y);
            }

            // skip out of bounds stuff
            if (image_x == 16 || image_y == 16 || image_x == -1 || image_y == -1) continue;


            // if pixel set by other coordinates skip
            int i = pixel_index(image_x, image_y, 16);
            if (image[i] == 0) {
                printf("x: %d, y: %d\n", image_x, image_y);

                // otherwise set it
                int tex_x, tex_y;
                model_coor_to_texture_x_y(h, y, &tex_x, &tex_y);
                int t = pixel_index(tex_x, tex_y, 16);

                image[i] = top_texture[t];
                image[i+1] = top_texture[t+1];
                image[i+2] = top_texture[t+2];
                image[i+3] = top_texture[t+3];
                // stbi_write_jpg("test.jpg", 16, 16, 4, image, 96);
            }
        }
    }
}

void set_right_square(uint8_t* image, int side, uint8_t* top_texture, cJSON* from, cJSON* to) {

    // DETERMINE indices
    int model_x_start;
    int model_x_end;
    int model_y_start;
    int model_y_end;
    int model_z_start;
    int model_z_end;
    if (side == 0) {
        model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
        model_x_end = cJSON_GetArrayItem(to, 0)->valueint;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(to, 2)->valueint;
        model_z_end = model_z_start;
    }
    else if (side == 1) {
        model_x_start = cJSON_GetArrayItem(to, 0)->valueint;
        model_x_end = model_x_start;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
        model_z_end = cJSON_GetArrayItem(to, 2)->valueint;
    }
    else if (side == 2) {
        model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
        model_x_end = cJSON_GetArrayItem(to, 0)->valueint;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
        model_z_end = model_z_start;
    }
    else if (side == 3) {
        model_x_start = cJSON_GetArrayItem(from, 0)->valueint;
        model_x_end = model_x_start;
        model_y_start = cJSON_GetArrayItem(from, 1)->valueint;
        model_y_end = cJSON_GetArrayItem(to, 1)->valueint;
        model_z_start = cJSON_GetArrayItem(from, 2)->valueint;
        model_z_end = cJSON_GetArrayItem(to, 2)->valueint;
    }

    apply_side_rotation(side, &model_x_start, &model_x_end, &model_z_start, &model_z_end);


    // FOR EACH square walk x y of square on texture converting to image pixels
    int x_step = (model_x_start <= model_x_end)? 1 : -1;
    int y_step = (model_y_start <= model_y_end)? 1 : -1;
    int z_step = (model_z_start <= model_z_end)? 1 : -1;

    int model_h_start = (model_x_start == model_x_end)? model_z_start : model_x_start;
    int model_h_end = (model_x_start == model_x_end)? model_z_end : model_x_end;
    int h_step = (model_x_start == model_x_end)? z_step : x_step;

    printf("\n");
    for (int y = model_y_start; y != model_y_end; y+=y_step) {
        for (int h = model_h_start; h != model_h_end; h+=h_step) {
            if (h == model_h_end) continue;
            if (y == model_y_end) continue;
            
            // convert to image coordinates
            int image_x, image_y;
            if (model_x_start == model_x_end) {
                model_x_y_z_to_image_x_y(model_x_start, y, h, &image_x, &image_y);
            }
            else {
                model_x_y_z_to_image_x_y(h, y, model_z_start, &image_x, &image_y);
            }

            // skip out of bounds stuff
            if (image_x == 16 || image_y == 16 || image_x == -1 || image_y == -1) continue;


            // if pixel set by other coordinates skip
            int i = pixel_index(image_x, image_y, 16);
            if (image[i] == 0) {
                printf("x: %d, y: %d\n", image_x, image_y);

                // otherwise set it
                int tex_x, tex_y;
                model_coor_to_texture_x_y(h, y, &tex_x, &tex_y);
                int t = pixel_index(tex_x, tex_y, 16);

                image[i] = top_texture[t];
                image[i+1] = top_texture[t+1];
                image[i+2] = top_texture[t+2];
                image[i+3] = top_texture[t+3];
                // stbi_write_jpg("test.jpg", 16, 16, 4, image, 96);
            }
        }
    }
}

void combine_images(uint8_t* result, uint8_t* img_1, uint8_t* img_2, uint8_t* img_3, int size) {
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            int i = pixel_index(x, y, size);
            
            if (img_1[i] != 0) {
                result[i] = img_1[i];
                result[i+1] = img_1[i+1];
                result[i+2] = img_1[i+2];
                result[i+3] = img_1[i+3];
            }

            if (img_2[i] != 0) {
                result[i] = img_2[i];
                result[i+1] = img_2[i+1];
                result[i+2] = img_2[i+2];
                result[i+3] = img_2[i+3];
            }

            if (img_3[i] != 0) {
                result[i] = img_3[i];
                result[i+1] = img_3[i+1];
                result[i+2] = img_3[i+2];
                result[i+3] = img_3[i+3];
            }
        }
    }
}



char* get_file_name_from_minecraft_name(char* jar_folder, char* minecraft_name, char* extension) {
    int c = 0;
    char** splits = split(minecraft_name, "/", &c);
    minecraft_name = splits[1];
    char* path = CAT(jar_folder, minecraft_name, extension, NULL);

    for (int i = 0; i < c; i++) {
        free(splits[i]);
    }
    free(splits);

    return path;
}


uint8_t* load_texture(char* texture_minecraft_name) {
    char* path = get_file_name_from_minecraft_name(TEXTURE_PATH, texture_minecraft_name, ".png");
    
    int width, height, n;
    uint8_t *data = stbi_load(path, &width, &height, &n, 4);
    free(path);

    return data;
}

cJSON* load_block_json(const char* block_minecraft_name) {
    char* path = get_file_name_from_minecraft_name(BLOCKS_PATH, block_minecraft_name, ".json");
    char* content = read_file(path);
    cJSON *json = cJSON_Parse(content);
    free(path);
    free(content);
    return json;
}

/**
 * Returns pixels (16x16) for a block. 
 * 
 * Blank pixels are set as -1.
 * 
 * The block_minecraft_name should be provided like 'minecraft:block/stairs'
 */
RenderedBlock* get_rendered_block(char* block_minecraft_name, Map* rendered_blocks) {

    RenderedBlock* block = at(rendered_blocks, block_minecraft_name);
    
    // render block if not rendered yet
    if (block == NULL) {
        // RENDER BLOCK SHAPE
        cJSON *json = load_block_json(block_minecraft_name);

        // GET TEXTURES
        uint8_t* top_texture; // 16x16x4
        uint8_t* side_texture; // 16x16x4
        if (cJSON_HasObjectItem(json, "textures")) {
            cJSON* textures = cJSON_GetObjectItem(json, "textures");
            if (cJSON_HasObjectItem(textures, "all")) {
                cJSON* all = cJSON_GetObjectItem(textures, "all");
                top_texture = load_texture(all->valuestring);
                side_texture = top_texture;
            }
            else if (cJSON_HasObjectItem(textures, "wall")) {
                cJSON* wall = cJSON_GetObjectItem(textures, "wall");
                top_texture = load_texture(wall->valuestring);
                side_texture = top_texture;
            }
            else {
                cJSON* top = cJSON_GetObjectItem(textures, "top");
                cJSON* side = cJSON_GetObjectItem(textures, "top");
                top_texture = load_texture(top->valuestring);
                side_texture = load_texture(side->valuestring);
            }
        }


        // GET OBJECT DIMENSIONS
        cJSON *model_json = json;
        cJSON* elements = cJSON_GetObjectItem(model_json, "elements");
        while (!elements || !cJSON_IsArray(elements)) {
            if (cJSON_HasObjectItem(model_json, "parent")) {
                char* parent = cJSON_GetObjectItem(model_json, "parent")->valuestring;
                cJSON *model_json = load_block_json(parent);
                elements = cJSON_GetObjectItem(model_json, "elements");
            }
            else {
                break;
            }
        }

        uint8_t pixels_0[16*16*4] = {0};
        uint8_t pixels_1[16*16*4] = {0};
        uint8_t pixels_2[16*16*4] = {0};
        uint8_t pixels_3[16*16*4] = {0};

        int squares[4][3];
        if (elements) {
            int num_elements = cJSON_GetArraySize(elements);
            
            for (int side = 0; side < 4; side++) {

                uint8_t* pixels;
                if (side == 0) 
                    pixels = pixels_0;
                else if (side == 1)
                    pixels = pixels_1;
                else if (side == 2)
                    pixels = pixels_2;
                else if (side == 3)
                    pixels = pixels_3;

                for (int i = 0; i < num_elements; i++) {
                    cJSON* element = cJSON_GetArrayItem(elements, i);

                    // DETERMINE the 3 squares facing the viewer
                    cJSON* from = cJSON_GetObjectItem(element, "from");
                    cJSON* to = cJSON_GetObjectItem(element, "to");


                    // top square
                    uint8_t top_pixels[16*16*4] = {0};
                    set_top_square(top_pixels, side, top_texture, from, to);
                    stbi_write_jpg("test.jpg", 16, 16, 4, top_pixels, 96);


                    // left square
                    uint8_t left_pixels[16*16*4] = {0};
                    set_left_square(left_pixels, side, side_texture, from, to);
                    stbi_write_jpg("test.jpg", 16, 16, 4, left_pixels, 96);
                    

                    // right square
                    uint8_t right_pixels[16*16*4] = {0};
                    set_right_square(right_pixels, side, side_texture, from, to);
                    stbi_write_jpg("test.jpg", 16, 16, 4, right_pixels, 96);

                    combine_images(pixels, top_pixels, left_pixels, right_pixels, 16);
                    stbi_write_jpg("test.jpg", 16, 16, 4, pixels, 96);

                }
            }
        }
        else {
            printf("%sCouldn't find block model for '%s' so we'll just use the default block for that one.%s\n", YELLOW, block_minecraft_name, RESET);

        }
        cJSON_free(json);

        // render each orientation


        // GET TEXTURE

        // GENERATE IMAGE
    }


    return block;
}


int main(int argc, char **argv) {

    // PARSE ARGS
    char *jar_path = "default_assets/1.21.8.jar";
    char *out_dir = NULL;
    char *angle = NULL;
    char *path = NULL;

    ArgOption options[] = {
        {
            "jar",    
            'j', 
            ARG_STRING, 
            "Path Minecraft client jar for the version of your world. Can be found under '.minecraft/versions/1.21.8/1.21.8.jar'"
            " or something similar. If nothing is provided then default assets will be used instead.", 
            &jar_path
        },
        {
            "out",    
            'o', 
            ARG_STRING, 
            "Output directory of the mapper. By default outputs to a directory 'OUT' in the current directory", 
            &out_dir
        },
        {
            "angle",    
            'a', 
            ARG_STRING, 
            "Viewing angle of the map. Options are 'NE', 'SE', 'SW', 'NW'.", 
            &angle
        },
        {
            "world",    
            'w', 
            ARG_STRING, 
            "The path to your minecraft world save folder. Minecraft worlds are saved in '.minecraft/saves/' as of 1.21.8", 
            &path
        },
        // XXX: maybe add something to specify mca file directory, and other key directories for future minecraft version changes

        // {"verbose", 'v', ARG_BOOL,   "Enable verbose output", &verbose},
        // {"count",   'n', ARG_INT,    "Number of iterations", &count},
    };
    parse_args(argc, argv, options, len(options));


    // extract minecraft jar and set paths to model files
    extract_jar(jar_path, "dump/jar");
    find_block_models();


    // init rendered block map
    Map* block_tag_to_rendered_blocks = new_map();
    get_rendered_block("minecraft:block/birch_stairs", block_tag_to_rendered_blocks);

    // init drawn blocks map
    Map* xyz_to_drawn_blocks = new_map();
    
    


    // COLLECT MCA FILES
    int n;
    char* region_folder = cat(path, "/region");
    char **files = collect_files(region_folder, &n);
    

    // SORT MCA FILES TO RENDER CLOSER TO VIEWER FIRST
    qsort(files, n, sizeof(char*), compare_mca_paths);
    
    for (int i = 0; i < n; i++) {
        printf("  %s\n", files[i]);
        free(files[i]);
    }
    free(files);
    free(region_folder);

    /*
        RENDER MCAS
    */

    // START AT CLOSEST TO VIEWPOINT, AND ONLY RENDER BLOCKS THAT WON'T BE COVERED UP
    /*
        add more image files as needed
        - images should be 
    */



    



    return 0;
}
