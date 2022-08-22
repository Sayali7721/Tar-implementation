#include "tar.h"
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdarg.h>
#include <string.h>

#include <dirent.h>
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
// only print in verbose mode
#define V_PRINT(f, fmt, ...) if (verbosity) { fprintf(f, fmt "\n", ##__VA_ARGS__); }
// generic error
#define ERROR(fmt, ...) fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return -1;
// capture errno when erroring
#define RC_ERROR(fmt, ...) const int rc = errno; ERROR(fmt, ##__VA_ARGS__); return -1;



// convert octal string to unsigned integer
unsigned int oct2uint(char * oct, unsigned int size){
    unsigned int out = 0;
    int i = 0;
    while ((i < size) && oct[i])
        out = (out << 3) | (unsigned int) (oct[i++] - '0');

    return out;
}

// force read() to complete
int read_size(int fd, char * buf, int size){
    int got = 0, rd;
    while ((got < size) && ((rd = read(fd, buf + got, size - got)) > 0)){
        got += rd;
    }
    return got;
}


int write_size(int fd, char * buf, int size){
    int wrote = 0, rc;
    while ((wrote < size) && ((rc = write(fd, buf + wrote, size - wrote)) > 0)){
        wrote += rc;
    }
    return wrote;
}

// check if a buffer is zeroed
int iszeroed(char * buf, size_t size){
    for(size_t i = 0; i < size; buf++, i++){
        if (* (char *) buf){
            return 0;
        }
    }
    return 1;
}

// recursive freeing of entries
void tar_free(struct tar_t * archive){
    while (archive){
        struct tar_t * next = archive -> next;
        free(archive);
        archive = next;
    }
}



// read a tar file
// archive should be address to null pointer
int tar_read(const int fd, struct tar_t ** archive, const char verbosity){
    if (fd < 0){                //fd is always > 0
        ERROR("Bad file descriptor");
    }
    //if archive is NOT NULL
    if (!archive || *archive){
        ERROR("Bad archive");
    }

    unsigned int offset = 0;   //distance between beginning and given object
    int count = 0;

    struct tar_t ** tar = archive;
    char update = 1;

    for(count = 0; ; count++){
        *tar = malloc(sizeof(struct tar_t));
        if (update && (read_size(fd, (*tar) -> block, 512) != 512)){
            V_PRINT(stderr, "Error: Bad read. Stopping");
            tar_free(*tar);
            *tar = NULL;
            break;
        }

        update = 1;
        // if current block is all zeros
        if (iszeroed((*tar) -> block, 512)){
            if (read_size(fd, (*tar) -> block, 512) != 512){
                V_PRINT(stderr, "Error: Bad read. Stopping");
                tar_free(*tar);
                *tar = NULL;
                break;
            }

            // check if next block is all zeros as well
            if (iszeroed((*tar) -> block, 512)){
                tar_free(*tar);
                *tar = NULL;

                // skip to end of record
                if (lseek(fd, RECORDSIZE - (offset % RECORDSIZE), SEEK_CUR) == (off_t) (-1)){
                    RC_ERROR("Unable to seek file: %s", strerror(rc));
                }

                break;
            }

            update = 0;
        }

        // set current entry's file offset
        (*tar) -> begin = offset;

        // skip over data and unfilled block
        unsigned int jump = oct2uint((*tar) -> size, 11);
        if (jump % 512){
            jump += 512 - (jump % 512);
        }

        // move file descriptor

        offset += 512 + jump;
        if (lseek(fd, jump, SEEK_CUR) == (off_t) (-1)){
            RC_ERROR("Unable to seek file: %s", strerror(rc));
        }

        // ready next value

        tar = &((*tar) -> next);
    }

    return count;
}





struct tar_t * exists(struct tar_t * archive, const char * filename, const char ori){
    while (archive){
        if (ori){
            if (!strncmp(archive -> original_name, filename, MAX(strlen(archive -> original_name), strlen(filename)) + 1)){
                return archive;
            }
        }
        else{
            if (!strncmp(archive -> name, filename, MAX(strlen(archive -> name), strlen(filename)) + 1)){
                return archive;
            }
        }
        archive = archive -> next;
    }
    return NULL;
}

