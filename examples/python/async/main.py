"""异步：用一条线程处理成千上万个并发 I/O——asyncio 的核心承诺。"""
import asyncio
import threading
import time

IO_DELAY = 0.05          # 模拟一次 I/O 的耗时
TASKS = 20


def blocking_io(n):
    time.sleep(IO_DELAY)                     # 阻塞式等待：线程被占住
    return n


async def async_io(n):
    await asyncio.sleep(IO_DELAY)            # 异步等待：让出控制权
    return n


async def main():
    print("== ① 钥匙实验：同样的 I/O 任务，三种做法 ==")

    t0 = time.perf_counter()
    serial = [blocking_io(i) for i in range(TASKS)]
    serial_ms = (time.perf_counter() - t0) * 1000

    t0 = time.perf_counter()
    threads = [threading.Thread(target=blocking_io, args=(i,)) for i in range(TASKS)]
    for t in threads: t.start()
    for t in threads: t.join()
    thread_ms = (time.perf_counter() - t0) * 1000

    t0 = time.perf_counter()
    results = await asyncio.gather(*(async_io(i) for i in range(TASKS)))
    async_ms = (time.perf_counter() - t0) * 1000

    print(f"  串行 {TASKS} 个 I/O:      {serial_ms:7.0f} ms（每个等 {IO_DELAY*1000:.0f} ms，全在排队）")
    print(f"  {TASKS} 个线程并发:       {thread_ms:7.0f} ms（加速比 {serial_ms/thread_ms:.1f}x）")
    print(f"  asyncio 并发（1 线程）:  {async_ms:7.0f} ms（加速比 {serial_ms/async_ms:.1f}x）")
    print(f"  当前线程数 = {threading.active_count()}   <- 异步全程只用一条线程")
    print(f"  结果正确: {results == list(range(TASKS))}")

    print("\n== ② 规模才是异步的主场 ==")
    BIG = 5_000
    t0 = time.perf_counter()
    await asyncio.gather(*(asyncio.sleep(0.01) for _ in range(BIG)))
    big_ms = (time.perf_counter() - t0) * 1000
    print(f"  {BIG} 个并发异步任务: {big_ms:.0f} ms，线程数仍是 {threading.active_count()}")
    print(f"  （{BIG} 个线程要 {BIG*12.2/1000:.0f} ms 创建开销 + 约 {BIG}MB 栈——第 39/40 章实测）")

    print("\n== ③ 协程是对象：栈帧住在堆上 ==")
    coro = async_io(1)
    print(f"  async 函数调用返回的是: {type(coro).__name__} 对象，而不是结果")
    print(f"  它有自己的状态: cr_frame = {coro.cr_frame is not None}（帧对象在堆上，第 32 章）")
    task = asyncio.ensure_future(coro)
    print(f"  包装成 Task 后才会被调度执行: {type(task).__name__}")
    await task
    print(f"  完成后: task.done() = {task.done()}, 结果 = {task.result()}")

    print("\n== ④ 阻塞调用会毁掉整个事件循环 ==")
    async def good():
        await asyncio.sleep(0.1)             # ✅ 让出
        return "good"

    async def bad():
        time.sleep(0.1)                      # ⚠️ 阻塞！整个事件循环停转
        return "bad"

    t0 = time.perf_counter()
    await asyncio.gather(good(), good(), good())
    good_ms = (time.perf_counter() - t0) * 1000
    t0 = time.perf_counter()
    await asyncio.gather(bad(), bad(), bad())
    bad_ms = (time.perf_counter() - t0) * 1000
    print(f"  3 个 await asyncio.sleep(0.1): {good_ms:.0f} ms（并发 ✅）")
    print(f"  3 个 time.sleep(0.1):          {bad_ms:.0f} ms（串行 ❌ 事件循环被占死）")
    print("  → 异步代码里绝不能有阻塞调用（用 run_in_executor 兜底）")

    print("\n== ⑤ async 的传染性（红蓝函数问题）==")
    print("  async 函数只能被 async 函数 await —— 调用链上每一层都得是 async")
    print("  同步函数想调 async：只能 asyncio.run()（会阻塞）或丢进 executor")
    print("  这就是「函数有颜色」：async 是红色，普通是蓝色，红色会向上传染整条调用链")

    print("\n== ⑥ 结构化并发（Python 3.11+ 的 TaskGroup）==")
    try:
        async with asyncio.TaskGroup() as tg:      # 组内任一失败则全组取消
            tg.create_task(async_io(1))
            tg.create_task(async_io(2))
        print("  TaskGroup: 组内任务全部完成（任一失败会取消其余）")
    except AttributeError:
        results = await asyncio.gather(async_io(1), async_io(2))
        print(f"  本机 Python 版本较早，用 gather 代替: {results}")


if __name__ == "__main__":
    asyncio.run(main())
