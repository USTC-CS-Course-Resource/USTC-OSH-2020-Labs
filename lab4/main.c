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
#include <stdarg.h>         // For args
#include <sys/sysmacros.h>	// For mknod

#define STACK_SIZE (1024 * 1024) // 1 MiB
#define PATH_SIZE_MAX 1024

const char *usage =
"Usage: %s <directory> <command> [args...]\n"
"\n"
"  Run <directory> as a container and execute <command>.\n";

typedef struct ChildArg {
    char **args;
    int fds_c2p[2];
    int fds_p2c[2];
} ChildArg;

// Part I.      For isolation and pivot_root
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
void mknod_needed() ;

// Part III.     For capabilities
void check_needed_cap();
void update_needed_cap();

// Part IV.    For seccomp
void set_seccomp();

// Part V.    For cgroup limit
int write_str(const char *fname, const char *str, int mode);
int append(const char *src, const char *dest);
void mount_cgroup_needed();
void cgroup_limit(pid_t pid);
void cgroup_append();

// Part VI. some utilities
void logger(const char *type, int error_exit_code, const char *format, ...);

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if (chdir(argv[1]) == -1)
		logger("error", 255, argv[1]);

    void *child_stack = mmap(NULL, STACK_SIZE,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                             -1, 0);
    // Assume stack grows downwards
    void *child_stack_start = child_stack + STACK_SIZE;

    // for requesting detach
    ChildArg carg;
    carg.args = argv;
    pipe(carg.fds_c2p);
	pipe(carg.fds_p2c);
	//cgroup_limit(getpid());
    int container_pid = clone(child, child_stack_start,
                   SIGCHLD | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID,
                   &carg);

	cgroup_limit(container_pid);
	close(carg.fds_p2c[0]);
	write(carg.fds_p2c[1], "ok", 3);
	close(carg.fds_p2c[1]);
	
    int status, ecode = 0;
    close (carg.fds_c2p[1]);
    char bind_path[PATH_SIZE_MAX] = {'\0'};
    read(carg.fds_c2p[0], bind_path, PATH_SIZE_MAX);
    
    umount2(bind_path, MNT_DETACH);
    if(rmdir(bind_path) == -1)
		logger("warning", 0, "rmdir /tmp/lab4-XXXXXX");
        
    wait(&status);
	cgroup_append();

    if(rmdir("/sys/fs/cgroup/memory/lab4") == -1) logger("warning", 0, "/sys/fs/cgroup/memory/lab4");
    if(rmdir("/sys/fs/cgroup/cpu,cpuacct/lab4") == -1) logger("warning", 0, "/sys/fs/cgroup/pu,cpuacct/lab4");
    if(rmdir("/sys/fs/cgroup/pids/lab4") == -1) logger("warning", 0, "/sys/fs/cgroup/pids/lab4");

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

	// wait for parent's cgroup setting
	char temp[32];
	close(carg.fds_p2c[1]);
	read(carg.fds_p2c[0], temp, 3);
	close(carg.fds_p2c[0]);

	unshare(CLONE_NEWCGROUP);

    // create the tmpdir for container's rootfs
    char tmpdir[] = "/tmp/lab4-XXXXXX";
    mkdtemp(tmpdir);
    //printf("tmpdir: %s\n", tmpdir);

    // pivot_root
    do_pivot(tmpdir);
    
    // mount something needed
    mount_needed();
	mount_cgroup_needed();
	mknod_needed();
    
    // send the tmpdir to parent process(host machine)
    close(carg.fds_c2p[0]);
    write(carg.fds_c2p[1], tmpdir, strlen(tmpdir) + 1);
    close(carg.fds_c2p[1]);

    // update capabilities and check them
    update_needed_cap();
    check_needed_cap();

    // use seccomp
    set_seccomp();
    
    execvp(args[2], args + 2);
	logger("error", 255, "exec");
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

    // recursively remount / as private
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 1)
        logger("error", 0, "mount-MS_PRIVATE");

    // get the full path of oldroot_path
    snprintf(oldroot_path, sizeof(char)*PATH_SIZE_MAX, "%s/%s", tmpdir, put_old);
    
    // bind the root of the container to tmpdir
    if (mount("./", tmpdir, NULL, MS_BIND, NULL) == -1)
        logger("error", 0, "mount-MS_BIND");

    // make dir oldroot_path (may fail because of EEXIST)
    mkdir(oldroot_path, 0777);
    if(errno != 0) logger("warning", 0, "mkdir oldroot_path");

    // And pivot the root filesystem
    if (pivot_root(tmpdir, oldroot_path) == -1)
        logger("error", 0, "pivot_root");

    // Switch the current working directory to "/"
    if (chdir("/") == -1)
        logger("error", 0, "chdir to /");

    // Unmount old root and remove mount point
    if (umount2(put_old, MNT_DETACH) == -1)
        logger("error", 0, "detach old");
    if (rmdir(put_old) == -1)
        logger("error", 0, "rmdir old");

	logger("info", 0, "%-32s\t[√]", "pivot_root");
}

