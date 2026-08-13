"""设计模式：一半的经典模式，在有一等函数的语言里退化成一个函数——逐个实测。"""
import functools
import time


# ============ ① 策略模式：GoF 版 vs Python 版 ============
# --- GoF 版：接口 + 三个类 + 一个持有者（约 25 行）---
class SortStrategy:                                   # 抽象策略
    def compare(self, a, b): raise NotImplementedError

class ByLength(SortStrategy):
    def compare(self, a, b): return len(a) - len(b)

class ByAlpha(SortStrategy):
    def compare(self, a, b): return (a > b) - (a < b)

class ByLastChar(SortStrategy):
    def compare(self, a, b): return (a[-1] > b[-1]) - (a[-1] < b[-1])

class Sorter:                                          # 上下文
    def __init__(self, strategy: SortStrategy):
        self.strategy = strategy
    def sort(self, items):
        return sorted(items, key=functools.cmp_to_key(self.strategy.compare))

# --- Python 版：策略就是一个函数（0 行额外结构）---
def sort_with(items, key):
    return sorted(items, key=key)


# ============ ③ 装饰器模式 ============
def log_calls(fn):
    """GoF 的装饰器模式在 Python 里是一个语法（@）"""
    calls = []
    @functools.wraps(fn)
    def wrapper(*a, **kw):
        calls.append(a)
        return fn(*a, **kw)
    wrapper.calls = calls
    return wrapper

def memoize(fn):
    cache = {}
    @functools.wraps(fn)
    def wrapper(n):
        if n not in cache:
            cache[n] = fn(n)
        return wrapper.misses, cache[n]
    wrapper.misses = 0
    def counted(n):
        if n not in cache:
            wrapper.misses += 1
        return wrapper(n)[1]
    counted.stats = lambda: (len(cache), wrapper.misses)
    return counted


# ============ ⑤ 观察者模式 ============
class Subject:
    def __init__(self):
        self._observers = []                          # 就是一个函数列表
    def subscribe(self, fn):
        self._observers.append(fn)
        return lambda: self._observers.remove(fn)     # 返回退订函数（闭包）
    def emit(self, event):
        for fn in list(self._observers):
            fn(event)


if __name__ == "__main__":
    words = ["banana", "kiwi", "apple", "fig"]

    print("== ① 策略模式：GoF 的类 vs Python 的函数（同样的结果）==")
    gof = Sorter(ByLength()).sort(words)
    pyt = sort_with(words, key=len)
    print(f"  GoF 版（接口+3个类+上下文，约 25 行）: {gof}")
    print(f"  Python 版 sort_with(words, key=len):    {pyt}")
    print(f"  结果一致: {gof == pyt}")
    print(f"  换策略——GoF: Sorter(ByAlpha()).sort(...)  → {Sorter(ByAlpha()).sort(words)}")
    print(f"  换策略——Python: sort_with(words, str)      → {sort_with(words, str)}")
    print(f"  换策略——Python: 甚至可以现写 lambda        → {sort_with(words, lambda w: w[-1])}")
    print("  → 「策略模式」的本质是【把行为参数化】——而函数就是参数化的行为")
    print("  → 在函数不是一等公民的语言里，你必须用「只有一个方法的对象」来模拟函数")
    print("  → 于是 GoF 的一半模式，本质上是【在没有一等函数的语言里模拟一等函数】")

    print("\n== ② 哪些 GoF 模式在 Python 里「消失」了 ==")
    table = [
        ("策略 Strategy",   "传一个函数（① 实测）"),
        ("命令 Command",    "函数 / functools.partial（第 55 章的部分应用）"),
        ("模板方法",        "传一个函数进去，或用默认参数"),
        ("工厂方法",        "类本身就是可调用对象——直接传类名"),
        ("抽象工厂",        "一个返回构造函数的函数"),
        ("单例 Singleton",  "模块本身就是单例（import 只执行一次）"),
        ("迭代器 Iterator", "语言内建（第 44 章的生成器）"),
        ("装饰器 Decorator","语法糖 @（③ 实测）"),
        ("观察者 Observer", "一个函数列表（⑤ 实测）"),
    ]
    for name, py in table:
        print(f"  {name:<16} → {py}")
    print("  → 不是「Python 不需要设计」，而是【这些设计已经被语言内建了】")
    print("  → GoF 1994 年写书时，主流语言是 C++/Smalltalk —— 模式反映的是那时的语言短板")

    print("\n== ③ 装饰器：GoF 模式变成了语言语法（实测）==")
    @log_calls
    def greet(name): return f"你好, {name}"
    greet("甲"); greet("乙")
    print(f"  @log_calls 包装后调用两次: 记录 = {greet.calls}")

    @memoize
    def slow_square(n):
        time.sleep(0.001)
        return n * n
    for n in [4, 4, 5, 4, 5]:
        slow_square(n)
    print(f"  @memoize 调用 5 次（只有 2 个不同参数）: 缓存 {slow_square.stats()[0]} 项，"
          f"实际计算 {slow_square.stats()[1]} 次")
    print("  → GoF 的装饰器模式需要: 抽象组件 + 具体组件 + 抽象装饰器 + 具体装饰器（4 个类）")
    print("  → Python 只要一个高阶函数 + 一个 @ —— 而且可以叠加多层")

    print("\n== ④ 单例：模块就是单例（实测）==")
    import sys
    m1 = sys.modules[__name__]
    import __main__ as m2
    print(f"  两次拿到本模块对象: 同一个吗 {m1 is m2}")
    print("  → import 只执行一次模块代码（第 14 章 sys.modules 缓存）→ 模块级变量天然是单例")
    print("  → 所以 Python 的单例惯用法是: 模块里写 `_instance = Thing()`，用的人 import 它")
    print("  → 而不是 Java 那套 private 构造器 + getInstance()（Java 版实测它的线程陷阱）")

    print("\n== ⑤ 观察者：一个函数列表（实测）==")
    subject = Subject()
    received = []
    unsub = subject.subscribe(lambda e: received.append(f"A收到{e}"))
    subject.subscribe(lambda e: received.append(f"B收到{e}"))
    subject.emit("事件1")
    unsub()                                            # A 退订
    subject.emit("事件2")
    print(f"  两个订阅者 → emit(事件1) → A 退订 → emit(事件2)")
    print(f"  收到: {received}")
    print("  → 「观察者模式」= 一个回调列表 + 遍历调用，就这么简单")
    print("  → 它是第 43 章事件循环的用户态版本，也是 JS 全部异步的思想源头（JS 版展开）")

    print("\n== ⑥ 那 Python 里还需要模式吗 ==")
    print("  仍然需要的:")
    print("    仓储 Repository / 适配器 Adapter —— 它们是【架构边界】，与语言特性无关（第 55 章）")
    print("    状态机 State —— 复杂状态转移仍然需要显式建模")
    print("    建造者 Builder —— 但 Python 常用关键字参数 + dataclass 代替")
    print("  已被语言吞掉的: ② 表里那九个")
    print("  → 判据: 这个模式解决的是【语言的缺陷】还是【领域的复杂度】？")
    print("     前者会随语言进化消失，后者永远存在")
