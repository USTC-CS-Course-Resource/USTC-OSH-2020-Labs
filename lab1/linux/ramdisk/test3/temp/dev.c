#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sysmacros.h>

int main() {
    if (mknod("/dev/ttyS0", S_IFCHR | S_IRUSR | S_IWUSR, makedev(1, 3)) == -1) {
        perror("mknod() failed");
    }
    return 0;
}