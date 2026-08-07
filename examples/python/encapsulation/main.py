"""第 25 章 · 封装 —— Python 示例
运行：python3 main.py

Python 的态度最鲜明：不提供强制私有，靠约定和文化。
"我们都是自愿成年人"（we're all consenting adults）
"""

print("=== 1. 不封装的后果：校验形同虚设 ===")


class BadAccount:
    def __init__(self):
        self.balance = 100          # 公开字段

    def deposit(self, n):
        if n <= 0:
            raise ValueError("金额必须为正")
        self.balance += n


acc = BadAccount()
try:
    acc.deposit(-50)
except ValueError as e:
    print(f"  acc.deposit(-50)   → {e}  ← 正门的校验生效")

acc.balance = -999                  # 直接绕过
print(f"  acc.balance = -999 → 余额变成 {acc.balance}  ← 从墙上的洞进来了")

print("\n=== 2. ⚠️ 两种下划线的真实行为 ===")


class Account:
    def __init__(self):
        self.balance = 100           # 公开
        self._internal = "内部用"     # 单下划线：约定，"请别碰"
        self.__secret = "私有"        # 双下划线：name mangling


a = Account()
print(f"  a.balance   = {a.balance}")
print(f"  a._internal = {a._internal}  ← 单下划线照样能访问，纯靠自觉")

try:
    print(a.__secret)
except AttributeError as e:
    print(f"  a.__secret  → AttributeError: {e}")

print("\n  但双下划线只是被改了名字：")
print(f"    实例的真实属性: {list(a.__dict__.keys())}")
print(f"    a._Account__secret = {a._Account__secret}  ← 照样能拿到！")
print("  → Python 没有真正的私有，__ 只是 name mangling")
print("  → 它的设计目的不是保密，而是避免子类意外覆盖父类属性（第 26 章）")

print("\n=== 3. @property：Python 的招牌手段 ===")


class Temperature:
    def __init__(self, celsius=0):
        self.celsius = celsius       # 注意：这里已经走了下面的 setter

    @property
    def celsius(self):
        return self._celsius

    @celsius.setter
    def celsius(self, value):
        if value < -273.15:
            raise ValueError("低于绝对零度")
        self._celsius = value

    @property
    def fahrenheit(self):            # 只读的计算属性（没有 setter）
        return self._celsius * 9 / 5 + 32


t = Temperature()
t.celsius = 25                       # 看起来像普通赋值，实际调用了 setter
print(f"  t.celsius = 25 后:")
print(f"    t.celsius    = {t.celsius}")
print(f"    t.fahrenheit = {t.fahrenheit}  ← 计算属性，永远不会不一致")

try:
    t.celsius = -300
except ValueError as e:
    print(f"    t.celsius = -300 → ValueError: {e}")

try:
    t.fahrenheit = 100
except AttributeError as e:
    print(f"    t.fahrenheit = 100 → AttributeError（只读属性）")

print("\n=== 4. property 的关键价值：调用方代码完全不用改 ===")


class V1:
    """第一版：就是个普通属性"""

    def __init__(self, score):
        self.score = score


class V2:
    """第二版：加了校验，但调用方写法完全一样"""

    def __init__(self, score):
        self.score = score

    @property
    def score(self):
        return self._score

    @score.setter
    def score(self, v):
        if not 0 <= v <= 100:
            raise ValueError("分数必须在 0..100")
        self._score = v


print(f"  V1(92).score = {V1(92).score}   ← 普通属性")
print(f"  V2(92).score = {V2(92).score}   ← 已升级为 property，调用方无感知")
try:
    V2(150)
except ValueError as e:
    print(f"  V2(150) → ValueError: {e}  ← 校验生效了")
print("  → 这就是 Python 不预防性写 getter/setter 的底气")
print("  → 对比 Java：公开字段一旦要加校验，所有调用方都得改")

print("\n=== 5. ⚠️ 封装泄漏：返回可变的内部对象 ===")


class BadRoster:
    def __init__(self):
        self._items = ["Alice", "Bob"]

    def get_items(self):
        return self._items           # ✗ 返回了内部列表本身


class GoodRoster:
    def __init__(self):
        self._items = ["Alice", "Bob"]

    def get_items(self):
        return tuple(self._items)    # ✓ 返回不可变副本

    @property
    def size(self):
        return len(self._items)


bad = BadRoster()
bad.get_items().append("入侵者")
print(f"  BadRoster:  外部 append 后内部变成 {bad.get_items()}")

good = GoodRoster()
try:
    good.get_items().append("入侵者")
except AttributeError:
    print(f"  GoodRoster: 外部无法 append（返回的是 tuple），内部仍是 {list(good.get_items())}")
print("  → 这是最隐蔽的封装泄漏：字段是私有的，但可变引用漏出去了")

print("\n=== 6. 暴露操作，而不是暴露状态 ===")


class SafeAccount:
    def __init__(self, balance=0):
        self._balance = balance

    @property
    def balance(self):               # 只读
        return self._balance

    def deposit(self, n):            # ✅ 有业务含义的操作
        if n <= 0:
            raise ValueError("金额必须为正")
        self._balance += n

    def withdraw(self, n):
        if n > self._balance:
            raise ValueError("余额不足")
        self._balance -= n


s = SafeAccount(100)
s.deposit(50)
print(f"  deposit(50) 后余额 = {s.balance}")
try:
    s.withdraw(1000)
except ValueError as e:
    print(f"  withdraw(1000) → {e}  ← 不变式 balance >= 0 得到保证")
try:
    s.balance = 999
except AttributeError:
    print(f"  s.balance = 999 → AttributeError（只有 getter，没有 setter）")
print("  → deposit/withdraw 表达业务意图，比 set_balance 好得多")

print("\n=== 7. __ 的真正用途：避免子类命名冲突（第 26 章伏笔）===")


class Base:
    def __init__(self):
        self.__data = "父类的数据"       # 会变成 _Base__data

    def show_base(self):
        return self.__data


class Child(Base):
    def __init__(self):
        super().__init__()
        self.__data = "子类的数据"       # 会变成 _Child__data，不会覆盖父类的

    def show_child(self):
        return self.__data


c = Child()
print(f"  父类的 __data: {c.show_base()}")
print(f"  子类的 __data: {c.show_child()}")
print(f"  实例里实际有: {list(c.__dict__.keys())}")
print("  → 两个 __data 互不干扰，这才是 name mangling 的设计目的")

print("\n=== 8. 小结 ===")
print("  · Python 没有强制私有，全靠约定：_ 是「请别碰」，__ 是改名防冲突")
print("  · 但 @property 让「先简单、需要时再加校验」成为无痛操作")
print("  · 封装的目的不是防蓄意，而是防意外 + 划清可维护的边界")
print("  · 最隐蔽的坑是封装泄漏：返回了可变的内部集合")
