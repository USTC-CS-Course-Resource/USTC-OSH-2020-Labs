#define _GNU_SOURCE         // Required for enabling clone(2)
#include <stdio.h>
#include <string.h>       
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>         // For mkdtemp(3)
#include <sys/types.h>      // For wait(2)
#include <sys/wait.h>       // For wait(2)
#include <sched.h>          // For clone(2)
#include <signal.h>         // For SIGCHLD constant
#include <sys/mman.h>       // For mmap(2)
#include <sys/mount.h>      // For mount(2)
#include <sys/syscall.h>    // For syscall(2)
#include <errno.h>          // For errno
#include <cap-ng.h>         // For libcap-ng series functions
#include <seccomp.h>        // For seccomp series functions
#include <fcntl.h>          // For file control

#include "syscall_filter.h"
#include "utility.h"


#define STACK_SIZE (1024 * 1024) // 1 MiB
#define PATH_SIZE_MAX 1024

const char *usage =
"Usage: %s <directory> <command> [args...]\n"
"\n"
"  Run <directory> as a container and execute <command>.\n";

// Part I.      For isolation and pivot_root
typedef struct ChildArg {
    char **args;
    int fds[2];
} ChildArg;
static int child(void *arg);
// implement the pivot_root by syscall
/* 
 * Function: do_pivot
 * Description: Do pivot, including 
 *  rprivate "/",
 *  binding "./" to tmpdir,
 *  mkdir for oldroot,
 *  invoke pivot_root(),
 *  detach and rmdir,
 */ 
int do_pivot(const char *tmpdir);

// Part II.     For mount
void mount_needed();

// Part III.     For capabilities
void check_needed_cap();
void update_needed_cap();

// Part IV.    For seccomp
void set_seccomp();

// Part V.    For cgroup limit
void cgroup_limit();

// Part VI. some utilities
void error_exit(int code, const char *message);

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
    char bind_path[PATH_SIZE_MAX] = {'\0'};
    read(carg.fds[0], bind_path, PATH_SIZE_MAX);
    
    umount2(bind_path, MNT_DETACH);
    if(rmdir(bind_path) == -1)
        perror("rmdir");
        
    
    wait(&status);

    if(rmdir("/sys/fs/cgroup/memory/lab4") == -1) perror("rmdir");
    if(rmdir("/sys/fs/cgroup/cpu,cpuacct/lab4") == -1) perror("rmdir");
    if(rmdir("/sys/fs/cgroup/pids/lab4") == -1) perror("rmdir");
    if(rmdir("/sys/fs/cgroup/blkio/lab4") == -1) perror("rmdir");

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

static int child(void *arg) {
    // recv the arg
    ChildArg carg = *((ChildArg*)arg);
    char **args = carg.args;

    // create the tmpdir for container's rootfs
    char tmpdir[] = "/tmp/lab4-XXXXXX";
    mkdtemp(tmpdir);
    printf("tmpdir: %s\n", tmpdir);

    // pivot_root
    do_pivot(tmpdir);
    
    // mount something needed
    mount_needed();
    
    // send the tmpdir to parent process(host machine)
    close(carg.fds[0]);
    write(carg.fds[1], tmpdir, strlen(tmpdir)+1);
    close(carg.fds[1]);

    // update capabilities and check them
    update_needed_cap();
    check_needed_cap();

    // use seccomp
    set_seccomp();

    // cgroup_limit()
    cgroup_limit();
    
    execvp(args[2], args + 2);
    error_exit(255, "exec");
}

/*
 * This part is for pivot_root
 */ 
static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

int do_pivot(const char *tmpdir) {
    char oldroot_path[PATH_SIZE_MAX];
    char put_old[] = "oldrootfs";

    // get the full path of oldroot_path
    snprintf(oldroot_path, sizeof(char)*PATH_SIZE_MAX, "%s/%s", tmpdir, put_old);
    // recursively remount / as private
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 1)
        errexit("[container][error] mount-MS_PRIVATE");
    
    // bind the root of the container to tmpdir
    if (mount("./", tmpdir, NULL, MS_BIND, NULL) == -1)
        errexit("[container][error] mount-MS_BIND");

    // make dir oldroot_path (may fail because of EEXIST)
    mkdir(oldroot_path, 0777);
    if(errno != 0) perror("[container][warning] mkdir");

    // And pivot the root filesystem
    if (pivot_root(tmpdir, oldroot_path) == -1)
        errexit("[container][error] pivot_root");

    // Switch the current working directory to "/"
    if (chdir("/") == -1)
        errexit("[container][error] chdir");

    // Unmount old root and remove mount point
    if (umount2(put_old, MNT_DETACH) == -1)
        errexit("[container][error] umount2(put_old, MNT_DETACH)");
    if (rmdir(put_old) == -1)
        errexit("[container][error] rmdir");
}

/*
 * This part is for mount
 */
