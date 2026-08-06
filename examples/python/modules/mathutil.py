"""被导入的模块。模块本身就是一个对象。"""
print("   [mathutil.py 被执行了]")

__all__ = ["average", "MAX_SCORE"]      # 声明 from mathutil import * 导出什么

_internal = "单下划线：约定为内部使用"   # 约定私有
MAX_SCORE = 100

def average(scores):
    return sum(scores) / len(scores) if scores else 0

if __name__ == "__main__":
    print("直接运行时才执行；被 import 时不执行")
