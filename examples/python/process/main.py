"""进程：Python 用多进程绕开 GIL——CPU 密集任务的唯一真并行手段。"""
import multiprocessing as mp
import os
import time

counter = 100                       # 模块级变量：子进程各持一份


def child_modifies():
    """子进程里改全局变量——父进程看不见。"""
    global counter
    counter += 1
    print(f"  [子进程 {os.getpid()}] 改后 counter = {counter}，父进程是 {os.getppid()}")


def cpu_task(n):
    """纯 CPU 计算：GIL 下多线程无法并行，多进程可以。"""
    total = 0
    for i in range(n):
        total += i * i
    return total


def worker(x):
    return cpu_task(x)


def producer(queue):
    """必须定义在模块顶层——spawn 模式下子进程要按名字 import 它。"""
    queue.put(f"来自进程 {os.getpid()} 的消息")


if __name__ == "__main__":          # spawn 模式必需（macOS/Windows 默认）
    print("== ① 进程身份 ==")
    print(f"  我是进程 {os.getpid()}，父进程 {os.getppid()}")
    print(f"  CPU 核数 = {mp.cpu_count()}，启动方式 = {mp.get_start_method()}")

    print("\n== ② 钥匙实验：进程之间内存隔离 ==")
    print(f"  父进程 counter = {counter}")
    p = mp.Process(target=child_modifies)
    p.start()
    p.join()
    print(f"  子进程改完之后，父进程 counter = {counter}   <- 纹丝不动（各持一份）")

    print("\n== ③ 真并行：多进程绕开 GIL ==")
    N = 8_000_000
    jobs = [N] * 4

    t0 = time.perf_counter()
    serial = [cpu_task(n) for n in jobs]        # 串行基线
    t1 = time.perf_counter()

    with mp.Pool(4) as pool:                    # 四进程并行
        parallel = pool.map(worker, jobs)
    t2 = time.perf_counter()

    serial_ms, parallel_ms = (t1 - t0) * 1000, (t2 - t1) * 1000
    print(f"  串行 4 个任务:  {serial_ms:7.0f} ms")
    print(f"  4 进程并行:     {parallel_ms:7.0f} ms")
    print(f"  加速比 = {serial_ms / parallel_ms:.2f}x   <- 接近核数（真并行）")
    print(f"  结果一致: {serial == parallel}")
    print("  （同样的任务用多线程加速比约等于 1——第 40 章 GIL 实测）")

    print("\n== ④ 进程间通信：Queue ==")
    q = mp.Queue()
    p2 = mp.Process(target=producer, args=(q,))
    p2.start()
    print(f"  父进程收到: {q.get()}")
    p2.join()
    print("  （数据经 pickle 序列化后跨进程传输——隔离的代价）")

    print("\n== ⑤ 什么时候用多进程 ==")
    print("  CPU 密集 + 需要真并行  -> 多进程（本节实测加速比）")
    print("  I/O 密集              -> 线程或异步更划算（第 40/42 章）")
    print("  需要崩溃隔离           -> 多进程（子进程挂了不影响父进程）")
