"""第 27 章 · 多态 —— Python 示例
运行：python3 main.py

Python 的多态完全基于鸭子类型：只看「有没有这个方法」，不看「是不是同一个类型」。
"""

from abc import ABC, abstractmethod
from typing import Protocol

print("=== 1. ⚠️ 鸭子类型：多态根本不需要继承 ===")


class Dog:
    def speak(self):
        return "汪！"


class Cat:
    def speak(self):
        return "喵～"


class Robot:                      # 与前两者毫无继承关系
    def speak(self):
        return "滴滴"


for obj in [Dog(), Cat(), Robot()]:
    print(f"  {type(obj).__name__:6} .speak() = {obj.speak()}")

common = set(Dog.__mro__) & set(Cat.__mro__) & set(Robot.__mro__)
print(f"\n  三个类的共同祖先: {[c.__name__ for c in common]}")
print("  → 除了 object，它们之间没有任何继承关系")
print("  → 但只要都有 speak() 方法，就能被同样使用")
print()
print("  「如果它走起来像鸭子、叫起来像鸭子，那它就是鸭子」")
print()
print("  本质区别：")
print("    静态类型语言（Java/C++/C#）：先声明关系（implements/继承），编译期检查")
print("    鸭子类型（Python/JS）      ：直接使用，运行时才检查")
print("  → 前者编译期就能发现错误，后者更灵活但错误推迟到运行时")

print("\n=== 2. 多态的价值：新增类型不改已有代码（开闭原则）===")


class Shape(ABC):
    @abstractmethod
    def area(self) -> float: ...

    @abstractmethod
    def name(self) -> str: ...


class Circle(Shape):
    def __init__(self, r): self.r = r
    def area(self): return 3.14159 * self.r ** 2
    def name(self): return "圆形"


class Rect(Shape):
    def __init__(self, w, h): self.w, self.h = w, h
    def area(self): return self.w * self.h
    def name(self): return "矩形"


def total_area(shapes):
    """这个函数不关心具体是什么图形"""
    return sum(s.area() for s in shapes)


shapes = [Circle(2), Rect(3, 4)]
for s in shapes:
    print(f"    {s.name()} 面积 = {s.area():.2f}")
print(f"  总面积 = {total_area(shapes):.2f}")


# 新增一种图形：只需加一个类，total_area 一个字不用改
class Triangle(Shape):
    def __init__(self, b, h): self.b, self.h = b, h
    def area(self): return self.b * self.h / 2
    def name(self): return "三角形"


shapes.append(Triangle(6, 5))
print(f"  新增三角形后，同一个 total_area() → {total_area(shapes):.2f}")
print("  → total_area 一个字都没改")

print("\n=== 3. ABC：把「运行时才发现没实现」提前到「实例化时报错」 ===")
try:
    Shape()
except TypeError as e:
    print(f"  Shape() → TypeError: {e}")


class Incomplete(Shape):
    def area(self): return 0
    # 忘了实现 name()


try:
    Incomplete()
except TypeError as e:
    print(f"  Incomplete() → TypeError: {e}")
print("  → 但注意：这仍然是「运行时」，而非编译期")

print("\n=== 4. Protocol：静态检查的鸭子类型（Python 3.8+）===")


class Speaker(Protocol):
    """只声明形状，不需要任何类来继承它"""

    def speak(self) -> str: ...


def make_speak(s: Speaker) -> str:
    return s.speak()


print(f"  make_speak(Dog())   = {make_speak(Dog())}")
print(f"  make_speak(Robot()) = {make_speak(Robot())}")
print(f"  Dog 的父类: {[c.__name__ for c in Dog.__bases__]}  ← 完全没继承 Speaker")
print()
print("  → Protocol 是「结构化子类型」：只要长得像 Speaker，类型检查器就认")
print("  → 既保留鸭子类型的灵活，又能在运行前（静态检查阶段）发现错误")
print("  → 这是 Python 类型系统近年最有价值的补充之一（呼应第 07 章）")

