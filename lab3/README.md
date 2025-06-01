# Lab 3

## 编译运行方法说明

本次实验我实现的选做部分有**线程池机制**。

在`./lab3/src`目录下，使用`make`或`make all`命令，生成线程池机制实现的`server`，即：

```makefile
make 		# 或make all，生成server
./server 	# 运行server
```

## 必作部分

### 并发请求处理

此处采用了线程池机制来处理并发请求。线程池的实现方式是使用一个循环队列来存储任务（即客户端套接字），每个线程从队列中取出一个任务进行处理。线程池的大小为100，任务队列的大小为40960。
每个线程在处理任务时，会先等待任务队列中有任务可用，然后从队列中取出一个任务进行处理。处理完毕后，线程会继续等待下一个任务。
在处理请求时，首先会从请求中读取请求行，然后解析请求行，获取请求的方法、URL、协议版本和Host请求行。接着，根据请求的URL获取请求资源的大小和文件指针，并向响应中写入返回头的内容。最后，将请求资源的内容写入响应中，并将响应发送给客户端。
具体表现为以下代码：

```c
void* thread_worker(void *arg);
void thread_pool_init(ThreadPool *pool);
void thread_pool_add_task(ThreadPool *pool, int clnt_sock);

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

int main () 
{
    // ...
    // 初始化线程池
    thread_pool_init(&thread_pool);

    while (1) // 一直循环
    {
        pthread_mutex_lock(&shutdown_mutex);
        if (shutdown_flag) {
            pthread_mutex_unlock(&shutdown_mutex);
            break;  // 退出循环
        }
        pthread_mutex_unlock(&shutdown_mutex);

        // 当没有客户端连接时，accept() 会阻塞程序执行，直到有客户端连接进来
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock == -1) {
            if (shutdown_flag) break;  // 因关闭标志触发 accept 错误
            perror("accept error!\n");
            continue;
        }
        // 处理客户端的请求
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
    
    // 实际上这里的代码不可到达，可以在 while 循环中收到 SIGINT 信号时主动 break
    // 关闭套接字
    close(serv_sock);
    return 0;
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
```

代码解析：
- 线程池结构体`ThreadPool`包含了处理并发请求所需的所有组件：
  1. 工作线程数组：固定大小为THREAD_POOL_SIZE的线程数组
  2. 任务队列：采用循环队列结构，大小为QUEUE_SIZE，存储客户端套接字描述符
  3. 队列指针：head指向队首(待处理任务)，tail指向队尾(新任务插入位置)
  4. 同步机制：包含互斥锁和条件变量，保证任务队列的并发安全
  5. 关闭标志：用于通知工作线程退出

- 线程池工作流程：
  1. 主线程通过`thread_pool_init`初始化线程池，创建工作线程
  2. 主线程接受客户端连接，通过`thread_pool_add_task`将任务添加到队列
  3. 工作线程执行`thread_worker`函数，不断从队列取出任务并处理
  4. 任务队列空时，工作线程阻塞在条件变量等待；队列满时，主线程阻塞等待

- 线程同步与并发控制：
  1. 使用互斥锁保护对任务队列的访问，防止竞态条件
  2. 使用条件变量`queue_not_empty`通知工作线程有新任务可处理
  3. 使用条件变量`queue_not_full`通知主线程队列有空位可添加任务
  4. 信号处理与优雅退出：通过shutdown标志通知所有线程退出

- 生产者-消费者模式：
  1. 主线程作为生产者，持续接受连接并生成任务
  2. 工作线程作为消费者，并行处理这些任务
  3. 循环队列作为缓冲区，解耦生产和消费过程
  4. 条件变量实现了生产者与消费者之间的同步

- 错误处理与资源清理：
  1. 服务器关闭时正确销毁线程池资源，包括线程、互斥锁和条件变量
  2. 工作线程在处理完客户端请求后负责关闭客户端套接字
  3. 处理客户端连接异常时有适当的错误处理机制

这种基于线程池的并发模型比为每个连接创建新线程更高效，避免了频繁的线程创建/销毁开销，适合处理大量短连接请求的HTTP服务器场景。


### 解析和检验 HTTP 头

