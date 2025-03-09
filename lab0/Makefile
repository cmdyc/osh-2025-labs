# 定义编译器和编译选项
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I.

# 定义目标可执行文件和依赖的源文件
TARGET = bubble_sort
SRCS = main.cpp bubblesort.cpp
OBJS = $(SRCS:.cpp=.o)

# 默认目标：编译可执行文件
all: $(TARGET)

# 生成可执行文件（链接目标文件）
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# 生成目标文件（自动推导依赖关系）
%.o: %.cpp bubblesort.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理生成的文件
clean:
	rm -f $(OBJS) $(TARGET)

# 声明伪目标
.PHONY: all clean
