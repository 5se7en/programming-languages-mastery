// 线程池：C++ 标准库【没有】线程池——第 42/44 章的结论第三次印证：给机制不给策略。
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ---------- 手写线程池：40 行，标准库一行都没给 ----------
class ThreadPool {
public:
    explicit ThreadPool(size_t n) {
        for (size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] { this->worker(); });
    }
    ~ThreadPool() { shutdown(); }

    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(m_);
            queue_.push(std::move(task));
            peak_ = std::max(peak_, queue_.size());   // 记录队列峰值
        }
        cv_.notify_one();                             // 唤醒一条空闲线程（第 41 章）
    }

    void wait_idle() {
        std::unique_lock<std::mutex> lk(m_);
        done_cv_.wait(lk, [this] { return queue_.empty() && busy_ == 0; });
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    size_t peak_queue() const { return peak_; }

private:
    void worker() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
                ++busy_;
            }
            task();
            {
                std::lock_guard<std::mutex> lk(m_);
                --busy_;
                if (queue_.empty() && busy_ == 0) done_cv_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex m_;
    std::condition_variable cv_, done_cv_;
    bool stop_ = false;
    size_t busy_ = 0, peak_ = 0;
};

int main() {
    unsigned cores = std::thread::hardware_concurrency();

    printf("== ① 每任务一线程 vs 复用线程池 ==\n");
    const int N = 2000;
    std::atomic<long> counter{0};

    auto t0 = Clock::now();
    {
        std::vector<std::thread> ts;
        ts.reserve(N);
        for (int i = 0; i < N; ++i) ts.emplace_back([&counter] { counter.fetch_add(1); });
        for (auto& t : ts) t.join();
    }
    double ms_raw = ms_since(t0);

    counter = 0;
    t0 = Clock::now();
    {
        ThreadPool pool(8);
        for (int i = 0; i < N; ++i) pool.submit([&counter] { counter.fetch_add(1); });
        pool.wait_idle();
    }
    double ms_pool = ms_since(t0);

    printf("  %d 个空任务，每任务新建线程: %.0f ms（%.1f μs/个）\n", N, ms_raw, ms_raw * 1000 / N);
    printf("  %d 个空任务，8 线程池复用    : %.0f ms（%.2f μs/个）\n", N, ms_pool, ms_pool * 1000 / N);
    printf("  → 池化快 %.1fx —— 省下的是 pthread_create + 8MB 栈映射（第 40 章实测）\n", ms_raw / ms_pool);

    printf("\n== ② 标准库没有线程池 ==\n");
    printf("  上面的 ThreadPool 有 40 行样板：队列 + 互斥量 + 条件变量 + 停机协议\n");
    printf("  std::thread（C++11）: 只是 OS 线程的薄封装，没有复用\n");
    printf("  std::async（C++11）:  实现可自由选择用不用池，行为不可移植\n");
    printf("  std::jthread（C++20）: 自动 join + 停止令牌，仍然不是池\n");
    printf("  std::execution（C++26）: 终于有了标准的执行器/调度器\n");
    printf("  → 与第 42 章的协程、第 44 章的 promise_type 同一个模式：给机制不给策略\n");

    printf("\n== ③ 队列积压：无界队列的危险直接可见 ==\n");
    {
        ThreadPool pool(2);
        for (int i = 0; i < 2000; ++i)
            pool.submit([] { std::this_thread::sleep_for(std::chrono::microseconds(100)); });
        printf("  向 2 线程的池提交 2000 个任务，队列峰值积压: %zu 个\n", pool.peak_queue());
        printf("  每个 std::function 至少 32 字节 → 百万任务积压 = 几十 MB 凭空占用\n");
        printf("  → 手写池最容易漏掉的就是【队列上界】和【拒绝策略】\n");
        pool.wait_idle();
    }

    printf("\n== ④ 池大小曲线：CPU 密集任务 ==\n");
    const long WORK = 400000000L;
    for (unsigned size : {1u, 2u, 4u, 8u, cores, cores * 2, cores * 4}) {
        std::atomic<long long> sum{0};
        auto s0 = Clock::now();
        {
            ThreadPool pool(size);
            const int CHUNKS = 40;
            for (int c = 0; c < CHUNKS; ++c) {
                pool.submit([&sum] {
                    long long local = 0;
                    for (long j = 0; j < WORK / 40; ++j) {
                        local += j;
                        __asm__ volatile("" ::"r"(local) : "memory");  // 阻止 -O2 把循环算成闭式（第 33 章的教训）
                    }
                    sum.fetch_add(local);
                });
            }
            pool.wait_idle();
        }
        double ms = ms_since(s0);
        const char* tag = (size == cores) ? "   ← 核心数" : (size > cores ? "   （超出核心数，收益归零）" : "");
        printf("  池大小 %2u: %6.1f ms%s\n", size, ms, tag);
    }
    printf("  → CPU 密集: 线程数 ≈ 核心数（本机 %u），再多只增加切换开销（第 40 章）\n", cores);

    printf("\n== ⑤ 手写池必须回答的六个问题 ==\n");
    printf("  ① 几条线程？          CPU 密集≈核心数；I/O 密集≈核心数×(1+等待/计算)\n");
    printf("  ② 队列多大？          无界=OOM 风险；有界=必须定拒绝策略\n");
    printf("  ③ 队列满了怎么办？    抛异常 / 提交者自己跑（反压）/ 丢弃\n");
    printf("  ④ 线程死了怎么办？    task() 抛异常会终结 worker —— 必须 try/catch 包住\n");
    printf("  ⑤ 怎么优雅停机？      stop 标志 + notify_all + join（本例的 shutdown）\n");
    printf("  ⑥ 任务里能提交任务吗？父任务等子任务会【线程饥饿死锁】（Java 版实测）\n");

    printf("\n== ⑥ 生产可用的替代品 ==\n");
    printf("  Intel TBB          : task_arena + 工作窃取（与 ForkJoinPool 同源思想）\n");
    printf("  Boost.Asio         : io_context + thread group（第 43 章的事件循环也用它）\n");
    printf("  OpenMP             : #pragma omp parallel for —— 编译器帮你建池\n");
    printf("  → 与其手写 40 行还漏掉异常安全，不如用这些\n");
    return 0;
}
