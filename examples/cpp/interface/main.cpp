// 第 28 章 · 接口 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o iface && ./iface
// C++ 没有 interface 关键字，但有两条路：纯虚类（运行期）和 Concept（编译期）

#include <concepts>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// ---------- ① 纯虚类：运行期契约 ----------
class Storage {
public:
    virtual ~Storage() = default;                         // ⚠️ 必须虚析构（第 26 章）
    virtual std::string save(const std::string& data) = 0;   // = 0 表示纯虚
};

class FileStorage : public Storage {
public:
    std::string save(const std::string& data) override { return "写入文件: " + data; }
};

class S3Storage : public Storage {
public:
    std::string save(const std::string& data) override { return "上传到 S3: " + data; }
};

class MemoryStorage : public Storage {
    std::vector<std::string> items;
public:
    std::string save(const std::string& data) override {
        items.push_back(data);
        return "存进内存: " + data;
    }
    std::size_t count() const { return items.size(); }
};

// ---------- ② 多继承接口类是安全的（因为没有状态）----------
class Flyable   { public: virtual ~Flyable() = default;   virtual std::string fly()  const = 0; };
class Swimmable { public: virtual ~Swimmable() = default; virtual std::string swim() const = 0; };
class Walkable  { public: virtual ~Walkable() = default;  virtual std::string walk() const = 0; };

class Animal {                                            // 有状态的基类，只继承一个
protected:
    std::string name;
public:
    explicit Animal(std::string n) : name(std::move(n)) {}
    virtual ~Animal() = default;
    const std::string& getName() const { return name; }
};

class Duck : public Animal, public Flyable, public Swimmable, public Walkable {
public:
    explicit Duck(std::string n) : Animal(std::move(n)) {}
    std::string fly()  const override { return "飞"; }
    std::string swim() const override { return "游"; }
    std::string walk() const override { return "走"; }
};

// ---------- ③ Concept：编译期契约（C++20）----------
template <typename T>
concept Speaker = requires(const T& t) {
    { t.speak() } -> std::convertible_to<std::string>;    // 要求有 speak() 且返回可转 string
};

struct Dog   { std::string speak() const { return "汪！"; } };
struct Robot { std::string speak() const { return "滴滴"; } };
struct Rock  { };                                          // 没有 speak()

template <Speaker T>                                       // 只接受满足 Speaker 的类型
std::string makeSpeak(const T& t) { return t.speak(); }

// ---------- ④ 依赖倒置 ----------
class ReportService {
    Storage& storage;                                      // 依赖契约（用引用避免所有权问题）
public:
    explicit ReportService(Storage& s) : storage(s) {}
    std::string generate(const std::string& content) {
        return storage.save("报表[" + content + "]");
    }
};

