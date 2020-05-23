# lab4

## 思考题的回答

### 1

#### 问题

> 用于限制进程能够进行的系统调用的 seccomp 模块实际使用的系统调用是哪个？用于控制进程能力的 capabilities 实际使用的系统调用是哪个？尝试说明为什么本文最上面认为「该系统调用非常复杂」。

#### 回答

> 根据`strace`的跟踪结果：
> 1. `seccomp 模块` 实际使用系统调用应为`seccomp(2)` 和 `prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)`(用以禁用特权) . 按照 man 文档的说明, 在 `SECCOMP_SET_MODE_FILTER` 模式下, 可以通过 `BPF` 来过滤任意系统调用.  
> 其**复杂之处**在于要构建BPF过滤器, 这涉及到十分复杂的BPF的指令及机器架构信息(从[seccomp的man手册](http://www.man7.org/linux/man-pages/man2/seccomp.2.html)中示例代码可见), 很难在一时间内完成.  
> 2. 控制能力的模块实际系统调用主要包括 `capget(2)`, `capset(2)` 以及 `prctl(2)` 以执行 `PR_CAPBSET_DROP` (丢弃能力)  
> 其**复杂之处**在于调用 `capget(2)` 和 `capset(2)` 的复杂位操作  

#### 参考资料

[seccomp(2)](http://www.man7.org/linux/man-pages/man2/seccomp.2.html)  

### 2

#### 问题

> 当你用 cgroup 限制了容器中的 CPU 与内存等资源后，容器中的所有进程都不能够超额使用资源，但是诸如 htop 等「任务管理器」类的工具仍然会显示主机上的全部 CPU 和内存（尽管无法使用）。查找资料，说明原因，尝试提出一种解决方案，使任务管理器一类的程序能够正确显示被限制后的可用 CPU 和内存（不要求实现）。  

#### 回答

> 为了解决这个问题, 我首先使用 `strace` 来跟踪诸如 `top`, `free` 等命令是如何获取包括内存, CPU等的信息的. 通过一番搜查, 我发现它们是读取了一些**特殊只读文件**来获取信息的.(查找资料后发现也是如此) 比如, 可以从 `/proc/stat`, `/proc/meminfo`等 中读取(并且经过检验, 在容器内, `/proc/meminfo` 等**依然为未限制之前的值**).   
> 那么问题就在于, 怎么才能让 `/proc` 中的信息正确呢? [查找资料的时候](https://time.geekbang.org/column/article/14653)发现, `LXCFS` 可能可以做到.  
> `LXCFS`是基于`FUSE`(FileSystem in Userspace)实现的一套文件系统. "`LXCFS`的FUSE实现会从容器对应的Cgroup中读取正确的内存限制. 从而使得应用获得正确的资源约束设定". 这就使得我们可以在容器中继续使用 `top` 等命令.  

#### 参考资料

[/proc/meminfo之谜](http://linuxperf.com/?p=142)  
[linux内存占用分析之meminfo](https://segmentfault.com/a/1190000022518282)  
[06 | 白话容器基础（二）：隔离与限制](https://time.geekbang.org/column/article/14653)  
[lxcfs 是什么？ 怎样通过 lxcfs 在容器内显示容器的 CPU、内存状态](https://www.lijiaocn.com/%E6%8A%80%E5%B7%A7/2019/01/09/kubernetes-lxcfs-docker-container.html)  


## 实现的简要描述

### clone 部分

这里只需要分配空间, 并调用 `clone` 即可. 由于后面 `pivot_root` 的需要, 传参给子进程时, 还传了管道的文件描述符.

### 命名空间隔离部分

命名空间隔离只需要在 `clone` 时填入对应参数即可, 如下

```c
clone(child, child_stack_start,
            SIGCHLD | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWCGROUP,
            &carg);
```

### 挂载必要节点

挂载必要节点的程序都在函数 `mount_needed()` 里. 挂载内容详见函数, 因为只是调用一下 `mount()` , 整体代码还是很清晰的.  
这里面的 `mountflags` 是按照主机的来填写的.

### pivot_root 部分

这一部分在pivot前对 `/` 做了递归重新私有式挂载. 所以在父进程(主机)可能不必重新卸载(因为这样就不会传播到主机上).  
但我还是写了一句用以卸载, 以防意外.

此外, 这一部分几乎都在函数 `int do_pivot(const char *tmpdir)` 里, 这个函数做的事大致有:
1. 递归重新私有地挂载 `/` 
2. 将当前目录 `./` (因为前面有chdir, 所以正好是容器根文件系统) `bind` 到 `tmpdir`上
3. 为 `oldroot` 创建文件夹(`mkdir`)
4. 调用 `pivot_root()` (已经用 `syscall` 实现)
5. 分离(`detach`)主机根文件系统, 并删除对应文件夹.

### 移除能力

这里用的是 `libcap-ng`, 直接调用相应函数 (`capng_clear()`, `capng_updatev`, `capng_apply`) 即可.

### 使用 SecComp 限制容器中能够进行的系统调用

类似于能力的处理, 依序调用 `seccomp_init()`, `seccomp_rule_add()`, `seccomp_load()`, `seccomp_release()`即可.

### cgroup 部分

关于正确隔离的部分, 采用的是在操作完后加一句
```c
unshare(CLONE_NEWCGROUP);
```
此外, 为防意外, 还写了另一种方法. 即, 将父进程的`pid`移入`lab4内的cgroup.procs`中, 再进行`clone`, 而后做好限制后, 再将父进程的`pid`移出.(在文件夹`cgroup_when_clone`里有对应实现的`main.c`)
除了统一都要向 `cgroup.procs` 追加当前容器的pid外, 还有以下配置:

#### memory 部分

1. 用户态内存: 将 `/sys/fs/cgroup/memory/lab4/memory.limit_in_bytes` 内容配置为 `67108864`
2. 内核态内存: 将 `/sys/fs/cgroup/memory/lab4/memory.kmem.limit_in_bytes` 内容配置为 `67108864` 
3. 禁用交换: 将 `/sys/fs/cgroup/memory/lab4/memory.swappiness` 内容配置为 `0`

#### cpu 部分

1. cpu.shares: 将 `/sys/fs/cgroup/cpu,cpuacct/lab4/cpu.shares` 配置为 `256`

#### pids 部分

1. cpu.shares: 将 `/sys/fs/cgroup/pids/lab4/pids.max` 配置为 `256`
