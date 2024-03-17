#ifndef PROJECT1_FILE_READER_H
#define PROJECT1_FILE_READER_H

#include <stdint.h>
#include "FAT_structures.h"

struct disk_t{
    FILE *disk;
    uint32_t number_of_sector;
};

struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);



struct volume_t{
    struct disk_t *disk;
    struct Boot_FAT Boot;
    void *FAT1;
    void *FAT2;
    void *Directory_data;
};

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);



struct file_t{
    struct SFN info;
    struct volume_t *volume;
    struct clusters_chain_t *cluster_chain;
    uint16_t position;
};

struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);
struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster);


struct dir_entry_t {
    char name[13];
    size_t size;
    int is_archived;
    int is_readonly;
    int is_system;
    int is_hidden;
    int is_directory;
    struct date_t creation_date;
    struct time_t creation_time;
};

struct dir_t {
    struct volume_t *volume;
    void *Directory_data;
    size_t directory_size;
    uint16_t directory_position;
    int file_is_open;
};

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);



struct clusters_chain_t {
    uint16_t *clusters;
    size_t size;
};

void parse_date(uint16_t date, struct date_t *parsed_date);

void parse_time(uint16_t time, struct time_t *parsed_time);

#endif //PROJECT1_FILE_READER_H