int print_tar_metadata(FILE * f, struct tar_t * archive){
    while (archive){
        print_entry_metadata(f, archive);
        archive = archive -> next;
    }

    return 0;
}

int print_entry_metadata(FILE * f, struct tar_t * entry){
    if (!entry){
        return -1;
    }

    time_t mtime = oct2uint(entry -> mtime, 12);
    char mtime_str[32];
    strftime(mtime_str, sizeof(mtime_str), "%c", localtime(&mtime));
    printf( "File Name: %s\n", entry -> name);
    printf( "Owner UID: %s (%d)\n", entry -> uid, oct2uint(entry -> uid, 12));
    printf( "Owner GID: %s (%d)\n", entry -> gid, oct2uint(entry -> gid, 12));
    printf( "File Mode: %s (%03o)\n", entry -> mode, oct2uint(entry -> mode, 8));
    printf( "File Size: %s (%d)\n", entry -> size, oct2uint(entry -> size, 12));
    printf( "Time     : %s (%s)\n", entry -> mtime, mtime_str);
    printf( "Checksum : %s\n", entry -> check);
    printf( "File Type: ");
    switch (entry -> type){
        case REGULAR: case NORMAL:
            printf( "Normal File");
            break;
        case HARDLINK:
            printf( "Hard Link");
            break;
        case SYMLINK:
            printf( "Symbolic Link");
            break;
        case CHAR:
            printf( "Character Special");
            break;
        case BLOCK:
            printf( "Block Special");
            break;
        case DIRECTORY:
            printf( "Directory");
            break;
        case FIFO:
            printf("FIFO");
            break;
        case CONTIGUOUS:
            printf( "Contiguous File");
            break;
    }

    printf("\n\n");
    return 0;
}

// ls command
int tar_ls(FILE * f, struct tar_t * archive, int filecount, const char * files[], const char verbosity){
    if (!verbosity){
        return 0;
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }


    while (archive){
        if (ls_entry(f, archive, filecount, files, verbosity) < 0){
            return -1;

        }
        archive = archive -> next;
    }

    return 0;
}


int ls_entry(FILE * f, struct tar_t * entry, int filecount, const char * files[], const char verbosity){
    if (!verbosity){
        return 0;
    }

    if (filecount && !files){
        V_PRINT(stderr, "Error: Non-zero file count given but no files given");
        return -1;
    }

    // figure out whether or not to print
    // if no files were specified, print everything
    char print = !filecount;
    // otherwise, search for matching names
    for(int i = 0; i < filecount; i++){
        if (strncmp(entry -> name, files[i], MAX(strlen(entry -> name), strlen(files[i])))){
            print = 1;
            break;
        }
    }

    if (print){
        if (verbosity > 1){

            const mode_t mode = oct2uint(entry -> mode, 7);
            const char mode_str[26] = { "-hlcbdp-"[entry -> type?entry -> type - '0':0],
                                        mode & S_IRUSR?'r':'-',
                                        mode & S_IWUSR?'w':'-',
                                        mode & S_IXUSR?'x':'-',
//                                        mode & S_IRGRP?'r':'-',
//                                        mode & S_IWGRP?'w':'-',
//                                        mode & S_IXGRP?'x':'-',
//                                        mode & S_IROTH?'r':'-',
//                                        mode & S_IWOTH?'w':'-',
//                                        mode & S_IXOTH?'x':'-',
                                        0};
            printf("%s %s/%s ", mode_str, entry -> gid, entry -> uid);
            char size_buf[22] = {0};
            int rc = -1;
            switch (entry -> type){
                case REGULAR: case NORMAL: case CONTIGUOUS:
                    rc = sprintf(size_buf, "%u", oct2uint(entry -> size, 11));
                    break;
                case HARDLINK: case SYMLINK: case DIRECTORY: case FIFO:
                    rc = sprintf(size_buf, "%u", oct2uint(entry -> size, 11));
                    break;

            }

            if (rc < 0){
                ERROR("Failed to write length");
            }

            printf("%s", size_buf);

            time_t mtime = oct2uint(entry -> mtime, 11);
            struct tm * time = localtime(&mtime);
            printf(" %d-%02d-%02d %02d:%02d ", time -> tm_year + 1900, time -> tm_mon + 1, time -> tm_mday, time -> tm_hour, time -> tm_min);
        }

        printf( "%s", entry -> name);


        printf("\n");
    }

    return 0;
}


