"""事件循环：asyncio 的循环——与 JS 同构，但队列只有一级。"""
import asyncio
import selectors
import time


async def main():
    print("== ① 钥匙实验：asyncio 的执行顺序 ==")
    loop = asyncio.get_running_loop()
    order = []

    loop.call_soon(lambda: order.append("3. call_soon（就绪队列）"))
    asyncio.ensure_future(coro_task(order))            # 协程也进就绪队列
    loop.call_later(0, lambda: order.append("5. call_later(0)（定时器队列）"))
    order.append("1. 同步代码")

    await asyncio.sleep(0)                             # 让出一次：就绪队列被执行
    order.append("4. await sleep(0) 之后")
    await asyncio.sleep(0.01)                          # 让定时器也到期
    for line in order:
        print(f"    {line}")

    print("\n== ② asyncio 的队列结构（与 JS 的差异）==")
    print("  JS:      宏任务队列 + 微任务队列（两级，微任务优先级更高）")
    print("  asyncio: 只有一个就绪队列（_ready）+ 一个定时器堆（_scheduled）")
    print("  → asyncio 没有「微任务饿死宏任务」的问题（所有回调平权排队）")
    print("  → 但同样有「一个长回调卡死整个循环」的问题")

    print("\n== ③ 事件循环的真身：selector + 队列 ==")
    print(f"  本机 selector = {type(loop._selector).__name__ if hasattr(loop, '_selector') else 'N/A'}"
          f"（macOS 上是 kqueue，Linux 上是 epoll）")
    print(f"  默认 selector = {selectors.DefaultSelector.__name__}")
    print("  循环体: ① 算出最近的定时器到期时间")
    print("          ② selector.select(timeout) —— 阻塞等 I/O（这是唯一睡觉的地方）")
    print("          ③ 把就绪的 I/O 回调 + 到期的定时器回调放进就绪队列")
    print("          ④ 依次执行就绪队列里的所有回调")

    print("\n== ④ 阻塞回调卡死循环（与 JS 同构）==")
    t0 = time.perf_counter()
    await asyncio.gather(asyncio.sleep(0.05), asyncio.sleep(0.05), asyncio.sleep(0.05))
    good = (time.perf_counter() - t0) * 1000

    def blocking_callback():
        time.sleep(0.05)                               # ⚠️ 在回调里阻塞

    t0 = time.perf_counter()
    for _ in range(3):
        loop.call_soon(blocking_callback)
    await asyncio.sleep(0.2)                           # 等它们跑完
    bad = (time.perf_counter() - t0) * 1000
    print(f"  3 个 await sleep(0.05) 并发: {good:.0f} ms ✅")
    print(f"  3 个阻塞回调:               {bad:.0f} ms（含 200ms 等待，实际串行了 150ms）❌")

    print("\n== ⑤ 调试模式：找出慢回调 ==")
    loop.set_debug(True)
    loop.slow_callback_duration = 0.02                 # 超过 20ms 的回调会告警
    loop.call_soon(lambda: time.sleep(0.03))
    await asyncio.sleep(0.1)
    print("  loop.set_debug(True) + slow_callback_duration 会打印 Executing <Handle...> took X seconds")
    print("  （上面若出现 WARNING 就是它抓到了阻塞回调——生产排查的第一工具）")
    loop.set_debug(False)

    print("\n== ⑥ 一个循环，一条线程 ==")
    print("  asyncio.run() 创建循环 → 跑到所有任务完成 → 关闭循环")
    print("  一个线程只能有一个运行中的循环（get_running_loop 会报错如果没有）")
    print("  多核要用多进程（第 39 章）或 loop.run_in_executor 把阻塞活儿丢给线程池")


async def coro_task(order):
    order.append("2. 协程被调度（也在就绪队列里）")


if __name__ == "__main__":
    asyncio.run(main())