#define ERROR_EXIT 0
/*
 * This part is for mount and mknod
 */
void mount_needed() {
    // mount /sys
    if(mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME | MS_RDONLY, NULL)) // mount "/sys" as MS_RDONLY
        logger("error", 0, "mount /sys");
    // mount /proc
    if(mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL))
		logger("error", 0, "mount /proc");
    // mount /dev
    if(mount("udev", "/dev", "tmpfs", MS_NOSUID | MS_NOEXEC | MS_RELATIME, NULL))
		logger("error", 0, "mount /dev");
    // mount /tmp
    if(mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOATIME, NULL))
		logger("error", 0, "mount /tmp");
		
	logger("info", 0, "%-32s\t[√]", "mount /sys, /proc, /dev, /tmp");
}

void mknod_needed() {
	// mknod null
	if(mknod("/dev/null", S_IFCHR, makedev(1, 3)))
		logger("error", ERROR_EXIT, "mknod /dev/null");
	if(chmod("/dev/null", 0666))
		logger("error", ERROR_EXIT, "chmod /dev/null");
	// mknod zero
	if(mknod("/dev/zero", S_IFCHR, makedev(1, 5)))
		logger("error", ERROR_EXIT, "mknod /dev/zero");
	if(chmod("/dev/zero", 0666))
		logger("error", ERROR_EXIT, "chmod /dev/zero");
	// mknod urandom
	if(mknod("/dev/urandom", S_IFCHR, makedev(1, 9)))
		logger("error", ERROR_EXIT, "mknod /dev/urandom");
	if(chmod("/dev/urandom", 0666))
		logger("error", ERROR_EXIT, "chmod /dev/urandom");	
	// mknod tty
	if(mknod("/dev/tty", S_IFCHR, makedev(5, 0)))
		logger("error", ERROR_EXIT, "mknod /dev/tty");
	if(chmod("/dev/tty", 0660))
		logger("error", ERROR_EXIT, "chmod /dev/tty");
	if(chown("/dev/tty", 0, 5))
		logger("error", ERROR_EXIT, "chown /dev/tty");

	logger("info", 0, "%-32s\t[√]", "mknod");
}

/*
 * This part is for cgroup
 */
#define APPEND 0
#define OVERWRITE 1
#define BUF_SIZE 1024
int write_str(const char *fname, const char *str, int mode) {
    int fd;
    if(mode == APPEND)
        fd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0777);
    else if(mode == OVERWRITE)
        fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    else return -1;
    write(fd, str, strlen(str));
    close(fd);
    return 0;
}

int append(const char *src, const char *dest) {
    int src_fd = open(src, O_RDONLY, 0777);
	int dest_fd = open(dest, O_WRONLY | O_CREAT | O_APPEND, 0777);
	char buffer[BUF_SIZE];
	char *p = buffer;
	ssize_t size;
	while((size = read(src_fd, buffer, BUF_SIZE)) == BUF_SIZE) {
		write(dest_fd, buffer, BUF_SIZE);
	}
	write(dest_fd, buffer, size);

	close(src_fd);
	close(dest_fd);
    return 0;
}

