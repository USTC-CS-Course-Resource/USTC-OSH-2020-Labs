# The Report for Lab1
PB18111697 王章瀚  

## 关于 Git 与 GitHub 仓库

### 仓库建立与邀请

于2020年3月1日创建私有仓库[OSH-2020-Labs](https://github.com/RabbitWhite1/OSH-2020-Labs), 并于次日邀请助教作为collaborator.  

### 实验过程管理

1. 每当做出一定量的修改时, 就用git提交.  
2. 设置好`.gitignore`文件以避免不必要文件的提交.  

## 关于 Linux 内核

### 内核初次编译

第一次编译内核时, 严格按照助教给出文档步骤进行, 最终经过约10分钟, 编译出一个8MB左右的Linux内核, 并在QEMU环境下运行.  

#### 遇到问题

运行时, 发现最后会出现`Kernel Panic`, 其原因是没有加载初始内存盘.  
由于第一次学习使用相关东西, 以为是出现了其他问题, 故在此花了不少时间.  

### 内核裁剪

- 在进行了一些摸索之后, 通过裁剪内核, 最终将内核裁剪为一个3.4MB左右的内核, 并且可以正常运行所有助教给出的程序. 该内核对应的`.config`文件在仓库目录下的`./lab1/linux/.config`.  
其中的裁剪主要根据是将对本实验无用的部分exclude. 对本实验无用的部分包括`Networking support`, `Kernel hacking`, 以及各种不必须的设备等.  
最后保留的几乎只剩下`64-bit kernel`中的各项及`Device Drivers`中可能需要的设备.  
- 为了执行助教给出的第三个程序, 内核中有一项是必不可少的: 即`Device Drivers ---> Graphics support ---> Frame buffer Devices ---> Support for frame buffer devieces ---> VESA VGA graphics support`, 故将其设置为`built-in`. 

## 关于 `initrd` 与 `init` 程序

### 初次测试

#### 编译`init`程序

首先按照助教给出的手册, 编译了`Hello Linux`的程序, 其中执行的gcc指令如下:  

```shell
gcc -static init.c -o init
```

此处使用的是**静态链接**, 其原因将在[思考题](#jumpthink)中的[3. Static Linkage](#jumpstatic)中阐述.  

#### 构建初始`initrd`内存盘

在`init`文件的目录下执行以下指令,  

```shell
find . | cpio --quiet -H newc -o | gzip -9 -n > ../initrd.cpio.gz
```

该指令的主要含义是: 通过`find`将目录下所有文件找出, 用管道输给`cpio`; 而后`cpio`用`newc`格式(经常用于制作`RamDisk`)生成归档包; 再传给`gzip`命令, 将归档包以最高压缩效率`-9`压缩; 并最后重定向到该文件夹的父文件夹中的`initrd.cpio.gz`文件中. 如此就完成了对`initrd`的构建.  

#### 测试程序

在执行如下命令:  

```shell
qemu-system-x86_64 -kernel bzImage -initrd initrd.cpio.gz
```

使用`qemu`来运行编译出的内核, 并载入初始内存盘. 此时又出现了`Kernel Panic`, 不同的是, 这次是这样的:  

```shell
[    1.304828] ---[ end Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000000 ]---
```

字面上理解, 这个指示的是正在尝试杀死`init`, 退出码为`0x00000000`. 经过实验, 发现这个退出码正是我们init程序的返回值, 因此推测这个panic很可能是由于程序的结束返回而造成此时无程序可进行.  
更详细的解释说明将在[思考题](#jumpthink)中的[2. Kernel Panic](#jumpkernelpanic)给出.  

### 对提供的三个程序的测试

有了前面的测试基础, 后面三个程序就比较容易测试了.  
助教提供了三个静态链接的程序, 功能分别是:  

- 程序 1: 调用 Linux 下的系统调用，每隔 1 秒显示一行信息。显示五次  
- 程序 2: 向串口输出一条信息。此程序依赖 /dev/ttyS0 设备文件  
- 程序 3: 在 TTY 中使用 Framebuffer 显示一张图片。需要 800x600 32ppm 的 VGA 设置。此程序依赖 /dev/fb0 设备文件  

实验要求编写一个`init`程序, 将这三个程序依次运行.  

#### 建立设备文件

由于使用的是自己笔记本电脑上双系统下运行的`Ubunt18`, 不存在不支持直接使用`mknod`的问题. 故在一个文件夹内直接创建了所需的串口设备文件和Framebuffer设备文件, 命令如下:  

```shell
mkdir dev
mknod dev/ttyS0 c 4 64
mknod dev/fb0 c 29 0
```

#### 编写 `init` 文件

该`init`文件的主要思路是, 调用`fork()`函数依次为三个程序`1`, `2`, `3`分配子进程, 然后使用`execvp()`函数来执行. 此外, 为了防止`init`程序退出引发内核恐慌, 加入了`while(1)`语句.  
该程序展示如下:
```c
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

#define MAX_SIZE 50

int main() {
    pid_t pid;
    int argc = 1;
    char** arg = (char**)calloc(sizeof(char*), argc+1);
    for(int i = 0; i <= argc; i++) {
        arg[i] = (char*)calloc(sizeof(char), MAX_SIZE);
    }
    arg[argc+1] = NULL; // the end mark of arg

    for(int i = 1; i <= 3; i++) {
        sprintf(arg[0], "./%d", i); // set the arg

        pid = fork(); // fork
        
        if(pid < 0) {
            fprintf(stderr, "Fork Failed\n");
            return 1;
        }
        else if(pid == 0) {
            execvp(arg[0], arg); // run the programe
        }
        else {
            wait(NULL); // wait the program
        }
    }
    
    while(1);
    return 0;
}
```
将`init.c`编译出的`init`文件放入该文件夹, 将该文件夹下的内容做成`initrd`. 该文件放在了仓库目录下的`./lab1/linux/initrd.cpio.gz`.  

#### 测试

输入以下命令(其中console=ttyS0是为了测试串口方便):  

```shell
`qemu-system-x86_64 -kernel bzImage -initrd initrd.cpio.gz -append 'vga=0x343 console=ttyS0' -serial stdio`
```

输出结果如下:  
```
#######################
# Welcome to OSH-2020 #
#######################
Message 1 / 5
Message 2 / 5
Message 3 / 5
Message 4 / 5
Message 5 / 5
hello serial port
The framebuffer device was opened successfully.
800x600, 32bpp
The framebuffer device was mapped to memory successfully.
Buffer size: 1920000
vinfo.xoffset = 0, vinfo.yoffset = 0, finfo.line_length = 3200
The image 'The KDE dragons', created by Tyson Tan, is under CC-BY-SA.
```

此外还显示了一张一群恐龙的图片.  
由于有程序的控制, 这个`initrd`最后并不会引起内核恐慌.  

#### 使用`BusyBox`构建`initrd`

除此之外, 我还用了`BusyBox`构建了一个`initrd`.  
其中主要思路是: 用BusyBox调出终端, 通过修改`/etc/init.d/rcS`来依次执行三个文件.  
这种方法下, 由于仍有终端在运行, 因此不会有程序退出而造成内核恐慌的问题.  
用该方法构建的`initrd`位于仓库目录下的`./lab1/linux/initrd_withshell.cpio.gz`.  
最终运行结果和前述方法的结果相似.  

## 关于 x86 裸金属 MBR 程序

### 程序编写

总体思路是对`[0x0046c]`的计时器跳转进行计数, 当跳转了18次则表示大约过了1秒, 这个过程由`.count`标签指示的部分控制; 每过1秒时, 则将`OSH`标签地址上的内容打印到屏幕上, 由`.print_str`部分调用BIOS的中断过程进行打印.  
另外, 为了做清屏操作, 需要有这样四句调用BIOS中断来完成.  

```x86
 mov ah, 0x0F
int 0x10
mov ah, 0x00
int 0x10   
```

由于有ICS课程的对LC-3的汇编基础, 整个过程并不算难. 整个程序如下:

```x86
[BITS 16]                                   ; 16 bits program
[ORG 0x7C00]                                ; starts from 0x7c00, where MBR lies in memory

main:
    mov ah, 0x0F
    int 0x10
    mov ah, 0x00
    int 0x10                                ; clear the screen
    sti
    .print_loop:                            ; the loop for print string
        mov ah, [0x046c]
        and ecx, 0x0000
        .count:                             ; count to 18, which is about 1 second
            mov al, ah
            .compare:                       ; compare the al and [0x046c]
                mov ah, [0x046c]
                cmp al, ah
                je .compare
            add ecx, 1
            cmp ecx, 18
            jl .count

        mov si, OSH                         ; si points to string OSH
        .print_str:                   ; print the string
            lodsb                           ; load char to al
            cmp al, 0                       ; is it the end of the string?
            je .print_loop                  ; if true, then halt the system
            mov ah, 0x0e                    ; if false, then set AH = 0x0e 
            int 0x10                        ; call BIOS interrupt procedure, print a char to screen
            jmp .print_str            ; loop over to print all chars


    .hlt:
        hlt

OSH db 'Hello, OSH 2020 Lab1!', 13, 10, 0   ; our string, null-terminated

TIMES 510 - ($ - $$) db 0                   ; the size of MBR is 512 bytes, fill remaining bytes to 0
DW 0xAA55                                   ; magic number, mark it as a valid bootloader to BIOS
```

该程序完成了首先进行清屏, 然后每隔一秒左右(并不是精确的一秒), 输出一次'Hello, OSH 2020 Lab1!'.  

## <span id="jumpthink">思考题</span>

挑选了以下四个思考题作回答.

### <span id="jumpkernelpanic">2. Kernel Panic</span>


> 2. 在「构建 initrd」的教程中我们创建了一个示例的 init 程序。为什么屏幕上输出 "Hello, Linux!" 之后，Linux 内核就立刻 kernel panic 了?  

首先, 要知道`kernel panic`是什么?  
- 内核错误(Kernel panic)是指操作系统在监测到内部的致命错误, 并无法安全处理此错误时采取的动作.  

`initrd`又是什么?  
- Linux初始RAM磁盘（initrd）是在系统引导过程中挂载的一个临时根文件系统  

这个`initrd`通常用来引导系统或作为最终系统. 当它用于引导系统的时候, 内核利用它来加载模块, 然后进入真实系统中, 此时`initrd`就被卸载释放. 这种情况下, 由于引导到了真实系统, 因此并不会造成内核错误.  
然而, 我们的实验中, 可以认为是将`initrd`作为一个最终系统来使用的. 这时, 如果不能维持程序运行, 自然会导致`kernel panic`.  


### <span id="jumpstatic">3. Static Linkage</span>

> 3. 为什么我们编写 C 语言版本的 init 程序在编译时需要静态链接? 我们能够在这里使用动态链接吗?

为了理解这个问题, 首先确认了一下静态链接和动态链接的定义.
- 静态链接: 静态链接是由链接器在链接时将库的内容加入到可执行程序中的做法
- 动态链接: 不对那些组成程序的目标文件进行链接，等到程序要运行时才进行链接。

当我们编译出来的内核在执行`init`的时候, 需要的是所有的代码内容. 如果采用动态链接, 那么此时将无法找到例如`printf()`函数等的对应代码, 因此将会出现无法运行以至于有`kernel panic`的情况.  
但如果采用静态链接, 由于所有需要的内容都放入了可执行程序中, 因此整个`init`程序可以正常执行.  

### 5. BusyBox

> 5. 在介绍 BusyBox 的一节，我们发现 init 程序可以是一段第一行是 #!/bin/sh 的 shell 脚本。尝试解释为什么它可以作为系统第一个启动的程序，并且说明这样做需要什么条件。  

BusyBox将许多常用的UNIX命令行工具打包为一个项目.

### 8. BIOS / UEFI
> 8. 目前, 越来越多的 PC 使用 UEFI 启动. 请简述使用 UEFI 时系统启动的流程, 并与传统 BIOS 的启动流程比较.  

#### BIOS 和 UEFI 是什么

- BIOS: 即`Basic Input Output System`. 它是一组固化到计算机内主板上一个ROM芯片上的程序，它保存着计算机最重要的基本输入输出的程序、开机后自检程序和系统自启动程序，它可从CMOS中读写系统设置的具体信息.
- UEFI: 即`统一可扩展固件接口(Unified Extensible Firmware Interface)`. 它用来定义操作系统与系统固件之间的软件界面，作为BIOS的替代方案.

#### 启动流程

- BIOS: 当计算机启动时, BIOS会被加载, 它负责唤醒计算机的硬件组件, 确保它们能够正常工作; 否则将会用一序列蜂鸣声指示error message. 然后它会运行bootloader, 以启动操作系统, 这一步中, BIOS会去寻找主引导记录(MBR), 以启动bootloader.  
- UEFI: UEFI在一个.efi文件中存储了所有的初始化和启动信息, 而这个文件存储在了一个叫EFI系统分区(ESP)的特殊分区. 这个ESP分区也会有`boot loader`程序来启动安装在这台计算机中的系统. 也就是说, 相比于BIOS, 它能够直接地引导操作系统, 这正是它优于BIOS的一个重要原因.  

#### References
[What Is UEFI, and How Is It Different from BIOS?](https://www.howtogeek.com/56958/HTG-EXPLAINS-HOW-UEFI-WILL-REPLACE-THE-BIOS/)  
[UEFI boot: how does that actually work, then?](https://www.happyassassin.net/2014/01/25/uefi-boot-how-does-that-actually-work-then/)  
[UEFI vs. BIOS – What’s the Differences and Which One Is Better ](https://www.partitionwizard.com/partitionmagic/uefi-vs-bios.html)  
