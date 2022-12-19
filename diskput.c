/*
*   Author: Jonathan Cote V00962634
*   Title: A3 CSC 360
*   Purpose: Copy file from local linux directory to FAT12 image
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
#include <linux/limits.h>
#include <time.h>

struct diskInfo{
    uint8_t num_of_fats;
    uint8_t sectors_per_cluster;
    uint16_t sector_per_fat;
    uint16_t reserved_sectors;
    uint16_t root_dir_entries;
    uint16_t bytes_per_sector;
    uint16_t sector_count;
    int used_space;
    int total_space;
    int free_space;
}diskInfo;

struct fileInfo{
    char *file_name;
    char *file_dir;
    int flc;
    int size;
    int date;
    int time;
}fileInfo;


struct subDir{
    char *path;
    int flc;
};

struct currDir{
    char *dir_name;
}currDir;

struct subDir *sub_dir_list = NULL;
int sub_dir_count = 0;
int insert_dir = -1;
FILE* fptr;
char *buffer;

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
void subdir_traversal_controller(char *p);
void read_file_info(char *p, uint16_t sector, uint16_t entry);
void get_disk_info(char *p);
unsigned int get_fat_entry(char *p, uint16_t flc);
uint16_t calc_data_loc(char *p, uint16_t flc);
void build_dir_path(char *dir_name);
void split_input_name(char *input);
void convert_to_upper(char *str);
void get_string(char *start, int byte_len, char *string_out);
void put_file(char* p);
int find_open_fat(char *p, int flc);
int find_open_dir(char *p, int start, int end, int sub_dir);
void insert_file_info(char *p, int offset);
void split_name_ext(char *name_ext, char *name, char *ext);
void get_file_mod_time();
int insert_file_data(char *p, int data_loc, int data_len, int data_inserted);
void set_next_fat_entry(char *p, uint16_t flc, uint16_t next_flc);
void read_file();


// Code referenced from mmap_test.c provided in tutorials
int main(int argc, char *argv[]){
	int fd;
	struct stat sb;

    // open file and get file stats
    if(argc != 3){
        printf("Input format: ./diskput {image file} {file path}\n");
    }else{
        fd = open(argv[1], O_RDWR); // add error msg for if file does not exist
        fstat(fd, &sb);

        char *temp = malloc(sizeof(char)*strlen(argv[2]));
        strcpy(temp, argv[2]);

        split_input_name(temp);

        free(temp);

        fptr = fopen(fileInfo.file_name, "r");
        if(fptr == NULL){
            printf("file not found\n");
            exit(1);
        }

        get_file_mod_time();

        convert_to_upper(fileInfo.file_name);
        convert_to_upper(fileInfo.file_dir);

        // save file size
        fseek(fptr, 0L, SEEK_END);
        fileInfo.size = ftell(fptr);
        rewind(fptr);

        // get file mod time

        // Make pointer to start of image
        char *p = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            printf("Error: failed to map memory\n");
            exit(1);
        }

        get_disk_info(p);

        if(insert_dir == -1 && strcmp(fileInfo.file_dir, "./") != 0){
            printf("Directory not found\n");
            exit(1);
        }
        
        // check if enough space on disc for the file else exit with error
        diskInfo.free_space = diskInfo.total_space - diskInfo.used_space;
        if(diskInfo.free_space < fileInfo.size){
            printf("Insufficient space on disk\n");
            exit(1);
        }

        read_file();


        put_file(p);

        // save changes to image and close
        //munmap(p, sb.st_size);
        fclose(fptr);
        close(fd);
    }
	
	return 0;
}

/*
* Function: put_file(char *p)
* =================================
* Purpose: control the file data insertion into FAT12 
*
* Input: 
*   char* p: image data pointer
*
*/
void put_file(char* p){
   // printf("Dir for file insertion: %s\n", fileInfo.file_dir);
   // printf("%s\n", sub_dir_list[insert_dir].path);
    int curr_flc = 2;
    int dir_entry;
    int data_loc;
    int data_len;
    int data_inserted;
    int next_flc;
    // 1 find open FAT entry, set flc for file as that FAT entry
    curr_flc = find_open_fat(p, curr_flc);

    // 2 put file info in the directory
    fileInfo.flc = curr_flc;

    if(insert_dir == -1){
        uint16_t root_dir_start = (diskInfo.num_of_fats * diskInfo.sector_per_fat) + diskInfo.reserved_sectors;
        uint16_t root_dir_ends = root_dir_start + (diskInfo.root_dir_entries / 16);
        dir_entry = find_open_dir(p, root_dir_start, root_dir_ends, 0);
    }else{
        uint16_t dir_loc = calc_data_loc(p, sub_dir_list[insert_dir].flc);
        uint16_t cluster_ends = dir_loc + diskInfo.sectors_per_cluster;
        dir_entry = find_open_dir(p, dir_loc, cluster_ends, 1);
    }

    insert_file_info(p, dir_entry);

    while(data_inserted < fileInfo.size){
        if(data_inserted + 512 <= fileInfo.size){
            data_len = 512;
        }else{
            data_len = 512 - fileInfo.size;
        }
        // 3 put data in data_section at that flc
        data_loc = calc_data_loc(p, curr_flc);
        data_inserted = data_inserted + insert_file_data(p, data_loc*512, data_len, data_inserted);

        // 4 find next entry/flc in FAT
        next_flc = find_open_fat(p, curr_flc+1);
        set_next_fat_entry(p, curr_flc, next_flc);
        curr_flc = next_flc;
    }
    // 5 repeat step 3 -> 4 tell file data is populated
}


