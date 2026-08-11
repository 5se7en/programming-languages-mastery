// 锁：把任意长的代码段变成临界区——比原子操作贵，但能保护跨多变量的不变式。
#include <chrono>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

int counter = 0;
std::mutex big_lock;

struct Account {
    const char* name;
    int balance;
    std::mutex lock;
};

int main() {
    constexpr int N = 200'000;

    printf("== ① 锁 vs 原子：成本对比 ==\n");
    {
        counter = 0;
        auto t0 = std::chrono::steady_clock::now();
        auto work = [] { for (int i = 0; i < N; ++i) { std::lock_guard<std::mutex> g(big_lock); counter++; } };
        std::thread a(work), b(work);
        a.join(); b.join();
        double lock_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        printf("  互斥锁: 结果 = %d（期望 %d）✅，耗时 %.1f ms\n", counter, 2 * N, lock_ms);

        std::atomic<int> ac{0};
        auto t1 = std::chrono::steady_clock::now();
        auto atom_work = [&ac] { for (int i = 0; i < N; ++i) ac++; };
        std::thread c(atom_work), d(atom_work);
        c.join(); d.join();
        double atom_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t1).count();
        printf("  原子操作: 结果 = %d，耗时 %.1f ms\n", ac.load(), atom_ms);
        printf("  锁比原子慢 %.1f 倍——表达力更强（能保护任意代码段），代价也更高\n", lock_ms / atom_ms);
    }

    printf("\n== ② 钥匙实验：死锁（用 try_lock 安全地演示）==\n");
    {
        std::mutex m1, m2;
        std::atomic<int> both_held{0}, both_tried{0};        // 两道屏障：都持锁、都试过
        std::thread t1([&] {
            std::lock_guard<std::mutex> g1(m1);              // 先拿 m1
            both_held++;
            while (both_held < 2) std::this_thread::yield();  // 等对方也拿到它那把
            if (m2.try_lock()) { printf("  t1 拿到了两把锁\n"); m2.unlock(); }
            else printf("  t1: 持有 m1，抢 m2 失败——m2 被 t2 攥着\n");
            both_tried++;
            while (both_tried < 2) std::this_thread::yield();  // 等对方也试完再释放 m1
        });
        std::thread t2([&] {
            std::lock_guard<std::mutex> g2(m2);              // ⚠️ 顺序相反：先拿 m2
            both_held++;
            while (both_held < 2) std::this_thread::yield();
            if (m1.try_lock()) { printf("  t2 拿到了两把锁\n"); m1.unlock(); }
            else printf("  t2: 持有 m2，抢 m1 失败——m1 被 t1 攥着\n");
            both_tried++;
            while (both_tried < 2) std::this_thread::yield();
        });
        t1.join(); t2.join();
        printf("  ↑ 两边都失败 = 互相持有对方想要的锁，这就是死锁的现场\n");
        printf("  ↑ 若把 try_lock 换成 lock()，这里就会永久卡死（真死锁）\n");
    }

    printf("\n== ③ scoped_lock：C++ 的官方解药（C++17）==\n");
    {
        Account a{"A", 1000}, b{"B", 1000};
        auto transfer = [](Account& from, Account& to, int amount) {
            std::scoped_lock lk(from.lock, to.lock);         // ✅ 一次锁多把，内部避免死锁
            from.balance -= amount;
            to.balance += amount;
        };
        std::thread x([&] { for (int i = 0; i < 1000; ++i) transfer(a, b, 1); });
        std::thread y([&] { for (int i = 0; i < 1000; ++i) transfer(b, a, 1); });  // 反方向！
        x.join(); y.join();
        printf("  双向转账 1000 次后: A=%d, B=%d，总额 = %d（守恒 ✅，且不死锁）\n",
               a.balance, b.balance, a.balance + b.balance);
        printf("  scoped_lock 内部用「全部拿到才算成功，否则全放开重试」的算法\n");
    }

    printf("\n== ④ 锁粒度：一把大锁 vs 分段锁 ==\n");
    {
        constexpr int SHARDS = 8, PER = 100'000;
        // 粗锁：所有线程抢同一把
        {
            long total = 0;
            std::mutex m;
            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::thread> ts;
            for (int t = 0; t < 4; ++t)
                ts.emplace_back([&] { for (int i = 0; i < PER; ++i) { std::lock_guard<std::mutex> g(m); total++; } });
            for (auto& th : ts) th.join();
            printf("  一把大锁:   %7.1f ms（结果 %ld）\n",
                   std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count(), total);
        }
        // 细锁：每个分片一把，最后汇总
        {
            struct alignas(64) Shard { long value = 0; std::mutex m; };   // 缓存行对齐
            std::vector<Shard> shards(SHARDS);
            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::thread> ts;
            for (int t = 0; t < 4; ++t)
                ts.emplace_back([&, t] {
                    for (int i = 0; i < PER; ++i) {
                        auto& s = shards[(t * PER + i) % SHARDS];
                        std::lock_guard<std::mutex> g(s.m);
                        s.value++;
                    }
                });
            for (auto& th : ts) th.join();
            long total = 0;
            for (auto& s : shards) total += s.value;
            printf("  分段锁(8):  %7.1f ms（结果 %ld）\n",
                   std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count(), total);
        }
        printf("  ↑ 锁越细，争用越少，吞吐越高——代价是代码复杂、更易出错\n");
    }

    printf("\n== ⑤ 死锁的四个必要条件（缺一不可）==\n");
    printf("  ① 互斥：资源同时只能被一个线程持有\n");
    printf("  ② 持有并等待：拿着 A 去等 B\n");
    printf("  ③ 不可抢占：不能强行夺走别人的锁\n");
    printf("  ④ 循环等待：形成环（t1→t2→t1）\n");
    printf("  破解任意一条即可——scoped_lock 破 ④，try_lock 破 ②，超时锁破 ③\n");
    return 0;
}
