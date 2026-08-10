// RAII：资源获取即初始化——把资源的生命周期焊在对象的生命周期上。
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

// 一个最小的 RAII 类：构造获取资源，析构释放资源
class FileHandle {
    std::string name;
public:
    explicit FileHandle(std::string n) : name(std::move(n)) {
        std::cout << "    [获取] 打开 " << name << "\n";
    }
    ~FileHandle() {
        std::cout << "    [释放] 关闭 " << name << "\n";
    }
};

void manual_style() {                       // 手动风格：异常一来，释放就被跳过
    FileHandle* f = new FileHandle("manual.txt");
    throw std::runtime_error("中途出错");
    delete f;                               // ⚠️ 永远执行不到——泄漏（第 33 章 leaks 实测过）
}

void raii_style() {                         // RAII 风格：栈展开时析构必被调用
    FileHandle f("raii.txt");
    throw std::runtime_error("中途出错");
}                                           // 析构在这里执行——即便异常正在飞

int main() {
    std::cout << "== ① RAII 基础：作用域即资源生命周期 ==\n";
    {
        FileHandle f("data.txt");
        std::cout << "    使用中……\n";
    }
    std::cout << "    块结束——无需任何 close 调用\n";

    std::cout << "\n== ② 钥匙实验：异常安全 ==\n";
    std::cout << "  手动风格（new + delete）:\n";
    try { manual_style(); }
    catch (const std::exception& e) { std::cout << "    捕获: " << e.what()
        << "   <- 没有任何 [释放] 打印！资源泄漏\n"; }
    std::cout << "  RAII 风格（栈对象）:\n";
    try { raii_style(); }
    catch (const std::exception& e) { std::cout << "    捕获: " << e.what()
        << "   <- [释放] 已在上一行打印：栈展开自动析构\n"; }

    std::cout << "\n== ③ 多个资源：逆序释放，异常也不例外 ==\n";
    try {
        FileHandle a("第一个");
        FileHandle b("第二个");
        FileHandle c("第三个");
        throw std::runtime_error("三个都开着的时候出错了");
    } catch (const std::exception&) {
        std::cout << "    三个全部释放，顺序是 3-2-1（构造的逆序）\n";
    }

    std::cout << "\n== ④ 标准库处处是 RAII ==\n";
    {
        std::mutex m;
        std::lock_guard<std::mutex> guard(m);   // 构造上锁，析构解锁
        std::cout << "    lock_guard 持锁中——就算这里抛异常，锁也一定会解开\n";
    }
    std::cout << "    锁已释放（guard 析构）\n";
    std::cout << "    同款：unique_ptr(内存) / ofstream(文件) / unique_lock / scoped_lock\n";

    std::cout << "\n== ⑤ 早返回也安全 ==\n";
    auto early_return = [](bool quit) {
        FileHandle f("early.txt");
        if (quit) { std::cout << "    提前 return——\n"; return; }
        std::cout << "    正常走完——\n";
    };
    early_return(true);
    std::cout << "    每一条离开作用域的路，都会经过析构函数\n";
    return 0;
}
