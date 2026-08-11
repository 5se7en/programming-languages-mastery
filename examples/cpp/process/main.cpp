// 进程：操作系统给的隔离执行单元——fork 之后，两个世界各走各的。
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int global_counter = 100;                   // 全局变量：fork 后父子各持一份

int main() {
    printf("== ① 进程身份：PID 与父子关系 ==\n");
    printf("  我是进程 %d，我的父进程是 %d\n", getpid(), getppid());

    printf("\n== ② 钥匙实验：fork 之后，内存是隔离的 ==\n");
    printf("  fork 之前 global_counter = %d，地址 = %p\n",
           global_counter, (void*)&global_counter);

    int* heap_value = new int(200);          // 堆上也放一个
    fflush(stdout);                          // ⚠️ fork 前必须刷新：否则缓冲区会被复制两份
    pid_t pid = fork();                      // ← 这一行之后，有两个进程在跑

    if (pid == 0) {
        // ---- 子进程 ----
        global_counter += 1;                 // 子进程改成 101
        *heap_value += 1;
        printf("  [子进程 %d] 改后 global=%d, heap=%d, 地址=%p\n",
               getpid(), global_counter, *heap_value, (void*)&global_counter);
        printf("  [子进程 %d] 我的父进程是 %d\n", getpid(), getppid());
        delete heap_value;
        fflush(stdout);                      // _exit 不刷新 stdio——必须手动来
        _exit(0);                            // 子进程退出（不跑父进程的后续代码）
    }

    // ---- 父进程 ----
    int status = 0;
    waitpid(pid, &status, 0);                // 等子进程结束
    printf("  [父进程 %d] 子进程改完之后，我的 global=%d, heap=%d, 地址=%p\n",
           getpid(), global_counter, *heap_value, (void*)&global_counter);
    printf("  ↑ 父进程的值纹丝不动——地址看着一样（虚拟地址，第 31 章），\n");
    printf("    但映射到的物理内存已经分家（写时复制 COW）\n");
    printf("  子进程退出码 = %d\n", WEXITSTATUS(status));
    delete heap_value;

    printf("\n== ③ fork 的返回值：一次调用，两次返回 ==\n");
    printf("  在父进程里返回子进程的 PID（本次 = %d）\n", pid);
    printf("  在子进程里返回 0 —— 这是区分父子的唯一手段\n");

    printf("\n== ④ 进程间通信：管道 ==\n");
    int fd[2];
    if (pipe(fd) == 0) {
        pid_t p2 = fork();
        if (p2 == 0) {
            fflush(stdout);
            close(fd[0]);
            const char* msg = "子进程说：隔离归隔离，话还是要讲的";
            write(fd[1], msg, strlen(msg));
            close(fd[1]);
            _exit(0);
        }
        close(fd[1]);
        char buf[128] = {0};
        ssize_t n = read(fd[0], buf, sizeof(buf) - 1);
        close(fd[0]);
        waitpid(p2, nullptr, 0);
        if (n > 0) printf("  父进程从管道读到: %s\n", buf);
        printf("  （隔离的代价：数据必须显式传递——序列化 + 拷贝，第 10 节详述）\n");
    }

    printf("\n== ⑤ 隔离的价值 ==\n");
    printf("  子进程崩溃（段错误、OOM）不会拖垮父进程\n");
    printf("  一个进程写坏自己的内存，碰不到别人（第 34 章的野指针只能伤自己）\n");
    printf("  代价：创建贵、通信贵——见下一章的线程对照\n");
    return 0;
}