print("\n  ⚠️ 默认的 Protocol 只能做静态检查，不能用于 isinstance：")
try:
    isinstance(Dog(), Speaker)
except TypeError as e:
    print(f"    isinstance(Dog(), Speaker) → TypeError: {e}")

print("\n  想在运行时也能检查，要加 @runtime_checkable：")
from typing import runtime_checkable


@runtime_checkable
class RuntimeSpeaker(Protocol):
    def speak(self) -> str: ...


print(f"    isinstance(Dog(),   RuntimeSpeaker) = {isinstance(Dog(), RuntimeSpeaker)}")
print(f"    isinstance(Robot(), RuntimeSpeaker) = {isinstance(Robot(), RuntimeSpeaker)}")
print(f"    isinstance(42,      RuntimeSpeaker) = {isinstance(42, RuntimeSpeaker)}  ← int 没有 speak()")
print("  → 但注意：运行时检查只看「有没有这个方法名」，不检查签名是否匹配")

print("\n=== 5. 运算符也是多态 ===")


class Vector:
    def __init__(self, x, y):
        self.x, self.y = x, y

    def __add__(self, other):          # 让 + 对 Vector 有意义
        return Vector(self.x + other.x, self.y + other.y)

    def __mul__(self, k):              # 让 * 对 Vector 有意义
        return Vector(self.x * k, self.y * k)

    def __eq__(self, other):
        return (self.x, self.y) == (other.x, other.y)

    def __repr__(self):
        return f"Vector({self.x}, {self.y})"


v1, v2 = Vector(1, 2), Vector(3, 4)
print(f"  Vector(1,2) + Vector(3,4) = {v1 + v2}")
print(f"  Vector(1,2) * 3           = {v1 * 3}")
print(f"  同一个 + 号，对 int/str/list/Vector 有不同含义：")
print(f"    1 + 2           = {1 + 2}")
print(f"    'a' + 'b'       = {'a' + 'b'!r}")
print(f"    [1] + [2]       = {[1] + [2]}")
print("  → 这是「特设多态」（第 12 章），靠 __add__ 等特殊方法实现")

print("\n=== 6. ⚠️ 别把多态退化成 isinstance ===")


def bad_area(shapes):
    """✗ 多态被浪费了"""
    total = 0
    for s in shapes:
        if isinstance(s, Circle):
            total += 3.14159 * s.r ** 2
        elif isinstance(s, Rect):
            total += s.w * s.h
        elif isinstance(s, Triangle):
            total += s.b * s.h / 2
    return total


print(f"  bad_area(shapes)   = {bad_area(shapes):.2f}   ← 每加一种图形都要回来改")
print(f"  total_area(shapes) = {total_area(shapes):.2f}   ← 一个字不用改")
print("  → 如果每加一个类型都要改多处代码，说明多态没用对地方")

print("\n=== 7. 方法查找的代价 ===")
print("  Python 的方法查找每次都要走 MRO（第 26 章），虽然有类型缓存，")
print("  但仍比编译型语言的 vtable 慢一个量级。")
print()
print("  各语言的派发实现对比：")
print("    C++/Java/C#  vtable         编译期建好的表，一次间接跳转")
print("    JavaScript   原型链+内联缓存  运行时学出来的表")
print("    Python       MRO 查找+缓存    比 vtable 慢一个量级")
print("  → 共同模式：所有实现都在「查表」和「缓存」之间做文章")
print("  → 这是动态性的固有代价，不要指望靠「少用继承」来优化它")

print("\n=== 8. 小结 ===")
print("  · Python 的多态是鸭子类型：三个无继承关系的类照样能统一处理")
print("  · ABC 把错误从「调用时」提前到「实例化时」—— 但仍是运行时")
print("  · Protocol 提供结构化子类型：兼得鸭子类型的灵活与静态检查的安全")
print("  · 运算符重载是特设多态，靠 __add__ 等特殊方法实现")
print("  · 多态的价值在于「新增类型的成本」，别退化成 isinstance 判断")
