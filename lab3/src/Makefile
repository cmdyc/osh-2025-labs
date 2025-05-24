# 编译器设置
CC = gcc
CFLAGS = -g -Wall -std=gnu11 -D_POSIX_SOURCE -D_XOPEN_SOURCE=700
LDFLAGS = -pthread
TARGET = server

# 源文件和目标文件
SRCS = server.c
OBJS = $(SRCS:.c=.o)

# 默认构建规则
all: $(TARGET)

# 生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# 编译单个源文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理生成的文件
clean:
	rm -f $(OBJS) $(TARGET)

# 重新构建
rebuild: clean all

# 声明伪目标
.PHONY: all clean rebuild
