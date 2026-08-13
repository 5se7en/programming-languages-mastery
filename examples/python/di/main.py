"""依赖注入：为什么动态语言的 DI 长得不一样——猴子补丁、默认参数、Protocol。"""
import time
import unittest.mock as mock
from typing import Protocol


# ============ 被测系统 ============
class Clock(Protocol):
    """结构化类型（第 28 章的鸭子类型）: 不需要 implements，长得像就行"""
    def now(self) -> float: ...


class SystemClock:
    def now(self) -> float:
        return time.time()


class FrozenClock:
    def __init__(self, t: float):
        self.t = t
    def now(self) -> float:
        return self.t


class Mailer:
    def send(self, to: str, msg: str) -> None:
        raise RuntimeError("真的发邮件了！（测试里绝不该走到这）")


class FakeMailer:
    def __init__(self):
        self.sent = []
    def send(self, to: str, msg: str) -> None:
        self.sent.append((to, msg))


# ---- 写法 A: 依赖写死在内部（不可测） ----
class HardCodedService:
    def __init__(self):
        self.clock = SystemClock()          # ⚠️ 写死
        self.mailer = Mailer()              # ⚠️ 写死

    def greet(self, user):
        stamp = int(self.clock.now())
        self.mailer.send(user, f"你好 @{stamp}")
        return stamp


# ---- 写法 B: 构造器注入（与 Java/C# 完全一样） ----
class InjectedService:
    def __init__(self, clock: Clock, mailer):
        self.clock, self.mailer = clock, mailer

    def greet(self, user):
        stamp = int(self.clock.now())
        self.mailer.send(user, f"你好 @{stamp}")
        return stamp


# ---- 写法 C: 默认参数注入（Python 特有的轻量写法） ----
def greet_with_defaults(user, clock=None, mailer=None):
    """生产直接调 greet_with_defaults(user)，测试传替身进来"""
    clock = clock or SystemClock()
    mailer = mailer or Mailer()
    stamp = int(clock.now())
    mailer.send(user, f"你好 @{stamp}")
    return stamp


if __name__ == "__main__":
    print("== ① 依赖写死时，动态语言【依然能】换掉它——猴子补丁 ==")
    svc = HardCodedService()
    fake = FakeMailer()
    svc.mailer = fake                          # 直接改实例属性
    svc.clock = FrozenClock(1_000_000)
    print(f"  改完实例属性后 greet('甲') → {svc.greet('甲')}，邮件: {fake.sent}")
    print("  → 静态语言里这一步做不到（private final 字段），所以【必须】先做好 DI")
    print("  → 这就是「Python 不太需要 DI 容器」的技术根源: 一切都是可写的属性")

    print("\n== ② 更彻底的猴子补丁：连模块级的名字都能换（实测）==")
    with mock.patch(__name__ + ".SystemClock", lambda: FrozenClock(2_000_000)), \
         mock.patch(__name__ + ".Mailer", FakeMailer):
        svc2 = HardCodedService()              # 内部 new 的是被替换后的类
        stamp = svc2.greet("乙")
        print(f"  patch 期间 HardCodedService 内部 new 出来的是替身: greet → {stamp}")
        print(f"  它的 mailer 类型: {type(svc2.mailer).__name__}，收到: {svc2.mailer.sent}")
    print(f"  退出 with 之后恢复原状: SystemClock 是 {SystemClock.__name__} ✓")
    print("  → unittest.mock.patch 直接改【模块命名空间里的名字绑定】（第 13 章作用域）")
    print("  ⚠️ 但这不是「更好的 DI」，而是【测试对实现细节的耦合】:")
    print("     patch 的字符串路径写死了「谁在哪里 new 了什么」——重构一下测试就红")

    print("\n== ③ 构造器注入：Python 里同样是首选（实测）==")
    fake2 = FakeMailer()
    svc3 = InjectedService(FrozenClock(3_000_000), fake2)
    print(f"  InjectedService(FrozenClock, FakeMailer).greet('丙') → {svc3.greet('丙')}")
    print(f"  邮件: {fake2.sent}")
    print("  → 不需要 patch，不需要知道内部实现——测试只依赖【构造器签名】这个公开契约")
    print("  → 猴子补丁能救你，但不该成为设计的替代品")

    print("\n== ④ 默认参数注入：Python 特有的轻量方案（实测）==")
    fake3 = FakeMailer()
    print(f"  生产调用: greet_with_defaults('丁', ...) 用真实依赖（此处省略，会真发邮件）")
    print(f"  测试调用: greet_with_defaults('丁', FrozenClock(4e6), FakeMailer())")
    print(f"    → {greet_with_defaults('丁', FrozenClock(4_000_000), fake3)}，邮件: {fake3.sent}")
    print("  → 一个函数就是一个「注入点」，不需要类、不需要容器")
    print("  ⚠️ 陷阱: 别写 def f(mailer=Mailer())——默认值在【定义时】求值一次，全局共享")
    print("     所以要写 mailer=None 再在函数体里兜底（本例的写法）")

    print("\n== ⑤ Protocol：静态检查 + 鸭子类型（Python 的接口答案）==")
    def uses_clock(c: Clock) -> float:
        return c.now()
    print(f"  SystemClock 满足 Clock 协议吗: 它有 now() → {hasattr(SystemClock(), 'now')}")
    print(f"  FrozenClock 满足吗: {hasattr(FrozenClock(0), 'now')}   （两者都没 implements 任何接口）")
    print(f"  uses_clock(FrozenClock(5e6)) → {uses_clock(FrozenClock(5_000_000))}")
    print("  → Protocol 是【结构化类型】: mypy 静态检查它，运行时不强制（第 28 章接口的第三条路）")
    print("  → 于是 Python 得到了「有类型提示的 DI」而不必引入接口继承的仪式")

    print("\n== ⑥ Python 需要 DI 容器吗 ==")
    print("  不太需要的理由:")
    print("    ① 一切可替换（① ② 实测）—— 静态语言的「必须先设计好」在这里不成立")
    print("    ② 函数是一等公民 —— 依赖常常是一个函数，直接传就行（④）")
    print("    ③ 鸭子类型 —— 不需要为了注入而先定义接口（⑤）")
    print("  仍然需要的场景:")
    print("    对象图很深（几十个服务互相依赖）、需要统一的生命周期管理、Web 框架的请求作用域")
    print("    → FastAPI 的 Depends()、依赖注入库 injector/dependency-injector")
    print("  → 通用结论: 【DI 是设计原则，容器是规模到了才需要的工具】——与 Java 版 ③ 同一句话")

    print("\n== ⑦ 判据：什么该注入 ==")
    print("  时间、随机、网络、数据库、文件系统、当前用户 —— 一切【外部世界】")
    print("  → 第 52 章实测过: 时间与随机是 flaky 测试的两大来源，注入即驯服")
    print("  → 反过来: 纯计算、数据结构、不可变值对象 —— 注入它们只会增加噪音")
