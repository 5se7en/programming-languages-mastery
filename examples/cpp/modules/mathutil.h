#pragma once            // 头文件守卫：防止重复包含导致重复定义
#include <vector>

namespace util {        // 命名空间才是 C++ 真正的命名隔离
    const int MAX_SCORE = 100;
    double average(const std::vector<int>& scores);   // 只放声明，不放实现
}
