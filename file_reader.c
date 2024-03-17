#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "file_reader.h"
#include "tested_declarations.h"
#include "rdebug.h"
#include "tested_declarations.h"
#include "rdebug.h"

#define SECTOR_SIZE 512

struct disk_t* disk_open_from_file(const char* volume_file_name){
    if(volume_file_name == NULL){
        errno = EFAULT;
        return NULL;
    }

    FILE *disk_from_file = fopen(volume_file_name, "rb");
    if(disk_from_file == NULL){
        errno = ENONET;
        return NULL;
    }

    struct disk_t *disk = malloc(sizeof(struct disk_t));
    if(disk == NULL){
        errno = ENOMEM;
        fclose(disk_from_file);
        return NULL;
    }

    disk->disk = disk_from_file;
    disk->number_of_sector = 0;

    uint8_t buffer[SECTOR_SIZE];
    while (fread(buffer, SECTOR_SIZE, 1, disk->disk)){
        disk->number_of_sector++;
    }

    return disk;
}

int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read){
    if(pdisk == NULL || first_sector < 0 || buffer == NULL || sectors_to_read < 0){
        errno = EFAULT;
        return -1;
    }

    if((uint32_t)(first_sector + sectors_to_read) > pdisk->number_of_sector){
        errno = ERANGE;
        return -1;
    }

    fseek(pdisk->disk, SECTOR_SIZE * first_sector, SEEK_SET);
    fread(buffer, SECTOR_SIZE, sectors_to_read, pdisk->disk);

    return sectors_to_read;
}

int disk_close(struct disk_t* pdisk){
    if(pdisk == NULL){
        errno = EFAULT;
        return -1;
    }
    fclose(pdisk->disk);
    free(pdisk);
    return 0;
}



struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector){
    if(pdisk == NULL){
        errno = EFAULT;
        return NULL;
    }

    struct volume_t *volume = malloc(sizeof(struct volume_t));
    if(volume == NULL){
        errno = ENOMEM;
        return NULL;
    }

    if(disk_read(pdisk, (int)first_sector, &volume->Boot, 1) == -1){
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    if(volume->Boot.signature != 0xaa55){
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    if(volume->Boot.boot_signature != 0x29 && volume->Boot.boot_signature != 0x28){
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    if(volume->Boot.number_of_fats != 2){
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    if(volume->Boot.bytes_per_sector != SECTOR_SIZE){
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    volume->FAT1 = malloc(volume->Boot.bytes_per_sector * volume->Boot.size_of_fat);
    if(volume->FAT1 == NULL){
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    volume->FAT2 = malloc(volume->Boot.bytes_per_sector * volume->Boot.size_of_fat);
    if(volume->FAT2 == NULL){
        free(volume->FAT1);
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    volume->Directory_data = malloc(volume->Boot.maximum_number_of_files * sizeof(struct SFN));
    if(volume->Directory_data == NULL){
        free(volume->FAT1);
        free(volume->FAT2);
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    disk_read(pdisk, volume->Boot.size_of_reserved_area, volume->FAT1, volume->Boot.size_of_fat);
    disk_read(pdisk, volume->Boot.size_of_reserved_area + volume->Boot.size_of_fat, volume->FAT2, volume->Boot.size_of_fat);
    disk_read(pdisk, volume->Boot.size_of_reserved_area + 2*volume->Boot.size_of_fat, volume->Directory_data, (int) sizeof(struct SFN) * volume->Boot.maximum_number_of_files/SECTOR_SIZE);

    if(memcmp(volume->FAT1, volume->FAT2, SECTOR_SIZE * volume->Boot.size_of_fat) != 0){
        free(volume->FAT1);
        free(volume->FAT2);
        free(volume->Directory_data);
        free(volume);
        errno = EINVAL;
        return NULL;
    }

    volume->disk = pdisk;

    return volume;
}

int fat_close(struct volume_t* pvolume){
    if(pvolume == NULL){
        errno = EFAULT;
        return -1;
    }
    free(pvolume->FAT1);
    free(pvolume->FAT2);
    free(pvolume->Directory_data);
    free(pvolume);
    return 0;
}


struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster) {
    if(buffer == NULL || size == 0 || first_cluster == 0){
        return NULL;
    }


    struct clusters_chain_t *cluster_chain = (struct clusters_chain_t *)malloc(sizeof(struct clusters_chain_t));
    if (cluster_chain == NULL) {
        return NULL;
    }

    // checking size of cluster chain
    size_t chain_size = 0;
    uint16_t current_cluster = first_cluster;
    while (current_cluster < 0xFF8){
        chain_size++;
        // next cluster from FAT array
        size_t offset = current_cluster + (current_cluster / 2);
        uint16_t entry;

        entry = *((uint16_t *)((uint8_t *)buffer + offset));

        if (current_cluster % 2 == 0) {
            entry &= 0x0FFF;
        } else {
            entry >>= 4;
        }

        current_cluster = entry;
    }

    if(chain_size == 0){
        free(cluster_chain);
        return NULL;
    }

    // allocating memory for cluster chain
    cluster_chain->clusters = (uint16_t *)malloc(size * sizeof(uint16_t));
    if(cluster_chain->clusters == NULL){
        free(cluster_chain);
        return NULL;
    }

    // fill the cluster chain
    cluster_chain->size = chain_size;
    current_cluster = first_cluster;
    for(size_t i=0; i<chain_size; i++){
        cluster_chain->clusters[i] = current_cluster;
        // next cluster from FAT array
        size_t offset = current_cluster + (current_cluster / 2);
        uint16_t entry;

        entry = *((uint16_t *)((uint8_t *)buffer + offset));

        if (current_cluster % 2 == 0) {
            entry &= 0x0FFF;
        } else {
            entry >>= 4;
        }

        current_cluster = entry;
    }

    return cluster_chain;
}


struct file_t* file_open(struct volume_t* pvolume, const char* file_name){
    if(pvolume == NULL || file_name == NULL){
        errno = EFAULT;
        return NULL;
    }

    struct file_t *result_file = malloc(sizeof(struct file_t));
    if(result_file == NULL){
        errno = ENOMEM;
        return NULL;
    }

    int dot = 0;
    char formatted_filename[11] = "           ";
    int j=0;
    for(int i=0; i<8; i++, j++){
        if(*(file_name + j) == '\0' || *(file_name + j) == '.'){
            if(*(file_name + j) == '.'){
                j++;
                dot = 1;
            }
            break;
        }
        *(formatted_filename + i) = *(file_name + j);
    }
    if(dot == 0){
        j++;
    }
    for(int i=8; i<11; i++){
        if(*(file_name + j) == '\0'){
            break;
        }
        *(formatted_filename + i) = *(file_name + j);
        j++;
    }

    struct SFN *SFN = pvolume->Directory_data;

    int file_found = 0;
    for(int i=0; i<pvolume->Boot.maximum_number_of_files; i++){
        if(strncmp(SFN->filename, formatted_filename, 11) == 0){
            if(SFN->file_attributes == 0x10 || SFN->file_size == 0 || SFN->file_attributes == 0x08){
                errno = EISDIR;
                free(result_file);
                return NULL;
            }
            file_found = 1;
            break;
        }
        SFN++;
    }

    if(file_found == 0){
        errno = ENONET;
        free(result_file);
        return NULL;
    }

    result_file->info = *SFN;
    result_file->volume = pvolume;
    result_file->position = 0;

    result_file->cluster_chain = get_chain_fat12(pvolume->FAT1, pvolume->Boot.size_of_fat * SECTOR_SIZE, result_file->info.low_order_address_of_first_cluster);
    if(result_file->cluster_chain == NULL){
        errno = ENONET;
        free(result_file);
        return NULL;
    }
    return result_file;
}

int file_close(struct file_t* stream){
    if(stream == NULL){
        errno = EFAULT;
        return -1;
    }
    free(stream->cluster_chain->clusters);
    free(stream->cluster_chain);
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL || stream == NULL || size <= 0 || nmemb <= 0) {
        errno = EFAULT;
        return -1;
    }

    int bytes_per_cluster = stream->volume->Boot.sectors_per_clusters * SECTOR_SIZE;
    size_t current_position = 0;
    size_t bytes_left_to_read = nmemb * size;
    size_t cluster_position = stream->position / bytes_per_cluster;
    size_t bytes_left_to_ignore = stream->position % bytes_per_cluster;



    int32_t first_byte_1 = (int32_t)(((stream->volume->Boot.size_of_reserved_area + 2 * stream->volume->Boot.size_of_fat) * SECTOR_SIZE) +
                                   + ((stream->cluster_chain->clusters[cluster_position] - 2) * bytes_per_cluster) +
                                   + (stream->volume->Boot.maximum_number_of_files * sizeof(struct SFN)) + bytes_left_to_ignore);

    int32_t bytes_to_read_1 = bytes_per_cluster - (int32_t)bytes_left_to_ignore;

    bytes_to_read_1 = (int32_t)(stream->info.file_size - stream->position) > bytes_to_read_1 ? bytes_to_read_1 : (int32_t)(stream->info.file_size - stream->position);
    bytes_to_read_1 = (int32_t)bytes_left_to_read > bytes_to_read_1 ? bytes_to_read_1 : (int32_t)bytes_left_to_read;

    fseek(stream->volume->disk->disk, first_byte_1, SEEK_SET);
    fread((uint8_t *)ptr + current_position, bytes_to_read_1, sizeof(uint8_t), stream->volume->disk->disk);

    current_position += bytes_to_read_1;
    stream->position += bytes_to_read_1;
    bytes_left_to_read -= (size_t)bytes_to_read_1 > bytes_left_to_read ? bytes_left_to_read : (size_t)bytes_to_read_1;


    while (bytes_left_to_read > 0 && stream->info.file_size > stream->position && cluster_position < stream->cluster_chain->size){
        cluster_position++;

        int32_t first_byte = (int32_t)(((stream->volume->Boot.size_of_reserved_area + 2 * stream->volume->Boot.size_of_fat) * SECTOR_SIZE) +
                           + ((stream->cluster_chain->clusters[cluster_position] - 2) * bytes_per_cluster) +
                           + (stream->volume->Boot.maximum_number_of_files * sizeof(struct SFN)));

        int32_t bytes_to_read;

        bytes_to_read = (int32_t)(stream->info.file_size - stream->position) > bytes_per_cluster ? bytes_per_cluster : (int32_t)(stream->info.file_size - stream->position);
        bytes_to_read = (int32_t)bytes_left_to_read > bytes_to_read ? bytes_to_read : (int32_t)bytes_left_to_read;

        fseek(stream->volume->disk->disk, first_byte, SEEK_SET);
        fread((uint8_t *)ptr + current_position, bytes_to_read, sizeof(uint8_t), stream->volume->disk->disk);

        current_position += bytes_to_read;
        stream->position += bytes_to_read;
        bytes_left_to_read -= (size_t)bytes_to_read > bytes_left_to_read ? bytes_left_to_read : (size_t)bytes_to_read;
    }

    return current_position / size;
}



int32_t file_seek(struct file_t* stream, int32_t offset, int whence){
    if(stream == NULL){
        errno = EFAULT;
        return -1;
    }

    if(whence < 0){
        errno = ENXIO;
        return -1;
    }

    uint16_t new_position = stream->position;

    if(whence == SEEK_SET){
        new_position = offset;
    }
    if(whence == SEEK_END){
        new_position = stream->info.file_size + offset;
    }
    if(whence == SEEK_CUR){
        new_position += offset;
    }

    if(new_position > stream->info.file_size){
        errno = ENXIO;
        return -1;
    }

    stream->position = new_position;
    return new_position;
}




struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path){
    if(pvolume == NULL || dir_path == NULL){
        errno = EFAULT;
        return NULL;
    }

    if(strcmp("\\",dir_path) != 0){
        errno=ENOENT;
        return NULL;
    }

    struct dir_t *directory = malloc(sizeof(struct dir_t));
    if(directory == NULL){
        errno = ENOMEM;
        return NULL;
    }

    directory->volume = pvolume;
    directory->Directory_data = pvolume->Directory_data;
    directory->directory_size = pvolume->Boot.maximum_number_of_files;
    directory->directory_position = 0;
    directory->file_is_open = 0;

    return directory;
}

int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry) {
    if (pdir == NULL || pentry == NULL) {
        errno = EFAULT;
        return -1;
    }

    struct SFN *current_entry = (struct SFN *)pdir->Directory_data + pdir->directory_position;

    // Loop until a valid directory entry is found
    // There are no more files/directories in this directory 0x00
    // The entry is unused 0xE5
    while (current_entry->filename[0] == (char )0xE5 || current_entry->filename[0] == 0x00) {
        pdir->directory_position++;

        if (pdir->directory_position >= pdir->directory_size) {
            return 1; // No more entries
        }

        current_entry = (struct SFN *)pdir->Directory_data + pdir->directory_position;
    }

    // Copy name
    int i;
    for(i=0; i<8 ; i++){
        if(!isalpha(*(current_entry->filename + i)) || *(current_entry->filename + i) == '\0'){
            break;
        }
        *(pentry->name + i) = (char)(*(current_entry->filename + i));
    }

    int dot = 0;
    for(int j=8; j<11; j++, i++){
        if(!isalpha(*(current_entry->filename + j)) || *(current_entry->filename + j) == '\0'){
            break;
        }
        if(dot == 0){
            *(pentry->name + i) = '.';
            i++;
            dot = 1;
        }
        *(pentry->name + i) = (*(current_entry->filename + j));
    }
    *(pentry->name + i) = '\0';


    pentry->is_archived = (current_entry->file_attributes & 0x20) >> 5;
    pentry->is_readonly = (current_entry->file_attributes & 0x01);
    pentry->is_hidden = (current_entry->file_attributes & 0x02) >> 1;
    pentry->is_system = (current_entry->file_attributes & 0x04) >> 2;
    pentry->is_directory = (current_entry->file_attributes & 0x10) >> 4;

    uint16_t creation_time = (current_entry->file_attributes + 14);
    uint16_t creation_date = (current_entry->file_attributes + 16);

    parse_time(creation_time, &pentry->creation_time);
    parse_date(creation_date, &pentry->creation_date);

    // Increment the directory position for the next read
    pdir->directory_position++;

    return 0;
}



int dir_close(struct dir_t* pdir){
    if(pdir == NULL){
        return -1;
    }
    free(pdir);
    return 0;
}



void parse_date(uint16_t date, struct date_t *parsed_date) {
    parsed_date->day = date & 0x1F;
    parsed_date->month = (date >> 5) & 0x0F;
    parsed_date->year = ((date >> 9) & 0x7F) + 1980;
}

void parse_time(uint16_t time, struct time_t *parsed_time) {
    parsed_time->minutes = (time >> 5) & 0x3F;
    parsed_time->hours = (time >> 11) & 0x1F;
}



