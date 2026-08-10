// 堆内存：malloc 的粒度、size class 与分配成本——分配器不是免费的。
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <malloc/malloc.h>   // macOS：malloc_size

int main() {
    std::cout << "== ① 分配粒度：要多少，给多少？ ==\n";
    void* p1 = malloc(1);
    void* p17 = malloc(17);
    void* p100 = malloc(100);
    std::cout << "malloc(1)   实际得到 " << malloc_size(p1) << " 字节\n";
    std::cout << "malloc(17)  实际得到 " << malloc_size(p17) << " 字节\n";
    std::cout << "malloc(100) 实际得到 " << malloc_size(p100) << " 字节\n";
    std::cout << "（分配器按 size class 取整——多给的部分就是内部碎片）\n";

    std::cout << "\n== ② size class：不同大小住不同街区 ==\n";
    void* s1 = malloc(32);
    void* s2 = malloc(32);
    void* b1 = malloc(4096);
    void* b2 = malloc(4096);
    std::cout << "两个 malloc(32):   " << s1 << " / " << s2
              << "  相距 " << (char*)s2 - (char*)s1 << " 字节\n";
    std::cout << "两个 malloc(4096): " << b1 << " / " << b2
              << "  相距 " << (char*)b2 - (char*)b1 << " 字节\n";
    std::cout << "（同级紧邻排布，不同级相隔甚远——按尺寸分区管理）\n";

    std::cout << "\n== ③ 分配的价格：malloc/free 一千万次 32 字节 ==\n";
    constexpr int n = 10'000'000;
    volatile char keep = 0;
    for (int i = 0; i < 1'000'000; ++i) {              // 预热
        char* p = (char*)malloc(32);
        p[0] = (char)i; keep = p[0];
        __asm__ volatile("" ::"r"(p) : "memory");      // 逃逸屏障：不许优化掉这次分配
        free(p);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        char* p = (char*)malloc(32);
        p[0] = (char)i; keep = p[0];
        __asm__ volatile("" ::"r"(p) : "memory");
        free(p);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "总耗时 " << ms << " ms，平均每对 malloc/free 约 "
              << ms * 1e6 / n << " ns\n";
    std::cout << "（对比：栈分配是一条 sub sp 指令，第 32 章实测序幕仅三条指令）\n";
    (void)keep;

    std::cout << "\n== ④ 释放的义务 ==\n";
    std::cout << "本程序 ①② 的六块内存故意不 free——用 macOS 的 leaks 工具\n";
    std::cout << "能逐块揪出来（见章节 shell 实测）\n";
    return 0;
}
