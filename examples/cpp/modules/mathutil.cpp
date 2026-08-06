#include "mathutil.h"

namespace util {
    double average(const std::vector<int>& scores) {
        if (scores.empty()) return 0;
        int sum = 0;
        for (int s : scores) sum += s;
        return static_cast<double>(sum) / scores.size();
    }
}
