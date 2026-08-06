# 第 20 章 · 哈希 — Python 示例
# 运行：python3 main.py

import time, random
from collections import Counter

# 1. dict 是 Python 的核心结构
scores = {"Alice": 92, "Bob": 75}
print("d['Alice']:", scores["Alice"], "| d.get('Carol', 0):", scores.get("Carol", 0), "← 默认值，不抛异常")
try:
    scores["Carol"]
except KeyError as e:
    print("d['Carol'] →", type(e).__name__, "← 直接下标取不存在的键会抛异常")

# 2. Python 3.7+ 保证 dict 保持插入顺序
d = {}
for k in ["zebra", "apple", "mango", "banana"]: d[k] = len(k)
print("\n插入顺序:", ["zebra", "apple", "mango", "banana"])
print("遍历顺序:", list(d.keys()), "← 3.7+ 保证一致（是插入序，不是排序）")

# 3. 哈希 vs 线性查找
N = 200000
data = [f"student{i}" for i in range(N)]
lst, st = data, set(data)
targets = random.sample(data, 300)
t = time.perf_counter()
for x in targets: _ = x in lst          # O(n)
list_ms = (time.perf_counter() - t) * 1000
t = time.perf_counter()
for x in targets: _ = x in st           # O(1)
set_ms = (time.perf_counter() - t) * 1000
print(f"\n在 {N} 个元素中查找 300 次:")
print(f"  list (O(n)): {list_ms:9.2f} ms")
print(f"  set  (O(1)): {set_ms:9.3f} ms")
print(f"  → 哈希快约 {list_ms/set_ms:.0f} 倍")

# 4. 哈希冲突：全部撞进同一个桶时退化成 O(n)
class GoodKey:
    __slots__ = ("v",)
    def __init__(self, v): self.v = v
    def __hash__(self): return hash(self.v)      # 正常分散
    def __eq__(self, o): return isinstance(o, GoodKey) and o.v == self.v

class BadKey:
    __slots__ = ("v",)
    def __init__(self, v): self.v = v
    def __hash__(self): return 1                  # ⚠️ 所有键哈希值相同
    def __eq__(self, o): return isinstance(o, BadKey) and o.v == self.v

M = 3000
print(f"\n{M} 次插入+查找:")
for cls, label in [(GoodKey, "哈希分散(正常)"), (BadKey, "哈希全冲突(退化)")]:
    dd = {}
    t = time.perf_counter()
    for i in range(M): dd[cls(i)] = i
    for i in range(M): _ = dd[cls(i)]
    print(f"  {label:<18}: {(time.perf_counter()-t)*1000:8.1f} ms")
print("  → 全冲突时 O(1) 退化成 O(n)；这也是「哈希碰撞攻击」的原理")

# 5. 键必须可哈希（不可变）
try:
    {[1, 2]: "列表不行"}
except TypeError as e:
    print(f"\n用列表作键 → TypeError: {e}")
print("元组可以作键:", {(1, 2): "元组不可变，可哈希"}[(1, 2)])

# 6. ⚠️ 只定义 __eq__ 会让类变得不可哈希（比 Java 静默出错更安全）
class OnlyEq:
    def __eq__(self, o): return True
try:
    {OnlyEq(): 1}
except TypeError as e:
    print(f"\n只定义 __eq__ → {type(e).__name__}: {e} ← Python 直接报错，不会静默出错")

# 7. 词频统计
words = "the quick brown fox jumps over the lazy dog the fox".split()
print("\n词频 Top3:", Counter(words).most_common(3))
