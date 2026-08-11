"""锁：Python 的 Lock / RLock —— 以及 GIL 之外你仍然需要它的理由。"""
import sys
import threading
import time

counter = 0
big_lock = threading.Lock()

lock_a = threading.Lock()
lock_b = threading.Lock()


def with_lock(times):
    global counter
    for _ in range(times):
        with big_lock:                       # ✅ RAII 风格（第 37 章的 with）
            counter += 1


def without_lock(times):
    global counter
    for _ in range(times):
        counter += 1


print("== ① 锁 vs 无锁：正确性与成本 ==")
N = 200_000
sys.setswitchinterval(1e-6)              # 缩短 GIL 切换间隔，让竞争显形（第 40 章同款）
counter = 0
t0 = time.perf_counter()
ts = [threading.Thread(target=without_lock, args=(N,)) for _ in range(2)]
for t in ts: t.start()
for t in ts: t.join()
race_ms = (time.perf_counter() - t0) * 1000
mark = "✅" if counter == 2 * N else f"❌ 丢了 {2*N - counter} 次"
print(f"  无锁: 结果 = {counter}（期望 {2*N}）{mark}，耗时 {race_ms:.1f} ms")

counter = 0
t0 = time.perf_counter()
ts = [threading.Thread(target=with_lock, args=(N,)) for _ in range(2)]
for t in ts: t.start()
for t in ts: t.join()
lock_ms = (time.perf_counter() - t0) * 1000
print(f"  加锁: 结果 = {counter}（期望 {2*N}）✅，耗时 {lock_ms:.1f} ms")
print(f"  锁让它慢了 {lock_ms/race_ms:.1f} 倍——正确性的价格")
sys.setswitchinterval(5e-3)              # 恢复默认

print("\n== ② 钥匙实验：死锁 ==")
deadlock_detected = threading.Event()


def worker_1():
    with lock_a:
        time.sleep(0.1)
        got = lock_b.acquire(timeout=0.5)     # 用超时代替永久阻塞，示例才能结束
        if got:
            print("  worker_1 拿到了两把锁")
            lock_b.release()
        else:
            print("  worker_1: 持有 A，等 B 超时——对方正持有 B")
            deadlock_detected.set()


def worker_2():
    with lock_b:                              # ⚠️ 顺序相反
        time.sleep(0.1)
        got = lock_a.acquire(timeout=0.5)
        if got:
            print("  worker_2 拿到了两把锁")
            lock_a.release()
        else:
            print("  worker_2: 持有 B，等 A 超时——对方正持有 A")
            deadlock_detected.set()


t1 = threading.Thread(target=worker_1)
t2 = threading.Thread(target=worker_2)
t1.start(); t2.start(); t1.join(); t2.join()
print(f"  死锁发生了吗: {deadlock_detected.is_set()}")
print("  （把 timeout 去掉就是真死锁——Python 没有 JVM 那样的自动检测）")

print("\n== ③ 破解：全局锁顺序 ==")
locks = {"A": lock_a, "B": lock_b}


def ordered_work(name1, name2, tag):
    for key in sorted([name1, name2]):        # ✅ 永远按名字排序取锁
        locks[key].acquire()
    print(f"  {tag}: 按顺序拿到了两把锁")
    for key in sorted([name1, name2], reverse=True):
        locks[key].release()


t1 = threading.Thread(target=ordered_work, args=("A", "B", "线程1"))
t2 = threading.Thread(target=ordered_work, args=("B", "A", "线程2"))
t1.start(); t2.start(); t1.join(); t2.join()
print("  两个线程需求相反，但取锁顺序一致 → 环等待不可能形成")

print("\n== ④ RLock：可重入锁 ==")
rlock = threading.RLock()
plain = threading.Lock()


def outer():
    with rlock:
        inner()                                # 同一线程再次获取同一把 RLock


def inner():
    with rlock:
        print("  RLock: 同一线程可以重复获取 ✅")


outer()
print(f"  普通 Lock 重复获取会怎样: 立即阻塞自己 → 自我死锁")
print(f"  （验证: plain.acquire() 后再 plain.acquire(timeout=0.1) = "
      f"{(plain.acquire(), plain.acquire(timeout=0.1))[1]}）")
plain.release()

print("\n== ⑤ Python 的锁家族 ==")
print("  Lock      : 最基本的互斥锁")
print("  RLock     : 可重入（同一线程可多次获取）")
print("  Semaphore : 允许 N 个线程同时进入（限流）")
print("  Condition : 等待某个条件成立（生产者-消费者）")
print("  Event     : 一次性的广播信号（本例用它记录死锁）")
print("  （GIL 保证不了你的业务原子性——第 40 章实测过，所以这些锁都是刚需）")