int tar_extract(const int fd, struct tar_t * archive, int filecount, const char * files[], const char verbosity){
    int ret = 0;

    // extract entries with given names
    if (filecount){
        if (!files){
            ERROR("Received NULL file list");
            return -1;
        }

        while (archive){
            for(size_t i = 0; i < filecount; i++){
                if (!strncmp(archive -> name, files[i], MAX(strlen(archive -> name), strlen(files[i])))){
                    //Start from particular File
                    if (lseek(fd, archive -> begin, SEEK_SET) == (off_t) (-1)){
                        RC_ERROR("Unable to seek file: %s", strerror(rc));
                    }

                    if (extract_entry(fd, archive, verbosity) < 0){
                        ret = -1;

                    }
                    break;
                }
            }
            archive = archive -> next;
        }
    }

     // extract all
    else{
        // move offset to beginning
        //Start from 0th location File
        if (lseek(fd, 0, SEEK_SET) == (off_t) (-1)){
            RC_ERROR("Unable to seek file: %s", strerror(rc));
        }

        // extract each entry
        while (archive){
            if (extract_entry(fd, archive, verbosity) < 0){
                ret = -1;
            }
            archive = archive -> next;
        }
    }

    return ret;

}

int extract_entry(const int fd, struct tar_t * entry, const char verbosity){
    V_PRINT(stdout, "%s", entry -> name);

    if ((entry -> type == REGULAR) || (entry -> type == NORMAL) || (entry -> type == CONTIGUOUS)){
        // create intermediate directories
        size_t len = strlen(entry -> name);
        if (!len)
        {
            ERROR("Attempted to extract entry with empty name");
        }

        char * path = calloc(len + 1, sizeof(char));
        strncpy(path, entry -> name, len);

        // remove file from path
        while (--len && (path[len] != '/'));
        path[len] = '\0';   // if nothing was found, path is terminated

        if (recursive_mkdir(path, DEFAULT_DIR_MODE, verbosity) < 0){
            V_PRINT(stderr, "Could not make directory %s", path);
            free(path);
            return -1;
        }
        free(path);

        if ((entry -> type == REGULAR) || (entry -> type == NORMAL) || (entry -> type == CONTIGUOUS)){
            // create file
            const unsigned int size = oct2uint(entry -> size, 11);
            int f = open(entry -> name, O_WRONLY | O_CREAT | O_TRUNC, oct2uint(entry -> mode, 7) & 0777);
            if (f < 0){
                RC_ERROR("Unable to open file %s: %s", entry -> name, strerror(rc));
            }

            // move archive pointer to data location
            if (lseek(fd, 512 + entry -> begin, SEEK_SET) == (off_t) (-1)){
                RC_ERROR("Bad index: %s", strerror(rc));
            }

            // copy data to file
            char buf[512];
            int got = 0;
            while (got < size){
                int r;
                if ((r = read_size(fd, buf, MIN(size - got, 512))) < 0){
                    RC_ERROR("Unable to read from archive: %s", strerror(rc));
                }

                if (write(f, buf, r) != r){
                    RC_ERROR("Unable to write to %s: %s", entry -> name, strerror(rc));
                }

                got += r;
            }

            close(f);
        }

    }

    return 0;
}


