// 内存四区：C++ 是唯一能直接打印各区真实地址的语言。
#include <iostream>

int global_init = 42;              // 静态区（.data：有初值的全局/静态变量）
int global_uninit;                 // 静态区（.bss：无初值，加载时清零）

void probe_stack(int depth, char* prev) {
    char local = 0;                // 每层递归都在栈上分配
    std::cout << "  第 " << depth << " 层局部变量地址: " << (void*)&local;
    if (prev) std::cout << "   <- 比上一层低 " << (prev - &local) << " 字节";
    std::cout << "\n";
    if (depth < 3) probe_stack(depth + 1, &local);
}

int main() {
    int local = 1;                 // Stack：局部变量
    int* heap = new int(2);        // Heap：new 出来的对象
    static int static_local = 3;   // 静态区：static 局部变量
    const char* literal = "hello"; // 常量区（.rodata，与代码段相邻）

    std::cout << "== ① 内存四区：打印真实地址 ==\n";
    std::cout << "代码区   probe_stack 函数:   " << (void*)&probe_stack << "\n";
    std::cout << "常量区   字符串字面量:       " << (void*)literal << "\n";
    std::cout << "静态区   全局变量(.data):    " << (void*)&global_init << "\n";
    std::cout << "静态区   全局变量(.bss):     " << (void*)&global_uninit << "\n";
    std::cout << "静态区   static 局部变量:    " << (void*)&static_local << "\n";
    std::cout << "Heap     new int:            " << (void*)heap << "\n";
    std::cout << "Stack    局部变量:           " << (void*)&local << "\n";

    std::cout << "\n== ② 栈向下增长：递归三层的地址 ==\n";
    probe_stack(1, nullptr);

    std::cout << "\n== ③ 堆向上增长：连续 new 的地址 ==\n";
    int* h1 = new int(1);
    int* h2 = new int(2);
    std::cout << "  第一次 new: " << (void*)h1 << "\n";
    std::cout << "  第二次 new: " << (void*)h2
              << "   <- 比上一次高 " << (char*)h2 - (char*)h1 << " 字节\n";

    std::cout << "\n== ④ 生命周期由区决定 ==\n";
    std::cout << "  local 随 main 返回自动消失（栈帧弹出）\n";
    std::cout << "  heap 必须手动 delete——忘了就是内存泄漏（第 33 章）\n";
    std::cout << "  global_init 活到进程结束\n";

    delete heap;
    delete h1;
    delete h2;
    return 0;
}
