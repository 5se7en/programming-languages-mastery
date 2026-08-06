# 第 12 章 · 函数 — Python 示例
# 运行：python3 functions.py

import sys

# 1. 定义与默认参数
def average(scores):
    return sum(scores) / len(scores) if scores else 0
print("平均分:", average([92, 75, 50]))

# 2. 灵活的参数机制
def greet(name="同学", *args, **kwargs):
    return f"{name} {args} {kwargs}"
print("可变参数:", greet("Alice", 1, 2, city="上海"))

# 3. ⚠️ 可变默认参数陷阱：默认值只在「定义时」创建一次
def bad(item, items=[]):
    items.append(item)
    return items
print("陷阱 bad(1):", bad(1))
print("陷阱 bad(2):", bad(2), " ← 上次的 1 还在！")

def good(item, items=None):        # 正确写法
    if items is None:
        items = []
    items.append(item)
    return items
print("正确 good(1):", good(1), "| good(2):", good(2), " ← 互不影响")

# 4. 值传递：传对象复制的是引用
def modify(d):   d["score"] = 60      # 改内容 → 外部可见
def reassign(d): d = {"score": 0}     # 重新绑定 → 外部不变
s = {"score": 92}
modify(s);   print("改内容后:  ", s["score"], " ← 外部可见")
reassign(s); print("重新赋值后:", s["score"], "← 外部没变")

# 5. 闭包
def make_counter():
    count = 0
    def inc():
        nonlocal count            # 修改外层变量必须声明 nonlocal
        count += 1
        return count
    return inc
c = make_counter(); c(); c()
print("闭包计数器:", c(), "← count 被记住了")

# 6. 递归深度上限
print("Python 递归上限:", sys.getrecursionlimit())
def depth(n=0):
    try: return depth(n + 1)
    except RecursionError: return n
print("实测递归到第", depth(), "层")
