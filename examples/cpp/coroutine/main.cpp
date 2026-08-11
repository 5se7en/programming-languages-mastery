// 协程：C++20 的 co_yield/co_await——但要自己实现 promise_type（第 42 章：给机制不给策略）。
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <string>
#include <vector>

// ---------- 自己实现一个 Generator 类型（标准库没有提供）----------
template <typename T>
struct Generator {
    struct promise_type {                       // ← 协程的「驱动器」，必须自己写
        T current_value;
        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }   // 创建后立刻暂停
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T value) {             // co_yield 走这里
            current_value = std::move(value);
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    explicit Generator(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Generator() { if (handle) handle.destroy(); }
    Generator(Generator&& o) noexcept : handle(o.handle) { o.handle = nullptr; }
    Generator(const Generator&) = delete;

    bool next() {                                // 恢复协程，跑到下一个 co_yield
        if (!handle || handle.done()) return false;
        handle.resume();
        return !handle.done();
    }
    T value() const { return handle.promise().current_value; }
    bool done() const { return !handle || handle.done(); }
};

// ① 生成器协程：co_yield 暂停，resume() 恢复
Generator<std::string> counter(std::string name, int n) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
        total += i;
        co_yield name + ": 第 " + std::to_string(i) + " 步，累计 " + std::to_string(total);
    }
}

int main() {
    printf("== ① C++20 协程：co_yield 暂停，resume 恢复 ==\n");
    {
        auto gen = counter("A", 3);
        printf("  创建协程后函数体一行都没执行（initial_suspend 立刻暂停）\n");
        gen.next();
        printf("  第一次 resume: %s\n", gen.value().c_str());
        gen.next();
        printf("  第二次 resume: %s   ← 从上次 co_yield 的下一行继续，total 还在\n",
               gen.value().c_str());
        printf("  局部变量 i、total 存在【coroutine frame】里——编译器在堆上分配的（第 32 章）\n");
    }

    printf("\n== ② C++ 的特殊之处：要自己写 promise_type ==\n");
    printf("  上面的 Generator<T> 有 40 行样板代码，标准库一行都没给\n");
    printf("  必须实现: get_return_object / initial_suspend / final_suspend\n");
    printf("            yield_value / return_void / unhandled_exception\n");
    printf("  → 第 42 章的结论再次印证：C++ 给了机制，策略要你自己（或第三方库）提供\n");
    printf("  → C++23 才加入 std::generator，这个样板终于可以省掉\n");

    printf("\n== ③ 钥匙实验：三十行搭一个协程调度器 ==\n");
    {
        std::vector<Generator<std::string>> tasks;
        tasks.push_back(counter("协程甲", 3));
        tasks.push_back(counter("协程乙", 2));
        std::deque<size_t> queue{0, 1};
        while (!queue.empty()) {
            size_t idx = queue.front();
            queue.pop_front();
            if (tasks[idx].next()) {
                printf("    %s\n", tasks[idx].value().c_str());
                queue.push_back(idx);            // 排回队尾
            }
        }
        printf("  ↑ 两个协程交替推进——与其他四种语言输出完全一致\n");
    }

    printf("\n== ④ coroutine frame：栈帧真的搬到了堆上 ==\n");
    printf("  编译器把协程的局部变量、参数、暂停点编号打包成一个 frame\n");
    printf("  frame 默认用 operator new 分配在堆上（第 33 章的分配成本）\n");
    printf("  handle.resume()  : 从 frame 恢复执行\n");
    printf("  handle.destroy() : 销毁 frame（本例在析构里做，RAII，第 37 章）\n");
    printf("  → 这是第 32 章「帧可以住在堆上」在 C++ 里最直白的形态\n");

    printf("\n== ⑤ HALO 优化：C++ 独有的「协程不分配」==\n");
    printf("  若编译器能证明协程的生命周期不超过调用者，可把 frame 分配到【栈上】\n");
    printf("  → 零堆分配的协程（Halo: Heap Allocation eLision Optimization）\n");
    printf("  → 这是 C++ 相对 JS/Python/C# 的独有优势（它们的协程必然堆分配）\n");

    printf("\n== ⑥ 三个关键字与它们的用途 ==\n");
    printf("  co_yield  : 产出一个值并暂停（生成器，本节主角）\n");
    printf("  co_await  : 等待一个可等待对象（异步，第 42 章）\n");
    printf("  co_return : 协程结束并返回\n");
    printf("  函数体里出现任意一个，这个函数就变成协程（无需特殊标注——与 async 关键字不同）\n");

    printf("\n== ⑦ 五语言协程能力对照 ==\n");
    printf("  Python : yield / async（无栈）+ greenlet（有栈，第三方）\n");
    printf("  JS     : function* / async（无栈，无有栈方案）\n");
    printf("  C#     : yield return / async（无栈）\n");
    printf("  C++    : co_yield / co_await（无栈，但可 HALO 优化到栈上）\n");
    printf("  Java   : ❌ 无 yield —— 改用虚拟线程（有栈，运行时搬运）\n");
    return 0;
}