//States difference between archive and file system
int tar_diff(FILE * f, struct tar_t * archive, const char verbosity){
    struct stat st;
    while (archive){
        V_PRINT(stdout,"%s", archive -> name);

        // if not found, print error
        if (stat(archive -> name, &st)){
            int rc = errno;
            printf("Could not ");
            if (archive -> type == SYMLINK){
                printf("readlink");
            }
            else{
                printf("stat");
            }
            printf(" %s: %s", archive -> name, strerror(rc));
        }
        else{

            if (st.st_mtime != oct2uint(archive -> mtime, 11)){
//                struct tm dt = *(gmtime(&st.st_mtime));
                printf("%s: Modification time differs\n", archive -> name);
//                printf("Modified on : %d-%d-%d %d:%d:%d\n", dt.tm_mday,dt.tm_mon,dt.tm_year+1900,dt.tm_hour,dt.tm_min,dt.tm_sec);
            }
            if (st.st_size != oct2uint(archive -> size, 11)){
                printf("%s: size differs \n", archive -> name);
            }
            if (st.st_mode != oct2uint(archive -> mode, 11)){
                printf("%s: Mode differs", archive -> name);
            }

        }
        printf("\n");
        printf("\n");
        archive = archive -> next;
    }
    return 0;
}


int recursive_mkdir(const char * dir, const unsigned int mode, const char verbosity){
   // int rc = 0;
    const size_t len = strlen(dir);

    if (!len){
        return 0;
    }

    char * path = calloc(len + 1, sizeof(char));
    strncpy(path, dir, len);

    // remove last '/'
    if (path[len - 1] ==  '/'){
       path[len - 1] = 0;
    }



    if (mkdir(path) < 0){
        RC_ERROR("Could not create directory %s: %s", path, strerror(rc));
    }

    free(path);
    return 0;
}

int tar_remove(const int fd, struct tar_t ** archive, int filecount, const char * files[], const char verbosity){
    if (fd < 0){
        return -1;
    }

    // archive has to exist
    if (!archive || !*archive){
        ERROR("Got bad archive");
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }

    if (!filecount){
        V_PRINT(stderr, "No entries specified");
        return 0;
    }



    // reset offset of original file
    if (lseek(fd, 0, SEEK_SET) == (off_t) (-1)){
        RC_ERROR("Unable to seek file: %s", strerror(rc));
    }

    // find first file to be removed that does not exist
    int ret = 0;
    for(int i = 0; i < filecount; i++){
        if (!exists(*archive, files[i], 0)){
            ERROR("'%s' not found in archive", files[i]);
        }
    }

    unsigned int read_offset = 0;
    unsigned int write_offset = 0;
    struct tar_t * prev = NULL;
    struct tar_t * curr = *archive;
    while(curr){
        // get original size
        int total = 512;

        if ((curr -> type == REGULAR) || (curr -> type == NORMAL)){
            total += oct2uint(curr -> size, 11);
            if (total % 512){
                total += 512 - (total % 512);
            }
        }

        const int match = check_match(curr, filecount, files);

        if (match < 0){
            ERROR("Match failed");
        }
   else if (!match){
            // if the old data is not in the right place, move it
            if (write_offset < read_offset){
                int got = 0;
                while (got < total){
                    // go to old data
                    if (lseek(fd, read_offset, SEEK_SET) == (off_t) (-1)){
                        RC_ERROR("Cannot seek: %s", strerror(rc));
                    }

                    char buf[512];

                    // copy chunk out
                    if (read_size(fd, buf, 512) != 512){// guarenteed 512 octets
                        ERROR("Read error");
                    }

                    // go to new position
                    if (lseek(fd, write_offset, SEEK_SET) == (off_t) (-1)){
                        RC_ERROR("Cannot seek: %s", strerror(rc));
                    }

                    // write data in
                    if (write_size(fd, buf, 512) != 512){
                        RC_ERROR("Write error: %s", strerror(rc));
                    }

                    // increment offsets
                    got += 512;
                    read_offset += 512;
                    write_offset += 512;
                }
            }
            else{
                read_offset += total;
                write_offset += total;

                // skip past data
                if (lseek(fd, read_offset, SEEK_SET) == (off_t) (-1)){
                    RC_ERROR("Cannot seek: %s", strerror(rc));
                }
            }
            prev = curr;
            curr = curr -> next;
        }

        else{// if name matches, skip the data
            struct tar_t * tmp = curr;
            if (!prev){
                *archive = curr -> next;
                if (*archive){
                    (*archive) -> begin = 0;
                }
            }
            else{
                prev -> next = curr -> next;

                if (prev -> next){
                    prev -> next -> begin = curr -> begin;
                }
            }
            curr = curr -> next;
            free(tmp);

            // next read starts after current entry
            read_offset += total;
        }
    }

    // resize file
    if (ftruncate(fd, write_offset) < 0){
        RC_ERROR("Could not truncate file: %s", strerror(rc));
    }


    return ret;
}

