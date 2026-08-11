"""线程：Python 有真线程，但 GIL 让它们无法并行执行字节码。"""
import sys
import threading
import time

counter = 0
lock = threading.Lock()


def race_worker(times):
    """⚠️ counter += 1 不是原子操作——字节码层面是读-改-写三步。"""
    global counter
    for _ in range(times):
        counter += 1


def locked_worker(times):
    global counter
    for _ in range(times):
        with lock:                       # ✅ 加锁：三步变成不可分割的一步
            counter += 1


def cpu_task(n):
    total = 0
    for i in range(n):
        total += i * i
    return total


def io_task(seconds):
    time.sleep(seconds)                  # I/O 等待期间 GIL 会被释放


print("== ① 线程共享一切，除了栈 ==")
print(f"  当前线程: {threading.current_thread().name}，活跃线程数 = {threading.active_count()}")
print(f"  GIL 切换间隔 = {sys.getswitchinterval() * 1000:.0f} ms")

print("\n== ② 钥匙实验：数据竞争 ==")
sys.setswitchinterval(1e-6)              # 缩短切换间隔，让竞争更容易暴露
N = 200_000
for run in range(1, 4):
    counter = 0
    t1 = threading.Thread(target=race_worker, args=(N,))
    t2 = threading.Thread(target=race_worker, args=(N,))
    t1.start(); t2.start(); t1.join(); t2.join()
    print(f"  第 {run} 次运行: 期望 {2*N}，实际 {counter}   （丢了 {2*N - counter} 次）")
print("  ↑ 有 GIL 也照样出错——GIL 保护的是解释器内部，不是你的业务逻辑")
sys.setswitchinterval(5e-3)              # 恢复默认

print("\n== ③ 加锁修复 ==")
for run in range(1, 3):
    counter = 0
    t1 = threading.Thread(target=locked_worker, args=(N,))
    t2 = threading.Thread(target=locked_worker, args=(N,))
    t1.start(); t2.start(); t1.join(); t2.join()
    print(f"  第 {run} 次运行: 期望 {2*N}，实际 {counter}   ✅")

print("\n== ④ 钥匙实验二：GIL 让 CPU 密集任务无法并行 ==")
M = 4_000_000
t0 = time.perf_counter()
for _ in range(4):
    cpu_task(M)
serial = (time.perf_counter() - t0) * 1000

t0 = time.perf_counter()
threads = [threading.Thread(target=cpu_task, args=(M,)) for _ in range(4)]
for t in threads: t.start()
for t in threads: t.join()
threaded = (time.perf_counter() - t0) * 1000

print(f"  串行 4 个 CPU 任务: {serial:7.0f} ms")
print(f"  4 线程并发:         {threaded:7.0f} ms")
print(f"  加速比 = {serial / threaded:.2f}x   <- 约等于 1！线程完全没帮上忙")
print("  （对比第 39 章：同样的任务用 4 进程，加速比 2.85x）")

print("\n== ⑤ 但 I/O 密集任务，线程非常有效 ==")
t0 = time.perf_counter()
for _ in range(4):
    io_task(0.1)
serial_io = (time.perf_counter() - t0) * 1000

t0 = time.perf_counter()
threads = [threading.Thread(target=io_task, args=(0.1,)) for _ in range(4)]
for t in threads: t.start()
for t in threads: t.join()
threaded_io = (time.perf_counter() - t0) * 1000

print(f"  串行 4 次 I/O 等待: {serial_io:7.0f} ms")
print(f"  4 线程并发:         {threaded_io:7.0f} ms")
print(f"  加速比 = {serial_io / threaded_io:.2f}x   <- 接近 4！")
print("  （等待 I/O 时 GIL 会释放——所以线程在 I/O 场景照样香）")
