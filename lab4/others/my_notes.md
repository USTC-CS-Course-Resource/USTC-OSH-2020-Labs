# Lab4踩坑记录(by Rabbit)

yysy, 这个实验建议在虚拟机上完成. 本人已经历重装Ubuntu...

## 1. 构建容器的根文件系统（rootfs）

### `chroot`和`systemd-nspawn`的行为差别测试

||chroot|systemd-nspawn|
|:-|:-|:-|
|mount|mount: failed to read mtab: No such file or directory|输出相应mount信息|
|ls /dev|无任何输出|(输出了设备文件名)<br>console  core  fd  full  net  null  ptmx  pts  random  shm  stderr  stdin  stdout  tty  urandom  zero|
|dd if=/dev/sda of=test bs=64k count=1|dd: failed to open '/dev/sda': No such file or directory|dd: failed to open '/dev/sda': No such file or directory|
|dd if=/dev/zero of=test bs=64k count=1|dd: failed to open '/dev/sda': No such file or directory|1+0 records in<br>1+0 records out<br>65536 bytes (66 kB, 64 KiB) copied, 0.000107553 s, 609 MB/s|
|echo $$|8800|1|
|reboot|System has not been booted with systemd as init system (PID 1). Can't operate. <br>Failed to talk to init daemon.|System has not been booted with systemd as init system (PID 1). Can't operate.<br>Failed to talk to init daemon.|

#### 查看`mountinfo`时

`chroot`没有这个文件, 需要自己去挂载proc等.

`systemd-nspawn`则自动有了这个文件, 并且输出结果中的`mount ID`与宿主系统的结果不同, 说明完成了隔离.

这个文件各字段含义见[proc(5)](http://man7.org/linux/man-pages/man5/proc.5.html)中的`/proc/[pid]/mountinfo (since Linux 2.6.26)`部分

```bash
root@rootfs:/# cat /proc/self/mountinfo
905 854 8:1 /home/rabbit/OSH-2020-Labs/lab4/rootfs / rw,relatime shared:434 master:1 - ext4 /dev/sda1 rw,errors=remount-ro
906 905 0:60 / /tmp rw,nosuid,nodev shared:435 - tmpfs tmpfs rw
907 905 0:22 / /sys ro,nosuid,nodev,noexec,relatime shared:436 - sysfs sysfs rw
908 905 0:62 / /dev rw,nosuid shared:437 - tmpfs tmpfs rw,mode=755
...
584 851 0:64 /proc-sys-kernel-random-boot-id//deleted /proc/sys/kernel/random/boot_id ro,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
585 916 0:64 /proc-sys-kernel-random-boot-id//deleted /proc/sys/kernel/random/boot_id rw,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
586 916 0:64 /kmsg//deleted /proc/kmsg rw,nosuid,nodev shared:439 - tmpfs tmpfs rw,mode=755
```

[systemd-nspawn的一些介绍](https://linux.cn/article-4678-1.html)

## 2. 使用 clone(2) 代替 fork(2)，并隔离命名空间

## 3. 在容器中使用 mount(2) 与 mknod(2) 挂载必要的文件系统结构

### 设置了CLONE_NEWNS, 退出后mount查看还在？

在宿主机用以下命令查看`/home`挂载是否为`shared`的.
```bash
cat /proc/self/mountinfo | grep "/ /home"
```

使用如下代码, 将根目录递归挂载为私有:
```c
if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 1)
    errexit("[error] mount-MS_PRIVATE\n");
```

> 注: 似乎助教也不是这样做的, 不建议用此方法.

[参考](https://bugzilla.redhat.com/show_bug.cgi?id=830427)

## 3. 使用 pivot_root(2) 替代 chroot(2) 完成容器内根文件系统的切换

### `syscall`的使用

[syscall文档](http://man7.org/linux/man-pages/man2/syscall.2.html)

#### 函数介绍

`syscall`是用于调用没有C封装的汇编语言接口的. 需要包含头文件`<sys/syscall.h>`

#### 使用方法

##### 宏定义

文档明确指出, 需要宏定义`_DEFAULT_SOURCE`(glibc 2.19之后, 之前有另外的宏定义).

##### `x86_64`的传参方式

|Arch/ABI|Instruction|System call #|Ret val|Ret val2|Error|Notes|
|:-|:-|:-|:-|:-|:-|:-|
|x86-64|syscall|rax|rax|rdx|-|5

|Arch/ABI|arg1|arg2|arg3|arg4|arg5|arg6|arg7|Notes|
|:-|:-|:-|:-|:-|:-|:-|:-|:-|
|x86-64|rdi|rsi|rdx|r10|r8|r9|-|

#### 示例代码

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

### `pivot_root`的使用

#### 函数说明

```c
int pivot_root(const char *new_root, const char *put_old);
```

这个函数将原有根文件系统移到`put_old`, 并将根文件系统`pivot`到`new_root`.  
其中, `put_old`必须为`new_root`的子孙文件夹.

#### 函数示例

见[pivot_root文档](http://man7.org/linux/man-pages/man2/pivot_root.2.html)末尾

### 步骤`从主机上隐藏容器的根文件系统`中, 主机对应文件夹并无挂载容器根文件系统

可能在子进程中递归私有化了宿主机根文件系统的挂载点, 比如

```c
// recursively remount / as private
if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 1)
    errexit("[error] mount-MS_PRIVATE");
```

这种情况下, 在父进程(主机上)也就自然没有子进程在`/tmp/lab4-tkytql`上挂载的容器根文件系统的挂载信息(因为隔离了挂载信息), 即不会传播到主机. 因此只需要使用`rmdir`删除`/tmp/lab4-tkytql`即可(这一点我问过助教了)

## 4. 使用 libcap 为容器缩减不必要的能力（capabilities）

### libcap-ng文档的主要信息提取

> 既然这个简单当然使用这个啦:grin:

[libcap-ng主页](https://people.redhat.com/sgrubb/libcap-ng/)
[libcap-ng的man列表](http://man7.org/linux/man-pages/dir_by_project.html#libcap-ng)

1. 可以用`netcap`查看网络能力, 用`pscap`查看各进程能力.
2. `#include <cap-ng.h>`
3. capability白名单列表: CAP_SETPCAP, CAP_MKNOD, CAP_AUDIT_WRITE, CAP_CHOWN, CAP_NET_RAW, CAP_DAC_OVERRIDE, CAP_FOWNER, CAP_FSETID, CAP_KILL, CAP_SETGID, CAP_SETUID, CAP_NET_BIND_SERVICE, CAP_SYS_CHROOT, CAP_SETFCAP

## 5. 使用 libseccomp 对容器中的系统调用进行白名单过滤

## 6. 使用 cgroup 限制容器中的 CPU、内存、进程数与 I/O 优先级

### 怎么测试`blkio`

[blkio介绍](https://blog.csdn.net/qq_39333816/article/details/103610545)
[可调参数说明(及整个cgroup的介绍)](https://access.redhat.com/documentation/zh-cn/red_hat_enterprise_linux/7/html/resource_management_guide/ch-subsystems_and_tunable_parameters)
[各类blkio调度算法介绍](https://www.cnblogs.com/conanwang/p/5858249.html)