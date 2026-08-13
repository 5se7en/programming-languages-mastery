// 部署：C++ 的「可执行文件」并不自包含——编译期烙进去的每一项，都是一条部署约束。
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

/** 跑一条命令，收集前 n 行输出（拿不到就返回空） */
static std::vector<std::string> run(const char* cmd, size_t n = 8) {
    std::vector<std::string> out;
    FILE* p = popen(cmd, "r");
    if (!p) return out;
    char buf[512];
    while (out.size() < n && fgets(buf, sizeof(buf), p)) {
        std::string s(buf);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) out.push_back(s);
    }
    pclose(p);
    return out;
}

int main(int argc, char** argv) {
    // 被 ④ 用来测「一个进程从 exec 到退出」要多久
    if (argc > 1 && strcmp(argv[1], "--noop") == 0) return 0;

    printf("== ① 交付物：一个可执行文件有多大（实测）==\n");
    struct stat st{};
    if (argc > 0 && stat(argv[0], &st) == 0) {
        printf("  本程序: %s\n", argv[0]);
        printf("  大小: %lld 字节（%.2f MB）\n",
               (long long)st.st_size, st.st_size / 1048576.0);
    }
    printf("  → 对照: JVM 运行时约 300 MB、node 二进制约 100 MB、Python 标准库约 50 MB\n");
    printf("  → C++ 的产物小【两三个数量级】——因为它【没有把运行时带上】\n");
    printf("  → 这既是优势（镜像可以只有几 MB），也是下面 ② 那些约束的来源\n");

    printf("\n== ② 但它不是自包含的：动态依赖（实测）==\n");
#if defined(__APPLE__)
    auto deps = run(("otool -L '" + std::string(argv[0]) + "' 2>/dev/null").c_str(), 8);
#else
    auto deps = run(("ldd '" + std::string(argv[0]) + "' 2>/dev/null").c_str(), 8);
#endif
    if (deps.empty()) {
        printf("  （本平台上没能列出动态依赖）\n");
    } else {
        for (const auto& d : deps) printf("    %s\n", d.c_str());
    }
    printf("  → 这些库【不在你的交付物里】，必须由目标机器提供\n");
    printf("  → 缺一个、或版本对不上，程序就【启动失败或行为不同】\n");
    printf("  → 静态链接可以把它们打进来（产物变大、更新库要重新编译整个程序），\n");
    printf("     但 glibc 静态链接有已知问题（NSS/dlopen），musl 才是常见解法\n");

    printf("\n== ③ 编译期烙进二进制的东西（实测）==\n");
    printf("    目标架构     = %s\n",
#if defined(__aarch64__)
           "aarch64"
#elif defined(__x86_64__)
           "x86_64"
#else
           "其他"
#endif
    );
    printf("    操作系统     = %s\n",
#if defined(__APPLE__)
           "macOS"
#elif defined(__linux__)
           "Linux"
#elif defined(_WIN32)
           "Windows"
#else
           "其他"
#endif
    );
    printf("    C++ 标准     = %ld\n", (long)__cplusplus);
#if defined(_LIBCPP_VERSION)
    printf("    标准库       = libc++ %d\n", _LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
    printf("    标准库       = libstdc++ %d\n", __GLIBCXX__);
#endif
#if defined(__clang__)
    printf("    编译器       = clang %d.%d\n", __clang_major__, __clang_minor__);
#elif defined(__GNUC__)
    printf("    编译器       = gcc %d.%d\n", __GNUC__, __GNUC_MINOR__);
#endif
    printf("    指针宽度     = %zu 位\n", sizeof(void*) * 8);
    printf("    优化         = %s\n",
#ifdef __OPTIMIZE__
           "开启"
#else
           "关闭（-O0）"
#endif
    );
    printf("    断言(NDEBUG) = %s\n",
#ifdef NDEBUG
           "已关闭"
#else
           "仍开启"
#endif
    );
    printf("  → 上面每一行都是【构建时决定、运行时无法更改】的\n");
    printf("  → 「在我机器上是好的」在 C++ 里最常见的成因就是这张表对不上:\n");
    printf("     换了架构（x86 → ARM）、换了 libc（glibc → musl）、\n");
    printf("     换了标准库 ABI（_GLIBCXX_USE_CXX11_ABI 那次著名的分裂）\n");
    printf("  → 所以 C++ 的可复现构建必须【锁定工具链本身】，而不只是锁定依赖版本\n");

    printf("\n== ④ 启动时间：没有运行时要初始化（实测）==\n");
    auto t0 = std::chrono::steady_clock::now();
    long long s = 0;
    for (int i = 0; i < 5'000'000; ++i) s += i % 7;
    double workMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
    printf("  一段 500 万次循环的业务代码: %.1f ms（结果 %lld）\n", workMs, s);

    // 实测「起一个进程」的成本: 以 shell 内建的 true 作基线，差值就是本程序的启动开销
    const int R = 30;
    std::string self = "'" + std::string(argv[0]) + "' --noop";
    auto bench = [&](const char* cmd) {
        auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < R; ++i) { int rc = system(cmd); (void)rc; }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - a).count() / R;
    };
    double base = bench("true");                    // shell 内建，几乎不含 exec
    double mine = bench(self.c_str());
    printf("  启动本程序并立即退出 %d 次，平均: %.2f ms/次\n", R, mine);
    printf("  同样次数的 shell 内建 true（基线）: %.2f ms/次\n", base);
    printf("  → 差值 %.2f ms 就是【exec + 动态链接 + 退出】的成本\n", mine - base);
    printf("  → 对照本章其他语言的实测: JVM 进入 main() 前约 28 ms，Node 到第一行约 12 ms\n");
    printf("  → 这就是 CLI 工具和 Serverless 偏爱 C++/Go/Rust 的原因\n");
    printf("  → 也是 GraalVM 原生镜像、.NET AOT 想要达到的目标（C# 版讲这个）\n");

    printf("\n== ⑤ 不可变基础设施 vs 配置漂移 ==\n");
    printf("  【可变】的做法: ssh 上去 apt install、改配置、打补丁\n");
    printf("     → 每台机器的状态由「历史上执行过的所有命令」决定\n");
    printf("     → 几个月后没有人能说清任意两台机器有什么区别 —— 这就是【配置漂移】\n");
    printf("     → 最典型的症状: 「重启那台机器就好了」和「只有 3 号机有问题」\n");
    printf("  【不可变】的做法: 要改就重新构建一个镜像，然后替换整台机器\n");
    printf("     → 机器的状态【完全由构建产物决定】，与它的历史无关\n");
    printf("     → 回滚 = 换回上一个镜像，而不是「把改动反着做一遍」\n");
    printf("  → 关键洞察: 不可变基础设施把【运维】问题变成了【构建】问题——\n");
    printf("     而构建问题是可以版本化、可复现、可测试的（第 54 章）\n");

    printf("\n== ⑥ 容器为什么解决了 C++ 的部署问题 ==\n");
    printf("  容器镜像 = 【应用 + 它需要的整个用户态环境】(libc、libstdc++、证书、时区库)\n");
    printf("  → 它把 ② 里那些「必须由目标机器提供」的东西，变成了交付物的一部分\n");
    printf("  → 于是 C++ 也获得了 Java/Node 那种「带着运行时一起走」的可复现性，\n");
    printf("     而且因为基础层可以共享，代价比想象的小\n");
    printf("  → 多阶段构建是标准做法: 构建阶段带完整工具链，运行阶段只留产物\n");
    printf("     FROM gcc AS build ... → FROM debian-slim（只 COPY 出二进制）\n");
    printf("  → 极端做法是 FROM scratch（静态链接，镜像只有几 MB），\n");
    printf("     代价是没有 shell、没有证书、没有时区库 —— 出事时【无法进去看】\n");
    printf("  → 又一个熟悉的权衡: 【最小化】与【可诊断性】是对立的\n");
    return 0;
}
