"""反射：Python 里一切皆对象，类型信息全程在场——反射就是日常。"""
import inspect
import timeit


class Student:
    def __init__(self, name="未命名", score=0):
        self.name = name
        self.score = score

    def get_name(self):
        return self.name

    def __secret(self):  # 双下划线：name mangling“私有”
        return f"{self.name} 的真实分数是 {self.score}"


print("== ① 类型信息随手可得 ==")
s = Student("小明", 90)
print(f"type(s) = {type(s).__name__}")
print(f"实例字段 __dict__ = {s.__dict__}")
print(f"方法: {[m for m in dir(s) if not m.startswith('_') and callable(getattr(s, m))]}")

print("\n== ② getattr / setattr：字符串就是成员名 ==")
print(f"getattr(s, 'name') = {getattr(s, 'name')}")
setattr(s, "score", 100)
print(f"setattr 后 s.score = {s.score}")
method = getattr(s, "get_name")
print(f"按名字拿方法再调用: {method()}")

print("\n== ③ 动态创建类：type() 就是造类的工厂 ==")
Dynamic = type("Dynamic", (Student,), {"motto": lambda self: f"{self.name}：好好学习"})
d = Dynamic("小红", 85)
print(f"运行时造出的类: {type(d).__name__}, motto() = {d.motto()}")

print("\n== ④ 击穿“私有”：name mangling 只是改名 ==")
print(f"s._Student__secret() = {s._Student__secret()}")

print("\n== ⑤ inspect：连签名和源码都能拿到 ==")
sig = inspect.signature(Student.__init__)
print(f"Student.__init__ 的签名: {sig}")

print("\n== ⑥ __slots__：唯一能挡住动态属性的机制 ==")


class Locked:
    __slots__ = ("name",)


locked = Locked()
locked.name = "可以"
try:
    locked.extra = "不行"
except AttributeError as e:
    print(f"AttributeError: {e}")

print("\n== ⑦ 性能：直接访问 vs getattr（各 1000 万次） ==")
n = 10_000_000
t_direct = timeit.timeit("s.score", globals={"s": s}, number=n)
t_getattr = timeit.timeit("getattr(s, 'score')", globals={"s": s}, number=n)
print(f"s.score           : {t_direct * 1000:6.1f} ms")
print(f"getattr(s,'score'): {t_getattr * 1000:6.1f} ms")
