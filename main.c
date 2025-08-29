#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#define NBT_IMPLEMENTATION
#include "dependencies/nbt.h"  // Make sure nbt.h is in your include path

// Simple NBT reader callback for libnbt
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


#define SECTION_SIZE 16
#define BLOCKS_PER_SECTION (SECTION_SIZE*SECTION_SIZE*SECTION_SIZE)



// DUMPERS
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

void print_region_to_file(const char *region_file_path, const char *file_name) {

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

}

void rip_textures_from_minecraft_jar(const char *jar_path, const char *out_dir) {


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
}

void rip_block_states_from_minecraft_jar(const char *jar_path, const char *out_dir) {


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
}

void rip_json_files_from_minecraft_jar(const char *jar_path, const char *json_files_jar_folder, const char *out_dir) {


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



// UTILITY METHODS

void extract_jar(const char *jar_path, const char *out_dir) {
    
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
        if (strstr(name, ".json")) {

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
}





int main() {
    
    // SORT MCA FILES TO RENDER CLOSER TO VIEWER FIRST


    /*
        RENDER MCAS
    */

    // DETERMINE ALL VISIBLE BLOCKS IN MCA

    // RENDER EACH ONE, SKIPPING IF COVERED BY ANOTHER BLOCK

    



    return 0;
}
