"""内存：Python 里一切皆对象、全部在堆上——"栈"里只有名字。"""
import sys

print("== ① 一切皆对象：id() 就是 CPython 的堆地址 ==")
x = 10**9
print(f"id(x) = {hex(id(x))}")


def f():
    y = x  # 局部"变量"只是栈帧里的一个名字
    print(f"函数内 id(y) = {hex(id(y))}   <- 同一个堆对象，栈帧里只有引用")


f()

print("\n== ② 对象的体积：连 int 都背着对象头 ==")
print(f"sys.getsizeof(0)       = {sys.getsizeof(0)} 字节（C 的 int 只要 4 字节）")
print(f"sys.getsizeof(10**9)   = {sys.getsizeof(10**9)} 字节")
print(f"sys.getsizeof(10**100) = {sys.getsizeof(10**100)} 字节（大整数按需扩容）")
print(f"sys.getsizeof([])      = {sys.getsizeof([])} 字节（空列表）")

print("\n== ③ 栈帧本身也是堆上的对象 ==")
frame = sys._getframe()
print(f"当前栈帧: {type(frame).__name__} 对象，id = {hex(id(frame))}")
print("（调用栈是用堆对象串起来的链表——这就是能随时自省它的原因）")

print("\n== ④ 递归限制是人造的：真正的栈远没满 ==")
print(f"sys.getrecursionlimit() = {sys.getrecursionlimit()}")

depth = 0


def recurse():
    global depth
    depth += 1
    recurse()


try:
    recurse()
except RecursionError as e:
    print(f"RecursionError，深度 = {depth}：{e}")
print("（解释器数着帧数主动喊停，可用 sys.setrecursionlimit 调整——但 C 栈真溢出会直接崩）")
