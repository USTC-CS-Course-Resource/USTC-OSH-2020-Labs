#define _GNU_SOURCE     // Required for enabling clone(2)
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>     // For mkdtemp(3)
#include <sys/types.h>  // For wait(2)
#include <sys/wait.h>   // For wait(2)
#include <sched.h>      // For clone(2)
#include <signal.h>     // For SIGCHLD constant
#include <sys/mman.h>   // For mmap(2)
#include <sys/mount.h>  // For mount(2)

#define STACK_SIZE (1024 * 1024) // 1 MiB

const char *usage =
"Usage: %s <directory> <command> [args...]\n"
"\n"
"  Run <directory> as a container and execute <command>.\n";

void error_exit(int code, const char *message);
int child(void *arg);

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if (chdir(argv[1]) == -1)
        error_exit(1, argv[1]);

    void *child_stack = mmap(NULL, STACK_SIZE,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                             -1, 0);
    // Assume stack grows downwards
    void *child_stack_start = child_stack + STACK_SIZE;

    int container_pid = clone(child, child_stack_start,
                   SIGCHLD | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWCGROUP,
                   argv);
    
    int status, ecode = 0;
    wait(&status);
    if (WIFEXITED(status)) {
        printf("Child exited with code %d\n", WEXITSTATUS(status));
        ecode = WEXITSTATUS(status);
    } 
    else if (WIFSIGNALED(status)) {
        printf("Killed by signal %d\n", WTERMSIG(status));
        ecode = -WTERMSIG(status);
    }
    return ecode;
}

void error_exit(int code, const char *message) {
    perror(message);
    _exit(code);
}

/*
mount | grep OSH && sudo umount rootfs/proc
sudo mount --make-private -o remount /

*/
int child(void *arg) {
    mount("/dev/sdb8", "/home", "ext4", MS_PRIVATE | MS_RELATIME, NULL);
    sethostname("container",20);
    char** argv = (char**)(arg);
    // Child goes for target program
    //if (chroot(".") == -1)
    //    error_exit(1, "chroot");

    //system("mount -t proc proc /proc");
    //mount("/", "/", "ext4", MS_PRIVATE, "");
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    //mount("none", "/tmp", "tmpfs", 0, "");
    // mount file system
    /*
    udev on /dev type devtmpfs (rw,nosuid,relatime,size=8115188k,nr_inodes=2028797,mode=755)
    proc on /proc type proc (rw,nosuid,nodev,noexec,relatime)
    sysfs on /sys type sysfs (rw,nosuid,nodev,noexec,relatime)
    /tmp

    Values  for  the  filesystemtype  argument  supported by the kernel are
       listed in  /proc/filesystems  (e.g.,  "btrfs",  "ext4",  "jfs",  "xfs",
       "vfat",  "fuse",  "tmpfs",  "cgroup",  "proc", "mqueue", "nfs", "cifs",
       "iso9660").  Further types may become available  when  the  appropriate
       modules are loaded.*/
    // TODO: 在 /sys/fs/cgroup 下挂载指定的四类 cgroup 控制器

    /*mount("udev", "/dev", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "size=8115188k nr_inodes=2028797 mode=755");
    */
    /*mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    mount("tmp", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "size=8115188k nr_inodes=2028797 mode=755");
    */
    /*
    char tmpdir[] = "/tmp/lab4-XXXXXX";
    mkdtemp(tmpdir);
    char oldrootdir[] = "";
    sprintf(oldrootdir, "%s/oldroot", tmpdir);
    pivot_root(tmpdir, oldrootdir);
    */
    execvp(argv[2], argv + 2);
    error_exit(255, "exec");
}