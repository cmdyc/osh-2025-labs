// server.c
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

#define BIND_IP_ADDR "127.0.0.1"
#define BIND_PORT 8000
#define MAX_RECV_LEN 1048576
#define MAX_SEND_LEN 1048576
#define MAX_BUFFER_SIZE 1048576
#define MAX_PATH_LEN 1024
#define MAX_HOST_LEN 1024
#define MAX_DIR_LEN 1024
#define MAX_CONN 1024
#define THREAD_POOL_SIZE 100
#define QUEUE_SIZE 40960

#define HTTP_STATUS_200 "200 OK"
#define HTTP_STATUS_404 "404 Not Found"
#define HTTP_STATUS_500 "500 Internal Server Error"

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

int serv_sock;
int clnt_sock;

//  函数原型
int parse_request(char* request, ssize_t req_len, char* path, ssize_t* path_len);
int parse_content(char *path, long *file_size, FILE **file);
void handle_clnt(int clnt_sock);
void* thread_worker(void *arg);
void thread_pool_init(ThreadPool *pool);
void thread_pool_add_task(ThreadPool *pool, int clnt_sock);

int main(){
    // 创建套接字，参数说明：
    //   AF_INET: 使用 IPv4
    //   SOCK_STREAM: 面向连接的数据传输方式
    //   IPPROTO_TCP: 使用 TCP 协议
    if((serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        perror("socket error!\n");
        exit(EXIT_FAILURE);
    }

    // 将套接字和指定的 IP、端口绑定
    //   用 0 填充 serv_addr（它是一个 sockaddr_in 结构体）
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    //   设置 IPv4
    //   设置 IP 地址
    //   设置端口
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(BIND_IP_ADDR);
    serv_addr.sin_port = htons(BIND_PORT);
    //   绑定
    if(bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind error!\n");
        exit(EXIT_FAILURE);
    }

    // 使得 serv_sock 套接字进入监听状态，开始等待客户端发起请求
    if(listen(serv_sock, MAX_CONN) == -1) {
        perror("listen error!\n");
        exit(EXIT_FAILURE);
    }

    // 接收客户端请求，获得一个可以与客户端通信的新的生成的套接字 clnt_sock
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size = sizeof(clnt_addr);

    // 初始化线程池
    thread_pool_init(&thread_pool);

    while (1) // 一直循环
    {
        // 当没有客户端连接时，accept() 会阻塞程序执行，直到有客户端连接进来
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock == -1) {
            perror("accept error!\n");
            continue;
        }
        // 处理客户端的请求
        thread_pool_add_task(&thread_pool, clnt_sock);
    }
    
    // 实际上这里的代码不可到达，可以在 while 循环中收到 SIGINT 信号时主动 break
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
    // 或者版本和请求不是以 \r\n 结尾，返回 1
    if ((strncmp(method, "GET", 3) != 0) || (strncmp(version, "HTTP/", 5) != 0) ||
        (strncmp(host, "Host:", 5) != 0) || (strncmp(version + strlen(version)-2, "\r\n", 2) != 0) ||
        (strncmp(host + strlen(host)-2, "\r\n", 2) != 0)) {
        free(method);
        free(url);  
        free(version);
        free(host);
        return 1;
    }

    memcpy(path, url, strlen(url) + 1);
    *path_len = strlen(url);

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
    
    // 安全检查1: 检查路径是否包含 ".." (目录回溯)
    if (strstr(path, "..") != NULL) {
        fprintf(stderr, "Security error: Path contains '..'\n");
        return 2;
    }
    
    // 获取当前目录
    if(!getcwd(cur_dir, sizeof(cur_dir))) {
        perror("getcwd error!\n");
        return 1;
    }

    // 安全拼接路径
    if (strlen(path) + strlen(cur_dir) + 2 < sizeof(file_path)) {
        snprintf(file_path, sizeof(file_path), "%s/%s", cur_dir, path);
    } else {
        fprintf(stderr, "Error: Path too long\n");
        return 1;
    }

    struct stat file_path_info;
    // 判断请求的资源路径是否存在
    if(stat(file_path, &file_path_info) == -1) {
        fprintf(stderr, "Error: File didn't exist\n");
        return 2;
    }
    
    // 判断请求的资源路径是否是目录
    // 如果是目录，返回 1
    if(S_ISDIR(file_path_info.st_mode)) {
        fprintf(stderr, "%s is a directory!\n", file_path);
        return 1;
    }
    
    // 判断请求的资源是否存在
    // 如果不存在，返回 2
    if(access(file_path, F_OK) == -1) {
        perror("access error!\n");
        return 2;
    }

    // 安全检查3: 确认最终路径确实是当前目录的子路径
    char real_path[MAX_PATH_LEN + MAX_DIR_LEN];
    char real_cur_dir[MAX_DIR_LEN];
    
    // 获取规范化的真实路径
    if (!realpath(file_path, real_path)) {
        perror("realpath error");
        return 1;
    }
    
    if (!realpath(cur_dir, real_cur_dir)) {
        perror("realpath error for current dir");
        return 1;
    }
    
    // 检查请求路径是否在当前目录之下
    if (strncmp(real_path, real_cur_dir, strlen(real_cur_dir)) != 0) {
        fprintf(stderr, "Security error: Path traversal attack detected\n");
        return 1;
    }
    
    // 打开文件
    *file = fopen(file_path, "rb");
    if(*file == NULL) {
        perror("fopen error");
        return 2;
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
    char req_buf[MAX_RECV_LEN];

    // 读取请求，直到遇到 "\r\n\r\n"
    ssize_t req_len = 0;

    char rep[MAX_SEND_LEN];
    char *response = rep;

    while (1) {
        ssize_t pointer = read(clnt_sock, req_buf + req_len, MAX_RECV_LEN - req_len);
        if(pointer < 0) {
            if(errno == EINTR) continue; // 被信号中断，继续读取
            perror("read error!\n");
            return;
        }
        req_len = req_len + pointer;
        if(strlen(req_buf) >= 3 && strncmp(req_buf, "GET", 3) != 0) {
            sprintf(response,
            "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_500);
            ssize_t write_len = 0;
            ssize_t response_len = strlen(response);
            while(response_len > 0) {
                if((write_len = write(clnt_sock, response, response_len)) < 0) {
                    perror("write error!\n");
                    return;
                }
            response = response + write_len;
            response_len = response_len - write_len;
            }
            return;
        }
        if (req_len >= 4 && strcmp(req_buf + req_len - 4, "\r\n\r\n") == 0) {
            break;
        }
        if (req_len >= MAX_RECV_LEN) {
            fprintf(stderr, "Request too long\n");
            return;
        }
    }

    // 用于储存路径信息
    char* path = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    ssize_t path_len = 0;
    int ret_request = parse_request(req_buf, req_len, path, &path_len);
    // 用于储存文件信息
    long file_size = 0;
    FILE *file = NULL;
    int ret_content = parse_content(path, &file_size, &file);

    // 构造要返回的数据
    // 注意，响应头部后需要有一个多余换行（\r\n\r\n），然后才是响应内容
    
    if(ret_request == 1 || ret_content == 1) {
        sprintf(response, "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n", HTTP_STATUS_500);
    }
    else if(ret_content == 2) {
        sprintf(response, "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n", HTTP_STATUS_404);
    }
    else if(ret_request == 0 && ret_content == 0) {
        sprintf(response, "HTTP/1.0 %s\r\nContent-Length: %zd\r\n\r\n", HTTP_STATUS_200, file_size);
    }
    ssize_t response_len = strlen(response);

    // 通过 clnt_sock 向客户端发送信息
    // 将 clnt_sock 作为文件描述符写内容
    ssize_t write_len = 0;
    while(response_len > 0) {
        if((write_len = write(clnt_sock, response, response_len)) < 0) {
            perror("write error!\n");
            free(path);
            return;
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

    // 释放内存
    free(path);
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