#define MESSAGE_SIZE 1024
void mkdir_logger(const char *path, mode_t mode) {
    if(mkdir(path, mode) == -1) {
		char message[MESSAGE_SIZE];
		if(errno != EEXIST) {
			logger("error", 0, "mkdir(\"%s\", %d)", path, mode);
		}
	}
}

void cgroup_limit(int pid) {
    char pid_str[32];
    sprintf(pid_str, "%d\n", pid);

    // memory part
    mkdir_logger("/sys/fs/cgroup/memory/lab4", 0777);
    //// limit user memory as 67108864 bytes
    write_str("/sys/fs/cgroup/memory/lab4/memory.limit_in_bytes", "67108864", OVERWRITE);
    //// limit user kernel as 67108864 bytes
    write_str("/sys/fs/cgroup/memory/lab4/memory.kmem.limit_in_bytes", "67108864", OVERWRITE);
    //// disable swap
    write_str("/sys/fs/cgroup/memory/lab4/memory.swappiness", "0", OVERWRITE);
    write_str("/sys/fs/cgroup/memory/lab4/cgroup.procs", pid_str, APPEND);

    // cpu,cpuacct part
    mkdir_logger("/sys/fs/cgroup/cpu,cpuacct/lab4", 0777);
    //// limit cpu.shares
    write_str("/sys/fs/cgroup/cpu,cpuacct/lab4/cpu.shares", "256", OVERWRITE);
    write_str("/sys/fs/cgroup/cpu,cpuacct/lab4/cgroup.procs", pid_str, APPEND);
	
    // pids part
    mkdir_logger("/sys/fs/cgroup/pids/lab4", 0777);
    //// limit pids.max
    write_str("/sys/fs/cgroup/pids/lab4/pids.max", "64", OVERWRITE);
    write_str("/sys/fs/cgroup/pids/lab4/cgroup.procs", pid_str, APPEND);

	logger("info", 0, "%-32s\t[√]", "cgroup limit");
}

