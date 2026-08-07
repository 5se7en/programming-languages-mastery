"""第 26 章 · 继承 —— Python 示例
运行：python3 main.py

Python 是这几门语言里唯一完整支持多继承的，代价是需要理解 MRO。
"""

print("=== 1. 基本继承与 super() ===")


class Animal:
    def __init__(self, name):
        self.name = name

    def speak(self):
        return f"{self.name} 发出声音"


class Dog(Animal):
    def speak(self):
        return super().speak() + "：汪！"


d = Dog("旺财")
print(f"  Dog('旺财').speak() = {d.speak()}")
print(f"  isinstance(d, Dog)    = {isinstance(d, Dog)}")
print(f"  isinstance(d, Animal) = {isinstance(d, Animal)}")

print("\n=== 2. 菱形继承：Python 用 MRO 解决 ===")


class A:
    def hello(self):
        return "A"


class B(A):
    def hello(self):
        return "B"


class C(A):
    def hello(self):
        return "C"


class D(B, C):
    pass


print(f"  class D(B, C) 的 MRO:")
print(f"    {' → '.join(c.__name__ for c in D.__mro__)}")
print(f"  D().hello() = {D().hello()}  ← 按 MRO 顺序找到第一个 B")
print("  → 不歧义（不像 C++ 要写 d.B::value），也不会有两份字段")
print("  → C3 线性化保证：子类总在父类之前、多父类保持声明顺序、结果唯一确定")

print("\n=== 3. ⚠️ super() 是「MRO 的下一个」，不是「我的父类」 ===")


class X:
    def who(self):
        return ["X"]


class Y(X):
    def who(self):
        return ["Y"] + super().who()


class Z(X):
    def who(self):
        return ["Z"] + super().who()


class W(Y, Z):
    def who(self):
        return ["W"] + super().who()


print(f"  继承结构: W(Y, Z)，而 Y 和 Z 都继承自 X")
print(f"  W 的 MRO: {' → '.join(c.__name__ for c in W.__mro__)}")
print(f"  W().who() = {' → '.join(W().who())}")
print()
print("  ⚠️ 注意：Y 里写的 super().who() 调到了 Z，而不是 Y 的父类 X！")
print("  → super() 的意思是「MRO 里的下一个」，不是「我的父类」")
print("  → 这意味着写 Y 时无法预知 super() 会去哪 —— 它取决于最终的继承结构")
print("  → 多继承时所有类都必须调 super()，否则链会断")

print("\n  演示「链断了」会怎样：")


class YBroken(X):
    def who(self):
        return ["Y(没调 super)"]        # ✗ 链在这里断了


class WBroken(YBroken, Z):
    def who(self):
        return ["W"] + super().who()


print(f"    WBroken 的 MRO: {' → '.join(c.__name__ for c in WBroken.__mro__)}")
print(f"    WBroken().who() = {' → '.join(WBroken().who())}")
print("    → Z 和 X 完全没被调用到！")

print("\n=== 4. MRO 无法构造时，定义类就会报错 ===")
try:
    class Bad(D, B):        # 与已有的 MRO 顺序冲突
        pass
except TypeError as e:
    print(f"  class Bad(D, B) → TypeError: {e}")
    print("  → Python 在「定义类」时就检查，不会等到运行时才出问题")

print("\n=== 5. ⚠️ 里氏替换原则：正方形 is-a 长方形？ ===")


class Rectangle:
    def __init__(self, w, h):
        self._w, self._h = w, h

    @property
    def width(self):
        return self._w

    @width.setter
    def width(self, v):
        self._w = v

    @property
    def height(self):
        return self._h

    @height.setter
    def height(self, v):
        self._h = v

    @property
    def area(self):
        return self._w * self._h


class Square(Rectangle):
    """数学上正方形是长方形，但代码里……"""

    def __init__(self, side):
        super().__init__(side, side)

    @Rectangle.width.setter
    def width(self, v):
        self._w = self._h = v          # 改宽必须同时改高

    @Rectangle.height.setter
    def height(self, v):
        self._w = self._h = v


def stretch(rect):
    """任何接受 Rectangle 的代码都应该能用 —— 这就是里氏替换"""
    rect.width = 4
    rect.height = 5
    return rect.area


