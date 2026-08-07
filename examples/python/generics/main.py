"""泛型：类型参数化在 Python 里的形态——纯提示，运行时不强制。"""
from typing import Generic, Protocol, TypeVar

T = TypeVar("T")


class Stack(Generic[T]):
    """Python 3.12+ 可以写 class Stack[T]:（PEP 695），这里用 3.9 兼容写法。"""

    def __init__(self) -> None:
        self._items: list[T] = []

    def push(self, item: T) -> None:
        self._items.append(item)

    def pop(self) -> T:
        return self._items.pop()


print("== ① 类型参数只是提示：运行时不检查 ==")
s: Stack[int] = Stack()
s.push(90)
s.push("九十八")  # 类型不对，但运行时完全不报错！
print(f"Stack[int] 里的内容: {s._items}")
print('静态检查器才会报: error: Argument 1 to "push" has incompatible type "str"')

print("\n== ② Stack[int] 在运行时是什么 ==")
print(f"type(Stack[int]) = {type(Stack[int]).__name__}")
inst = Stack[int]()
print(f"实例的 __class__ 还是 Stack: {inst.__class__ is Stack}")
print(f"但 __orig_class__ 记住了参数: {inst.__orig_class__}")

print("\n== ③ 内置容器的泛型写法（PEP 585，3.9+） ==")
scores: list[int] = [90, 85]
scores.append("九十八")  # 同样不报错
print(f"list[int] 里的内容: {scores}")

print("\n== ④ TypeVar 的约束与边界 ==")
Num = TypeVar("Num", int, float)  # 约束：只能是 int 或 float


def add(a: Num, b: Num) -> Num:
    return a + b


print(f"add(90, 8) = {add(90, 8)}")
print(f"add(0.5, 0.25) = {add(0.5, 0.25)}")


class Comparable(Protocol):
    def __lt__(self, other) -> bool: ...


C = TypeVar("C", bound=Comparable)  # 边界：C 必须可比较


def max_of(items: "list[C]") -> C:
    best = items[0]
    for x in items:
        if best < x:
            best = x
    return best


print(f"max_of([90, 85, 98]) = {max_of([90, 85, 98])}")
print(f"max_of(['小明', '小红']) = {max_of(['小明', '小红'])}")
