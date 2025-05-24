// server.c
#define _POSIX_SOURCE

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#define BIND_IP_ADDR "127.0.0.1"
#define BIND_PORT 8000
#define MAX_RECV_LEN 1048576
#define MAX_SEND_LEN 1048576
#define MAX_PATH_LEN 1024
#define MAX_DIR_LEN 1024
#define MAX_HOST_LEN 1024
#define MAX_BUFFER_SIZE 1048576
#define MAX_CONN 20
#define THREAD_POOL_SIZE 10
#define QUEUE_SIZE 50

#define HTTP_STATUS_200 "200 OK"
#define HTTP_STATUS_404 "404 Not Found"
#define HTTP_STATUS_500 "500 Internal Server Error"

volatile sig_atomic_t shutdown_flag = 0;  // 全局变量，用于处理 SIGINT 信号
pthread_mutex_t shutdown_mutex = PTHREAD_MUTEX_INITIALIZER; // 互斥锁，用于保护 shutdown_flag

//  线程池结构体
typedef struct {
    pthread_t threads[THREAD_POOL_SIZE];  // 线程数组
    int task_queue[QUEUE_SIZE];           // 任务队列
    int queue_head;                       // 队列头指针
    int queue_tail;                       // 队列尾指针
    int shutdown;                         // 关闭标志，用于在主线程中退出循环
    pthread_mutex_t queue_mutex;          // 队列互斥锁
    pthread_cond_t queue_not_empty;       // 队列非空条件变量
    pthread_cond_t queue_not_full;        // 队列未满条件变量
} ThreadPool;

ThreadPool thread_pool;

int serv_sock; // 服务器套接字

//  函数声明
int parse_request(char* request, ssize_t req_len, char* path, ssize_t* path_len);
int parse_content(char *path, long *file_size, FILE **file);
void handle_clnt(int clnt_sock);
void* thread_worker(void* arg);
void thread_pool_init(ThreadPool* pool);
void thread_pool_add_task(ThreadPool* pool, int clnt_sock);

void sigint_handler(int signum) {
    // 处理 SIGINT 信号
    pthread_mutex_lock(&shutdown_mutex);
    shutdown_flag = 1;  // 设置关闭标志
    pthread_mutex_unlock(&shutdown_mutex);
    printf("Received SIGINT, shutting down...\n");

    // 关闭服务器套接字以中断 accept 阻塞
    close(serv_sock);
}