print("  stretch 函数：把宽设成 4、高设成 5，期望面积 = 4 × 5 = 20")
print(f"    传入 Rectangle(2, 3) → 面积 = {stretch(Rectangle(2, 3))}   ✓")
print(f"    传入 Square(2)       → 面积 = {stretch(Square(2))}   ✗ 期望 20！")
print()
print("  为什么：Rectangle 隐含了一条行为约定 —— 宽和高可以独立设置")
print("          Square 破坏了它，于是「对所有长方形都正确」的函数就错了")
print("  → 判断能不能继承，不看「概念上像不像」，而看「行为上能不能替换」")
print("  → 数学上的 is-a 不等于代码里的 is-a")
print("  → 正确做法：让两者都实现同一个 Shape 接口，而不是互相继承")

print("\n=== 6. 方法查找发生在调用时 ===")


class Base:
    def greet(self):
        return "Base"


class Child(Base):
    pass


c = Child()
print(f"  重写前 c.greet() = {c.greet()}")
Child.greet = lambda self: "Child(运行时加的)"      # 运行时给类加方法
print(f"  运行时改 Child.greet 后 = {c.greet()}  ← 已创建的实例也变了")
print("  → 方法查找沿 MRO 发生在「调用时」，不是「创建实例时」")
print("  → Python 没有 final，无法阻止别人继承你的类或重写你的方法")

print("\n=== 7. 组合优于继承 ===")
from collections import UserList


class CountingListBad(list):
    """✗ 继承 list：依赖 list 的实现细节"""

    def __init__(self):
        super().__init__()
        self.add_count = 0

    def append(self, item):
        self.add_count += 1
        super().append(item)

    def extend(self, items):
        self.add_count += len(items)
        super().extend(items)      # CPython 的 list.extend 不走 append，所以这里恰好没翻倍
                                    # 但这正是问题：你依赖了一个未文档化的实现细节


class CountingListGood:
    """✓ 组合：只依赖 list 的公开接口"""

    def __init__(self):
        self._inner = []
        self.add_count = 0

    def append(self, item):
        self.add_count += 1
        self._inner.append(item)

    def extend(self, items):
        items = list(items)
        self.add_count += len(items)
        self._inner.extend(items)

    def __len__(self):
        return len(self._inner)

    def __repr__(self):
        return f"CountingListGood({self._inner})"


bad = CountingListBad()
bad.extend(["x", "y", "z"])
good = CountingListGood()
good.extend(["x", "y", "z"])
print(f"  继承版本: extend 3 个 → add_count = {bad.add_count}")
print(f"  组合版本: extend 3 个 → add_count = {good.add_count}")
print()
print("  ⚠️ 注意：这里继承版本恰好也是对的 —— 因为 CPython 的 list.extend 不调用 append。")
print("     但这正是脆弱基类的可怕之处：")
print("       · 它是否调用 append 是未文档化的实现细节")
print("       · 换个 Python 实现（PyPy）、换个版本，结果就可能变")
print("       · Java 的 HashSet.addAll 就确实调用了 add，导致计数翻倍（见 java 示例）")
print("  → 组合版本不依赖任何实现细节，在任何实现上都正确")

print("\n=== 8. 判断该不该继承的三个问题 ===")
print("  ① 是 is-a 还是 has-a？")
print("     「Dog 是 Animal」✓        「CountingList 是 list」✗（它只是「用了」list）")
print("  ② 满足里氏替换吗？")
print("     实测反例：Square 让 stretch 算出 25 而不是 20")
print("  ③ 父类会变吗？")
print("     第三方库的类随时可能改实现 → 脆弱基类风险")
print("  → 只要有一个答案不理想，就该用组合")

print("\n=== 9. 小结 ===")
print("  · Python 是唯一完整支持多继承的，用 C3 线性化（MRO）解决菱形问题")
print("  · super() 是「MRO 的下一个」，不是「我的父类」—— 实测 Y 的 super() 调到了 Z")
print("  · 多继承时所有类都要调 super()，否则协作链会断")
print("  · 里氏替换：实测 Square 让 stretch 算出 25 而非 20")
print("  · Python 没有 final，「不要重写」只能靠文档和命名约定")
