"""智能指针：Python 没有这个概念——因为它的引用天生就是 shared_ptr。"""
import gc
import sys
import weakref


class Student:
    __slots__ = ("name", "partner", "__weakref__")

    def __init__(self, name):
        self.name = name
        self.partner = None
        print(f"    [构造] {name}")

    def __del__(self):
        print(f"    [析构] {self.name}")


print("== ① Python 的引用 = C++ 的 shared_ptr ==")
s1 = Student("小明")
print(f"    创建后引用计数 = {sys.getrefcount(s1) - 1}")
s2 = s1
print(f"    多一个名字后 = {sys.getrefcount(s1) - 1}   <- 与 shared_ptr::use_count 同义")
del s2
print(f"    del 之后 = {sys.getrefcount(s1) - 1}")
del s1
print("    （计数归零即析构——语言内置，无需选择指针类型）")

print("\n== ② 钥匙实验：同样的环，同样的泄漏 ==")
x = Student("环-甲")
y = Student("环-乙")
x.partner = y
y.partner = x                      # 成环
print("    del x, del y ——")
del x
del y
print("    ↑ 什么都没打印！引用计数救不了环（与 C++ shared_ptr 成环同款）")
print("    调用 gc.collect() ——")
collected = gc.collect()           # 副引擎出手：C++ 没有这个后备
print(f"    ↑ 副引擎回收了 {collected} 个对象   <- Python 有兜底，C++ 只能靠 weak_ptr")

print("\n== ③ weakref = weak_ptr：拆环的同款药方 ==")
a = Student("拆环-甲")
b = Student("拆环-乙")
a.partner = b                      # 甲 →强→ 乙
b.partner = weakref.ref(a)         # 乙 ⇢弱⇢ 甲（不计数）
print(f"    甲的引用计数 = {sys.getrefcount(a) - 1}   <- 弱引用没有让它 +1")
print("    del a, del b ——")
del a
del b
print("    ↑ 两个 [析构] 都打印了：环被拆开（与 C++ weak_ptr 同款结论）")

print("\n== ④ 提升：weakref 的 ref() ↔ weak_ptr 的 lock() ==")
owner = Student("被观察者")
observer = weakref.ref(owner)
print(f"    对象活着: observer() = {observer().name}")
del owner
print(f"    对象死后: observer() = {observer()}   <- 与 lock() 返回空指针同义")

print("\n== ⑤ Python 没有 unique_ptr 的对应物 ==")
print("    因为 Python 无法表达「唯一所有权」——任何赋值都会产生新引用")
print("    C++ 用类型系统区分独占/共享；Python 只有一种共享语义（第 35 章的分野）")
