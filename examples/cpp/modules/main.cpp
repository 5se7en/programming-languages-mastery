// 第 14 章 · 模块 — C++ 示例
// 运行：g++ -std=c++17 -o main *.cpp && ./main
// 注意：#include 只是预处理器的「文本粘贴」，不是模块系统。
//       用 g++ -E main.cpp 可以看到头文件内容被原样插入。
#include <iostream>
#include <vector>
#include "mathutil.h"       // 引号：先找当前目录
                            // 尖括号 <> ：找系统/标准库路径

namespace app = util;       // 命名空间别名

int main() {
    std::vector<int> scores = {92, 75, 50};

    std::cout << "跨文件调用: " << util::average(scores)
              << " | 常量: " << util::MAX_SCORE << "\n";
    std::cout << "命名空间别名: " << app::average(scores) << "\n";
    std::cout << "#include 是文本粘贴，声明在 .h、实现在 .cpp，最后由链接器合并\n";
    std::cout << "C++20 起可用 import 实现真正的模块\n";
    return 0;
}