int main(){
    // 注册信号处理函数
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    } 

    // 创建套接字，参数说明：
    //   AF_INET: 使用 IPv4
    //   SOCK_STREAM: 面向连接的数据传输方式
    //   IPPROTO_TCP: 使用 TCP 协议
    serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serv_sock == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 将套接字和指定的 IP、端口绑定
    //   用 0 填充 serv_addr（它是一个 sockaddr_in 结构体）
    //   sockaddr 是通用的地址结构体，但实际使用时需要具体类型（如IPv4、IPv6）
    //   sockaddr_in 结构体用于表示 IPv4地址和端口号，它是 sockaddr 结构体的具体实现，专门用于IPv4协议
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    //   设置 IPv4
    //   设置 IP 地址
    //   设置端口
    serv_addr.sin_family = AF_INET;
    //   指定地址族（Address Family），IPv4固定为 AF_INET
    serv_addr.sin_addr.s_addr = inet_addr(BIND_IP_ADDR);
    //   16位的端口号，需通过 htons() 函数将主机字节序转换为网络字节序（大端模式）
    serv_addr.sin_port = htons(BIND_PORT);
    //   存储IPv4地址，通过 struct in_addr 的 s_addr 字段设置
    //   inet_addr() 函数将点分十进制字符串转换为网络字节序的二进制地址

    //   在函数调用时（如 bind、connect），需将 sockaddr_in* 强制转换为 sockaddr*

    //   绑定
    //   bind() 函数将套接字 serv_sock 和指定的地址 serv_addr 绑定在一起
    //   bind() 函数的第一个参数是套接字描述符，第二个参数是 sockaddr 结构体指针，第三个参数是结构体大小
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind failed");
        close(serv_sock);
        exit(EXIT_FAILURE);
    }
    //   使得 serv_sock 套接字进入监听状态，开始等待客户端发起请求
    //   listen() 函数的第一个参数是套接字描述符，第二个参数是最大连接数
    if (listen(serv_sock, MAX_CONN) == -1) {
        perror("listen failed");
        close(serv_sock);
        exit(EXIT_FAILURE);
    }

    // 初始化线程池
    thread_pool_init(&thread_pool);

    // 接收客户端请求，获得一个可以与客户端通信的新的生成的套接字 clnt_sock
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size = sizeof(clnt_addr);

    while (1) // 一直循环
    {
        pthread_mutex_lock(&shutdown_mutex);
        if (shutdown_flag) {
            pthread_mutex_unlock(&shutdown_mutex);
            break;  // 退出循环
        }
        pthread_mutex_unlock(&shutdown_mutex);

        // 当没有客户端连接时，accept() 会阻塞程序执行，直到有客户端连接进来
        // accept() 函数的第一个参数是监听套接字，第二个参数是客户端地址结构体指针，第三个参数是结构体大小指针
        int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if (clnt_sock == -1) {
            if (shutdown_flag) break;  // 因关闭标志触发 accept 错误
            perror("accept error");
            continue;
        }

        // 处理客户端的请求
        // 将任务添加到线程池队列
        thread_pool_add_task(&thread_pool, clnt_sock);
    }

    // 关闭线程池
    pthread_mutex_lock(&thread_pool.queue_mutex);
    thread_pool.shutdown = 1;  // 设置线程池关闭标志
    pthread_cond_broadcast(&thread_pool.queue_not_empty);  // 唤醒所有工作线程
    pthread_cond_broadcast(&thread_pool.queue_not_full);
    pthread_mutex_unlock(&thread_pool.queue_mutex);

    // 等待所有工作线程退出
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(thread_pool.threads[i], NULL);
    }

    // 销毁同步资源
    pthread_mutex_destroy(&thread_pool.queue_mutex);
    pthread_cond_destroy(&thread_pool.queue_not_empty);
    pthread_cond_destroy(&thread_pool.queue_not_full);

    // 关闭套接字
    close(serv_sock);
    return 0;
}

//  处理请求的函数
int parse_request(char* request, ssize_t req_len, char* path, ssize_t* path_len)
{
    char* req = request;

    // 解析请求行
    // 这里假设请求行格式为:
    // > GET /path HTTP/1.0
    // > Host: 127.0.0.1:8000
    // 解析方法、路径、版本和 Host
    char* method = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    char* url = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    char* version = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    char* host = (char*) malloc(MAX_PATH_LEN * sizeof(char));

    // 一个粗糙的解析方法，可能有 BUG！
    // 获取第一个空格 (s1) 之前的内容，为 method
    ssize_t s1 = 0;
    while(s1 < req_len && req[s1] != ' ') {
        method[s1] = req[s1];
        s1++;
    }
    method[s1] = '\0';
    // 获取第一个空格 (s1) 和第二个空格 (s2) 之间的内容，为 url
    ssize_t s2 = s1 + 1;
    while(s2 < req_len && req[s2] != ' ') {
        url[s2 - s1 - 1] = req[s2];
        s2++;
    }
    url[s2 - s1 - 1] = '\0';
    // 获取第二个空格 (s2) 和换行符 (s3) 之间的内容，为 version
    ssize_t s3 = s2 + 1;
    while(s3 < req_len && req[s3] != '\n') {
        version[s3 - s2 - 1] = req[s3];
        s3++;
    }
    version[s3 - s2 - 1] = '\n';
    version[s3- s2] = '\0';
    // 获取换行符 (s3) 和第二个换行符 (s4) 之间的内容，为 host
    ssize_t s4 = s3 + 1;
    while(s4 < req_len && req[s4] != '\n') {
        host[s4 - s3 - 1] = req[s4];
        s4++;
    }
    host[s4 - s3 - 1] = '\n';
    host[s4 - s3] = '\0';

    // 如果请求方法不是 GET 或者版本不是以 HTTP/ 开头或者请求不是以 Host: 开头
    // 或者版本和请求不是以 \r\n 结尾，返回 -1
    if ((strncmp(method, "GET", 3) != 0) || (strncmp(version, "HTTP/", 5) != 0) ||
        (strncmp(host, "Host:", 5) != 0) || (strncmp(version[strlen(version)-2], "\r\n", 2) != 0) ||
        (strncmp(host[strlen(host)-2], "\r\n", 2) != 0)) {
        path = NULL;
        *path_len = 0;
        free(method);
        free(url);
        free(version);
        free(host);
        return 1;
    }

    memcpy(path, req + s1 + 1, (s2 - s1 - 1) * sizeof(char));
    path[s2 - s1 - 1] = '\0';
    *path_len = (s2 - s1 - 1);
    free(method);
    free(url);
    free(version);
    free(host);
    return 0;
}

