/*
*   Author: Jonathan Cote V00962634
*   Title: A3 CSC 360
*   Purpose: Display file structure of MSDOS FAT12 Image.
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
    char file_type;
    char file_name[9];
    int file_size;
    char time[20];
    char date[5];
}fileInfo;


struct subDir{
    char *path;
    int flc;
};

struct currDir{
    char *dir_name;
    int flag;
}currDir;

struct subDir *sub_dir_list = NULL;
int sub_dir_count = 0;

struct subDir* mem_alloc(struct subDir *dir, int size){
    struct subDir *temp = NULL;
    if(dir == NULL){
        temp = (struct subDir *) malloc(sizeof(struct subDir)*size);
        return temp;
    }
    else{
        temp = (struct subDir *)realloc(dir, sizeof(struct subDir)*size);
        
        if(temp != NULL){
            return temp;
        }
        else{
            printf("realloc failed");
            exit(1);
        }
    }
}


void traverse(char *p, uint16_t start, uint16_t ends, int sub_dir);
void traverse_sub_directory(char *p, uint16_t flc);
void read_file_info(char *p, uint16_t sector, uint16_t entry);
void get_disk_info(char *p);
unsigned int get_fat_entry(char *p, uint16_t flc);
uint16_t calc_data_loc(char *p, uint16_t flc);
void subdir_traversal_controller(char *p);
void print_info();
void build_dir_path(char *dir_name);
void date_time(char *dir_start);


// Code referenced from mmap_test.c provided in tutorials
int main(int argc, char *argv[]){
	int fd;
	struct stat sb;

    // open file and get file stats
    if(argc != 2){
        printf("Input format: ./diskinfo {image file}\n");
    }else{
        fd = open(argv[1], O_RDWR); // add error msg for if file does not exist
        fstat(fd, &sb);

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

    currDir.dir_name = malloc(sizeof(char));
    strcpy(currDir.dir_name, "./");
    currDir.flag = 0;
    traverse(p, root_dir_start, root_dir_ends, 0);

   
    subdir_traversal_controller(p);
}


/*
* Function: print_info()
* =================================
* Purpose: print the image file info
*
*/
void print_info(){
    if(currDir.flag == 0){ // this can be moved elsewhere so we only need 1 header print (improve modularity)
        currDir.flag = 1;
        printf("\n%s\n", currDir.dir_name);
        printf("====================================================\n");
    }
    printf("%c %10u %20s %s %s\n", fileInfo.file_type, fileInfo.file_size, fileInfo.file_name, fileInfo.date, fileInfo.time);
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
                    if((k < 3 && sub_dir == 1) || k == 0){
                        printf("\n%s\n", currDir.dir_name);
                        printf("====================================================\n");
                    }
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
* Function: traverse_sub_directory(char *p, uint16_t flc)
* =================================
* Purpose: set needed data for sub dir traversal
*
* Input: 
*   char* p: image data pointer
*   uint16_t flc: first logical cluster of directory start
*
*/
void traverse_sub_directory(char *p, uint16_t flc){
    uint16_t data_loc;
    uint16_t cluster_ends;
    
    // get sector data starts
    while(flc != 0xFFF){
        data_loc = calc_data_loc(p, flc);
        cluster_ends = data_loc + diskInfo.sectors_per_cluster;

        traverse(p, data_loc, cluster_ends, 1);

        // check FAT for next cluster
        flc = get_fat_entry(p, flc);
    }
}


/*
* Function: subdir_traversal_controller(char *p)
* =================================
* Purpose: controller to control subdir selection for traversal
*
* Input: 
*   char* p: image data pointer
*
*/
void subdir_traversal_controller(char *p){
    // travel the sub directories
    for(int i = 0; i < sub_dir_count; i++){
        currDir.dir_name = realloc(currDir.dir_name, (sizeof(char)*strlen(sub_dir_list[i].path)));
        strcpy(currDir.dir_name, sub_dir_list[i].path);
        currDir.flag = 0;
        traverse_sub_directory(p, sub_dir_list[i].flc);
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
    char file_name[9];
    //int dir = 0;

    memcpy(&file_attributes, (p + (512*sector + 32*entry) + 11), 1);

    if(file_attributes != 0x0F && !(0x04 & file_attributes)){
        memcpy(file_name, (p + (512*sector + 32*entry)), 8);
        file_name[9] = '\0';
        strcpy(fileInfo.file_name, file_name);

        memcpy(&fileInfo.file_size, (p + (512*sector + 32*entry) + 28), 4);

        memcpy(&flc, (p + (512*sector + 32*entry) + 26), 2);

        if(0x10 & file_attributes){
            if(flc > 1){
                sub_dir_list = mem_alloc(sub_dir_list, sub_dir_count+1);
                build_dir_path(file_name);
                sub_dir_list[sub_dir_count].flc = flc;
                sub_dir_count++;

                fileInfo.file_type = 'D';
                memset(fileInfo.date, 0, sizeof(fileInfo.date));
                memset(fileInfo.time, 0, sizeof(fileInfo.time));
                print_info();
            }
        }
        else if(!(0x08 & file_attributes)){
            fileInfo.file_type = 'F';
            date_time(p + (512*sector + 32*entry));
            print_info();
        }
    }
}


/*
* Function: build_dir_path(char *dir_name)
* =================================
* Purpose: build the directory path name
*
* Input: 
*   char* dir_name: name of the directory to build a path for
*
*/
void build_dir_path(char *dir_name){
    int p_path_len = strlen(currDir.dir_name);
    int c_path_len = strlen(dir_name);
    int total_len = p_path_len + c_path_len;
    char *path = malloc(sizeof(char)*total_len);

    strcpy(path, currDir.dir_name);
    if(strcmp(currDir.dir_name, "./") != 0){
        strcat(path, "/");
    }
    strcat(path, dir_name);

    sub_dir_list[sub_dir_count].path = malloc(sizeof(char));
    int size = 1;
    for(int i = 0; i <= total_len; i++){
        if(isspace(path[i]) == 0){
            sub_dir_list[sub_dir_count].path[i] = path[i];
            sub_dir_list[sub_dir_count].path = realloc(sub_dir_list[sub_dir_count].path, (sizeof(char))*size+1);
            size++;
        }
    }
}


/*
* Function: date_time(char *dir_start), Code referenced from sample_time_date_2.c provided in tutorial
* =================================
* Purpose: extract date and time of creation for a file in FAT12
*
* Input: 
*   char* dir_start: start location of the directory entry
*
*/
void date_time(char *dir_start){
    int time, date;
    int hours, minutes, day, month, year;

    time = *(unsigned short *)(dir_start + 14);
    date = *(unsigned short *)(dir_start + 16);
    
    year = ((date & 0xFE00) >> 9) + 1980;
    month = (date & 0x1E0) >> 5;
    day = (date & 0x1F);
    hours = (time & 0xF800) >> 11;
    minutes = (time & 0x7E0) >> 5;

    sprintf(fileInfo.date, "%d-%02d-%02d", year, month, day);
    sprintf(fileInfo.time, "%02d:%02d", hours, minutes);
}