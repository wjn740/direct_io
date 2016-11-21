#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#define BLOCKSIZE 512
char image[] =
{
    'P', '5', ' ', '2', '4', ' ', '7', ' ', '1', '5', '\n',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 3, 3, 3, 3, 0, 0, 7, 7, 7, 7, 0, 0,11,11,11,11, 0, 0,15,15,15,15, 0,
    0, 3, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0,11, 0, 0, 0, 0, 0,15, 0, 0,15, 0,
    0, 3, 3, 3, 0, 0, 0, 7, 7, 7, 0, 0, 0,11,11,11, 0, 0, 0,15,15,15,15, 0,
    0, 3, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0,11, 0, 0, 0, 0, 0,15, 0, 0, 0, 0,
    0, 3, 0, 0, 0, 0, 0, 7, 7, 7, 7, 0, 0,11,11,11,11, 0, 0,15, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
};
int main()
{
    void *buffer;
    int i;
    posix_memalign(&buffer, BLOCKSIZE, BLOCKSIZE);
    memcpy(buffer, image, sizeof(image));
    int f = open("feep.pgm", O_CREAT|O_TRUNC|O_WRONLY|O_DIRECT, S_IRWXU);
    //int f = open("feep.pgm", O_CREAT|O_TRUNC|O_WRONLY|O_SYNC, S_IRWXU);
    //int f = open("feep.pgm", O_CREAT|O_TRUNC|O_WRONLY, S_IRWXU);
    while(i<0xFFFF) {
        write(f, buffer, BLOCKSIZE);
        i++;
    }
    //fdatasync(f);
    //fsync(f);
    close(f);
    free(buffer);
    return 0;
}