/*
* Function: find_out_fat(char *p, int flc)
* =================================
* Purpose: find and open FAT entry
*
* Input: 
*   char* p: image data pointer
*   int flc: first logical cluster to start looking from
*
* Return:
*   int: location of first open entry in FAT
*
*/
int find_open_fat(char *p, int flc){
    int entry = get_fat_entry(p, flc);

    if(entry == 0){
        return flc;
    }else{
        entry = find_open_fat(p, flc + 1);
    }
}


/*
* Function: set_next_fat_entry(char *p, uint16_t flc, uint16_t next_flc)
* =================================
* Purpose: set the entry value at a FAT location
*
* Input: 
*   char* p: image data pointer
*   uint16_t flc: location of entry
*   uint16_t next_flc: entry to be put into FAT
*
*/
void set_next_fat_entry(char *p, uint16_t flc, uint16_t next_flc){
    uint32_t ent_offset = (flc * 3) / 2;
    uint8_t first, second;
    memcpy(&first, (p + 512 + ent_offset), 1);
    memcpy(&second, (p + 512 + ent_offset + 1), 1);

    if(flc % 2 == 1){
        first = (uint8_t)(0xff & next_flc);
        second = (uint8_t)((0xf0 & second) | (0x0f & (next_flc >> 8)));
        
    }else{
        first = (uint8_t)((0x0f & first) | ((0x0f & next_flc) << 4));
        second = (uint8_t)(0xff & (next_flc >> 4));
    }
    memcpy((p + 512 + ent_offset), &first, 1);
    memcpy((p + 512 + ent_offset + 1), &second, 1);
}


/*
* Function: insert_file_data(char *p, int data_loc, int data_len, int data_inserted)
* =================================
* Purpose: insert file data into data sector
*
* Input: 
*   char* p: image data pointer
*   int data_loc: start location for data entry
*   int data_len: the amount of data to be entered
*   int data_inserted: current amount of data already inserted
*
* Return:
*   int: the new amount of data inserted this run
*
*/
int insert_file_data(char *p, int data_loc, int data_len, int data_inserted){
    memcpy((p + data_loc), buffer + data_inserted, data_len);

    return data_len;
}


/*
* Function: read_file()
* =================================
* Purpose: read the file data into a buffer
*
*/
void read_file(){
    buffer = realloc(buffer, fileInfo.size);
    fread(buffer, 1, fileInfo.size, fptr);
}


