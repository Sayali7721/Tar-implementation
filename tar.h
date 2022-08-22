#ifndef TAR_H_INCLUDED
#define TAR_H_INCLUDED


#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define S_IRGRP (S_IRUSR >> 3)
#define S_IXGRP (S_IXUSR >> 3)
#define S_IROTH (S_IRGRP >> 3)
#define S_IXOTH (S_IXGRP >> 3)
#define DEFAULT_DIR_MODE S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
#define BLOCKSIZE       512
#define BLOCKING_FACTOR 20
#define RECORDSIZE      10240

// file type values (1 octet)
#define REGULAR          0
#define NORMAL          '0'
#define HARDLINK        '1'
#define SYMLINK         '2'
#define CHAR            '3'
#define BLOCK           '4'
#define DIRECTORY       '5'
#define FIFO            '6'
#define CONTIGUOUS      '7'



struct tar_t {

    char original_name[100];                // original filenme; only availible when writing into a tar
    unsigned int begin;                     // location of data in file (including metadata)
    union {
        union {
            // Pre-POSIX.1-1988 format
            struct {
                char name[100];             // file name
                char mode[8];               // permissions
                char uid[8];                // user id (octal)
                char gid[8];                // group id (octal)
                char size[12];              // size (octal)
                char mtime[12];             // modification time (octal)
                char check[8];              // sum of unsigned characters in block, with spaces in the check field while calculation is done (octal)
                char link;                  // link indicator
                char link_name[100];        // name of linked file
            };

            // UStar format (POSIX IEEE P1003.1)
            struct {
                char old[156];              // first 156 octets of Pre-POSIX.1-1988 format
                char type;                  // file type
                char also_link_name[100];   // name of linked file
                char ustar[8];              // ustar\000
                char owner[32];             // user name (string)
                char group[32];             // group name (string)
                char major[8];              // device major number
                char minor[8];              // device minor number
                char prefix[155];
            };
        };

        char block[512];                    // raw memory (500 octets of actual data, padded to 1 block)
    };

    struct tar_t * next;
};


int tar_read(const int fd, struct tar_t ** archive, const char verbosity);

int write_entries(const int fd, struct tar_t ** archive, struct tar_t ** head, const size_t filecount, const char * files[], int * offset, const char verbosity);

int tar_ls(FILE * f, struct tar_t * archive, int filecount, const char * files[], const char verbosity);

int ls_entry(FILE * f, struct tar_t * entry, int filecount, const char * files[], const char verbosity);

int tar_extract(const int fd, struct tar_t * archive, int filecount, const char * files[], const char verbosity);

int extract_entry(const int fd, struct tar_t * entry, const char verbosity);

int tar_remove(const int fd, struct tar_t ** archive, int filecount, const char * files[], const char verbosity);

int tar_diff(FILE * f, struct tar_t * archive, const char verbosity);

int check_match(struct tar_t * entry, int filecount, const char * files[]);

// convert octal string to unsigned integer
unsigned int oct2uint(char * oct, unsigned int size);

// force read() to complete
int read_size(int fd, char * buf, int size);

// recursive freeing of entries
void tar_free(struct tar_t * archive);

// check if a buffer is zeroed
int iszeroed(char * buf, size_t size);

int print_tar_metadata(FILE * f, struct tar_t * archive);

int print_entry_metadata(FILE * f, struct tar_t * entry);

struct tar_t * exists(struct tar_t * archive, const char * filename, const char ori);

unsigned int calculate_checksum(struct tar_t * entry);

int tar_write(const int fd, struct tar_t ** archive, int filecount, const char * files[], const char verbosity);

int write_end_data(const int fd, int size, const char verbosity);

int format_tar_data(struct tar_t * entry, const char * filename, const char verbosity);

int recursive_mkdir(const char * dir, const unsigned int mode, const char verbosity);

int tar_update(const int fd, struct tar_t ** archive, const size_t filecount, const char * files[], const char verbosity);

#endif // TAR_H_INCLUDED