// check if entry is a match for any of the given file names
// returns index + 1 if match is found
int check_match(struct tar_t * entry, int filecount, const char * files[]){
    if (!entry){
        return -1;
    }

    if (!filecount){
        return 0;
    }

    if (filecount && !files){
        return -1;
    }

    for(int i = 0; i < filecount; i++){
        if (!strncmp(entry -> name, files[i], MAX(strlen(entry -> name), strlen(files[i])) + 1)){
            return i + 1;
        }
    }

    return 0;
}

//writing
int tar_write(const int fd, struct tar_t ** archive,int filecount, const char * files[], const char verbosity){
    if (fd < 0){
        ERROR("Bad file descriptor");
    }

    if (!archive){
        ERROR("Bad archive");
    }

    // where file descriptor offset is
    int offset = 0;

    // if there is old data
    struct tar_t ** tar = archive;


    if (*tar){
        // skip to last entry
        while (*tar && (*tar) -> next){
            tar = &((*tar) -> next);

        }

        // get offset past final entry
        unsigned int jump = 512 + oct2uint((*tar) -> size, 11);
        if (jump % 512){
            jump += 512 - (jump % 512);
        }

        // move file descriptor
        offset = (*tar) -> begin + jump;

        if (lseek(fd, offset, SEEK_SET) == (off_t) (-1)){
            RC_ERROR("Unable to seek file: %s", strerror(rc));
        }
        tar = &((*tar) -> next);
    }


    // write entries first
    if (write_entries(fd, tar, archive, filecount, files, &offset, verbosity) < 0){
        ERROR("Failed to write entries");
    }

    // write ending data
    if (write_end_data(fd, offset, verbosity) < 0){
        ERROR("Failed to write end data");
    }

    // clear original names from data
    tar = archive;
    while (*tar){
        memset((*tar) -> name, 0, 100);
        tar = &((*tar) -> next);
    }

    return offset;

}