/*
* Function: insert_file_info(char *p, int offset)
* =================================
* Purpose: insert file meta data into directory entry
*
* Input: 
*   char* p: image data pointer
*   int offset: start location for directory entry
*
*/
void insert_file_info(char *p, int offset){
    char buff[11];
    char file_name[8];
    char file_ext[3];
    uint8_t attributes = 0x00;
    uint16_t date = fileInfo.date;
    uint16_t time = fileInfo.time;
    uint8_t flc = fileInfo.flc;
    uint32_t size = fileInfo.size;

    strcpy(buff, fileInfo.file_name);

    char *token;
    token = strtok(buff, ".");
    strcpy(file_name, token);
    memcpy(p + offset, file_name, 8);
    
    token = strtok(NULL, ".");
    strcpy(file_ext, token);
    memcpy(p + offset + 8, file_ext, 3);

    memcpy(p + offset + 26, &flc, 2);
    memcpy(p + offset + 28, &size, 4);

    // (TODO) figure out date-time
    memcpy(p + offset + 16, &date, 2);
    memcpy(p + offset + 24, &date, 2);
    memcpy(p + offset + 22, &time, 2);
    memcpy(p + offset + 14, &time, 2);



}


/*
* Function: get_file_mod_time()
* =================================
* Purpose: get last mod time-date of file to be inserted, then calculate the value for FAT12 insert
*
*/
void get_file_mod_time(){
    struct stat attr;
    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)) == NULL){
        printf("getcwd() error");
        exit(1);
    }
    char path[PATH_MAX+strlen(fileInfo.file_name)];
    strcpy(path, cwd);
    strcat(path, "/");
    strcat(path, fileInfo.file_name);
    
    stat(path, &attr);
    char weekday[3];
    char month_name[3];
    int month;
    int day;
    int year;
    int hour;
    int min;
    int sec;

    sscanf(ctime(&attr.st_mtime), "%s %s %d %d:%d:%d %d", weekday, month_name, &day, &hour, &min, &sec, &year);
   // printf("time: %d:%d:%d\n", hour, min, sec);
    switch(month_name[0]){
        case 'J':
            switch(month_name[1]){
                case 'a':
                    month = 1;
                    break;
                case 'u':
                    switch(month_name[2]){
                        case 'n':
                            month = 6;
                            break;
                        case 'l':
                            month = 7;
                            break;
                    }
                    break;
            }
            break;
        case 'M':
            switch(month_name[2]){
                case 'r':
                    month = 3;
                    break;
                case 'y':
                    month = 5;
                    break;
            }
            break;
        case 'F':
            month = 2;
            break;
        case 'A':
            switch(month_name[1]){
                case 'p':
                    month = 4;
                    break;
                case 'u':
                    month = 8;
                    break;
            }
            break;
        case 'S':
            month = 9;
            break;
        case 'O':
            month = 10;
            break;
        case 'N':
            month = 11;
            break;
        case 'D':
            month = 12;
            break;
    }

    year = year-1980;
    year <<= 9;
    month <<= 5;

    fileInfo.date = year+month+day;

    hour <<= 11;
    min <<=  5;

    fileInfo.time = hour+min;
}


