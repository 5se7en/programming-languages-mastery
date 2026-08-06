# 第 14 章 · 模块 — Python 示例
# 运行：python3 main.py

import sys

# 1. 三种导入方式
import mathutil
from mathutil import average
from mathutil import MAX_SCORE as MAX

print("导入整个模块:", round(mathutil.average([92, 75, 50]), 2))
print("导入单个名字:", round(average([92, 75, 50]), 2), "| 别名导入:", MAX)

# 2. 模块只加载一次（sys.modules 缓存）
import mathutil          # 不会再打印 "[mathutil.py 被执行了]"
print("再次 import → 没有重复执行；在 sys.modules 中吗:", "mathutil" in sys.modules)

# 3. 模块本身是对象
print("模块的类型:", type(mathutil).__name__, "| __all__ =", mathutil.__all__)

# 4. 单下划线只是约定，并非强制私有
print("仍能访问 _internal:", mathutil._internal, "← 约定而非强制")

# 5. __name__ 惯用法
print("当前模块的 __name__ =", __name__, "| 被导入模块的 =", mathutil.__name__)
