// 构建工具：C++ 的编译模型——为什么改一个头文件要重编一片；以及头文件展开的真实代价。
// 本程序【构建并调用真实的 g++】来测量，不是模拟。
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static std::string tmpdir;

static void write_file(const std::string& name, const std::string& content) {
    FILE* f = fopen((tmpdir + "/" + name).c_str(), "w");
    fputs(content.c_str(), f);
    fclose(f);
}

// 返回预处理后的行数（-E 展开所有 #include）
static long preprocessed_lines(const std::string& src) {
    std::string cmd = "g++ -E -I" + tmpdir + " " + tmpdir + "/" + src + " 2>/dev/null | wc -l";
    FILE* p = popen(cmd.c_str(), "r");
    long n = 0;
    if (p) { fscanf(p, "%ld", &n); pclose(p); }
    return n;
}

int main() {
    char tmpl[] = "/tmp/pl-mastery-cppbuild-XXXXXX";
    tmpdir = mkdtemp(tmpl);

    printf("== ① C++ 的编译单元：每个 .cpp 独立编译，头文件被【复制】进去 ==\n");
    printf("  C++ 没有模块（C++20 前）: #include 是【文本粘贴】——头文件内容被拷进每个用它的 .cpp\n");
    printf("  → 于是: 改一个头文件 = 所有 #include 它的 .cpp 全部过期，必须重编\n");
    printf("  → 这就是 C++ 增量构建的核心难点: 依赖关系藏在预处理器的 #include 展开里\n\n");

    // 建一个共享头 + 三个 cpp
    write_file("common.hpp", "#pragma once\nstruct Config { int a, b, c; };\nint helper(int);\n");
    write_file("a.cpp", "#include \"common.hpp\"\nint fa(Config c){return c.a+helper(c.b);}\n");
    write_file("b.cpp", "#include \"common.hpp\"\nint fb(Config c){return c.b*2;}\n");
    write_file("c.cpp", "#include <cstdio>\nvoid fc(){printf(\"no shared header\\n\");}\n");
    printf("  依赖: a.cpp、b.cpp 都 #include \"common.hpp\"；c.cpp 不 include 它\n");
    printf("  → 改 common.hpp: a.cpp、b.cpp 必须重编，c.cpp 不用——扇出 = 2\n");
    printf("  → 这正是 Python 版 ② 的抽象在 C++ 里的真身: 头文件是依赖图里扇出最大的节点\n");

    printf("\n== ② 头文件展开的真实代价（调用 g++ -E 实测）==\n");
    write_file("tiny.cpp", "int add(int a,int b){return a+b;}\n");
    write_file("with_iostream.cpp", "#include <iostream>\nint add(int a,int b){return a+b;}\n");
    write_file("with_vector.cpp", "#include <vector>\n#include <string>\n#include <map>\nint add(int a,int b){return a+b;}\n");

    long l0 = preprocessed_lines("tiny.cpp");
    long l1 = preprocessed_lines("with_iostream.cpp");
    long l2 = preprocessed_lines("with_vector.cpp");
    printf("  一行加法函数，预处理后:\n");
    printf("    不含任何头文件:        %6ld 行\n", l0);
    printf("    #include <iostream>:   %6ld 行（膨胀 %ldx）\n", l1, l0 ? l1 / (l0 ? l0 : 1) : 0);
    printf("    #include <vector/string/map>: %6ld 行（膨胀 %ldx）\n", l2, l0 ? l2 / (l0 ? l0 : 1) : 0);
    printf("  → 你写的两行代码，编译器实际要处理【几万行】——每个 .cpp 都重新处理一遍\n");
    printf("  → 这就是 C++ 编译慢的第一大来源: 头文件在每个编译单元里被重复展开和解析\n");

    printf("\n== ③ 编译真实测一次增量 vs 全量 ==\n");
    auto compile_one = [](const std::string& src, const std::string& obj) {
        std::string cmd = "g++ -std=c++17 -I" + tmpdir + " -c " + tmpdir + "/" + src +
                          " -o " + tmpdir + "/" + obj + " 2>/dev/null";
        return system(cmd.c_str());
    };
    auto t0 = Clock::now();
    compile_one("a.cpp", "a.o"); compile_one("b.cpp", "b.o"); compile_one("c.cpp", "c.o");
    double full = ms_since(t0);

    // 改 c.cpp（不含共享头）: 增量只需重编 c.o
    t0 = Clock::now();
    compile_one("c.cpp", "c.o");
    double inc_c = ms_since(t0);

    // 改 common.hpp: 增量要重编 a.o + b.o
    t0 = Clock::now();
    compile_one("a.cpp", "a.o"); compile_one("b.cpp", "b.o");
    double inc_hdr = ms_since(t0);

    printf("  全量编译 3 个 .cpp:        %.0f ms\n", full);
    printf("  改 c.cpp（无共享头）→ 重编 1 个: %.0f ms（省了 %.0f%%）\n", inc_c, 100 - 100 * inc_c / full);
    printf("  改 common.hpp → 重编 2 个:  %.0f ms（省了 %.0f%%）\n", inc_hdr, 100 - 100 * inc_hdr / full);
    printf("  → 改动落在依赖图哪个节点，决定了增量能省多少——头文件是最贵的改动\n");

    printf("\n== ④ 链接：编译之后的第二步（把 .o 拼成可执行）==\n");
    printf("  编译: 每个 .cpp → .o（各自独立，可并行，可缓存——ccache 就缓存这一步）\n");
    printf("  链接: 所有 .o + 库 → 可执行文件（解析跨文件的符号引用，第 53 章的符号世界）\n");
    printf("  → 链接【不能增量】: 改一个 .o 就要重链整个可执行——大项目链接常是瓶颈\n");
    printf("  → 所以有了 lld/mold 这样的并行链接器，和「增量链接」这种脆弱的优化\n");

    printf("\n== ⑤ C++ 减少重编的四种武器 ==\n");
    printf("  ① 前置声明(forward declaration): 头文件里能不 #include 就不 include\n");
    printf("     —— 少一条依赖边，就少一片重编（Pimpl 是它的极致，第 53 章 ABI 也靠它）\n");
    printf("  ② 预编译头(PCH): 把 <vector> 这类稳定大头文件预处理一次，复用（治 ② 的膨胀）\n");
    printf("  ③ ccache: 编译结果按【预处理后内容的哈希】缓存（Python 版 ⑤ 的 C++ 版）\n");
    printf("  ④ C++20 modules: 从根上取消文本展开——import 一次解析，产出二进制接口\n");
    printf("     —— 这才是治本: 让 C++ 终于有了 JS/Java 早就有的「模块」（第 14/53 章）\n");

    std::string clean = "rm -rf " + tmpdir;
    system(clean.c_str());
    return 0;
}