void mount_needed() {
    mount("udev", "/dev", "devtmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME | MS_RDONLY, NULL); // mount "/sys" as MS_RDONLY
    mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOATIME, NULL);
    mount("tmpfs", "/sys/fs/cgroup", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL);
    mkdir("/sys/fs/cgroup/memory", 0777);
    mount("cgroup", "/sys/fs/cgroup/memory", "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "memory");
    mkdir("/sys/fs/cgroup/cpu,cpuacct", 0777);
    mount("cgroup", "/sys/fs/cgroup/cpu,cpuacct", "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "cpu,cpuacct");
    mkdir("/sys/fs/cgroup/pids", 0777);
    mount("cgroup", "/sys/fs/cgroup/pids", "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "pids");
    mkdir("/sys/fs/cgroup/blkio", 0777);
    mount("cgroup", "/sys/fs/cgroup/blkio", "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "blkio");
}

/*
 * This part is for cgroup
 */
#define APPEND 0
#define OVERWRITE 1
int write_str(const char *fname, const char *str, int mode) {
    int fd;
    if(mode == APPEND)
        fd = open(fname, O_WRONLY|O_CREAT|O_APPEND, 0777);
    else if(mode == OVERWRITE)
        fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0777);
    else return -1;
    write(fd, str, strlen(str));
    close(fd);
    return 0;
}

void cgroup_limit() {
    pid_t pid = getpid();
    //printf("get pid: %d\n", pid);
    char pid_str[32];
    sprintf(pid_str, "%d", pid);

    // memory part
    mkdir("/sys/fs/cgroup/memory/lab4", 0777);
    //// limit user memory as 67108864 bytes
    write_str("/sys/fs/cgroup/memory/lab4/memory.limit_in_bytes", "67108864", OVERWRITE);
    //// limit user kernel as 67108864 bytes
    write_str("/sys/fs/cgroup/memory/lab4/memory.kmem.limit_in_bytes", "67108864", OVERWRITE);
    //// disable swap
    write_str("/sys/fs/cgroup/memory/lab4/memory.swappiness", "0", OVERWRITE);
    write_str("/sys/fs/cgroup/memory/lab4/cgroup.procs", pid_str, APPEND);

    // cpu part
    mkdir("/sys/fs/cgroup/cpu,cpuacct/lab4", 0777);
    //// limit cpu.shares
    write_str("/sys/fs/cgroup/cpu,cpuacct/lab4/cpu.shares", "256", OVERWRITE);
    write_str("/sys/fs/cgroup/cpu,cpuacct/lab4/cgroup.procs", pid_str, APPEND);

    // pids part
    mkdir("/sys/fs/cgroup/pids/lab4", 0777);
    //// limit cpu.shares
    write_str("/sys/fs/cgroup/pids/lab4/pids.max", "64", OVERWRITE);
    write_str("/sys/fs/cgroup/pids/lab4/cgroup.procs", pid_str, APPEND);

    // blkio part
    mkdir("/sys/fs/cgroup/blkio/lab4", 0777);
    //// limit cpu.shares
    write_str("/sys/fs/cgroup/blkio/lab4/blkio.weight", "50", OVERWRITE);
    write_str("/sys/fs/cgroup/blkio/lab4/cgroup.procs", pid_str, APPEND);

}


/*
 * This part is for capabilities
 */ 
#define CAPS_NUM 14
#define CAP_STR_SIZE_MAX 22
#define CAPS CAP_SETPCAP, CAP_MKNOD, CAP_AUDIT_WRITE,\
             CAP_CHOWN, CAP_NET_RAW, CAP_DAC_OVERRIDE,\
             CAP_FOWNER, CAP_FSETID, CAP_KILL,\
             CAP_SETGID, CAP_SETUID, CAP_NET_BIND_SERVICE,\
             CAP_SYS_CHROOT, CAP_SETFCAP

const int CAPS_LIST[14] = 
    {CAP_SETPCAP, CAP_MKNOD, CAP_AUDIT_WRITE,
     CAP_CHOWN, CAP_NET_RAW, CAP_DAC_OVERRIDE,
     CAP_FOWNER, CAP_FSETID, CAP_KILL,
     CAP_SETGID, CAP_SETUID, CAP_NET_BIND_SERVICE,
     CAP_SYS_CHROOT, CAP_SETFCAP};

const char CAPS_STR_LIST[14][CAP_STR_SIZE_MAX] = 
    {"CAP_SETPCAP", "CAP_MKNOD", "CAP_AUDIT_WRITE",
     "CAP_CHOWN", "CAP_NET_RAW", "CAP_DAC_OVERRIDE",
     "CAP_FOWNER", "CAP_FSETID", "CAP_KILL",
     "CAP_SETGID", "CAP_SETUID", "CAP_NET_BIND_SERVICE",
     "CAP_SYS_CHROOT", "CAP_SETFCAP"};

void update_needed_cap() {
    capng_clear(CAPNG_SELECT_BOTH);
    capng_updatev(CAPNG_ADD, CAPNG_EFFECTIVE|CAPNG_PERMITTED,
                  CAPS, -1);
    capng_apply(CAPNG_SELECT_BOTH);
}

void check_needed_cap() {
    for(int i = 0; i < CAPS_NUM; i++) {
        if (capng_have_capability(CAPNG_EFFECTIVE, CAPS_LIST[i]))
            logger("info", "%22s\t[√]\n", CAPS_STR_LIST[i]);
        else
            logger("info", "%22s\t[x]\n", CAPS_STR_LIST[i]);
    }
}

