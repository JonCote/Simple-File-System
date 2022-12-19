Compiling:
    - Navigate to file location in the terminal
    - Run the Makefile using command 'make' in terminal

diskinfo:
    - Functionality: list out a FAT12 disk image's following meta data
        - OS Name:
        - Label of the disk:
        - Free size of the disk:
        - Number of files:
        - Number of FAT copies:
        - Sectors per FAT:

    - Run command: ./diskinfo {image file}

disklist:
    - Functionality: list out all directories and files in a human readable format
    - Run command: ./disklist {image file}

diskget:
    - Functionality: copy a file from the root directory of a image to your current local directory
    - Run command: ./diskget {image file} {file name}

diskput:
    - Functionality: copy a file from your current local directory to a directory on the image
    - Run command: ./diskinfo {image file} {image path}/{file name}


