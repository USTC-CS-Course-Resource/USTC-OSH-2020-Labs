# lab4

##  TODO

### 1
助教说: 
> 在容器中使用 mount(2) 与 mknod(2) 挂载必要的文件系统结构
但是没有看到对 `mknod` 的相关详细要求.

### 2
`/tmp` 的挂载只是简单挂载一个 `tmpfs` 吗？

### 3
seccomp 这步是直接每个系统调用写一条 `add_rule` 吗？

### 4
`blkio`的权重还不知道怎么分配, 网上说有 `blkio.weight` 这一项, 但我的系统中没有.

## 实现的简要描述

### clone 部分

这里只需要分配空间, 并调用 `clone` 即可. 由于后面 `pivot_root` 的需要, 传参给子进程时, 还传了管道的文件描述符.

### 命名空间隔离部分

命名空间隔离只需要在 `clone` 时填入对应参数即可, 如下

```c
lone(child, child_stack_start,
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

除了统一都要向 `cgroup.procs` 追加当前容器的pid外, 还有以下配置:

#### memory 部分

1. 用户态内存: 将 `/sys/fs/cgroup/memory/lab4/memory.limit_in_bytes` 内容配置为 `67108864`
2. 内核态内存: 将 `/sys/fs/cgroup/memory/lab4/memory.kmem.limit_in_bytes` 内容配置为 `67108864` 
3. 禁用交换: 将 `/sys/fs/cgroup/memory/lab4/memory.swappiness` 内容配置为 `0`

#### cpu 部分

1. cpu.shares: 将 `/sys/fs/cgroup/cpu,cpuacct/lab4/cpu.shares` 配置为 `256`

#### pids 部分

1. cpu.shares: 将 `/sys/fs/cgroup/pids/lab4/pids.max` 配置为 `256`

#### blkio 部分

1. cpu.shares: 将 `/sys/fs/cgroup/blkio/lab4/blkio.weight` 配置为 `50`
