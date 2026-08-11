"""协程：可暂停、可恢复的函数——栈帧住在堆上（第 32 章最彻底的兑现）。"""
import asyncio
import sys
import threading
import time
import tracemalloc


# ---------- ① 生成器：协程的原型 ----------
def counter(name, n):
    """每次 yield 暂停，下次 next() 从原地恢复——局部变量全程保留。"""
    total = 0
    for i in range(n):
        total += i
        yield f"{name}: 第 {i} 步，累计 {total}"
    return total


# ---------- ③ 手工协程调度器：三十行实现协作式多任务 ----------
def scheduler(tasks):
    """轮转执行：每个协程跑一步就让出，实现"看起来同时在跑"。"""
    queue = list(tasks)
    trace = []
    while queue:
        task = queue.pop(0)
        try:
            trace.append(next(task))       # 恢复它，跑到下一个 yield
            queue.append(task)             # 没结束就排回队尾（与第 43 章事件循环同构）
        except StopIteration:
            pass                           # 这个协程跑完了
    return trace


print("== ① 生成器就是协程：暂停与恢复 ==")
gen = counter("A", 3)
print(f"  调用生成器函数返回: {type(gen).__name__} 对象（函数体一行都没执行）")
print(f"  第一次 next(): {next(gen)}")
print(f"  第二次 next(): {next(gen)}   ← 从上次 yield 的下一行继续，total 还在")
print(f"  它的帧对象: gi_frame = {gen.gi_frame is not None}（活在堆上，第 32 章）")
print(f"  帧里的局部变量: {dict(list(gen.gi_frame.f_locals.items())[:3])}")

print("\n== ② 协程 vs 函数：唯一的区别是能不能中途出来 ==")
print("  普通函数: 调用 → 一路执行到 return → 栈帧销毁，局部变量全没了（第 32 章）")
print("  协程:     调用 → 执行到 yield → 栈帧【保留在堆上】→ 下次从原地继续")
print("  → 这就是为什么协程能实现「暂停的执行流」，而函数不能")

print("\n== ③ 钥匙实验：三十行搭一个协程调度器 ==")
trace = scheduler([counter("协程甲", 3), counter("协程乙", 2)])
for line in trace:
    print(f"    {line}")
print("  ↑ 两个协程交替推进——单线程上实现了「并发」，且完全没有锁")

print("\n== ④ 规模实验：协程 vs 线程 ==")
N = 50_000


async def tiny_coro():
    await asyncio.sleep(0.01)


def measure_coroutines():
    tracemalloc.start()
    t0 = time.perf_counter()

    async def run():
        await asyncio.gather(*(tiny_coro() for _ in range(N)))

    asyncio.run(run())
    ms = (time.perf_counter() - t0) * 1000
    _, peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    return ms, peak / 1024 / 1024


coro_ms, coro_mb = measure_coroutines()
print(f"  {N} 个协程: {coro_ms:.0f} ms，峰值内存 {coro_mb:.1f} MB，线程数 {threading.active_count()}")
print(f"  {N} 个线程: 光创建就要约 {N * 12.2 / 1000:.0f} ms（第 39 章实测 12.2 μs/个）")
print(f"             栈空间约 {N} MB（每个 1 MB，第 31 章实测）→ 根本开不出来")
print(f"  → 协程每个约 {coro_mb * 1024 / N:.1f} KB，线程每个 1 MB —— 相差三个数量级")

print("\n== ⑤ 协程的两种流派 ==")
print("  有栈协程（stackful）: 每个协程有自己的完整栈（可在任意深度让出）")
print("                        代表: goroutine、Java 虚拟线程、greenlet")
print("                        → 阻塞代码自动变让出，无 async 传染（第 42 章）")
print("  无栈协程（stackless）: 只保存必要的局部变量（只能在协程函数体内让出）")
print("                        代表: Python async、JS async、C# async、C++20 协程")
print("                        → 内存更省，但 async 会传染整条调用链")

print("\n== ⑥ async 就是「生成器 + 事件循环」==")


async def demo():
    await asyncio.sleep(0)
    return 42


coro = demo()
print(f"  async 函数返回: {type(coro).__name__}（与生成器同源，第 42 章实测过 cr_frame）")
print(f"  CPython 里 async def 复用了生成器的字节码机制（yield from → await）")
print(f"  send/throw/close 三个方法生成器与协程都有: "
      f"{[m for m in ('send', 'throw', 'close') if hasattr(coro, m)]}")
coro.close()

print("\n== ⑦ Python 的协程家族 ==")
print(f"  生成器 yield        : 最原始的暂停/恢复（本节 ①③ 实测）")
print(f"  yield from          : 委托给另一个生成器（协程组合的基础）")
print(f"  async/await         : 生成器机制 + 事件循环调度（第 42/43 章）")
print(f"  greenlet（第三方）  : 有栈协程，可在任意深度让出")
print(f"  本机 Python {sys.version_info.major}.{sys.version_info.minor}")
