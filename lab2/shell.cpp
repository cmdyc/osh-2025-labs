// IO
#include <iostream>
// std::string
#include <string>
// std::vector
#include <vector>
// std::string 转 int
#include <sstream>
// PATH_MAX 等常量
#include <climits>
// POSIX API
#include <unistd.h>
// wait
#include <sys/wait.h>
// perror
#include <errno.h>
// open
#include <fcntl.h>

std::vector<std::string> split(std::string s, const std::string &delimiter);

int main() {
  // 不同步 iostream 和 cstdio 的 buffer
  std::ios::sync_with_stdio(false);

  // 用来存储读入的一行命令
  std::string cmd;

  while (true) {
    // 如果输入按下Ctr+D（识别为EOF）,退出shell程序
    if(std::cin.eof()) {
      std::cout << "^D\n";
      return 0;
    }
    // 打印提示符
    std::cout << "$ ";

    // 读入一行。std::getline 结果不包含换行符。
    std::getline(std::cin, cmd);

    // 按空格分割命令为单词
    std::vector<std::string> args = split(cmd, " ");

    // 按" | "分割命令为子命令
    std::vector<std::string> scomds = split(cmd, " | ");

    // 为创建管道而设置的数组
    int fd[scomds.size() - 1][2];

    // 没有可处理的命令
    if (args.empty()) {
      continue;
    }

    // 退出
    if (args[0] == "exit") {
      if (args.size() <= 1) {
        return 0;
      }

      // std::string 转 int
      std::stringstream code_stream(args[1]);
      int code = 0;
      code_stream >> code;  //?

      // 转换失败
      if (!code_stream.eof() || code_stream.fail()) {
        std::cout << "Invalid exit code\n";
        continue;
      }

      return code;
    }

    // 打印当前工作目录
    if (args[0] == "pwd") {
      if (args.size() <= 1) {
        char *cwd = getcwd(NULL, 0); // 自动分配内存
        if (cwd) {
          printf("Current directory: %s\n", cwd);
          free(cwd); // 手动释放内存
        } else {
          perror("getcwd() error"); // perror()是 C 语言标准库中的一个函数，主要用于将系统错误信息输出到标准错误流
        }
        continue;
      } else {
        std::cout << "Invalid pwd code\n";
        continue;
      }
    }

    // 更改当前工作目录为指定目录
    if (args[0] == "cd") {
      if (args.size() == 1) {
        if (chdir("/home") == -1) {
          perror("chdir() failed");
          continue;
        }
        continue;
      } else if (args.size() == 2) {
        if (args[1] == "-") {
          const char *oldpwd = getenv("OLDPWD");
          if (oldpwd == NULL) {
            std::cout << "OLDPWD not set\n";
            continue;
          }
          if (chdir(oldpwd) == -1) {
            perror("chdir() failed");
            continue;
          }
          continue;
        } else {
          // chdir()函数的参数为*char，因此需要转换格式
          if (chdir(args[1].c_str()) == -1) {
            perror("chdir() failed");
            continue;
          }
          continue;
        }
      } else {
        std::cout << "Invalid cd code\n";
        continue;
      }
    }

    // 处理外部命令

    // 用户在终端输入命令（如 ls）
    // Shell（父进程）调用 fork() 创建子进程
    // 子进程调用 execvp() 执行 ls 命令，父进程通过 wait() 等待子进程结束
    // 效果：用户可以在不关闭 Shell 的情况下运行其他程序

    // 父进程调用 fork() 后，操作系统会复制父进程的上下文（如内存、文件描述符等），生成子进程
    // 子进程从 fork() 的返回点开始执行，但两者的返回值不同

    // fork() 用于创建一个与父进程几乎完全相同的子进程
    // 调用后，父进程和子进程会并行执行后续代码
    // fork() 会给父进程和子进程均返回一个返回值
    // 父进程收到的的返回值为子进程的PID（进程ID）
    // 子进程收到的的返回值为 0
    // 创建失败时返回 -1
    pid_t pid = fork();
    
    if (pid < 0) {
      perror("fork failed");
      continue;
    } else if (pid == 0) {
      // 子进程
      if (scomds.size() > 1) {
        for (size_t i = 0; i < scomds.size() - 1; ++i)
          if (pipe(fd[i]) == -1) {
            perror("pipe failed");
            continue;;
          }
      } // 创建管道
      for (size_t i = 0; i < scomds.size(); ++i) {
        // 创建子进程执行指令
        pid_t cpid = fork();
        if (cpid < 0) {
          perror("fork failed");
          continue;
        } else if (cpid == 0) {
          // 实现管道功能
          if (scomds.size() > 1) {
            // 输入重定向
            if (i > 0) {
              dup2(fd[i-1][0], STDIN_FILENO);
              close(fd[i-1][0]);
              close(fd[i-1][1]);
            }
            // 输出重定向
            if (i < scomds.size() - 1) {
              dup2(fd[i][1], STDOUT_FILENO);
              close(fd[i][0]);
              close(fd[i][1]);
            }
          }

          // 存储分割后的子命令参数
          std::vector<std::string> scomd_args = split(scomds[i], " ");
          // 实现重定向功能
          for (size_t i = 0; i < scomd_args.size(); i++) {
            if (scomd_args[i] == "<") {
              int fd = open(scomd_args[i+1].c_str(), O_RDONLY, 0644);
              if (fd < 0) {
                perror("open failed");
                continue;
              } else {
                dup2(fd, STDIN_FILENO);
                close(fd);
              }
              scomd_args[i] = " ";
              scomd_args[i+1] = " ";
            }
            if (scomd_args[i] == ">>") {
              int fd = open(scomd_args[i+1].c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
              if (fd < 0) {
                perror("open failed");
                continue;
              } else {
                dup2(fd, STDOUT_FILENO);
                close(fd);
              }
              scomd_args[i] = " ";
              scomd_args[i+1] = " ";
            }
            if (scomd_args[i] == ">") {
              int fd = open(scomd_args[i+1].c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
              if (fd < 0) {
                perror("open failed");
                continue;
              } else {
                dup2(fd, STDOUT_FILENO);
                close(fd);
              }
              scomd_args[i] = " ";
              scomd_args[i+1] = " ";
            }
          }

          // 执行子命令
          std::vector<char*> argv; // 存储转换为C风格字符串的指针
          // 遍历 scomd_args 中的每个元素, auto& 声明引用以避免拷贝
          for (auto& arg : scomd_args) {
            if (arg != " ")
              argv.push_back(const_cast<char*>(arg.c_str())); // execvp 函数要求第一个参数是 const char*, 强制移除
          }
          argv.push_back(nullptr); // execvp 系列的 argv 需要以 nullptr 结尾

          // execvp 会完全更换子进程接下来的代码，所以正常情况下 execvp 之后这里的代码就没意义了
          // 如果 execvp 之后的代码被运行了，那就是 execvp 出问题了
          execvp(argv[0], argv.data());
          // 所以这里直接报错
          perror("execvp failed");
          exit(255);
        } else {
          // 父进程
          // 关闭上一个命令的读端和写端，防止阻塞
          close(fd[i-1][0]);
          close(fd[i-1][1]);
        }
      }
      if (scomds.size() > 1) {
        // 关闭最后一个管道的读端与写端
        close(fd[scomds.size()-2][1]);
        close(fd[scomds.size()-2][1]);
      }
      while (wait(nullptr) > 0); // 等待所有子进程结束
      return 0;
    } else {    
      // 这里只有 Shell 才会进入
      // 父进程调用 wait(nullptr) 阻塞等待子进程结束，并回收其资源
      // 成功：返回终止的子进程PID
      // 失败：返回 -1（如无子进程或信号中断）
      int ret = wait(nullptr);
      if (ret < 0) {
        std::cout << "wait failed";
      }
    }
  }
}

// 经典的 cpp string split 实现
// https://stackoverflow.com/a/14266139/11691878
// 功能: 将字符串 s 按分隔符 delimiter 分割为子字符串，通过函数返回值存储在一个 vector<string> 中
// 更改了部分函数的功能，该函数仅会返回非空的子字符串
std::vector<std::string> split(std::string s, const std::string &delimiter) {
  std::vector<std::string> res;
  size_t pos = 0;
  std::string token;
  while ((pos = s.find(delimiter)) != std::string::npos) {
    token = s.substr(0, pos);
    if (!token.empty()) {
      res.push_back(token);
    }
    s = s.substr(pos + delimiter.length());
  }
  // 处理末尾分隔符
  if (!s.empty()) {
    res.push_back(s);
  }
  return res;
}
