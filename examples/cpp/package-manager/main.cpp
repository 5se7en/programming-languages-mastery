// 包管理：C++ 的困境——semver 管不到 ABI；以及 inline namespace 这个「链接期版本号」。
#include <cstdio>
#include <cstring>

// ============ ① ABI 断裂的实测演示 ============
// 库 v1.2.0 的头文件里，User 长这样:
struct UserV1 {
    int id;
    char name[16];
};

// 库 v1.3.0「小版本升级」: 在中间加了个字段——API 完全兼容（旧代码照常编译）
struct UserV2 {
    int id;
    int age;            // ← 新增字段插在中间
    char name[16];
};

// ============ ③ inline namespace: C++ 的「链接期版本号」 ============
namespace httplib {
    inline namespace v2 {                    // inline: 外界写 httplib::get 就用 v2
        const char* get() { return "v2 的实现（默认）"; }
    }
    namespace v1 {                           // 老版本仍然存在，显式写 httplib::v1::get
        const char* get() { return "v1 的实现（显式选择）"; }
    }
}

int main() {
    printf("== ① semver 的盲区：ABI（实测一次内存布局错位）==\n");
    printf("  库从 1.2.0 升到 1.3.0，只是在 struct User 中间加了个 age 字段\n");
    printf("  API 视角: 完全兼容——所有旧代码【重新编译】都能过\n");
    printf("  ABI 视角: 灾难——sizeof 从 %zu 变成 %zu，name 的偏移从 %zu 挪到 %zu\n",
           sizeof(UserV1), sizeof(UserV2),
           offsetof(UserV1, name), offsetof(UserV2, name));

    // 模拟: app 是用 v1 头文件编译的（没重编），库已经换成 v2 —— 同一块内存两种解读
    UserV2 real{42, 30, "zhang"};                       // 库(v2)写入的真实数据
    UserV1* stale = (UserV1*)&real;                     // app 还按 v1 布局去读
    printf("\n  库(v2)写入: id=42, age=30, name=\"zhang\"\n");
    printf("  未重编的 app 按 v1 布局读出: id=%d, name=\"%.15s\"\n", stale->id, stale->name);
    printf("  → name 读到的是【age 的字节 + 错位的字符串】——数据悄悄坏掉，不崩溃、无警告\n");
    printf("  → 这就是 ABI 断裂: 结构体布局/虚表顺序/内联函数……都不在 semver 的 API 承诺里\n");
    printf("  → 惨案清单: 改私有成员(布局变)、加虚函数(虚表变)、改 std::string 实现(GCC 5)\n");

    printf("\n== ② 为什么 C++ 一个进程只能有一个版本（比 Java 还严格）==\n");
    printf("  ODR（单一定义规则）: 整个程序里每个符号只能有一个定义\n");
    printf("  两个静态库各内嵌 fmt 的不同版本 → 链接期 duplicate symbol 直接报错\n");
    printf("  更阴险的形态: 符号恰好不冲突时【静默选一个】——两个版本的对象互相解读，回到 ①\n");
    printf("  → Java 好歹能用 ClassLoader 隔离（Java 版实测），C++ 的链接器没有命名空间概念\n");
    printf("  → 对比 npm 的自由共存（JS 版实测）: C++ 是约束最强的一端\n");

    printf("\n== ③ inline namespace：把版本号写进符号名（实测）==\n");
    printf("  httplib::get()     → %s\n", httplib::get());
    printf("  httplib::v1::get() → %s\n", httplib::v1::get());
    printf("  → inline namespace 让「默认版本」可以升级，而老版本【换个名字继续共存】\n");
    printf("  → 符号名里带着版本(mangled: httplib::v2::get vs v1::get)——不再违反 ODR\n");
    printf("  → libc++ 的 std::__1::string、glibc 的 symbol versioning 都是这个思想:\n");
    printf("     【把版本编进符号】是链接器世界里唯一的多版本共存术\n");

    printf("\n== ④ C++ 没有统一包管理器的真正原因 ==\n");
    printf("  npm/pip/NuGet 分发的是【源码或平台无关字节码】——一份产物到处能用\n");
    printf("  C++ 的产物是【机器码】，它绑定了:\n");
    printf("    编译器(gcc/clang/msvc) × 标准库(libstdc++/libc++) × 构建类型(Debug/Release)\n");
    printf("    × ABI 开关(_GLIBCXX_USE_CXX11_ABI) × 平台/架构 —— 组合爆炸\n");
    printf("  → 预编译二进制几乎不可能通用，所以:\n");
    printf("     vcpkg/conan 的默认答案是【从源码构建一切依赖】（用你的编译器、你的开关）\n");
    printf("     代价: 第一次构建几十分钟起——这是 ABI 组合爆炸的直接账单\n");

    printf("\n== ⑤ 六生态的分发单位对照（本章总纲的 C++ 视角）==\n");
    printf("  npm   : JS 源码          —— 解释执行，无 ABI 问题\n");
    printf("  pip   : 源码 + wheel     —— C 扩展的 wheel 按平台×Python版本预编译(manylinux)\n");
    printf("  Maven : jar（字节码）    —— JVM 统一了 ABI，一份 jar 到处跑\n");
    printf("  NuGet : dll（IL 字节码） —— CLR 同上\n");
    printf("  vcpkg : 源码             —— 因为机器码 ABI 无法统一（④）\n");
    printf("  → 规律: 【离机器越近，包管理越难】——ABI 是那道所有人都绕不过的墙\n");

    printf("\n== ⑥ C++ 工程的实务清单 ==\n");
    printf("  ① 库的公共头文件里: 用 Pimpl 隐藏成员布局（加字段不再破坏 ABI）\n");
    printf("  ② 跨 so/dll 边界: 只传 C 风格接口或稳定 ABI 类型，别传 std::string\n");
    printf("  ③ 升级依赖后: 全量重编（make clean）——① 的错位就是增量编译的经典事故\n");
    printf("  ④ vcpkg manifest 模式(vcpkg.json) + 版本锁(builtin-baseline)——锁文件思想的 C++ 版\n");
    return 0;
}
