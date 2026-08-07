"""第 28 章 · 接口 —— Python 示例
运行：python3 main.py

Python 有两套接口方案：ABC（名义化）和 Protocol（结构化）。
"""

from abc import ABC, abstractmethod
from typing import Protocol, runtime_checkable

print("=== 1. ABC：名义化契约（必须显式继承）===")


class StorageABC(ABC):
    @abstractmethod
    def save(self, data: str) -> str: ...


class FileStorageABC(StorageABC):        # 必须显式继承
    def save(self, data): return f"文件: {data}"


print(f"  FileStorageABC().save('x') = {FileStorageABC().save('x')}")
print(f"  FileStorageABC 的父类: {[c.__name__ for c in FileStorageABC.__bases__]}")

try:
    StorageABC()
except TypeError as e:
    print(f"  StorageABC() → TypeError: {e}")


class Incomplete(StorageABC):
    pass                                  # 忘了实现 save


try:
    Incomplete()
except TypeError as e:
    print(f"  Incomplete() → TypeError: {e}")
print("  → ABC 把「运行时才发现没实现」提前到「实例化时报错」")

print("\n=== 2. Protocol：结构化契约（不需要继承，Python 3.8+）===")


class Storage(Protocol):
    """只声明形状，不需要任何类来继承它"""

    def save(self, data: str) -> str: ...


class FileStorage:                        # ⚠️ 完全没有继承 Storage
    def save(self, data): return f"写入文件: {data}"


class S3Storage:
    def save(self, data): return f"上传到 S3: {data}"


class MemoryStorage:
    def __init__(self): self.items = []
    def save(self, data):
        self.items.append(data)
        return f"存进内存: {data}"


print(f"  FileStorage 的父类: {[c.__name__ for c in FileStorage.__bases__]}  ← 没继承 Storage")
print("  但类型检查器认可它满足 Storage 契约")
print("  → 这就是「结构化子类型」：长得像就行")

print("\n=== 3. 依赖倒置：接口最重要的应用 ===")


class ReportService:
    def __init__(self, storage: Storage):     # 注入的是「契约」
        self.storage = storage

    def generate(self, content):
        return self.storage.save(f"报表[{content}]")


print("  同一个 ReportService，换不同的 Storage：")
for s in [FileStorage(), S3Storage(), MemoryStorage()]:
    result = ReportService(s).generate("月度")
    print(f"    {type(s).__name__:15} → {result}")

print()
print("  → ReportService 的代码一个字都不用改")
print("  → 这就是「依赖倒置原则」：高层不依赖低层，两者都依赖抽象")

print("\n=== 4. 用内存实现做单元测试 ===")
mem = MemoryStorage()
svc = ReportService(mem)
svc.generate("一月")
svc.generate("二月")
print(f"  生成 2 份报表后，MemoryStorage 里有 {len(mem.items)} 条: {mem.items}")
print("  → 不碰真实文件/网络就能验证业务逻辑 —— 这是接口最实际的价值")

print("\n=== 5. ⚠️ Protocol 默认不能用于 isinstance ===")
try:
    isinstance(FileStorage(), Storage)
except TypeError as e:
    print(f"  isinstance(FileStorage(), Storage) → TypeError:")
    print(f"    {e}")

print("\n  想在运行时也能检查，要加 @runtime_checkable：")


@runtime_checkable
class RuntimeStorage(Protocol):
    def save(self, data: str) -> str: ...


print(f"    isinstance(FileStorage(),  RuntimeStorage) = {isinstance(FileStorage(), RuntimeStorage)}")
print(f"    isinstance(S3Storage(),    RuntimeStorage) = {isinstance(S3Storage(), RuntimeStorage)}")
print(f"    isinstance(42,             RuntimeStorage) = {isinstance(42, RuntimeStorage)}")
print("  ⚠️ 但运行时检查只看「有没有这个方法名」，不检查签名是否匹配")


class FakeStorage:
    def save(self):              # 签名不对！少了 data 参数
        return "假的"


print(f"    isinstance(FakeStorage(),  RuntimeStorage) = {isinstance(FakeStorage(), RuntimeStorage)}"
      "  ← 签名不对也返回 True！")
print("  → 所以 Protocol 的正确用法是静态类型检查（mypy/pyright），不是运行时")

print("\n=== 6. Protocol 的杀手锏：适配第三方类型 ===")


class ThirdPartyLogger:
    """假设这是你无法修改的第三方类"""

    def save(self, data): return f"第三方日志: {data}"


print(f"  ThirdPartyLogger 没有也不可能继承你的 Storage")
print(f"  但它照样能用: {ReportService(ThirdPartyLogger()).generate('季度')}")
print("  → 用 ABC 的话，你得写一个适配器类把它包起来")
print("  → 这是 Protocol 相对 ABC 最大的优势")

print("\n=== 7. ABC vs Protocol 怎么选 ===")
print("  场景                      ABC      Protocol")
print("  你控制所有实现类           ✅        ✅")
print("  要适配第三方类型           ❌        ✅")
print("  需要提供部分实现           ✅        ❌")
print("  想在实例化时就报错         ✅        ❌（只有静态检查）")
print("  表达「能做什么」            一般      ✅ 更贴切")

print("\n=== 8. 接口隔离原则 ===")


class Workable(Protocol):
    def work(self) -> str: ...


class Feedable(Protocol):
    def eat(self) -> str: ...


class Robot:                              # 只实现需要的
    def work(self): return "机器人在工作"


class Human:
    def work(self): return "人在工作"
    def eat(self): return "人在吃饭"


def do_work(w: Workable): return w.work()


print("  ❌ 胖接口会逼实现类写 raise NotImplementedError")
print("  ✅ 拆成小接口：")
for w in [Robot(), Human()]:
    print(f"    {type(w).__name__:6} → {do_work(w)}")
print(f"    Human 还能: {Human().eat()}")
print("  → 判断信号：实现类里出现 raise NotImplementedError，说明接口太胖了")

print("\n=== 9. 小结 ===")
print("  · ABC 是名义化契约：必须继承，实例化时检查")
print("  · Protocol 是结构化契约：长得像就行，静态检查")
print("  · Protocol 的杀手锏是适配第三方类型 —— ABC 做不到")
print("  · ⚠️ Protocol 默认不能 isinstance，加了 @runtime_checkable 也不检查签名")
print("  · 依赖倒置 + 内存实现 = 不碰真实资源就能测试业务逻辑")
