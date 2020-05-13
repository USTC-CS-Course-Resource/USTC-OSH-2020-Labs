# Lab4

## `chroot`和`systemd-nspawn`的行为差别测试

||chroot|systemd-nspawn|
|:-|:-|:-|
|mount|mount: failed to read mtab: No such file or directory|输出相应mount信息|
|ls /dev|无任何输出|(输出了设备文件名)<br>console  core  fd  full  net  null  ptmx  pts  random  shm  stderr  stdin  stdout  tty  urandom  zero|
|dd if=/dev/sda of=test bs=64k count=1|dd: failed to open '/dev/sda': No such file or directory|dd: failed to open '/dev/sda': No such file or directory|
|dd if=/dev/zero of=test bs=64k count=1|dd: failed to open '/dev/sda': No such file or directory|1+0 records in<br>1+0 records out<br>65536 bytes (66 kB, 64 KiB) copied, 0.000107553 s, 609 MB/s|
|echo $$|8800|1|
|reboot|System has not been booted with systemd as init system (PID 1). Can't operate. <br>Failed to talk to init daemon.|System has not been booted with systemd as init system (PID 1). Can't operate.<br>Failed to talk to init daemon.|

### 查看`mountinfo`时

`chroot`没有这个文件, 需要自己去挂载proc等.

`systemd-nspawn`则自动有了这个文件, 并且输出结果中的`mount ID`与宿主系统的结果不同, 说明完成了隔离.

这个文件各字段含义见[proc(5)](http://man7.org/linux/man-pages/man5/proc.5.html)中的`/proc/[pid]/mountinfo (since Linux 2.6.26)`部分

```bash
root@rootfs:/# cat /proc/self/mountinfo
905 854 8:1 /home/rabbit/OSH-2020-Labs/lab4/rootfs / rw,relatime shared:434 master:1 - ext4 /dev/sda1 rw,errors=remount-ro
906 905 0:60 / /tmp rw,nosuid,nodev shared:435 - tmpfs tmpfs rw
907 905 0:22 / /sys ro,nosuid,nodev,noexec,relatime shared:436 - sysfs sysfs rw
908 905 0:62 / /dev rw,nosuid shared:437 - tmpfs tmpfs rw,mode=755
909 908 0:63 / /dev/shm rw,nosuid,nodev shared:438 - tmpfs tmpfs rw
910 908 0:65 / /dev/pts rw,nosuid,noexec,relatime shared:440 - devpts devpts rw,gid=5,mode=620,ptmxmode=666
911 908 0:23 /1 /dev/console rw,nosuid,noexec,relatime shared:441 master:3 - devpts devpts rw,gid=5,mode=620,ptmxmode=000
912 905 0:64 / /run rw,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
913 912 0:24 /systemd/nspawn/propagate/rootfs /run/systemd/nspawn/incoming ro,relatime master:5 - tmpfs tmpfs rw,size=802664k,mode=755
916 905 0:67 / /proc rw,nosuid,nodev,noexec,relatime shared:442 - proc proc rw
851 916 0:67 /sys /proc/sys ro,nosuid,nodev,noexec,relatime shared:442 - proc proc rw
580 916 0:67 /sysrq-trigger /proc/sysrq-trigger ro,nosuid,nodev,noexec,relatime shared:442 - proc proc rw
567 907 0:68 / /sys/fs/cgroup ro,nosuid,nodev,noexec shared:443 - tmpfs tmpfs ro,mode=755
569 567 0:36 / /sys/fs/cgroup/cpuset ro,nosuid,nodev,noexec,relatime shared:444 - cgroup cgroup rw,cpuset
570 567 0:33 / /sys/fs/cgroup/memory ro,nosuid,nodev,noexec,relatime shared:445 - cgroup cgroup rw,memory
572 567 0:31 / /sys/fs/cgroup/freezer ro,nosuid,nodev,noexec,relatime shared:446 - cgroup cgroup rw,freezer
573 567 0:38 / /sys/fs/cgroup/devices ro,nosuid,nodev,noexec,relatime shared:447 - cgroup cgroup rw,devices
574 567 0:39 / /sys/fs/cgroup/perf_event ro,nosuid,nodev,noexec,relatime shared:448 - cgroup cgroup rw,perf_event
575 567 0:41 / /sys/fs/cgroup/rdma ro,nosuid,nodev,noexec,relatime shared:449 - cgroup cgroup rw,rdma
576 567 0:35 / /sys/fs/cgroup/hugetlb ro,nosuid,nodev,noexec,relatime shared:450 - cgroup cgroup rw,hugetlb
577 567 0:32 / /sys/fs/cgroup/net_cls,net_prio ro,nosuid,nodev,noexec,relatime shared:451 - cgroup cgroup rw,net_cls,net_prio
578 567 0:34 / /sys/fs/cgroup/cpu,cpuacct ro,nosuid,nodev,noexec,relatime shared:452 - cgroup cgroup rw,cpu,cpuacct
579 567 0:40 / /sys/fs/cgroup/pids ro,nosuid,nodev,noexec,relatime shared:453 - cgroup cgroup rw,pids
581 567 0:37 / /sys/fs/cgroup/blkio ro,nosuid,nodev,noexec,relatime shared:454 - cgroup cgroup rw,blkio
582 567 0:28 / /sys/fs/cgroup/unified rw,nosuid,nodev,noexec,relatime shared:455 - cgroup2 cgroup rw,nsdelegate
583 567 0:29 / /sys/fs/cgroup/systemd rw,nosuid,nodev,noexec,relatime shared:456 - cgroup cgroup rw,xattr,name=systemd
584 851 0:64 /proc-sys-kernel-random-boot-id//deleted /proc/sys/kernel/random/boot_id ro,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
585 916 0:64 /proc-sys-kernel-random-boot-id//deleted /proc/sys/kernel/random/boot_id rw,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
586 916 0:64 /kmsg//deleted /proc/kmsg rw,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
```

[systemd-nspawn的一些介绍](https://linux.cn/article-4678-1.html)

## `syscall`的使用

[syscall文档](http://man7.org/linux/man-pages/man2/syscall.2.html)

### 函数介绍

`syscall`是用于调用没有C封装的汇编语言接口的. 需要包含头文件`<sys/syscall.h>`

### 使用方法

#### 宏定义

文档明确指出, 需要宏定义`_DEFAULT_SOURCE`(glibc 2.19之后, 之前有另外的宏定义).

#### `x86_64`的传参方式

|Arch/ABI|Instruction|System call #|Ret val|Ret val2|Error|Notes|
|:-|:-|:-|:-|:-|:-|:-|
|x86-64|syscall|rax|rax|rdx|-|5

|Arch/ABI|arg1|arg2|arg3|arg4|arg5|arg6|arg7|Notes|
|:-|:-|:-|:-|:-|:-|:-|:-|:-|
|x86-64|rdi|rsi|rdx|r10|r8|r9|-|

### 示例代码

```c
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <signal.h>

int
main(int argc, char *argv[])
{
    pid_t tid;

    tid = syscall(SYS_gettid);
    syscall(SYS_tgkill, getpid(), tid, SIGHUP);
}
```

