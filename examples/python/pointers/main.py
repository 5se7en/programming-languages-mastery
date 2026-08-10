"""指针：Python 说"没有指针"——但 id() 是地址，ctypes 是侧门。"""
import ctypes
import sys

print("== ① 变量都是引用：is 就是「地址相等」 ==")
a = [1, 2, 3]
b = a                       # 不复制——两个名字指向同一对象
b.append(4)
print(f"b = a 后 b.append(4)，a = {a}   <- 改的是同一个对象")
print(f"a is b: {a is b}（id 相同: {id(a) == id(b)}）")

print("\n== ② ctypes 侧门：用 id() 当真地址，读出对象头 ==")
x = 3141592653589
refcount = ctypes.c_ssize_t.from_address(id(x))     # CPython 对象头第一个字段：引用计数
print(f"从地址 id(x) 直接读内存: 引用计数 = {refcount.value}")
y = x                                                # 多一个引用
print(f"y = x 之后再读:          引用计数 = {ctypes.c_ssize_t.from_address(id(x)).value}")
print(f"对照 sys.getrefcount(x) = {sys.getrefcount(x)}（它自己也算一个引用）")
print("（id() 在 CPython 里就是对象的堆地址——第 31 章实测的伏笔，在此穿透）")

print("\n== ③ 没有指针算术：引用不可运算 ==")
try:
    a + 1                   # 列表引用加 1？
except TypeError as e:
    print(f"引用 + 1 -> TypeError: {e}")
print("（地址不可见、不可算——CPython 才敢在内部自由管理对象）")

print("\n== ④ 可变默认参数：引用语义的经典事故 ==")


def enroll(name, roster=[]):        # 默认值只创建一次——所有调用共享同一个列表！
    roster.append(name)
    return roster


print(f"enroll('小明') = {enroll('小明')}")
print(f"enroll('小红') = {enroll('小红')}   <- 上一次的还在！同一个对象")
print("（没有指针语法，不代表没有「共享可变状态」的事故——引用一样会咬人）")
