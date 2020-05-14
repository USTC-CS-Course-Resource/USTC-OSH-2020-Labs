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
#include <sys/syscall.h>    // For syscall(2)
#include <stdarg.h>
#include <errno.h>
#include <string.h>       
#include <sys/stat.h>


#define STACK_SIZE (1024 * 1024) // 1 MiB
#define PATH_SIZE_MAX 1024

const char *usage =
"Usage: %s <directory> <command> [args...]\n"
"\n"
"  Run <directory> as a container and execute <command>.\n";

void error_exit(int code, const char *message);
static int child(void *arg);
// implement the pivot_root by syscall
static int pivot_root(const char *new_root, const char *put_old);
void errexit(const char *format, ...);

typedef struct ChildArg {
    char **args;
    int fds[2];
} ChildArg;

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

    // for requesting detach
    ChildArg carg;
    carg.args = argv;
    pipe(carg.fds);

    int container_pid = clone(child, child_stack_start,
                   SIGCHLD | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWCGROUP,
                   &carg);
    
    int status, ecode = 0;
    close (carg.fds[1]);
    char bind_path[PATH_SIZE_MAX];
    read(carg.fds[0], bind_path, PATH_SIZE_MAX);

    char ls_com[PATH_SIZE_MAX+3] = "ls ";
    strcat(ls_com, bind_path);
    printf("命令: %s\n", ls_com);
    sleep(2);
    system(ls_com);
    if(umount2(bind_path, MNT_DETACH) == -1)
        perror("[error] umount2");
    if(rmdir(bind_path) == -1)
        perror("[error] rmdir");

    wait(&status);
    if(WIFEXITED(status)) {
        printf("Child exited with code %d\n", WEXITSTATUS(status));
        ecode = WEXITSTATUS(status);
    } 
    else if(WIFSIGNALED(status)) {
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
static int child(void *arg) {
    // recv the arg
    ChildArg carg = *((ChildArg*)arg);
    char **args = carg.args;

    // create the directory for the old root
    char tmpdir[] = "/tmp/lab4-XXXXXX";
    mkdtemp(tmpdir);
    char oldroot_path[PATH_SIZE_MAX];
    char put_old[] = "oldrootfs";

    // expand the oldroot_path
    snprintf(oldroot_path, sizeof(char)*PATH_SIZE_MAX, "%s/%s", tmpdir, put_old);
    
    // make dir oldroot_path (may fail because of EEXIST)
    mkdir(oldroot_path, 0777);
    printf("mkdir errno: %d\n", errno);

    printf("tempdir: %s\n", tmpdir);
    printf("oldroot_dir: %s\n", oldroot_path);
    printf("access tmpdir: %d\n", access(tmpdir, F_OK));
    printf("access oldroot_path: %d\n", access(oldroot_path, F_OK));

    // recursively remount / as private
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 1)
        errexit("[error] mount-MS_PRIVATE");
    
    // bind the root of the container to tmpdir
    if (mount("./", tmpdir, NULL, MS_BIND, NULL) == -1)
        errexit("[error] mount-MS_BIND");

    // And pivot the root filesystem
    if (pivot_root(tmpdir, oldroot_path) == -1)
        errexit("[error] pivot_root");

    // Switch the current working directory to "/"
    if (chdir("/") == -1)
        errexit("[error] chdir");
    
    system("ls \tmp");
    printf("finish ls");
    // Unmount old root and remove mount point
    if (umount2(put_old, MNT_DETACH) == -1)
        perror("[error] umount2");
    if (rmdir(put_old) == -1)
        perror("[error] rmdir");

    close(carg.fds[0]);
    write(carg.fds[1], tmpdir, strlen(tmpdir)+1);
    close(carg.fds[1]);

    
    mount("udev", "/dev", "devtmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME | MS_RDONLY, NULL); // mount "/sys" as MS_RDONLY
    mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOATIME, NULL);
    // TODO: 在 /sys/fs/cgroup 下挂载指定的四类 cgroup 控制器
    
    execvp(args[2], args + 2);
    error_exit(255, "exec");
}

static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

void errexit(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\terrno: %d\n", errno);
    va_end(args);
    exit(1);
} 