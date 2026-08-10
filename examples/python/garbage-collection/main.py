"""垃圾回收：CPython 的双引擎——引用计数（主力）+ 循环收集器（补漏）。"""
import gc
import sys
import weakref


class Student:
    def __init__(self, name):
        self.name = name
        self.partner = None

    def __del__(self):
        print(f"    __del__: {self.name} 被回收了")


print("== ① 引用计数：计数归零，立刻回收 ==")
a = Student("小明")
print(f"引用计数 = {sys.getrefcount(a) - 1}（getrefcount 自己算一个，已减去）")
b = a
print(f"b = a 之后 = {sys.getrefcount(a) - 1}")
del b
print(f"del b 之后 = {sys.getrefcount(a) - 1}")
print("del a 执行——")
del a
print("——上一行结束前 __del__ 已打印：回收时机是确定的（不用等 GC 心情）")

print("\n== ② 循环引用：引用计数数不清的死角 ==")
x = Student("小红")
y = Student("小刚")
x.partner = y
y.partner = x                     # x ⇄ y 互指成环
print("del x, del y 执行——")
del x
del y
print("——没有任何 __del__ 打印！计数各剩 1（对方手里），谁也归不了零")

print("\n== ③ 循环收集器出手：gc.collect() ==")
collected = gc.collect()
print(f"gc.collect() 报告回收了 {collected} 个对象（两个 Student + 属性字典等随葬品）")

print("\n== ④ 分代阈值：循环收集器也搞分代 ==")
print(f"gc.get_threshold() = {gc.get_threshold()}")
print("（第 0 代满 700 次分配差额触发；0 代扫 10 次才扫 1 次 1 代……越老越少扫）")

print("\n== ⑤ weakref：观察而不挽留 ==")
s = Student("小强")
ref = weakref.ref(s)
print(f"对象活着: ref() = {ref().name}")
del s                             # 弱引用不算数——计数归零，立刻回收
print(f"del s 之后: ref() = {ref()}   <- 弱引用眼睁睁看着对象死去")
