# 第 16 章 · 数组 — Python 示例
# 运行：python3 main.py

import sys, time
from array import array

scores = [92, 75, 88]

# 1. Python 的 list 是「指针数组」，不是连续存值
print("类型:", type(scores).__name__, "| 长度:", len(scores))
print("list 可以异构:", [1, "两", True, None])

# 2. 负索引与切片 —— Python 的招牌
print("负索引 a[-1]:", scores[-1], "← 从末尾数")
print("切片 a[0:2]:", scores[0:2], "| 反转 a[::-1]:", scores[::-1])

# 3. 越界抛 IndexError（比 JS 的静默 undefined 安全）
try:
    scores[10]
except IndexError as e:
    print("a[10] →", type(e).__name__ + ":", e)

# 4. 真正连续的数组：array 模块
a = array("i", [92, 75, 88])
print("array 模块:", a.tolist(), "| 每元素", a.itemsize, "字节（连续存储）")

# 5. 内存对比：list 存指针 + 对象，array 存值
n = 10000
lst = list(range(n))
arr = array("i", range(n))
print(f"\n{n} 个整数的内存占用:")
print(f"  list : {sys.getsizeof(lst):>8} 字节（仅指针数组，还不含 {n} 个 int 对象）")
print(f"  array: {sys.getsizeof(arr):>8} 字节（连续存值）")

# 6. 缓存局部性：行优先 vs 列优先
N = 600
m = [[1] * N for _ in range(N)]
t0 = time.perf_counter()
s1 = sum(m[i][j] for i in range(N) for j in range(N))       # 行优先
t1 = time.perf_counter()
s2 = sum(m[i][j] for j in range(N) for i in range(N))       # 列优先
t2 = time.perf_counter()
print(f"\n缓存局部性: 行优先 {(t1-t0)*1000:.1f}ms vs 列优先 {(t2-t1)*1000:.1f}ms"
      f" → 慢 {(t2-t1)/(t1-t0):.1f} 倍（校验和一致: {s1==s2}）")
