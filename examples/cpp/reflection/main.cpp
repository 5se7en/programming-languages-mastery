// C++ 没有反射——只有 RTTI（运行时类型识别）。
// 本例演示 RTTI 能做什么、不能做什么，以及生态里的手工替代方案。
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>

struct Student {
    virtual ~Student() = default;
    std::string name = "未命名";
    int score = 0;
    virtual std::string title() const { return "学生"; }
};

struct GradStudent : Student {
    std::string title() const override { return "研究生"; }
};

int main() {
    std::cout << std::boolalpha;

    std::cout << "== ① RTTI：typeid 能问\"动态类型是什么\" ==\n";
    std::unique_ptr<Student> s = std::make_unique<GradStudent>();
    const Student& ref = *s;
    std::cout << "静态类型 Student&，typeid(ref) = " << typeid(ref).name()
              << "   <- 认出了 GradStudent\n";
    std::cout << "typeid(ref) == typeid(GradStudent): " << (typeid(ref) == typeid(GradStudent)) << "\n";

    std::cout << "\n== ② dynamic_cast：带检查的向下转型 ==\n";
    if (auto* g = dynamic_cast<GradStudent*>(s.get())) {
        std::cout << "转成 GradStudent 成功: " << g->title() << "\n";
    }
    Student plain;
    std::cout << "把普通 Student 转成 GradStudent: "
              << (dynamic_cast<GradStudent*>(&plain) == nullptr ? "nullptr（安全失败）" : "成功")
              << "\n";

    std::cout << "\n== ③ RTTI 的边界：到此为止 ==\n";
    std::cout << "枚举 Student 有哪些字段/方法？  做不到——语言里没有这个能力\n";
    std::cout << "按字符串名字调用 title()？      做不到——函数名编译后就消失了\n";

    std::cout << "\n== ④ 生态的替代：手工注册表（框架的常见做法） ==\n";
    std::map<std::string, std::function<std::unique_ptr<Student>()>> factory;
    factory["Student"] = [] { return std::make_unique<Student>(); };
    factory["GradStudent"] = [] { return std::make_unique<GradStudent>(); };
    auto obj = factory["GradStudent"]();               // “按字符串创建对象”——自己登记才有
    std::cout << "factory[\"GradStudent\"]() -> title() = " << obj->title() << "\n";
    std::cout << "（Qt 的 moc、protobuf 的代码生成，本质都是自动化的这张表）\n";
    return 0;
}
