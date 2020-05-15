## `cgroup`配置

除了统一都要向`cgroup.procs`追加当前容器的pid外, 还有以下配置:

### memory 部分

1. 用户态内存: 将`/sys/fs/cgroup/memory/lab4/memory.limit_in_bytes`内容配置为`67108864`
2. 内核态内存: 将`/sys/fs/cgroup/memory/lab4/memory.kmem.limit_in_bytes`内容配置为`67108864`
3. 禁用交换: 将`/sys/fs/cgroup/memory/lab4/memory.swappiness`内容配置为`0`

### cpu 部分

1. cpu.shares: 将`/sys/fs/cgroup/cpu,cpuacct/lab4/cpu.shares`配置为`256`

### pids 部分

1. cpu.shares: 将`/sys/fs/cgroup/pids/lab4/pids.max`配置为`256`

### blkio 部分

1. cpu.shares: 将`/sys/fs/cgroup/blkio/lab4/blkio.weight`配置为`50`