int write_entries(const int fd, struct tar_t ** archive, struct tar_t ** head, const size_t filecount, const char * files[], int * offset, const char verbosity){
    if (fd < 0){
        ERROR("Bad file descriptor");
    }

    if (!archive || *archive){
        ERROR("Bad archive");
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }

    // add new data
    struct tar_t ** tar = archive;  // current entry
    for(unsigned int i = 0; i < filecount; i++){
        *tar = malloc(sizeof(struct tar_t));

        // stat file
        if (format_tar_data(*tar, files[i], verbosity) < 0){
            ERROR("Failed to stat %s", files[i]);
        }

        (*tar) -> begin = *offset;

        // directories need special handling
        if ((*tar) -> type == DIRECTORY){
            // save parent directory name (source will change)
            const size_t len = strlen((*tar) -> name);
            char * parent = calloc(len + 1, sizeof(char));
            strncpy(parent, (*tar) -> name, len);

            // add a '/' character to the end
            if ((len < 99) && ((*tar) -> name[len - 1] != '/')){
                (*tar) -> name[len] = '/';
                (*tar) -> name[len + 1] = '\0';
                calculate_checksum(*tar);
            }

            V_PRINT(stdout, "Writing %s", (*tar) -> name);

            // write metadata to (*tar) file
            if (write_size(fd, (*tar) -> block, 512) != 512){
                ERROR("Failed to write metadata to archive");
            }

            // go through directory
            DIR * d = opendir(parent);
            if (!d){
                ERROR("Cannot open directory %s", parent);
            }

            struct dirent * dir;
            while ((dir = readdir(d))){
                // if not special directories . and ..
                const size_t sublen = strlen(dir -> d_name);
                if (strncmp(dir -> d_name, ".", sublen) && strncmp(dir -> d_name, "..", sublen)){
                    char * path = calloc(len + sublen + 2, sizeof(char));
                    sprintf(path, "%s/%s", parent, dir -> d_name);

                    // recursively write each subdirectory
                    if (write_entries(fd, &((*tar) -> next), head, 1, (const char **) &path, offset, verbosity) < 0){
                        ERROR("Recurse error");
                    }

                    // go to end of new data
                    while ((*tar) -> next){
                        tar = &((*tar) -> next);
                    }

                    free(path);
                }
            }
            closedir(d);

            free(parent);

            tar = &((*tar) -> next);
        }
        else{ // if (((*tar) -> type == REGULAR) || ((*tar) -> type == NORMAL) || ((*tar) -> type == CONTIGUOUS) || ((*tar) -> type == SYMLINK) || ((*tar) -> type == CHAR) || ((*tar) -> type == BLOCK) || ((*tar) -> type == FIFO)){
            V_PRINT(stdout, "Writing %s", (*tar) -> name);

            char tarred = 0;   // whether or not the file has already been put into the archive
            if (((*tar) -> type == REGULAR) || ((*tar) -> type == NORMAL) || ((*tar) -> type == CONTIGUOUS) || ((*tar) -> type == SYMLINK)){
                struct tar_t * found = exists(*head, files[i], 1);
                tarred = (found != (*tar));

                // if file has already been included, modify the header
                if (tarred){
                    // change type to hard link
                    (*tar) -> type = HARDLINK;

                    // change link name to (*tar)red file name (both are the same)
                    strncpy((*tar) -> link_name, (*tar) -> name, 100);

                    // change size to 0
                    memset((*tar) -> size, '0', sizeof((*tar) -> size) - 1);

                    // recalculate checksum
                    calculate_checksum(*tar);
                }
            }

            // write metadata to (*tar) file
            if (write_size(fd, (*tar) -> block, 512) != 512){
                ("Failed to write metadata to archive");
            }

            if (((*tar) -> type == REGULAR) || ((*tar) -> type == NORMAL) || ((*tar) -> type == CONTIGUOUS)){
                // if the file isn't already in the tar file, copy the contents in
                if (!tarred){
                    int f = open((*tar) -> name, O_RDONLY);
                    if (f < 0){
                        ERROR("Could not open %s", files[i]);
                    }

                    int r = 0;
                    char buf[512];
                    while ((r = read_size(f, buf, 512)) > 0){
                        if (write_size(fd, buf, r) != r){
                            RC_ERROR("Could not write to archive: %s", strerror(rc));
                        }

                    }

                    close(f);
                }
            }

            // pad data to fill block
            const unsigned int size = oct2uint((*tar) -> size, 11);
            const unsigned int pad = 512 - size % 512;
            if (pad != 512){
                for(unsigned int j = 0; j < pad; j++){
                    if (write_size(fd, "\0", 1) != 1){
                        ERROR("Could not write padding data");
                    }
                }
                *offset += pad;
            }
            *offset += size;
            tar = &((*tar) -> next);
        }

        // add metadata size
        *offset += 512;
    }

    return 0;
}

