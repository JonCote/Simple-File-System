/*
*   Author: Jonathan Cote V00962634
*   Title: A3 CSC 360
*   Purpose: Display information about a MS-DOS file system.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


struct diskInfo{
    char os_name[9]; // try to switch these 2 char fields to use malloc
    char disk_label[9];
    int total_space;
    int used_space;
    int free_space;
    int file_count;
    uint8_t num_of_fats;
    uint8_t sectors_per_cluster;
    uint16_t sector_per_fat;
    uint16_t reserved_sectors;
    uint16_t root_dir_entries;
    uint16_t bytes_per_sector;
    
}diskInfo;


void traverse(char *p, uint16_t start, uint16_t ends, int sub_dir);
void traverse_sub_directory(char *p, uint16_t flc);
void read_file_info(char *p, uint16_t sector, uint16_t entry);
void get_disk_info(char *p);
unsigned int get_fat_entry(char *p, uint16_t flc);
uint16_t calc_data_loc(char *p, uint16_t flc);
void print_info();


// Code referenced from mmap_test.c provided in tutorials
int main(int argc, char *argv[]){
	int fd;
	struct stat sb;

    // open file and get file stats
	fd = open(argv[1], O_RDWR);
	fstat(fd, &sb);

    // Make pointer to start of image
	char *p = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		printf("Error: failed to map memory\n");
		exit(1);
	}

    // collection of disk info into diskInfo struct
    get_disk_info(p);
    
    // Print diskInfo
    print_info();

    // save changes to image and close
	//munmap(p, sb.st_size);
    close(fd);
	return 0;
}


/*
* Function: print_info()
* =================================
* Purpose: print the disk info
*
*/
void print_info(){
    printf("OS Name: %s\n", diskInfo.os_name);
    printf("Label of the disk: %s\n", diskInfo.disk_label); // TODO check boot sector in image2020 for label
    printf("Total size of the disk: %u\n", diskInfo.total_space);
    printf("Free size of the disk: %u\n", diskInfo.free_space);
    printf("==============\n");
    printf("The number of files: %u\n", diskInfo.file_count);
    printf("=============\n");
    printf("Number of FAT copies: %u\n", diskInfo.num_of_fats);
    printf("Sectors per FAT: %u\n", diskInfo.sector_per_fat);
}


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
    uint8_t sectors_per_cluster;
    uint16_t data_loc;
    uint16_t cluster_ends;

    memcpy(&sectors_per_cluster, (p + 13), 1);
    
    // get sector data starts
    while(flc != 0xFFF){
        data_loc = calc_data_loc(p, flc);
        cluster_ends = data_loc + sectors_per_cluster;
        // traverse data
        traverse(p, data_loc, cluster_ends, 1);

        // check FAT for next cluster
        flc = get_fat_entry(p, flc);
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
    uint8_t sectors_per_cluster;
    uint8_t data_region_start;
    uint16_t reserved_sectors;
    uint16_t root_dir_entries;
    uint16_t data_loc;

    memcpy(&reserved_sectors, (p + 14), 2);
    memcpy(&sectors_per_cluster, (p + 13), 1);
    memcpy(&root_dir_entries, (p + 17), 2);

    data_region_start = (diskInfo.num_of_fats * diskInfo.sector_per_fat) + (root_dir_entries / 16) + reserved_sectors;

    data_loc = ((flc - 2) * sectors_per_cluster) + data_region_start;

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
    char file_name[9];
    uint8_t file_attributes;
    uint16_t flc;
    uint32_t file_size;

    memcpy(&file_attributes, (p + (512*sector + 32*entry) + 11), 1);

    if(file_attributes != 0x0F){
        memcpy(file_name, (p + (512*sector + 32*entry)), 8);
        file_name[9] = '\0';

        memcpy(&file_size, (p + (512*sector + 32*entry) + 28), 4);
        diskInfo.used_space = diskInfo.used_space + file_size;

        memcpy(&flc, (p + (512*sector + 32*entry) + 26), 2);

        if(!(0x04 & file_attributes)){
            if(0x08 & file_attributes){
               strcpy(diskInfo.disk_label, file_name);

            }
            if(0x10 & file_attributes){
                // loop through this sub directory (use FAT and flc)
                traverse_sub_directory(p, flc);

            }
            if(!(0x10 & file_attributes) && !(0x08 & file_attributes)){
                diskInfo.file_count++;
            }
        }
    }

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
    uint16_t reserved_sectors;
    uint16_t max_root_dir_entries;
    uint16_t bytes_per_sector;
    uint16_t root_dir_entries;
    uint16_t sector_count;
    
    memcpy(&root_dir_entries, (p + 17), 2);
    memcpy(&diskInfo.num_of_fats, (p + 16), 1);
    memcpy(&diskInfo.sector_per_fat, (p + 22), 2);
    memcpy(diskInfo.os_name, (p + 3), 8);
    diskInfo.os_name[9] = '\0';
    memcpy(&reserved_sectors, (p + 14), 2);
    memcpy(&sector_count, (p + 19), 2);
    memcpy(&bytes_per_sector, (p + 11), 2);

    uint16_t root_dir_start = (diskInfo.num_of_fats * diskInfo.sector_per_fat) + reserved_sectors;
    uint16_t root_dir_ends = root_dir_start + (root_dir_entries / 16);
    diskInfo.total_space = bytes_per_sector * sector_count;

    traverse(p, root_dir_start, root_dir_ends, 0);
   
    diskInfo.free_space = diskInfo.total_space - diskInfo.used_space;

}
