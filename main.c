#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "tar.h"

int main(int argc, char** argv) {
   // char *buf =(char*)calloc(3500,sizeof(char));

    int fd = open(argv[1],O_RDWR);
    FILE * f = fopen("F:/PPL/PPL.tar","r");
    char verbosity = 2;
    struct tar_t * archive = NULL;
    const char * filename[] = {"B-1.txt"};

    if(argv[2][1] == 't') {
        int cnt =  tar_read(fd,&archive, verbosity);

        for(int i = 0; i < cnt; i++) {
             printf("%s\n", archive->name);
             archive = archive->next;
        }

    }

    if(argv[2][1] == 'c') {
        const char * create[argc - 3];
        int cnt = argc, i = 0;

        while(i < cnt - 3) {
            create[i] = argv[3 + i];
            i++;
        }

        int fd1 = open(argv[1],O_CREAT | O_RDWR);
        tar_update(fd1, &archive,i,create,verbosity);

    }


    if(argv[2][1] == 'd') {
        tar_read(fd,&archive, verbosity);
        tar_diff(f, archive, verbosity);
    }


    if(argv[2][1] == 'x') {
        tar_read(fd,&archive, verbosity);
        tar_extract(fd, archive, 6, filename, verbosity);
    }


    if(argv[2][1] == 'l' && argv[2][2] == 's') {
        tar_read(fd,&archive, verbosity);
        tar_ls(f, archive, 6, filename, verbosity);
    }

    if(argv[2][1] == 'c' && argv[2][2] == 'a' && argv[2][3] == 't') {
        tar_read(fd,&archive, verbosity);
        print_tar_metadata(f, archive);

    }

    if(argv[2][1] == 'u') {
        tar_read(fd,&archive, verbosity);
        tar_update(fd, &archive,1,filename,verbosity);

    }

    if(argv[2][1] == 'r') {
        tar_read(fd,&archive, verbosity);
        tar_remove(fd, &archive, 1, filename, verbosity);
    }



    return 0;
}