void mount_cgroup_needed() {
	char pid_str[32];
	sprintf(pid_str, "%d\n", getpid());
    // mount cgroup
    if(mount("cgroup", "/sys/fs/cgroup", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "mode=755"))
		logger("error", 0, "mount /sys/fs/cgroup");

    // mounot cgroup/memory
	mkdir_logger("/sys/fs/cgroup/memory", 0777);
    if(mount("cgroup", "/sys/fs/cgroup/memory", "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "memory"))
		logger("error", 0, "mount /sys/fs/cgroup/memory");
    write_str("/sys/fs/cgroup/memory/cgroup.procs", pid_str, APPEND);

    // mounot cgroup/cpu,cpuacct
	mkdir_logger("/sys/fs/cgroup/cpu,cpuacct", 0777);
    if(mount("cgroup", "/sys/fs/cgroup/cpu,cpuacct", "cgroup", MS_NOSUID | MS_NOEXEC | MS_NODEV| MS_RELATIME, "cpu,cpuacct"))
		logger("error", 0, "mount /sys/fs/cgroup/cpu,cpuacct");
    write_str("/sys/fs/cgroup/cpu,cpuacct/cgroup.procs", pid_str, APPEND);

    // mounot cgroup/pids
	mkdir_logger("/sys/fs/cgroup/pids", 0777);
	if(mount("cgroup", "/sys/fs/cgroup/pids", "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, "pids"))
		logger("error", 0, "mount /sys/fs/cgroup/pids");
    write_str("/sys/fs/cgroup/pids/cgroup.procs", pid_str, APPEND);

	logger("info", 0, "%-32s\t[√]", "mount cgroup");
}

// let inside procs information be appended to outside ones
void cgroup_append() {
    // memory part
	append("/sys/fs/cgroup/memory/lab4/cgroup.procs", "/sys/fs/cgroup/memory/cgroup.procs");
    // cpu part
	append("/sys/fs/cgroup/cpu,cpuacct/lab4/cgroup.procs", "/sys/fs/cgroup/cpu,cpuacct/cgroup.procs");
    // pids part
	append("/sys/fs/cgroup/pids/lab4/cgroup.procs", "/sys/fs/cgroup/pids/cgroup.procs");
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

const int CAPS_LIST[CAPS_NUM] = 
    {CAP_SETPCAP, CAP_MKNOD, CAP_AUDIT_WRITE,
     CAP_CHOWN, CAP_NET_RAW, CAP_DAC_OVERRIDE,
     CAP_FOWNER, CAP_FSETID, CAP_KILL,
     CAP_SETGID, CAP_SETUID, CAP_NET_BIND_SERVICE,
     CAP_SYS_CHROOT, CAP_SETFCAP};

const char CAPS_STR_LIST[CAPS_NUM][CAP_STR_SIZE_MAX] = 
    {"CAP_SETPCAP", "CAP_MKNOD", "CAP_AUDIT_WRITE",
     "CAP_CHOWN", "CAP_NET_RAW", "CAP_DAC_OVERRIDE",
     "CAP_FOWNER", "CAP_FSETID", "CAP_KILL",
     "CAP_SETGID", "CAP_SETUID", "CAP_NET_BIND_SERVICE",
     "CAP_SYS_CHROOT", "CAP_SETFCAP"};

void update_needed_cap() {
    capng_clear(CAPNG_SELECT_BOTH);
    capng_updatev(CAPNG_ADD, CAPNG_EFFECTIVE | CAPNG_PERMITTED | CAPNG_BOUNDING_SET,
                  CAPS, -1);
    capng_apply(CAPNG_SELECT_BOTH);
}

void check_needed_cap() {
	int counter = 0;
    for(int i = 0; i < CAPS_NUM; i++) {
        if (capng_have_capability(CAPNG_EFFECTIVE, CAPS_LIST[i]))
			counter++;
            //logger("info", 0, "%-32s\t[√]", CAPS_STR_LIST[i]);
        else
            logger("warning", 0, "%-32s\t[x]", CAPS_STR_LIST[i]);
    }
	if(counter == CAPS_NUM)
		logger("info", 0, "%-32s\t[√]", "set capbilities");
	else
		logger("info", 0, "%-32s\t[x]", "set capbilities");

}

/*
 * For set the seccomp
 */ 
void set_seccomp() {
    scmp_filter_ctx ctx;

    ctx = seccomp_init(SCMP_ACT_KILL);
    if (ctx == NULL)
            goto out;
            
    // line 54 names
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(accept), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(accept4), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(access), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(adjtimex), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(alarm), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(bind), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(capget), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(capset), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chdir), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chmod), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chown), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chown32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_getres), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_getres_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_gettime), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_gettime64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_nanosleep), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_nanosleep_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(close), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(connect), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(copy_file_range), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(creat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup3), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_create), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_create1), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_ctl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_ctl_old), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_pwait), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_wait), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_wait_old), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(eventfd), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(eventfd2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execve), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execveat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(faccessat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fadvise64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fadvise64_64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fallocate), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fanotify_mark), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fchdir), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fchmod), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fchmodat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fchown), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fchown32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fchownat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fdatasync), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fgetxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(flistxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(flock), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fork), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fremovexattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fsetxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstatat64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstatfs), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstatfs64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fsync), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ftruncate), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ftruncate64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futimesat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getcpu), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getcwd), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getegid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getegid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(geteuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(geteuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getgid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getgroups), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getgroups32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getitimer), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpeername), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpgrp), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getppid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpriority), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getrandom), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getresgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getresgid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getresuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getresuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getrlimit), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(get_robust_list), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getrusage), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getsid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getsockname), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getsockopt), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(get_thread_area), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(gettid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(gettimeofday), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(inotify_add_watch), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(inotify_init), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(inotify_init1), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(inotify_rm_watch), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_cancel), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioctl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_destroy), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_getevents), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_pgetevents), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_pgetevents_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioprio_get), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioprio_set), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_setup), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_submit), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_uring_enter), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_uring_register), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(io_uring_setup), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ipc), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(kill), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lchown), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lchown32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lgetxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(link), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(linkat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(listen), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(listxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(llistxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(_llseek), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lremovexattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lseek), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lsetxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lstat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lstat64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(madvise), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(memfd_create), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mincore), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mkdir), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mkdirat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mknod), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mknodat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mlock), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mlock2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mlockall), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_getsetattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_notify), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_open), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_timedreceive), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_timedreceive_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_timedsend), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_timedsend_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mq_unlink), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mremap), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(msgctl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(msgget), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(msgrcv), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(msgsnd), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(msync), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munlock), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munlockall), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(nanosleep), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(newfstatat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(_newselect), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pause), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pipe), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pipe2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(poll), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ppoll), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ppoll_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(prctl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pread64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(preadv), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(preadv2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(prlimit64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pselect6), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pselect6_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwrite64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwritev), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwritev2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readahead), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlink), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlinkat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readv), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recv), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvfrom), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvmmsg), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvmmsg_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvmsg), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(remap_file_pages), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(removexattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rename), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(renameat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(renameat2), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(restart_syscall), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rmdir), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigaction), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigpending), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigprocmask), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigqueueinfo), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigsuspend), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigtimedwait), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigtimedwait_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_tgsigqueueinfo), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_getaffinity), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_getattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_getparam), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_get_priority_max), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_get_priority_min), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_getscheduler), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_rr_get_interval), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_rr_get_interval_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_setaffinity), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_setattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_setparam), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_setscheduler), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sched_yield), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(seccomp), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(select), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(semctl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(semget), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(semop), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(semtimedop), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(semtimedop_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(send), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendfile), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendfile64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendmmsg), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendmsg), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendto), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setfsgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setfsgid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setfsuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setfsuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setgid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setgroups), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setgroups32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setitimer), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setpgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setpriority), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setregid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setregid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setresgid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setresgid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setresuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setresuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setreuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setreuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setrlimit), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_robust_list), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setsid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setsockopt), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_thread_area), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_tid_address), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setuid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setuid32), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setxattr), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(shmat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(shmctl), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(shmdt), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(shmget), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(shutdown), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigaltstack), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(signalfd), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(signalfd4), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigprocmask), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigreturn), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socketcall), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socketpair), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(splice), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(stat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(stat64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(statfs), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(statfs64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(statx), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(symlink), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(symlinkat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sync), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sync_file_range), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(syncfs), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sysinfo), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(tee), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(tgkill), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(time), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_create), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_delete), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_getoverrun), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_gettime), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_gettime64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_settime), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timer_settime64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timerfd_create), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timerfd_gettime), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timerfd_gettime64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timerfd_settime), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(timerfd_settime64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(times), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(tkill), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(truncate), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(truncate64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ugetrlimit), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(umask), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(uname), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(unlink), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(unlinkat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(utime), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(utimensat), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(utimensat_time64), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(utimes), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(vfork), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(vmsplice), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(wait4), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(waitid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(waitpid), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0)) goto out;
	if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev), 0)) goto out;

    // arch_prctl
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(arch_prctl), 0);
    // modify_ldt
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(modify_ldt), 0);

    // line 586 names
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(bpf), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clone), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fanotify_init), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lookup_dcookie), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mount), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(name_to_handle_at), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(perf_event_open), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(quotactl), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setdomainname), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sethostname), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setns), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(syslog), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(umount), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(umount2), 0)) goto out;
    if(seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(unshare), 0)) goto out;

    seccomp_load(ctx);
    seccomp_release(ctx);
    logger("info", 0, "%-32s\t[√]", "set seccmop");
    return;
out:
    seccomp_release(ctx);
    logger("error", 255, "%-32s\t[x]", "set seccmop");
}

/*
 * logger:
 * 	When type is "error" and exit_code isn't 0, the logger will invoke exit(exit_code)
 * 	otherwise, no exitting will occur.
 */ 
void logger(const char *type, int exit_code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[container][%s]\t", type);
    vfprintf(stderr, format, args);
	if(strcmp(type, "error") == 0) {
		perror(" ");
		if(exit_code != 0)
			exit(exit_code);
	}
	else fprintf(stderr, "\n");
    va_end(args);
}