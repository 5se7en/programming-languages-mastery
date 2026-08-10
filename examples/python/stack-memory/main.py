"""栈内存：CPython 的栈帧是一等对象——能遍历、能读局部变量、能反汇编。"""
import dis
import sys
import timeit


def add(a, b):
    total = a + b
    return total


def level3():
    print("沿 f_back 向下走的调用链（栈顶在前）:")
    frame = sys._getframe()
    while frame is not None:
        name = frame.f_code.co_name
        local_names = list(frame.f_locals.keys())[:3]
        print(f"  {name}()  局部变量: {local_names}")
        frame = frame.f_back          # 指向调用者的帧——栈就是这条链
    return None


def level2():
    secret = "level2 的局部变量"
    return level3()


def level1():
    return level2()


print("== ① 栈帧是对象：f_back 串起整条调用链 ==")
level1()

print("\n== ② dis：看 add 的字节码如何使用局部变量表与求值栈 ==")
dis.dis(add)

print("\n== ③ 函数调用的价格（一千万次） ==")
def empty():
    pass

n = 10_000_000
t_call = timeit.timeit(empty, number=n)
t_pass = timeit.timeit("pass", number=n)
print(f"调用空函数 : {t_call * 1000:6.1f} ms")
print(f"纯 pass    : {t_pass * 1000:6.1f} ms")
print(f"每次调用约 {(t_call - t_pass) / n * 1e9:.0f} ns   <- 建帧/压参/拆帧的成本")
