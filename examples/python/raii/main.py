"""RAII：Python 的答案是 with 语句——上下文管理器协议。"""
import contextlib


class FileHandle:
    """最小上下文管理器：__enter__ 获取，__exit__ 释放。"""

    def __init__(self, name):
        self.name = name

    def __enter__(self):
        print(f"    [获取] 打开 {self.name}")
        return self                       # 返回值绑定到 as 后面的名字

    def __exit__(self, exc_type, exc_val, exc_tb):
        print(f"    [释放] 关闭 {self.name}"
              + (f"   <- 带着异常信息 {exc_type.__name__} 退出" if exc_type else ""))
        return False                      # False = 不吞异常，让它继续传播


print("== ① with：作用域即资源生命周期 ==")
with FileHandle("data.txt"):
    print("    使用中……")
print("    块结束——无需任何 close 调用")

print("\n== ② 钥匙实验：异常安全 ==")
print("  手动风格（open ... close）:")
try:
    print("    [获取] 打开 manual.txt")
    raise RuntimeError("中途出错")
    print("    [释放] 关闭 manual.txt")     # noqa: 永远执行不到
except RuntimeError as e:
    print(f"    捕获: {e}   <- 没有任何 [释放] 打印！句柄泄漏")
print("  with 风格:")
try:
    with FileHandle("raii.txt"):
        raise RuntimeError("中途出错")
except RuntimeError as e:
    print(f"    捕获: {e}   <- [释放] 已在上一行打印，且 __exit__ 收到了异常信息")

print("\n== ③ 多个资源：逆序退出 ==")
try:
    with FileHandle("第一个"), FileHandle("第二个"), FileHandle("第三个"):
        raise RuntimeError("三个都开着的时候出错了")
except RuntimeError:
    print("    三个全部释放，顺序是 3-2-1（进入的逆序）")

print("\n== ④ contextlib：用生成器写上下文管理器 ==")


@contextlib.contextmanager
def managed(name):
    print(f"    [获取] {name}")
    try:
        yield name                        # yield 之前 = __enter__，之后 = __exit__
    finally:
        print(f"    [释放] {name}")        # finally 保证异常路径也执行


with managed("生成器风格"):
    print("    使用中……")

print("\n== ⑤ 危险特性：__exit__ 返回 True 会吞掉异常 ==")


class Swallower:
    def __enter__(self): return self
    def __exit__(self, *args):
        print("    [释放] 并且吞掉了异常")
        return True                       # ⚠️ True = 异常到此为止


with Swallower():
    raise RuntimeError("这个异常永远不会被外面看到")
print("    程序继续运行——异常被 __exit__ 吞了（慎用！）")