int format_tar_data(struct tar_t * entry, const char * filename, const char verbosity){
    if (!entry){
        ERROR("Bad destination entry");
    }

    struct stat st;
    if (stat(filename, &st)){
        RC_ERROR("Cannot stat %s: %s", filename, strerror(rc));
    }

    // remove relative path
    int move = 0;
    if (!strncmp(filename, "/", 1)){
        move = 1;
    }
    else if (!strncmp(filename, "./", 2)){
        move = 2;
    }
    else if (!strncmp(filename, "../", 3)){
        move = 3;
    }

    // start putting in new data (all fields are NULL terminated ASCII strings)
    memset(entry, 0, sizeof(struct tar_t));
    strncpy(entry -> original_name, filename, 100);
    strncpy(entry -> name, filename + move, 100);
    snprintf(entry -> mode,  sizeof(entry -> mode),  "%07o", st.st_mode & 0777);
    snprintf(entry -> uid,   sizeof(entry -> uid),   "%07o", st.st_uid);
    snprintf(entry -> gid,   sizeof(entry -> gid),   "%07o", st.st_gid);
    snprintf(entry -> size,  sizeof(entry -> size),  "%011o", (int) st.st_size);
    snprintf(entry -> mtime, sizeof(entry -> mtime), "%011o", (int) st.st_mtime);

    // figure out filename type and fill in type-specific fields
    switch (st.st_mode & S_IFMT) {
        case S_IFREG:
            entry -> type = NORMAL;
            break;


        case S_IFDIR:
            memset(entry -> size, '0', 11);
            entry -> type = DIRECTORY;
            break;
        case S_IFIFO:
            entry -> type = FIFO;
            break;

        default:
            entry -> type = -1;
            ERROR("Error: Unknown filetype");
    }

    // get the checksum
    calculate_checksum(entry);

    return 0;
}

int write_end_data(const int fd, int size, const char verbosity){
    if (fd < 0){
        return -1;
    }

    // complete current record
    const int pad = RECORDSIZE - (size % RECORDSIZE);
    for(int i = 0; i < pad; i++){
        if (write(fd, "\0", 1) != 1){
            V_PRINT(stderr, "Error: Unable to close tar file");
            return -1;
        }
    }

    // if the current record does not have 2 blocks of zeros, add a whole other record
    if (pad < (2 * BLOCKSIZE)){
        for(int i = 0; i < RECORDSIZE; i++){
            if (write(fd, "\0", 1) != 1){
                V_PRINT(stderr, "Error: Unable to close tar file");
                return -1;
            }
        }
        return pad + RECORDSIZE;
    }

    return pad;
}


unsigned int calculate_checksum(struct tar_t * entry){
    // use spaces for the checksum bytes while calculating the checksum
    memset(entry -> check, ' ', 8);

    // sum of entire metadata
    unsigned int check = 0;
    for(int i = 0; i < 512; i++){
        check += (unsigned char) entry -> block[i];
    }

    snprintf(entry -> check, sizeof(entry -> check), "%06o0", check);

    entry -> check[6] = '\0';
    entry -> check[7] = ' ';
    return check;
}


int tar_update(const int fd, struct tar_t ** archive, const size_t filecount, const char * files[], const char verbosity){
    if (!filecount){
        return 0;
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }

    // buffer for subset of files that need to be updated
    char ** newer = calloc(filecount, sizeof(char *));

    struct stat st;
    int count = 0;
    int all = 1;

    // check each source to see if it was updated
    struct tar_t * tar = *archive;
    for(int i = 0; i < filecount; i++){
        // make sure original file exists
        if (stat(files[i], &st)){
            all = 0;
            RC_ERROR("Could not stat %s: %s", files[i], strerror(rc));
        }

        // find the file in the archive
        struct tar_t * old = exists(tar, files[i], 1);
        newer[count] = calloc(strlen(files[i]) + 1, sizeof(char));

        // if there is an older version, check its timestamp
        if (old){
            if (st.st_mtime > oct2uint(old -> mtime, 11)){
                strncpy(newer[count++], files[i], strlen(files[i]));
                V_PRINT(stdout, "%s", files[i]);
            }
        }
        // if there is no older version, just add it
        else{
            strncpy(newer[count++], files[i], strlen(files[i]));
            V_PRINT(stdout, "%s", files[i]);
        }
    }

    // update listed files only
    if (tar_write(fd, archive, count, (const char **) newer, verbosity) < 0){
        ERROR("Unable to update archive");
    }

    // cleanup
    for(int i = 0; i < count; i++){
        free(newer[i]);
    }
    free(newer);

    return all?0:-1;
}
