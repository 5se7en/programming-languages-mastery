// 异步：C++ 直到 C++20 才有协程——在此之前只有 future/promise 这类「异步的骨架」。
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
constexpr int IO_DELAY_MS = 50;
constexpr int TASKS = 20;

int blocking_io(int n) {
    std::this_thread::sleep_for(std::chrono::milliseconds(IO_DELAY_MS));
    return n;
}

int main() {
    printf("== ① 钥匙实验：串行 vs std::async ==\n");
    {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < TASKS; ++i) blocking_io(i);
        double serial_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        t0 = std::chrono::steady_clock::now();
        std::vector<std::future<int>> futures;
        for (int i = 0; i < TASKS; ++i)
            futures.push_back(std::async(std::launch::async, blocking_io, i));
        for (auto& f : futures) f.get();
        double async_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        printf("  串行 %d 个 I/O:  %7.0f ms\n", TASKS, serial_ms);
        printf("  std::async:     %7.0f ms（加速比 %.1fx）\n", async_ms, serial_ms / async_ms);
        printf("  ⚠️ 但 std::async(launch::async) 是「每个任务开一条线程」——\n");
        printf("     它不是真异步，只是把阻塞挪到了别的线程上（第 39 章：线程 12.2 μs/个）\n");
    }

    printf("\n== ② future/promise：异步的骨架 ==\n");
    {
        std::promise<int> p;
        std::future<int> f = p.get_future();
        std::thread producer([&p] {
            std::this_thread::sleep_for(30ms);
            p.set_value(42);                       // 生产者填结果
        });
        printf("  主线程等待结果……\n");
        printf("  拿到 future 的值: %d\n", f.get());   // 消费者阻塞等待
        producer.join();
        printf("  （promise 写、future 读——这正是 JS Promise / C# Task 的同一抽象）\n");
    }

    printf("\n== ③ C++ 缺的是什么：await ==\n");
    printf("  f.get() 会「阻塞当前线程」直到结果就绪 —— 这不是异步，是同步等待\n");
    printf("  真正的异步需要：暂停当前函数、让出线程、结果就绪时从原地恢复\n");
    printf("  → 这要求把「函数的栈帧」搬到堆上（第 32 章）——C++20 协程做的就是这件事\n");

    printf("\n== ④ C++20 协程：语言层的三个关键字 ==\n");
    printf("  co_await  : 暂停并等待（对应 JS/C#/Python 的 await）\n");
    printf("  co_yield  : 产出一个值并暂停（对应生成器）\n");
    printf("  co_return : 协程的返回\n");
    printf("  但标准库没有配套的协程类型——需要自己实现 promise_type，\n");
    printf("  或依赖第三方库（cppcoro、Boost.Asio、folly）\n");
    printf("  这是 C++ 异步生态最大的痛点：语言给了机制，没给策略\n");

    printf("\n== ⑤ 五门语言的异步成熟度对照 ==\n");
    printf("  JavaScript : Promise + async/await（原生，生态统一）\n");
    printf("  C#         : Task + async/await（最早引入，设计最完整）\n");
    printf("  Python     : asyncio + async/await（生态分裂：同步库无法直接用）\n");
    printf("  Java       : CompletableFuture（无 await）→ 虚拟线程另辟蹊径（第 44 章）\n");
    printf("  C++        : future（阻塞）+ C++20 协程（无标准库支持）\n");
    return 0;
}