/*
* Function: find_open_dir(char *p, int start, int end, int sub_dir)
* =================================
* Purpose: find the first open directory entry
*
* Input: 
*   char* p: image data pointer
*   int start: start location for directory
*   int end: end location for directory
*   int sub_dir: flag to control slight difference in looping for subdir
*
*/
int find_open_dir(char *p, int start, int end, int sub_dir){
    uint8_t entry_free = 0x01; // Just a inital value so while loop enters
    int i = start;

    while(i < end && entry_free != 0x00){
        for(int k = 0; k < 16; k++){
            if(sub_dir != 1 || k > 1){
                memcpy(&entry_free , (p + 512*i + 32*k), 1);
                if(entry_free == 0x00){
                    return 512*i + 32*k;
                }
            }
        }
        i++;
    }
    return -1;
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
    memcpy(&diskInfo.sector_count, (p + 19), 2);

    diskInfo.total_space = diskInfo.bytes_per_sector * diskInfo.sector_count;
    uint16_t root_dir_start = (diskInfo.num_of_fats * diskInfo.sector_per_fat) + diskInfo.reserved_sectors;
    uint16_t root_dir_ends = root_dir_start + (diskInfo.root_dir_entries / 16);

    currDir.dir_name = malloc(sizeof(char));
    strcpy(currDir.dir_name, "./");
    
    traverse(p, root_dir_start, root_dir_ends, 0);

    subdir_traversal_controller(p);
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
        traverse_sub_directory(p, sub_dir_list[i].flc);
    }
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

    memcpy(&file_attributes, (p + (512*sector + 32*entry) + 11), 1);

    if(file_attributes != 0x0F && !(0x04 & file_attributes)){
        memcpy(file_name, (p + (512*sector + 32*entry)), 8);
        file_name[9] = '\0';

        memcpy(&flc, (p + (512*sector + 32*entry) + 26), 2);
        memcpy(&file_size, (p + (512*sector + 32*entry) + 28), 4);
        diskInfo.used_space = diskInfo.used_space + file_size;

        if(0x10 & file_attributes){
            if(flc > 1){
                sub_dir_list = mem_alloc(sub_dir_list, sub_dir_count+1);
                build_dir_path(file_name);
                sub_dir_list[sub_dir_count].flc = flc;
                if(strcmp(sub_dir_list[sub_dir_count].path, fileInfo.file_dir) == 0){
                    insert_dir = sub_dir_count;
                }

                sub_dir_count++;
            }
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
            sub_dir_list[sub_dir_count].path = realloc(sub_dir_list[sub_dir_count].path, (sizeof(char))*size);
            size++;
        }
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
* Function: split_input_name(char *input)
* =================================
* Purpose: split user input into file name and file directory
*
* Input: 
*   char* input: user input to be split 
*
*/
void split_input_name(char *input){
    char *raw = malloc(sizeof(char)*strlen(input));
    strcpy(raw, input);
    int raw_len = strlen(raw);
    int end_of_dir = 0;
    int count_dir = 0;
    int count_name = 0;

    for(int i = raw_len - 1; i >= 0; i--){
        if(raw[i] == '/'){
            end_of_dir = i;
            break;
        }
    }

    if(end_of_dir <= 1){
        fileInfo.file_dir = realloc(fileInfo.file_dir, sizeof(char)*2);
        fileInfo.file_name = realloc(fileInfo.file_name, sizeof(char)*8);
        int ind = 0;
        int i = 0;
        if(raw[0] == '.'){
            i = 1;
        }
        for(i; i < raw_len; i++){
            if(raw[i] != '/'){
                fileInfo.file_name[ind] = raw[i];
                ind++;
            }
        }       


        strcpy(fileInfo.file_dir, "./");
    }else{
        for(int i = 0; i < raw_len; i++){
            if(i < end_of_dir){
                if(count_dir == 0){
                    if(raw[i] != '.'){
                        fileInfo.file_dir = realloc(fileInfo.file_dir, sizeof(char)*count_dir+1);
                        fileInfo.file_dir[0] = '.';
                        count_dir++;
                    }
                }
                if(count_dir == 1){
                    if(raw[i] != '/'){
                        fileInfo.file_dir = realloc(fileInfo.file_dir, sizeof(char)*count_dir+1);
                        fileInfo.file_dir[1] = '/';
                        count_dir++;
                    }
                }
                fileInfo.file_dir = realloc(fileInfo.file_dir, sizeof(char)*count_dir+1);
                fileInfo.file_dir[count_dir] = raw[i];
                count_dir++;
                
            }else if(i > end_of_dir){  
                fileInfo.file_name = realloc(fileInfo.file_name, sizeof(char)*count_name+1);
                fileInfo.file_name[count_name] = raw[i];
                count_name++;
                
            }
        }
    }
    free(raw);
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