//  读取文件的函数
int parse_content(char *path, long *file_size, FILE **file)
{
    // 初始化为默认值
    *file_size = 0;
    *file = NULL;
    // 用于储存当前目录
    char cur_dir[MAX_DIR_LEN];
    // 用于储存用户请求的资源路径
    char file_path[MAX_PATH_LEN + MAX_DIR_LEN];
    /*
    // 安全检查1: 检查路径是否包含 ".." (目录回溯)
    if (strstr(path, "..") != NULL) {
        fprintf(stderr, "Security error: Path contains '..'\n");
        return -1;
    }
    
    // 安全检查2: 检查路径是否以 "/" 开头 (绝对路径)
    if (path[0] == '/') {
        // 如果是以/开头，去掉开头的/
        path++;
    }
    */
    // 获取当前目录
    if(!getcwd(cur_dir, sizeof(cur_dir))) {
        perror("getcwd error!\n");
        return -1;
    }

    // 安全拼接路径
    if (strlen(path) + strlen(cur_dir) + 2 < sizeof(file_path)) {
        snprintf(file_path, sizeof(file_path), "%s/%s", cur_dir, path);
    } else {
        fprintf(stderr, "Error: Path too long\n");
        return -1;
    }
    
    // 安全检查3: 确认最终路径确实是当前目录的子路径
    char real_path[MAX_PATH_LEN + MAX_DIR_LEN];
    char real_cur_dir[MAX_DIR_LEN];
    
    // 获取规范化的真实路径
    if (realpath(file_path, real_path) == NULL) {
        perror("realpath error");
        return -1;
    }
    
    if (realpath(cur_dir, real_cur_dir) == NULL) {
        perror("realpath error for current dir");
        return -1;
    }
    
    // 检查请求路径是否在当前目录之下
    if (strncmp(real_path, real_cur_dir, strlen(real_cur_dir)) != 0) {
        fprintf(stderr, "Security error: Path traversal attack detected\n");
        return -1;
    }

    struct stat file_path_info;
    // 判断请求的资源路径是否存在
    if(stat(file_path, &file_path_info) == -1) {
        perror("stat error!\n");
        return -1;
    }
    
    // 判断请求的资源路径是否是目录
    // 如果是目录，返回 1
    if(S_ISDIR(file_path_info.st_mode)) {
        return 1;
    }
    
    // 判断请求的资源是否存在
    // 如果不存在，返回 2
    if(access(file_path, F_OK) == -1) {
        perror("access error!\n");
        return 2;
    }
    
    // 打开文件
    *file = fopen(file_path, "rb");
    if(*file == NULL) {
        perror("fopen error");
        return -1;
    }
    
    // 获取文件大小
    *file_size = file_path_info.st_size;
    
    return 0;
}

