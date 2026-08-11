// 线程：同一进程内的多条执行线——共享地址空间，只有栈各自独立。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

volatile int shared_counter = 0;           // volatile：强制每次真读真写内存
                                           // （但 volatile 不保证原子性——正是本章要证明的）
std::atomic<int> atomic_counter{0};        // 原子版本
int global_marker = 0;                     // 用来打印地址，证明共享

void race_worker(int times) {
    for (int i = 0; i < times; ++i)
        shared_counter = shared_counter + 1;               // ⚠️ 读-改-写三步，中间可被打断
}

void atomic_worker(int times) {
    for (int i = 0; i < times; ++i) atomic_counter++;      // ✅ 原子：一步完成
}

void show_addresses(int id) {
    int local_var = id;                                    // 栈上：每个线程各一份
    printf("  线程 %d: 局部变量地址 = %p，全局变量地址 = %p\n",
           id, (void*)&local_var, (void*)&global_marker);
}

int main() {
    printf("== ① 共享什么，不共享什么 ==\n");
    std::thread t1(show_addresses, 1);
    std::thread t2(show_addresses, 2);
    t1.join(); t2.join();
    printf("  ↑ 局部变量地址不同（各有各的栈，第 32 章），全局变量地址相同（共享）\n");
    printf("  硬件并发度 = %u\n", std::thread::hardware_concurrency());

    printf("\n== ② 钥匙实验：数据竞争 ==\n");
    constexpr int N = 1'000'000;
    for (int run = 1; run <= 3; ++run) {
        shared_counter = 0;
        std::thread a(race_worker, N), b(race_worker, N);
        a.join(); b.join();
        printf("  第 %d 次运行: 期望 %d，实际 %d   （丢了 %d 次自增）\n",
               run, 2 * N, shared_counter, 2 * N - shared_counter);
    }
    printf("  ↑ 每次结果都不一样，而且都小于期望值——这就是数据竞争\n");

    printf("\n== ③ 为什么会丢：counter++ 不是一步 ==\n");
    printf("  counter++ 实际是三步: ① 读到寄存器 ② 加一 ③ 写回内存\n");
    printf("  线程 A 读到 100 ─┐\n");
    printf("  线程 B 也读到 100 ┤ 两个都算出 101，都写回 101\n");
    printf("  结果 = 101，而不是 102 —— 一次自增凭空消失\n");

    printf("\n== ④ 原子操作：把三步变成一步 ==\n");
    for (int run = 1; run <= 3; ++run) {
        atomic_counter = 0;
        std::thread a(atomic_worker, N), b(atomic_worker, N);
        a.join(); b.join();
        printf("  第 %d 次运行: 期望 %d，实际 %d   ✅\n", run, 2 * N, atomic_counter.load());
    }
    printf("  ↑ 每次都精确正确——原子操作由硬件保证不可分割\n");

    printf("\n== ⑤ volatile 不等于原子 ==\n");
    printf("  上面的 shared_counter 是 volatile：每次都真的读内存、真的写内存\n");
    printf("  可它依然出错——因为「读」和「写」之间仍有空隙让另一个线程插入\n");
    printf("  volatile 保证的是「可见性」，不是「原子性」（Java 的 volatile 同理）\n");

    printf("\n== ⑥ 原子的代价 ==\n");
    auto bench = [N](auto&& fn, const char* name) {
        auto t0 = std::chrono::steady_clock::now();
        std::thread a(fn, N), b(fn, N);
        a.join(); b.join();
        auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        printf("  %-12s %7.1f ms\n", name, ms);
        return ms;
    };
    shared_counter = 0; atomic_counter = 0;
    double race_ms = bench(race_worker, "非原子(错的)");
    double atom_ms = bench(atomic_worker, "原子(对的)");
    printf("  原子版慢 %.1f 倍——正确性不是免费的（第 41 章的锁更贵）\n", atom_ms / race_ms);
    return 0;
}
