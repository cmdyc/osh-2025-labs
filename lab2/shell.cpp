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

std::vector<std::string> split(std::string s, const std::string &delimiter);

int main() {
  // 不同步 iostream 和 cstdio 的 buffer
  std::ios::sync_with_stdio(false);

  // 用来存储读入的一行命令
  std::string cmd;

  while (true) {
    // 打印提示符
    std::cout << "$ ";

    // 读入一行。std::getline 结果不包含换行符。
    std::getline(std::cin, cmd);

    // 按空格分割命令为单词
    std::vector<std::string> args = split(cmd, " ");

    // 按" | "分割命令为子命令
    std::vector<std::string> scomds = split(cmd, " | ");

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
    // 父进程返回子进程的PID（进程ID）
    // 子进程返回 0
    // 失败时返回 -1
    pid_t pid = fork();
    /*
    if (pid < 0) {
      // fork() 失败
      perror("fork failed");
      continue;
    } else if (pid == 0) {
      // 子进程
      if (scomds.size() > 1) {
        // 管道
        int fd[scomds.size()-1][2];
        // 为scomds中的所有子命令创建进程，并通过管道按顺序连接起来，实现进程间通信，之后执行这些命令
        for (int i = 0; i < scomds.size(); i++) {
          // 创建管道
          if (pipe(fd[i]) == -1) {
            perror("pipe failed");
            continue;
          }
          // 将子命令分割为命令与参数的格式
          std::vector<std::string> scomd_args = split(scomds[i], " ");
          // fork 子进程
          pid_t cpid = fork();
          if (cpid < 0) {
            perror("fork failed");
            continue;
          } else {
            // 将子命令的输出与输入重定向到管道
            // 由于是子进程进行的fork()，所以这里的pid如果正确生成的话均为0

            // 将scomds中除第一个子进程外所有的子进程的输入重定向到管道
            if (i > 0) {
              // 关闭管道的写端
              close(fd[i-1][1]);
              // 将标准输入重定向到管道的读端
              dup2(fd[i-1][0], STDIN_FILENO);
              // 关闭管道的读端
              close(fd[i-1][0]);
            }
            // 将scomds中除最后一个子进程外所有的子进程的输出重定向到管道
            if (i < scomds.size()-1) {
              // 关闭管道的读端
              close(fd[i][0]);
              // 将标准输出重定向到管道的写端
              dup2(fd[i][1], STDOUT_FILENO);
              // 关闭管道的写端
              close(fd[i][1]);
            } 
          }
          // std::vector<std::string> 转 char **
          char *scomd_arg_ptrs[scomd_args.size() + 1];
          for (auto i = 0; i < scomd_args.size(); i++) {
            scomd_arg_ptrs[i] = &scomd_args[i][0];
          }
          // execvp 系列的 argv 需要以 nullptr 结尾
          scomd_arg_ptrs[scomd_args.size()] = nullptr;

          // 执行子命令
          // execvp 会完全更换子进程接下来的代码，所以正常情况下 execvp 之后这里的代码就没意义了
          // 如果 execvp 之后的代码被运行了，那就是 execvp 出问题了
          execvp(scomd_args[0].c_str(), scomd_arg_ptrs);

          // 所以这里直接报错
          perror("execvp failed");
          exit(255);
        }
      } else {
        // 只有一个子命令，即没有使用管道通信
        // std::vector<std::string> 转 char **
        char *arg_ptrs[args.size() + 1];
        for (auto i = 0; i < args.size(); i++) {
          arg_ptrs[i] = &args[i][0];
        }
        // execvp 系列的 argv 需要以 nullptr 结尾
        arg_ptrs[args.size()] = nullptr;

        // 执行命令
        // execvp 会完全更换子进程接下来的代码，所以正常情况下 execvp 之后这里的代码就没意义了
        // 如果 execvp 之后的代码被运行了，那就是 execvp 出问题了
        execvp(args[0].c_str(), arg_ptrs);

        // 所以这里直接报错
        perror("execvp failed");
        exit(255);
      }
    }
    */
if (pid < 0) {
    perror("fork failed");
    continue;
} else if (pid == 0) {
    // 子进程
    if (scomds.size() > 1) {
        int pipe_count = scomds.size() - 1;
        int fd[scomds.size() - 1][2];
        for (int i = 0; i < pipe_count; ++i) {
            if (pipe(fd[i]) == -1) {
                perror("pipe failed");
                continue;;
            }
        }

        std::vector<pid_t> child_pids;

        for (int i = 0; i < scomds.size(); ++i) {
            pid_t cpid = fork();
            if (cpid < 0) {
                perror("fork failed");
                continue;
            } else if (cpid == 0) {
                // 输入重定向
                if (i > 0) {
                    dup2(fd[i-1][0], STDIN_FILENO);
                    close(fd[i-1][0]);
                }
                // 输出重定向
                if (i < pipe_count) {
                    dup2(fd[i][1], STDOUT_FILENO);
                    close(fd[i][1]);
                }
                // 关闭所有管道描述符
                for (int j = 0; j < pipe_count; ++j) {
                    close(fd[j][0]);
                    close(fd[j][1]);
                }
                // 执行子命令
                std::vector<std::string> scomd_args = split(scomds[i], " "); // 存储分割后的子命令参数
                std::vector<char*> argv; // 存储转换为C风格字符串的指针
                // 遍历 scomd_args 中的每个元素, auto& 声明引用以避免拷贝
                for (auto& arg : scomd_args) {
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
                child_pids.push_back(cpid);
            }
        }

        // 父进程关闭所有管道
        for (int i = 0; i < pipe_count; ++i) {
            close(fd[i][0]);
            close(fd[i][1]);
        }

        // 等待所有子进程
        for (pid_t pid : child_pids) {
            waitpid(pid, nullptr, 0);
        }

        continue;
    } else {
        // 处理单个命令
        std::vector<std::string> scomd_args = split(scomds[0], " ");
        std::vector<char*> argv;
        for (auto& arg : scomd_args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        perror("execvp failed");
        exit(255);
    }
}

    // 这里只有父进程（原进程）才会进入
    // 父进程调用 wait(nullptr) 阻塞等待子进程结束，并回收其资源
    // 成功：返回终止的子进程PID
    // 失败：返回 -1（如无子进程或信号中断）
    int ret = wait(nullptr);
    if (ret < 0) {
      std::cout << "wait failed";
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