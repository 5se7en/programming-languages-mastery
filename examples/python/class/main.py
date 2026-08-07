"""第 23 章 · 类 —— Python 示例
运行：python3 main.py
"""

import copy
from dataclasses import dataclass

print("=== 1. 不用类的痛点：数据分散、关联脆弱 ===")
names = ["Alice", "Bob"]
scores = [92, 75]
ages = [16, 17]
print("  平行数组:", names, scores, ages)
print("  ⚠️ 三个列表必须严格保持顺序一致，排序其中一个就全乱了")


def is_passing_bad(score):
    return score >= 60


print("  is_passing_bad(ages[0]) =", is_passing_bad(ages[0]), " ← 传错参数，语法却完全合法")

print("\n=== 2. 用类打包：数据和行为待在一起 ===")


class Student:
    school = "第一中学"          # 类属性：所有实例共享，只存一份
    count = 0                    # 类属性：实例计数

    def __init__(self, name, score, age):    # 构造函数
        self.name = name                      # 实例属性：每个对象一份
        self.score = score
        self.age = age
        Student.count += 1                    # 注意用类名访问，不是 self

    def is_passing(self):                     # self 是显式的第一个参数
        return self.score >= 60

    @classmethod
    def create(cls, name):                    # cls 是类本身
        return cls(name, 0, 0)

    @staticmethod
    def pass_line():                          # 既不需要实例也不需要类
        return 60

    def __repr__(self):
        return f"Student({self.name!r}, {self.score})"


alice = Student("Alice", 92, 16)
bob = Student("Bob", 45, 17)
print(f"  {alice.name}: 分数 {alice.score}, 及格? {alice.is_passing()}")
print(f"  {bob.name}: 分数 {bob.score}, 及格? {bob.is_passing()}")
print("  类属性 Student.school =", Student.school, " ← 所有实例共享")
print("  已创建实例数 =", Student.count)
print("  及格线（静态方法）=", Student.pass_line())

print("\n=== 3. self 是显式的第一个参数（实测两种写法等价）===")
print("  alice.is_passing()          =", alice.is_passing())
print("  Student.is_passing(alice)   =", Student.is_passing(alice), " ← 实际发生的就是这个")
print("  → 其他语言把 this 藏起来，Python 让你写出来")

print("\n=== 4. 类属性 vs 实例属性：内存里存几份 ===")
a, b = Student("A", 80, 16), Student("B", 70, 17)
print(f"  a.school={a.school}  b.school={b.school}")
print(f"  两者是同一个对象吗？ {a.school is b.school}  ← 类属性只存一份")

Student.school = "第二中学"
print(f"  改类属性后: a.school={a.school}  b.school={b.school}  ← 都变了")

print("\n  ⚠️ 经典陷阱：给实例赋值不会修改类属性")
a.school = "第三中学"                          # 这是创建实例属性，不是改类属性
print(f"    a.school       = {a.school}   ← a 自己多了一个实例属性")
print(f"    b.school       = {b.school}   ← b 没变")
print(f"    Student.school = {Student.school}   ← 类属性也没变")
print(f"    a.__dict__     = {a.__dict__}")
print("    规则：读取先找实例再找类；赋值永远创建/修改实例属性")

print("\n=== 5. ⚠️ 更严重的陷阱：可变对象做类属性 ===")


class BadStudent:
    tags = []                    # ✗ 所有实例共享同一个列表！


class GoodStudent:
    def __init__(self):
        self.tags = []           # ✓ 每个实例一份


x, y = BadStudent(), BadStudent()
x.tags.append("优秀")
print(f"  BadStudent:  x.tags={x.tags}  y.tags={y.tags}  ← y 也被改了！")
print(f"               x.tags is y.tags → {x.tags is y.tags}")

p, q = GoodStudent(), GoodStudent()
p.tags.append("优秀")
print(f"  GoodStudent: p.tags={p.tags}  q.tags={q.tags}  ← 互不影响")

print("\n=== 6. 引用语义：b = a 只是起了个别名 ===")
a2 = Student("Alice", 90, 16)
b2 = a2                          # 不是拷贝！
b2.name = "Bob"
print(f"  赋值后: a2.name={a2.name}  b2.name={b2.name}  ← a2 也变了！")
print(f"  id(a2)={id(a2)}  id(b2)={id(b2)}  ← 同一个对象")

c2 = copy.copy(a2)               # 想要拷贝必须显式说
c2.name = "Carol"
print(f"  copy.copy 后: a2.name={a2.name}  c2.name={c2.name}  ← 这才是拷贝")

print("\n=== 7. Python 特有：类本身也是对象 ===")
print("  type(Student)      →", type(Student), " ← 类的类型是 type")
print("  Student 是对象吗？ →", isinstance(Student, object))

Student.motto = "求真"           # 运行时给类动态加属性
print("  运行时给类加属性   →", Student.motto)

# 甚至可以在运行时凭空造一个类
Dynamic = type("Dynamic", (), {"hello": lambda self: "我是运行时造出来的"})
print("  运行时造类         →", Dynamic().hello())
print("  → class 语句只是「创建一个 type 类型的对象」的语法糖")
print("  → 这是装饰器、ORM、各种框架魔法的基础（第 30 章反射会展开）")

print("\n=== 8. @dataclass：纯数据类的简写 ===")


@dataclass
class Point:
    x: int
    y: int


p1, p2 = Point(1, 2), Point(1, 2)
print("  Point(1, 2)        =", p1)
print("  p1 == p2           =", p1 == p2, " ← 自动实现了基于值的比较")
print("  → 少写样板代码，也少犯「忘记实现 __eq__」的错")
