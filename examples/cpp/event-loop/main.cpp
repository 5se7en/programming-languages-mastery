// 事件循环：C++ 没有内置的——但用队列 + 定时器堆，五十行就能搭一个。
#include <chrono>
#include <cstdio>
#include <functional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

class EventLoop {
    struct Timer {
        Clock::time_point deadline;
        std::function<void()> task;
        bool operator>(const Timer& o) const { return deadline > o.deadline; }
    };
    std::queue<std::function<void()>> ready_;                                   // 就绪队列
    std::priority_queue<Timer, std::vector<Timer>, std::greater<>> timers_;     // 定时器小顶堆
    bool running_ = true;

public:
    void post(std::function<void()> f) { ready_.push(std::move(f)); }

    void post_delayed(std::function<void()> f, int delay_ms) {
        timers_.push({Clock::now() + std::chrono::milliseconds(delay_ms), std::move(f)});
    }

    void stop() { running_ = false; }

    void run() {
        while (running_ && (!ready_.empty() || !timers_.empty())) {
            // ① 先把到期的定时器搬进就绪队列
            while (!timers_.empty() && timers_.top().deadline <= Clock::now()) {
                ready_.push(timers_.top().task);
                timers_.pop();
            }
            // ② 执行就绪队列里的一个任务（执行到底，不可抢占）
            if (!ready_.empty()) {
                auto task = ready_.front();
                ready_.pop();
                task();
            } else if (!timers_.empty()) {
                std::this_thread::sleep_until(timers_.top().deadline);  // ③ 没活干就睡到下个定时器
            }
        }
    }
};

int main() {
    printf("== ① C++ 没有内置事件循环 ==\n");
    printf("  标准库只有线程与 future（第 42 章实测过 std::async 其实是多线程）\n");
    printf("  事件循环由库提供: Boost.Asio 的 io_context、libuv、libevent、Qt 的 QEventLoop\n");

    printf("\n== ② 钥匙实验：五十行搭一个事件循环 ==\n");
    EventLoop loop;
    std::vector<std::string> order;

    loop.post([&] {
        order.push_back("1. 第一个任务");
        loop.post([&] { order.push_back("3. 任务里排的新任务（排到队尾）"); });
        order.push_back("2. 第一个任务的剩余部分（不可抢占）");
    });
    loop.post([&] { order.push_back("4. 第二个任务"); });
    loop.post_delayed([&] { order.push_back("5. 延时 20ms 的任务"); }, 20);
    loop.post_delayed([&] { loop.stop(); }, 40);

    loop.run();
    for (const auto& line : order) printf("    %s\n", line.c_str());
    printf("  ↑ 「取一个任务 → 执行到底 → 取下一个」：与 JS/asyncio 同一台引擎\n");

    printf("\n== ③ 事件循环的三个必备零件 ==\n");
    printf("  ① 就绪队列（FIFO）      : 立刻可以跑的回调\n");
    printf("  ② 定时器堆（小顶堆）    : 按到期时间排序，取最近的\n");
    printf("  ③ I/O 多路复用（本例省略）: epoll/kqueue/IOCP —— 唯一「睡觉」的地方\n");
    printf("  真实循环体: 算出最近定时器的时间 → 用它当 select 的 timeout → 阻塞等 I/O\n");
    printf("             → 醒来后把就绪的 I/O 回调与到期定时器一起执行\n");

    printf("\n== ④ 为什么「阻塞」是事件循环的头号禁忌 ==\n");
    printf("  循环体是单线程的：一个回调不返回，下一个就永远等着\n");
    printf("  第 42 章实测过：3 个阻塞回调让并发退化为串行（Python 101→309ms）\n");
    printf("  → 任何 CPU 密集或阻塞 I/O 都必须丢给线程池（第 45 章）\n");

    printf("\n== ⑤ Boost.Asio：C++ 的事实标准 ==\n");
    printf("  io_context ctx;                    // 就是本例的 EventLoop\n");
    printf("  ctx.post([]{ ... });               // 投递任务\n");
    printf("  socket.async_read(..., handler);   // 注册 I/O 回调\n");
    printf("  ctx.run();                         // 跑循环\n");
    printf("  C++20 协程可与它配合: co_await socket.async_read(..., use_awaitable)\n");
    printf("  → 这才让 C++ 的异步代码变得可读（第 42 章：语言给机制，库给策略）\n");
    return 0;
}