在处理请求时，需要解析 HTTP 请求头并进行检验。请求头通常包含请求方法、URL、HTTP 版本和 Host 等信息。对于不符合规范的请求，需要返回错误响应。
在我的实现中，解析请求头的函数 `parse_request` 会从请求字符串中提取出请求方法、URL、HTTP 版本和 Host，并进行基本的检验。如果请求方法不是 GET，或者 HTTP 版本不是以 "HTTP/" 开头，或者 Host 不符合规范，则返回错误代码。
如果请求头解析成功，则将 URL 存储在 `path` 中，并返回 0；如果解析失败，则返回 1。

我的处理方式如下：

```c
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
```

代码解析：
- `parse_request`函数负责解析HTTP请求头，从中提取出方法、URL、版本和Host等信息。
- 函数首先按空格和换行符分隔请求行，依次提取出各部分内容。
- 解析完成后进行请求合法性检查，包括：
  1. 确认请求方法是否为GET
  2. 确认HTTP版本是否以"HTTP/"开头
  3. 确认Host头是否存在且格式正确
  4. 确认请求行和Host行是否以"\r\n"结尾
- 如果任何检查失败，函数返回错误码1；否则提取URL到path参数并返回0。
- 该函数使用了动态内存分配来存储临时字符串，并在函数返回前释放这些内存。
- 这种解析方式简单直观，但对请求格式有较严格的要求，可能无法处理某些不规范的HTTP请求。


### 实现读取请求资源内容

在处理请求时，需要读取请求的资源内容，并将其发送给客户端。首先需要根据请求的 URL 获取请求资源的路径，然后检查该路径是否存在，是否是一个文件，是否在当前目录下等。如果路径不合法，则返回错误响应；如果路径合法，则打开文件并读取内容，将其发送给客户端。
在我的实现中，读取请求资源内容的函数 `parse_content` 会根据请求的路径获取文件信息，并进行一系列安全检查，包括路径是否包含 `..`（目录回溯）、路径是否在当前目录下等。如果检查通过，则打开文件并获取文件大小，最后返回文件指针和文件大小。    

我的处理方式如下：

```c
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
```

代码解析：
- `parse_content`函数负责根据请求路径获取文件内容，并进行多层安全检查：
  1. 检查路径是否包含".."（目录回溯）以防止路径遍历攻击
  2. 获取规范化的真实路径，确保请求路径确实在服务器目录下
  3. 检查请求资源是否存在且是一个文件而非目录
- 函数使用`stat`和`access`进行文件存在性和权限检查
- 如果所有检查通过，打开文件并返回文件指针和大小
- 返回值为0表示成功，1表示服务器错误(500)，2表示资源不存在(404)

- `handle_clnt`函数是处理客户端请求的主函数：
  1. 首先读取并解析HTTP请求，直到遇到空行"\r\n\r\n"
  2. 调用`parse_request`解析请求头
  3. 调用`parse_content`获取请求资源
  4. 根据解析结果构建HTTP响应头
  5. 发送响应头到客户端
  6. 如果资源存在且请求合法，以缓冲区方式读取文件内容并发送
  7. 函数处理了各种边缘情况和错误，包括信号中断(EINTR)

- 整个实现采用了分块读写的方式处理文件，使用固定大小的缓冲区，即使对大文件也能高效处理，避免一次性加载整个文件到内存


## 测试结果

命令：`curl --http1.0 http://127.0.0.1:8000/index.html -v`

![success](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-1.png)

命令：`curl -d '@index.html' --http1.0 http://127.0.0.1:8000/index.html -v`

![POST](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-2.png)

命令：`curl --http1.0 http://127.0.0.1:8000/index/ -v`

![directory](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-3.png)

命令：`curl --http1.0 http://127.0.0.1:8000/../ -v`

![notfound](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-4.png)

命令：`siege -c 50 -r 10 http://127.0.0.1:8000/index.html`

文件大小：< 1 MiB

![little_file](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-5.png)

命令：`siege -c 50 -r 10 http://127.0.0.1:8000/large_file.txt`

文件大小：1.46 MiB

![large_file](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-6.png)

最终的退出服务器提示，通过`Ctrl+C`触发：

![exit](https://github.com/cmdyc/Markdown-images/blob/main/osh-lab3-7.png)

## 结果分析

值得注意的是，在测试过程中，我发现如果先测试siege命令，再测试curl命令，可能会导致curl请求失败。但是如果先测试curl命令，再测试siege命令，则不会出现这个问题。我猜测这是因为在使用siege进行测试时，服务器会被大量的并发请求占用资源，导致后续的curl请求无法正常处理；而curl请求的资源较小，服务器能够快速处理完毕，释放资源，从而不会影响后续的siege请求。