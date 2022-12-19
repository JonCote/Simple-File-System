/*
*   Author: Jonathan Cote V00962634
*   Title: A3 CSC 360
*   Purpose: Copy file from FAT12 image to local linux directory 
*   
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct diskInfo{
    uint8_t num_of_fats;
    uint8_t sectors_per_cluster;
    uint16_t sector_per_fat;
    uint16_t reserved_sectors;
    uint16_t root_dir_entries;
    uint16_t bytes_per_sector;
}diskInfo;

struct fileInfo{
    char *file_name;
    char file_org_name[11];
    int file_size;
    int flc;
    char *data;
}fileInfo;




void traverse(char *p, uint16_t start, uint16_t ends, int sub_dir);
void read_file_info(char *p, uint16_t sector, uint16_t entry);
void get_disk_info(char *p);
unsigned int get_fat_entry(char *p, uint16_t flc);
uint16_t calc_data_loc(char *p, uint16_t flc);
void get_file_data(char *p);
void get_string(char *start, int byte_len, char *string_out);
void build_comp_name(char* name, char* ext, char* comp_name);
void write_data_to_file(char* data_start, int data_len, FILE *fptr);
void convert_to_upper(char *str);


// Code referenced from mmap_test.c provided in tutorials
int main(int argc, char *argv[]){
	int fd;
	struct stat sb;

    // open file and get file stats
    if(argc != 3){
        printf("Input format: ./diskget {image file} {file}");
    }else{
        fd = open(argv[1], O_RDWR); // add error msg for if file does not exist
        fstat(fd, &sb);

        fileInfo.file_name = malloc(sizeof(char)*strlen(argv[2]));
        strcpy(fileInfo.file_name, argv[2]);
        strcpy(fileInfo.file_org_name, fileInfo.file_name);
        
        // convert to upper case
        convert_to_upper(fileInfo.file_name);

        // Make pointer to start of image
        char *p = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            printf("Error: failed to map memory\n");
            exit(1);
        }

        get_disk_info(p);

        // save changes to image and close
        //munmap(p, sb.st_size);
        close(fd);
    }
	
	return 0;
}


/*
* Function: get_disk_info(char *p)
* =================================
* Purpose: collect all the meta data about the disk image
*
* Input: 
*   char* p: image data pointer
*
*/
void get_disk_info(char *p){
    memcpy(&diskInfo.root_dir_entries, (p + 17), 2);
    memcpy(&diskInfo.num_of_fats, (p + 16), 1);
    memcpy(&diskInfo.sector_per_fat, (p + 22), 2);
    memcpy(&diskInfo.reserved_sectors, (p + 14), 2);
    memcpy(&diskInfo.sectors_per_cluster, (p + 13), 1);
    memcpy(&diskInfo.bytes_per_sector, (p + 11), 2);

    uint16_t root_dir_start = (diskInfo.num_of_fats * diskInfo.sector_per_fat) + diskInfo.reserved_sectors;
    uint16_t root_dir_ends = root_dir_start + (diskInfo.root_dir_entries / 16);

    traverse(p, root_dir_start, root_dir_ends, 0);

    if(fileInfo.flc == 0){
        printf("File not found.\n");
        return;
    }


}


/*
* Function: traverse(char *p, uint16_t start, uint16_t ends, int sub_dir)
* =================================
* Purpose: traverse a directory
*
* Input: 
*   char* p: image data pointer
*   uint16_t start: location to start the traversal
*   uint16_t ends: location traversal ends
*   int sub_dir: flag for controlling subdir traversal differences
*
*/
void traverse(char *p, uint16_t start, uint16_t ends, int sub_dir){
    uint8_t entry_free = 0x01; // Just a inital value so while loop enters
    int i = start;

    while(i < ends && entry_free != 0x00){
        for(int k = 0; k < 16; k++){
            if(sub_dir != 1 || k > 1){
                memcpy(&entry_free , (p + 512*i + 32*k), 1);
                if(entry_free == 0x00){
                    break;
                }
                if(entry_free != 0xE5){
                    read_file_info(p, i, k);
                }
            }
        }
        i++;
    }
}


/*
* Function: get_file_data(char *p)
* =================================
* Purpose: controller for reading file data from FAT12 to local file
*
* Input: 
*   char* p: image data pointer
*
*/
void get_file_data(char *p){
    uint16_t data_loc;
    uint16_t cluster_ends;
    int last_sector_bytes = fileInfo.file_size % diskInfo.bytes_per_sector;
    int full_sector_span = (fileInfo.file_size - (last_sector_bytes)) / diskInfo.bytes_per_sector;
    int read_count = 0;
    int size_copied = 0;
    FILE *fptr = fopen(fileInfo.file_org_name, "w");
    char *test = malloc(sizeof(char)*512);

    // get sector data starts
    while((fileInfo.flc != 0xFFF) && (fileInfo.file_size != size_copied)){
        data_loc = calc_data_loc(p, fileInfo.flc);

        if(read_count == full_sector_span){
            write_data_to_file((p + data_loc*512), last_sector_bytes, fptr);
            size_copied = size_copied + last_sector_bytes;
            read_count++;
        }else{
            write_data_to_file((p + data_loc*512), diskInfo.bytes_per_sector, fptr);
            size_copied = size_copied + diskInfo.bytes_per_sector;
            read_count++;
        }

        // check FAT for next cluster
        fileInfo.flc = get_fat_entry(p, fileInfo.flc);
    }
    fclose(fptr);
}


