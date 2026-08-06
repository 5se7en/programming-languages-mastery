# 第 17 章 · 列表 — Python 示例
# 运行：python3 main.py

import sys, time
from collections import deque

# 1. 观察 list 的扩容规律（用 getsizeof 反推容量）
lst = []
base = sys.getsizeof(lst)
last = 0
print("list 追加时的容量变化（增长因子逐渐递减）:")
for i in range(100):
    lst.append(i)
    cap = (sys.getsizeof(lst) - base) // 8        # 每个指针 8 字节
    if cap != last:
        line = f"  len={len(lst):>3} → 容量 {cap:>3}"
        if last:
            line += f"   增长倍数 {cap/last:.3f}"
        print(line)
        last = cap

# 2. 容量 ≥ 长度
print(f"\n当前 len={len(lst)}，底层容量={last} → 容量总是 ≥ 长度（差额是预留空位）")

# 3. 头部插入 O(n) vs deque 头部插入 O(1)
N = 20000
t = time.perf_counter()
a = []
for i in range(N): a.insert(0, i)          # O(n)：每次搬移全部元素
list_ms = (time.perf_counter() - t) * 1000

t = time.perf_counter()
d = deque()
for i in range(N): d.appendleft(i)         # O(1)
deque_ms = (time.perf_counter() - t) * 1000

print(f"\n{N} 次头部插入:")
print(f"  list.insert(0,x)   : {list_ms:8.1f} ms  (O(n) 每次)")
print(f"  deque.appendleft(x): {deque_ms:8.1f} ms  (O(1))")
print(f"  → deque 快约 {list_ms/deque_ms:.0f} 倍")

# 4. 列表推导式通常比循环 append 快
N2 = 200000
t = time.perf_counter()
r1 = []
for x in range(N2): r1.append(x * x)
loop_ms = (time.perf_counter() - t) * 1000
t = time.perf_counter()
r2 = [x * x for x in range(N2)]
comp_ms = (time.perf_counter() - t) * 1000
print(f"\n构造 {N2} 个元素: 循环append {loop_ms:.1f}ms vs 推导式 {comp_ms:.1f}ms"
      f" → 推导式快 {loop_ms/comp_ms:.1f} 倍")
