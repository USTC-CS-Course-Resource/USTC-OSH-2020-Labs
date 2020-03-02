# troubles

助教给出的命令是调用了`qemu`的图形界面的，然而这个图形界面中我找不到办法查看以输出的被冲掉的信息，因此无法确认整个任务是否完成。需要用下述命令来将输出转移到终端。
此外，程序名应为init

```bash
find . | cpio --quiet -H newc -o | gzip -9 -n > ../initrd.cpio.gz
cd ../../../
qemu-system-x86_64 -kernel linux-5.4.22/arch/x86_64/boot/bzImage -initrd ramdisk/test3/initrd.cpio.gz -nographic -append "root=/dev/sda console=ttyS0" 
```

qemu-system-x86_64 -kernel bzImage -initrd initrd.cpio.gz -append 'vga=0x343' -serial stdio

为了支持FrameBuffer, 需要打开的有
[*]   Enable Video Mode Handling Helpers
[*]   Enable Tile Blitting Support 
<*>   VGA 16-color graphics support
[*]   EFI-based Framebuffer Support  