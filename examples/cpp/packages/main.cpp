// 第 15 章 · 包 — C++ 示例
// 运行：g++ -std=c++17 -o main *.cpp && ./main
#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <sstream>

struct SemVer {
    int major = 0, minor = 0, patch = 0;
    std::string pre;

    static SemVer parse(const std::string& s) {
        SemVer v;
        auto dash = s.find('-');
        std::string core = (dash == std::string::npos) ? s : s.substr(0, dash);
        if (dash != std::string::npos) v.pre = s.substr(dash + 1);
        std::istringstream iss(core);
        char dot;
        iss >> v.major >> dot >> v.minor >> dot >> v.patch;
        return v;
    }
    // 按数字比较；预发布版排在正式版之前
    int compare(const SemVer& o) const {
        auto a = std::make_tuple(major, minor, patch);
        auto b = std::make_tuple(o.major, o.minor, o.patch);
        if (a != b) return a < b ? -1 : 1;
        if (!pre.empty() && o.pre.empty()) return -1;
        if (pre.empty() && !o.pre.empty()) return 1;
        return 0;
    }
};

bool satisfies(const std::string& version, const std::string& spec) {
    char op = (spec[0] == '^' || spec[0] == '~') ? spec[0] : '=';
    SemVer base = SemVer::parse(op == '=' ? spec : spec.substr(1));
    SemVer v = SemVer::parse(version);
    if (!v.pre.empty()) return false;
    if (v.compare(base) < 0) return false;
    if (op == '^') return v.major == base.major;
    if (op == '~') return v.major == base.major && v.minor == base.minor;
    return v.compare(base) == 0;
}

int main() {
    std::vector<std::string> versions = {"1.2.3", "1.2.9", "1.3.0", "1.9.9", "2.0.0"};
    for (const auto& spec : {"^1.2.3", "~1.2.3", "1.2.3"}) {
        std::cout << spec << "\t匹配 → ";
        for (const auto& v : versions)
            if (satisfies(v, spec)) std::cout << v << " ";
        std::cout << "\n";
    }

    std::cout << "\n字符串比较 \"1.10.0\" > \"1.9.0\" → "
              << (std::string("1.10.0") > std::string("1.9.0") ? "true" : "false") << " ← 错误！\n";
    std::cout << "数字比较   SemVer 1.10.0 > 1.9.0 → "
              << (SemVer::parse("1.10.0").compare(SemVer::parse("1.9.0")) > 0 ? "true" : "false")
              << " ← 正确\n";

    std::cout << "\nC++ 没有官方包管理器，第三方方案：vcpkg / Conan\n";
    std::cout << "根本困难：包需按「平台 x 编译器 x 编译选项」分别编译（ABI 不兼容）\n";
    std::cout << "所以 vcpkg 分发的常是源码，需在本机现场编译\n";
    return 0;
}
