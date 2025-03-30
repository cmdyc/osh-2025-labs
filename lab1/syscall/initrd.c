#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

void sys_hello(char *buf, int buf_len)
{
        long res = syscall(548, buf, buf_len);
        if(res == -1)
        {
                printf("Error! The lenth %d is too short\n", buf_len);
        }
        else
        {
                printf("The content is %s\n",buf);
        }
}

int main() {
    //printf("Hello! PB22081571\n"); // Your Student ID
    char buf20[20], buf50[50];
    sys_hello(buf20,20);
    sys_hello(buf50,50);
    while(1) {}
}