// 栈帧解剖：用编译器内建函数直接读出帧指针与返回地址。
#include <iostream>

__attribute__((noinline))
void inner(int arg) {
    int local = 7;
    std::cout << "== inner 的栈帧 ==\n";
    std::cout << "  参数 arg 的地址:    " << (void*)&arg << "\n";
    std::cout << "  局部 local 的地址:  " << (void*)&local << "\n";
    std::cout << "  帧指针 fp:          " << __builtin_frame_address(0) << "\n";
    std::cout << "  返回地址:           " << __builtin_return_address(0)
              << "   <- 指向 outer 函数体内部！\n";
    (void)arg; (void)local;
}

__attribute__((noinline))
void outer() {
    std::cout << "== outer 的栈帧 ==\n";
    std::cout << "  帧指针 fp:          " << __builtin_frame_address(0) << "\n";
    std::cout << "  outer 函数的起点:   " << (void*)&outer << "\n\n";
    inner(42);
}

// 尾递归：return 后面只剩下一次调用，编译器可以把它变成循环（-O2）
long countdown(long n, long acc) {
    if (n == 0) return acc;
    return countdown(n - 1, acc + 1);      // 尾调用：本帧再无未竟之事
}

int main() {
    outer();

    std::cout << "\n== 验证：inner 的返回地址落在 outer 函数范围内 ==\n";
    std::cout << "  outer 起点 < 返回地址 < outer 起点 + 几十字节 —— 返回地址就是\n";
    std::cout << "  「call 的下一条指令」，函数靠它知道往哪回（见章节汇编实测）\n";

    std::cout << "\n== 尾递归（本文件由 run-all 以 -O0/-O2 皆可能编译，用安全深度演示） ==\n";
    std::cout << "  countdown(100000, 0) = " << countdown(100'000, 0) << "\n";
    std::cout << "  （-O2 下编译器把它变成循环，一亿层也不爆栈——见章节 shell 实测）\n";
    return 0;
}