//  处理客户端请求的函数
void handle_clnt(int clnt_sock)
{
    // 一个粗糙的读取方法，可能有 BUG！
    // 读取客户端发送来的数据，并解析
    char* req_buf = (char*) malloc(MAX_RECV_LEN * sizeof(char));

    // 读取请求，直到遇到 "\r\n\r\n"
    ssize_t req_len = 0;

    while (1) {
        ssize_t pointer = read(clnt_sock, req_buf + req_len, MAX_RECV_LEN - req_len);
        if(pointer == -1) {
            if(errno == EINTR) continue; // 被信号中断，继续读取
            perror("read error!\n");
            free(req_buf);
            break;
        }
        req_len = req_len + pointer;
        if (req_len >= 4 && strcmp(req_buf + req_len - 4, "\r\n\r\n") == 0) {
            break;
        }
        if (req_len >= MAX_RECV_LEN) {
            fprintf(stderr, "Request too long\n");
            free(req_buf);
            break;
        }
    }

    // 用于储存路径信息
    char* path = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    ssize_t path_len;
    int ret_request = parse_request(req_buf, req_len, path, &path_len);
    // 用于储存文件信息
    long file_size;
    FILE *file = NULL;
    int ret_content = parse_content(path, &file_size, &file);

    // 构造要返回的数据
    // 注意，响应头部后需要有一个多余换行（\r\n\r\n），然后才是响应内容
    char* response = (char*) malloc(MAX_SEND_LEN * sizeof(char)) ;
    
    if(ret_request == 1 || ret_content == 1) {
        sprintf(response,
            "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_500);
    }
    else if(ret_content == 2) {
        sprintf(response,
            "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_404);
    }
    else if(ret_request == 0 && ret_content == 0) {
        sprintf(response,
            "HTTP/1.0 %s\r\nContent-Length: %zd\r\n\r\n",
            HTTP_STATUS_200, file_size);
    }
    ssize_t response_len = strlen(response);

    // 通过 clnt_sock 向客户端发送信息
    // 将 clnt_sock 作为文件描述符写内容
    ssize_t write_len = 0;
    while(response_len > 0) {
        if((write_len = write(clnt_sock, response, response_len)) < 0) {
            perror("write error!\n");
            free(req_buf);
            free(path);
            free(response);
            return -1;
        }
        response = response + write_len;
        response_len = response_len - write_len;
    }

    // 处理文件内容
    if(ret_content == 0) {
        char buffer[MAX_BUFFER_SIZE];
        while (!feof(file)) {
            size_t file_size = fread(buffer, 1, MAX_BUFFER_SIZE, file);
            if (file_size < 0) {
                perror("read error!\n"); 
                break;
            }
            ssize_t write_len = 0;
            ssize_t write_ret = 0;
            while (write_len < file_size) {
                if ((write_ret = write(clnt_sock, buffer + write_len, file_size - write_len)) < 0) {
                    if(errno == EINTR) continue; // 被信号中断，继续写入
                    perror("write error!\n");
                    break;
                }
                write_len = write_len + write_ret;
            }
        }
        fclose(file);
    }

    // 关闭客户端套接字
    close(clnt_sock);

    // 释放内存
    free(req_buf);
    free(path);
    free(response);
}

//  工作线程函数
void* thread_worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // 等待队列非空
        while (pool->queue_head == pool->queue_tail && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_not_empty, &pool->queue_mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            pthread_exit(NULL);
        }

        // 取出任务
        int clnt_sock = pool->task_queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % QUEUE_SIZE;

        // 通知主线程队列有空位
        pthread_cond_signal(&pool->queue_not_full);
        pthread_mutex_unlock(&pool->queue_mutex);

        // 处理客户端请求
        handle_clnt(clnt_sock);
        close(clnt_sock);  // 在处理线程中关闭套接字
    }
    return NULL;
}

//  初始化线程池
void thread_pool_init(ThreadPool* pool) {
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->shutdown = 0;
    pthread_mutex_init(&pool->queue_mutex, NULL);
    pthread_cond_init(&pool->queue_not_empty, NULL);
    pthread_cond_init(&pool->queue_not_full, NULL);

    // 创建工作线程
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&pool->threads[i], NULL, thread_worker, pool);
    }
}

//  向线程池添加任务
void thread_pool_add_task(ThreadPool* pool, int clnt_sock) {
    pthread_mutex_lock(&pool->queue_mutex);
    
    // 等待队列未满
    while ((pool->queue_tail + 1) % QUEUE_SIZE == pool->queue_head && !pool->shutdown) {
        pthread_cond_wait(&pool->queue_not_full, &pool->queue_mutex);
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        return;
    }

    // 添加任务到队列
    pool->task_queue[pool->queue_tail] = clnt_sock;
    pool->queue_tail = (pool->queue_tail + 1) % QUEUE_SIZE;

    // 通知工作线程有新任务
    pthread_cond_signal(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->queue_mutex);
}