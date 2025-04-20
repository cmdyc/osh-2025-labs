# lab2 report

## 一、目录导航

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-9.png)

**选做项目：**

`cd` 在没有第二个参数时，默认进入家目录

`cd - `可以切换为上一次所在的目录

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-7.png)

## 二、管道

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-1.png)

## 三、重定向

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-2.png)

## 四、信号处理

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-3.png)

## 五、前后台进程

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-8.png)

为了显示的表现出后台进程是否结束运行，`wait`命令会在进程结束后汇报运行状况
本次实验的实现与linux内置终端有所不同，通过将子进程组的输入输出重定向到 /dev/null，避免后台进程与终端交互，防止其干扰前台终端的正常使用

## 六、可选扩展功能

### 处理`CTRL-D`按键

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-5.png)

### 处理`echo $SHELL`命令

**运行结果**

![*](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab2-6.png)

   


