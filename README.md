# my_os
一个纯粹的玩具OS

TO-DO List：

- [x] 虚拟内存及内存管理
- [x] 内核级线程
- [x] 用户态进程
- [] 文件系统
- [] 交互Shell

`MBR`位于磁盘`LBA 0号扇区`开始的**1**个扇区内

`Boot Loader`位于磁盘`LBA 2号扇区`开始的**4**个扇区内

`Kernel`位于磁盘`LBA 9号扇区`开始的**200**个扇区内

![](./resource/内存布局.png)