int main() {
    std::cout << "=== 1. 纯虚类 = C++ 版的接口 ===\n";
    {
        FileStorage fs;
        std::cout << "  只含纯虚函数、没有数据成员的类，就是 C++ 版的接口\n";
        std::cout << "    fs.save(\"data\") = " << fs.save("data") << "\n";
        std::cout << "  ⚠️ 必须写 virtual ~Storage() = default\n";
        std::cout << "     否则通过基类指针删除对象时，子类析构不会被调用（第 26 章）\n";
    }

    std::cout << "\n=== 2. 多继承接口类是安全的（因为没有状态）===\n";
    {
        Duck d("唐老鸭");
        std::cout << "  class Duck : public Animal, public Flyable, public Swimmable, public Walkable\n";
        std::cout << "    " << d.getName() << " 能: " << d.fly() << " " << d.swim()
                  << " " << d.walk() << "\n";
        std::cout << "  → C++ 本来就支持多继承，所以不需要专门的 interface 关键字\n";
        std::cout << "  → 无状态的纯虚类多继承不会引发菱形问题 —— 与 Java 的推理完全一致\n";
        std::cout << "  → 有状态的 Animal 只继承一个，这是自觉遵守的设计约束\n";
    }

    std::cout << "\n=== 3. Concept：编译期契约（C++20）===\n";
    {
        std::cout << "  makeSpeak(Dog{})   = " << makeSpeak(Dog{}) << "\n";
        std::cout << "  makeSpeak(Robot{}) = " << makeSpeak(Robot{}) << "\n";
        std::cout << "  Dog 和 Robot 有共同基类吗？ 没有 —— 但都满足 Speaker concept\n\n";
        std::cout << "  编译期就能判断：\n";
        std::cout << std::boolalpha;
        std::cout << "    Speaker<Dog>   = " << Speaker<Dog>   << "\n";
        std::cout << "    Speaker<Robot> = " << Speaker<Robot> << "\n";
        std::cout << "    Speaker<Rock>  = " << Speaker<Rock>  << "  ← 没有 speak()\n";
        std::cout << "    makeSpeak(Rock{}) → 编译错误：constraint not satisfied\n\n";
        std::cout << "  → C++20 Concept = 编译期检查的结构化契约\n";
        std::cout << "  → 与 Python 的 Protocol 是同一个思路，但检查发生在编译期\n";
    }

    std::cout << "\n=== 4. 纯虚类 vs Concept：怎么选 ===\n";
    std::cout << "                    纯虚类              Concept\n";
    std::cout << "    契约检查        运行期（vtable）     编译期\n";
    std::cout << "    运行时开销      一次间接跳转         零\n";
    std::cout << "    需要继承        ✅                  ❌\n";
    std::cout << "    能放进同一容器  ✅ vector<ptr<S>>   ❌ 类型各异\n";
    std::cout << "    错误信息        清晰                C++20 前的模板错误极难读\n";
    std::cout << "  → Concept 的一大价值就是错误信息：\n";
    std::cout << "     以前模板参数不满足要求会喷出几十行天书，现在直接说「不满足 Speaker」\n";

    std::cout << "\n=== 5. 依赖倒置：换实现不用改代码 ===\n";
    {
        FileStorage fs;
        S3Storage s3;
        MemoryStorage mem;
        Storage* impls[] = {&fs, &s3, &mem};
        const char* names[] = {"FileStorage", "S3Storage", "MemoryStorage"};

        for (int i = 0; i < 3; ++i) {
            ReportService svc(*impls[i]);
            std::cout << "    " << names[i] << " → " << svc.generate("月度") << "\n";
        }
        std::cout << "  → ReportService 的代码一个字都不用改\n";

        std::cout << "\n  用内存实现做单元测试：\n";
        MemoryStorage testStore;
        ReportService svc(testStore);
        svc.generate("一月");
        svc.generate("二月");
        std::cout << "    生成 2 份报表后，MemoryStorage 里有 " << testStore.count() << " 条\n";
        std::cout << "  → 不碰真实文件/网络就能验证业务逻辑\n";
    }

    std::cout << "\n=== 6. 纯虚类能放进同一个容器（Concept 做不到）===\n";
    {
        std::vector<std::unique_ptr<Storage>> stores;
        stores.push_back(std::make_unique<FileStorage>());
        stores.push_back(std::make_unique<S3Storage>());
        stores.push_back(std::make_unique<MemoryStorage>());

        for (const auto& s : stores)
            std::cout << "    " << s->save("统一处理") << "\n";
        std::cout << "  → 这是运行期契约的独有能力：不同类型放进同一个容器\n";
        std::cout << "  → Concept 是编译期的，每个类型是独立的，做不到这一点\n";
    }

    std::cout << "\n=== 7. 小结 ===\n";
    std::cout << "  · C++ 没有 interface 关键字，因为它本来就支持多继承\n";
    std::cout << "  · 纯虚类 = 运行期契约，能放进同一容器，代价是一次间接跳转\n";
    std::cout << "  · Concept = 编译期契约，零开销，但类型各异不能统一存放\n";
    std::cout << "  · 接口类必须写 virtual ~Base() = default\n";
    return 0;
}