/*
* Function: write_data_to_file(char* data_start, int data_len, FILE *fptr)
* =================================
* Purpose: write file data
*
* Input: 
*   char* data_start: start location on image of data to be written
*   int data_len: length of data to read
*   FILE *fptr: pointer to file to write to
*
*/
void write_data_to_file(char* data_start, int data_len, FILE *fptr){
    char *buffer = malloc(sizeof(char));
    for(int i = 0; i<data_len; i++){
        memcpy(buffer, (data_start + i), 1);
        putc(buffer[0], fptr);
    }
}


/*
* Function: calc_data_loc(char *p, uint16_t flc)
* =================================
* Purpose: calculate the data location for a given flc
*
* Input: 
*   char* p: image data pointer
*   uint16_t flc: first logical cluster 
*
*/
uint16_t calc_data_loc(char *p, uint16_t flc){
    uint8_t data_region_start;
    uint16_t data_loc;

    data_region_start = (diskInfo.num_of_fats * diskInfo.sector_per_fat) + (diskInfo.root_dir_entries / 16) + diskInfo.reserved_sectors;
    data_loc = ((flc - 2) * diskInfo.sectors_per_cluster) + data_region_start;
    
    return data_loc;
}


/*
* Function: get_fat_entry(char *p, uint16_t flc)
* =================================
* Purpose: get the entry value at a FAT location
*
* Input: 
*   char* p: image data pointer
*   uint16_t flc: first logical cluster 
*
*/
unsigned int get_fat_entry(char *p, uint16_t flc){
    int ent_offset = (flc * 3) / 2;
    unsigned short entry = *(unsigned short*)(p + 512 + ent_offset);
    
    if(flc % 2 == 1){
        entry >>= 4;
        
    }else{
        entry &= 0x0fff;
    }
    return entry;
}


/*
* Function: read_file_info(char *p, uint16_t sector, uint16_t entry)
* =================================
* Purpose: read the file meta data from directory entry
*
* Input: 
*   char* p: image data pointer
*   uint16_t sector: FAT12 sector
*   uint16_t entry: entry in the sector
*
*/
void read_file_info(char *p, uint16_t sector, uint16_t entry){
    uint8_t file_attributes;
    uint16_t flc;
    uint32_t file_size;
   // char *name_buffer = malloc(sizeof(char));
   // char *ext_buffer = malloc(sizeof(char));
    char *file_name = malloc(sizeof(char));
    char *file_ext = malloc(sizeof(char));
    char *comp_file_name = malloc(sizeof(char));
    //int dir = 0;

    memcpy(&file_attributes, (p + (512*sector + 32*entry) + 11), 1);
    if(file_attributes != 0x0F && !(0x04 & file_attributes)){
        get_string((p + (512*sector + 32*entry)), 8, file_name);  // this might not work..
        get_string((p + (512*sector + 32*entry) + 8), 3, file_ext); // this might not work... test more

        build_comp_name(file_name, file_ext, comp_file_name);

        if(!(0x08 & file_attributes)){
            if(strcmp(fileInfo.file_name, comp_file_name) == 0){
                memcpy(&fileInfo.flc, (p + (512*sector + 32*entry) + 26), 2);
                memcpy(&fileInfo.file_size, (p + (512*sector + 32*entry) + 28), 4);
                get_file_data(p);
            }
        }
    }
}



/*
* Function: get_string(char *start, int byte_len, char *string_out)
* =================================
* Purpose: get a string from the FAT12 data
*
* Input: 
*   char* start: start location in image of string
*   int byte_len: number of bytes to read
*   char *string_out: output string location
*
*/
void get_string(char *start, int byte_len, char *string_out){
    char *buffer = malloc(sizeof(char)*byte_len);
    int size = 1;
    for(int i = 0; i<byte_len; i++){
        memcpy(&buffer[i], (start + i), 1);
        if(isspace(buffer[i]) == 0){
            string_out[i] = buffer[i];
            string_out = realloc(string_out, sizeof(char)*size);
            size++;
        }
    }
    free(buffer);
}


/*
* Function: build_comp_name(char* name, char* ext, char* comp_name)
* =================================
* Purpose: build file name with extension attached
*
* Input: 
*   char* name: file name
*   char* ext: file extension
*   char* comp_name: output string location
*
*/
void build_comp_name(char* name, char* ext, char* comp_name){
    int len = strlen(name) + strlen(ext) + 1;
    
    comp_name = realloc(comp_name, sizeof(char)*len);
    strcpy(comp_name, name);
    strcat(comp_name, ".");
    strcat(comp_name, ext);
}


/*
* Function: convert_to_upper(char *str)
* =================================
* Purpose: convert string to uppercase
*
* Input: 
*   char* str: string to be converted
*
*/
void convert_to_upper(char *str){
    for(int i = 0; i < strlen(str); i++){
        str[i] = toupper(str[i]);
    }
}