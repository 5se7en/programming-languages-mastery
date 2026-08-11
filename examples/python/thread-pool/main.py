"""线程池：为什么必须复用线程——以及 GIL 如何决定「该用线程还是进程」。"""
import concurrent.futures as cf
import os
import threading
import time

CPU_N = 3_000_000


# 顶层函数：ProcessPoolExecutor 在 macOS 用 spawn，闭包/内嵌函数无法 pickle（第 39 章的教训）
def cpu_task(n=CPU_N):
    s = 0
    for i in range(n):
        s += i * i
    return s


def io_task(_=None):
    time.sleep(0.01)
    return 1


def noop():
    pass


def noop_arg(_=None):
    return None


def warm_process_pool(n):
    """建池并【预热】：spawn 出子进程、导入模块，之后计时才是稳态吞吐。"""
    pp = cf.ProcessPoolExecutor(max_workers=n)
    list(pp.map(noop_arg, range(n * 2)))
    return pp


def timed(fn):
    t0 = time.perf_counter()
    r = fn()
    return (time.perf_counter() - t0) * 1000, r


if __name__ == "__main__":
    cores = os.cpu_count()

    print("== ① 每任务一线程 vs 复用线程池 ==")
    N = 2000
    def per_task():
        ts = [threading.Thread(target=noop) for _ in range(N)]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
    ms_raw, _ = timed(per_task)

    def pooled():
        with cf.ThreadPoolExecutor(max_workers=8) as ex:
            list(ex.map(lambda _: None, range(N)))
    ms_pool, _ = timed(pooled)
    print(f"  {N} 个空任务，每任务新建线程: {ms_raw:.0f} ms（{ms_raw*1000/N:.1f} μs/个）")
    print(f"  {N} 个空任务，8 线程池复用    : {ms_pool:.0f} ms（{ms_pool*1000/N:.1f} μs/个）")
    print(f"  → 池化快 {ms_raw/ms_pool:.1f}x —— 省下的全是线程创建/销毁的开销（第 40 章实测 12.2 μs/个）")

    print("\n== ② CPU 密集：GIL 让线程池毫无意义 ==")
    ms_serial, _ = timed(lambda: [cpu_task() for _ in range(4)])

    # 池要【预热】后再计时——生产中池在启动时建好，稳态吞吐才是要衡量的东西
    tp = cf.ThreadPoolExecutor(max_workers=4)
    list(tp.map(noop_arg, range(4)))
    ms_thread, _ = timed(lambda: list(tp.map(cpu_task, [CPU_N] * 4)))
    tp.shutdown()

    ms_spawn, pp = timed(lambda: warm_process_pool(4))
    ms_proc, _ = timed(lambda: list(pp.map(cpu_task, [CPU_N] * 4)))
    pp.shutdown()

    print(f"  4 个纯计算任务，串行:       {ms_serial:.0f} ms")
    print(f"  4 个纯计算任务，4 线程池:   {ms_thread:.0f} ms（加速 {ms_serial/ms_thread:.2f}x）← GIL 卡死")
    print(f"  4 个纯计算任务，4 进程池:   {ms_proc:.0f} ms（加速 {ms_serial/ms_proc:.2f}x）← 真并行")
    print(f"  （进程池的启动成本单独计: {ms_spawn:.0f} ms —— macOS 用 spawn，每个子进程要重新导入模块）")
    print("  → Python 的池化第一问不是「多大」，而是「线程还是进程」（第 41 章 GIL）")

    print("\n== ③ I/O 密集：池大小直接决定吞吐 ==")
    TASKS = 64
    for size in (1, 2, 4, 8, 16, 32, 64):
        def run(size=size):
            with cf.ThreadPoolExecutor(max_workers=size) as ex:
                list(ex.map(io_task, range(TASKS)))
        ms, _ = timed(run)
        print(f"  池大小 {size:>2}: {TASKS} 个 10ms I/O 任务耗时 {ms:>6.1f} ms"
              f"  (理论下限 {(TASKS + size - 1)//size * 10} ms)")
    print("  → I/O 密集时线程数远大于核心数才划算——线程在睡觉，不占 CPU")

    print("\n== ④ 无界队列：OOM 的经典配方 ==")
    ex = cf.ThreadPoolExecutor(max_workers=2)
    futs = [ex.submit(io_task) for _ in range(1000)]
    print(f"  向 2 线程的池提交 1000 个任务后，队列里积压: {ex._work_queue.qsize()} 个")
    print("  Python 的 ThreadPoolExecutor 队列【无上限】——提交多快就堆多快")
    print("  → 生产者比消费者快时，队列会一直涨到 OOM（Java 的 newFixedThreadPool 同病）")
    print("  → 正确做法: 有界队列 + 拒绝策略（Python 需自己用 Semaphore 限流）")
    for f in futs:
        f.result()
    ex.shutdown()

    print("\n== ⑤ 默认池大小与两个公式 ==")
    print(f"  本机核心数: {cores}")
    print(f"  ThreadPoolExecutor 默认 max_workers = min(32, cpu+4) = {min(32, cores + 4)}")
    print(f"  ProcessPoolExecutor 默认 max_workers = cpu = {cores}")
    print(f"  CPU 密集公式: 线程数 ≈ 核心数 = {cores}")
    print(f"  I/O 密集公式: 线程数 ≈ 核心数 × (1 + 等待/计算)")
    print(f"    例：等待 90ms 计算 10ms → {cores} × 10 = {cores*10} 条")

    print("\n== ⑥ 一个池 vs 多个池 ==")
    print("  单一大池: 慢任务会拖垮快任务（队头阻塞）")
    print("  按用途拆池: 数据库池 / HTTP 池 / 计算池 —— 故障隔离（舱壁模式）")
    print("  → 与第 39 章「进程隔离」同一个思想，只是粒度更细")
