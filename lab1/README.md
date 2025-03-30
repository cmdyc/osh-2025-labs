# Lab1

## 选做

> 在init.c中将while (1) {}删去之后会使编译的内存盘 kernel panic，请你查阅资料并在 lab1 README.md 中解释原因

原因分析：`init`将会作为第一个用户态进程被启动，成为所有后续进程的父进程。因此需要`init.c`一直运行下去，如果没有`while(1)`循环，那么`init.c`很快就会执行完，后续进程就找不到父进程，内存盘就会陷入`kernel panic`