"""第 24 章 · 对象 —— Python 示例
运行：python3 main.py
"""

import sys

print("=== 1. Python 里一切皆对象，连整数都带头部 ===")
for value, label in [(1, "整数 1"), ("", "空字符串"), ([], "空列表"), ({}, "空字典")]:
    print(f"  {label:10} sys.getsizeof = {sys.getsizeof(value):3} 字节")
print("  → 没有「裸值」，所有东西都是带头部的对象")
print("  → 这就是 Python 内存占用高的根源")

print("\n=== 2. ⚠️ __dict__ vs __slots__：内存差距 ===")


class WithDict:
    """普通类：每个实例带一个 __dict__ 哈希表"""

    def __init__(self, x, y):
        self.x, self.y = x, y


class WithSlots:
    """__slots__ 类：固定偏移布局，没有 __dict__"""

    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x, self.y = x, y


def total_size(obj):
    """对象本体 + __dict__（如果有）"""
    body = sys.getsizeof(obj)
    d = getattr(obj, "__dict__", None)
    dict_size = sys.getsizeof(d) if d is not None else 0
    return body + dict_size, body, dict_size


a, b = WithDict(1, 2), WithSlots(1, 2)
ta, ba, da = total_size(a)
tb, bb, db = total_size(b)

print(f"  普通类:      对象本体 {ba} + __dict__ {da} = {ta} 字节")
print(f"  __slots__类: 对象本体 {bb} + __dict__ {db} = {tb} 字节")
print(f"  → 省了 {ta - tb} 字节，约 {100 * (ta - tb) / ta:.0f}%")
print(f"  → 一百万个对象省 {(ta - tb) * 1_000_000 / 1024 / 1024:.1f} MB")

print("\n=== 3. __slots__ 的代价：失去动态性 ===")
print("  普通类实例有 __dict__ 吗？ ", hasattr(a, "__dict__"), " 内容:", a.__dict__)
print("  slots 类实例有 __dict__ 吗？", hasattr(b, "__dict__"))

a.z = 3  # 普通类：随时可以加属性
print("  给普通类实例加属性 a.z = 3 →  成功，a.__dict__ =", a.__dict__)

try:
    b.z = 3
except AttributeError as e:
    print("  给 slots 实例加属性 b.z = 3 →  AttributeError:", e)

print("\n  → __dict__ 是「每个实例一个哈希表」，灵活但占内存（第 20 章）")
print("  → __slots__ 改用固定偏移的数组式布局，本质上就是变回了 C 的 struct")

print("\n=== 4. ⚠️ 继承时子类也要声明 __slots__ ===")


class Base:
    __slots__ = ("x",)

    def __init__(self, x):
        self.x = x


class ChildBad(Base):
    """✗ 没声明 __slots__，实例又有了 __dict__，优化前功尽弃"""


class ChildGood(Base):
    """✓ 声明空 __slots__，保持优化"""

    __slots__ = ()


cb, cg = ChildBad(1), ChildGood(1)
print(f"  ChildBad  有 __dict__ 吗？ {hasattr(cb, '__dict__')}  ← 优化没了")
print(f"  ChildGood 有 __dict__ 吗？ {hasattr(cg, '__dict__')}  ← 保持住了")

print("\n=== 5. 属性查找：先找实例，再找类 ===")


class Demo:
    class_attr = "来自类"

    def __init__(self):
        self.inst_attr = "来自实例"


d = Demo()
print(f"  d.inst_attr  = {d.inst_attr}   ← 在实例的 __dict__ 里找到")
print(f"  d.class_attr = {d.class_attr}   ← 实例里没有，去类里找")
print(f"  d.__dict__       = {d.__dict__}")
print(f"  'class_attr' 在实例 __dict__ 里吗？ {'class_attr' in d.__dict__}")
print("  → 这就是第 23 章那个陷阱的原理：读取会往上找，赋值只写实例")

print("\n=== 6. 什么时候该用 __slots__ ===")
print("  ✅ 要创建大量实例（十万级以上）")
print("  ✅ 属性集合固定，不需要动态添加")
print("  ✅ 性能敏感的数据类")
print("  ❌ 需要动态加属性")
print("  ❌ 只有几十个实例（省下的内存没有意义）")

print("\n=== 7. 大量数值应该用 array，而不是 list ===")
import array

n = 100_000
py_list = list(range(n))
py_array = array.array("i", range(n))     # 'i' = 有符号 int，紧凑存放

list_total = sys.getsizeof(py_list) + sum(sys.getsizeof(x) for x in py_list[:1000]) * n // 1000
print(f"  {n:,} 个整数:")
print(f"    list  容器本身 {sys.getsizeof(py_list):>9,} 字节")
print(f"          加上里面的 int 对象估算约 {list_total:>9,} 字节")
print(f"    array 总共     {sys.getsizeof(py_array):>9,} 字节  ← 数据紧凑存放，无逐个对象开销")
print("  → 需要处理大量数值时用 array / numpy / bytes，绕开逐对象开销")

print("\n=== 8. ⚠️ sys.getsizeof 不递归 ===")
nested = [1, 2, 3]
print(f"  sys.getsizeof([1,2,3]) = {sys.getsizeof(nested)} 字节")
print(f"  但里面三个 int 各占 {sys.getsizeof(1)} 字节，并没有被算进去")
print(f"  真实总和约 {sys.getsizeof(nested) + sum(sys.getsizeof(x) for x in nested)} 字节")
print("  → 测容器时记得手工累加，否则会低估")

print("\n=== 9. 小结 ===")
print("  · Python 对象 = PyObject 头（引用计数+类型指针）+ __dict__ 指针")
print(f"  · __slots__ 实测省 {100 * (ta - tb) / ta:.0f}%，代价是失去动态性")
print("  · 属性访问是哈希查找，比 C++/Java 的固定偏移慢")
print("  · 换来的是完全的动态性 —— 这是一笔明确的交易")
