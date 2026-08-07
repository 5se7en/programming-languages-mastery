// 第 24 章 · 对象 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o obj && ./obj
// C++ 让你完全控制内存布局，也因此最能看清「对象到底长什么样」

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

// ---------- 同样的字段，不同的声明顺序 ----------
struct Bad  { char c; int i; char d; };
struct Good { int i; char c; char d; };

struct Bad2  { char a; double d; char b; int i; };
struct Good2 { double d; int i; char a; char b; };

// ---------- 有无虚函数的差别 ----------
struct Plain      { int x; };
struct WithVirtual { virtual void f() {} int x; };

// ---------- 取消填充（谨慎使用）----------
#pragma pack(push, 1)
struct Packed { char c; int i; };
#pragma pack(pop)
struct NotPacked { char c; int i; };

// ---------- 强制对齐到缓存行 ----------
struct alignas(64) CacheAligned { int x; };

int main() {
    std::cout << "=== 1. 基础类型的大小与对齐要求 ===\n";
    std::cout << "  类型      大小   对齐\n";
    std::cout << "  char      " << sizeof(char)   << "      " << alignof(char)   << "\n";
    std::cout << "  int       " << sizeof(int)    << "      " << alignof(int)    << "\n";
    std::cout << "  double    " << sizeof(double) << "      " << alignof(double) << "\n";
    std::cout << "  → 对齐要求 = 该类型必须放在几的倍数的地址上\n";

    std::cout << "\n=== 2. ⚠️ 同样的字段，顺序不同 → 大小不同 ===\n";
    std::cout << "  struct Bad  { char c; int i; char d; };  → " << sizeof(Bad)  << " 字节\n";
    std::cout << "  struct Good { int i; char c; char d; };  → " << sizeof(Good) << " 字节\n";
    std::cout << "  省了 " << sizeof(Bad) - sizeof(Good) << " 字节 ("
              << std::fixed << std::setprecision(0)
              << (100.0 * (sizeof(Bad) - sizeof(Good)) / sizeof(Bad)) << "%)\n";

    std::cout << "\n  Bad 的实际布局（offsetof 打印真实偏移）:\n";
    std::cout << "    c 在偏移 " << offsetof(Bad, c) << "   占 1 字节\n";
    std::cout << "    ▒▒▒ 偏移 1-3        ← 3 字节填充！因为 int 必须放在 4 的倍数上\n";
    std::cout << "    i 在偏移 " << offsetof(Bad, i) << "   占 4 字节\n";
    std::cout << "    d 在偏移 " << offsetof(Bad, d) << "   占 1 字节\n";
    std::cout << "    ▒▒▒ 偏移 9-11       ← 3 字节尾部填充（整体对齐到 4 的倍数）\n";

    std::cout << "\n  Good 的实际布局:\n";
    std::cout << "    i 在偏移 " << offsetof(Good, i) << "   占 4 字节\n";
    std::cout << "    c 在偏移 " << offsetof(Good, c) << "   占 1 字节\n";
    std::cout << "    d 在偏移 " << offsetof(Good, d) << "   占 1 字节\n";
    std::cout << "    ▒▒ 偏移 6-7         ← 只有 2 字节填充\n";

    std::cout << "\n=== 3. 更极端的例子 ===\n";
    std::cout << "  struct Bad2  { char a; double d; char b; int i; };  → "
              << sizeof(Bad2) << " 字节\n";
    std::cout << "  struct Good2 { double d; int i; char a; char b; };  → "
              << sizeof(Good2) << " 字节\n";
    std::cout << "  省了 " << sizeof(Bad2) - sizeof(Good2) << " 字节 ("
              << (100.0 * (sizeof(Bad2) - sizeof(Good2)) / sizeof(Bad2)) << "%)\n";
    std::cout << "  → 一百万个对象时，Bad2 比 Good2 多占 " << std::setprecision(1)
              << (sizeof(Bad2) - sizeof(Good2)) * 1000000.0 / 1024 / 1024 << " MB\n";
    std::cout << "  → 规则：把大字段放前面、小字段放后面\n";
    std::cout << "  → C++ 不会替你重排（标准保证声明顺序），这是你的责任\n";

    std::cout << "\n=== 4. C++ 没有对象头 ===\n";
    std::cout << "  struct Plain { int x; };  → sizeof = " << sizeof(Plain) << " 字节\n";
    std::cout << "  → 就是一个 int，一个字节都不多\n";
    std::cout << "  → 对比 Java：同样「只有一个 int」的对象要 16 字节（12 字节对象头）\n";
    std::cout << "  → 这就是「不用的东西不付代价」：没有 GC 就不需要 GC 信息\n";

    std::cout << "\n  但用了虚函数就不一样了：\n";
    std::cout << "    struct WithVirtual { virtual void f(); int x; };  → sizeof = "
              << sizeof(WithVirtual) << " 字节\n";
    std::cout << "    → 多了一个 vptr（虚表指针），这是多态的代价（第 27 章）\n";

    std::cout << "\n=== 5. 控制布局：#pragma pack ===\n";
    std::cout << "  struct NotPacked { char c; int i; };  → " << sizeof(NotPacked) << " 字节\n";
    std::cout << "  #pragma pack(1) 后同样的定义     → " << sizeof(Packed) << " 字节\n";
    std::cout << "  ⚠️ 省了空间，但未对齐的访问在某些架构上会变慢甚至崩溃\n";
    std::cout << "  → 只在网络协议、文件格式等有严格布局要求时使用\n";

    std::cout << "\n=== 6. alignas：强制对齐（避免多线程伪共享）===\n";
    std::cout << "  struct alignas(64) CacheAligned { int x; };\n";
    std::cout << "    sizeof  = " << sizeof(CacheAligned) << " 字节  ← 被撑到一整个缓存行\n";
    std::cout << "    alignof = " << alignof(CacheAligned) << "\n";
    std::cout << "  → 常用于避免多线程下的「伪共享」（Part 6 并发会讲）\n";

    std::cout << "\n=== 7. 对象地址与字段地址的关系 ===\n";
    {
        Good g{42, 'a', 'b'};
        const char* base = reinterpret_cast<const char*>(&g);
        std::cout << "  对象基址        " << static_cast<const void*>(base) << "\n";
        std::cout << "  &g.i            " << static_cast<const void*>(&g.i)
                  << "   = 基址 + " << offsetof(Good, i) << "\n";
        std::cout << "  &g.c            " << static_cast<const void*>(&g.c)
                  << "   = 基址 + " << offsetof(Good, c) << "\n";
        std::cout << "  → 读 g.i 就是「从 基址+0 读 4 字节」，一次内存访问\n";
        std::cout << "  → 这就是「固定偏移」访问，也是第 16 章地址计算公式的延续\n";
    }

    std::cout << "\n=== 8. 小结 ===\n";
    std::cout << "  · 对象 = 对象头 + 字段 + 填充，C++ 的对象头为 0（除非有虚函数）\n";
    std::cout << "  · 字段顺序影响大小，按「从大到小」声明能省 33%\n";
    std::cout << "  · sizeof 的结果是确定性的（由 ABI 规定），可以放心引用\n";
    return 0;
}
