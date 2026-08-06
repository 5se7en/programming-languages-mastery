# 第 19 章 · 队列 — Python 示例
# 运行：python3 main.py

import time, heapq
from collections import deque

# 1. Python 队列的唯一正解：collections.deque
q = deque()
q.append("A"); q.append("B"); q.append("C")     # 入队 O(1)
print("deque 出队:", q.popleft(), "| 队首:", q[0], "| 剩余:", len(q))

# 2. ⚠️ 别用 list.pop(0)（O(n)）
N = 20000
t = time.perf_counter()
lst = list(range(N))
while lst: lst.pop(0)                            # O(n) 每次
list_ms = (time.perf_counter() - t) * 1000
t = time.perf_counter()
dq = deque(range(N))
while dq: dq.popleft()                           # O(1)
deque_ms = (time.perf_counter() - t) * 1000
print(f"\n{N} 个元素出队: list.pop(0) {list_ms:.1f}ms vs deque.popleft() {deque_ms:.1f}ms"
      f" → 慢 {list_ms/deque_ms:.0f} 倍")

# 3. 栈 vs 队列 = DFS vs BFS（唯一区别是 pop() 还是 pop(0)）
tree = {1: [2, 3], 2: [4, 5], 3: [6, 7], 4: [], 5: [], 6: [], 7: []}
def traverse(root, use_stack):
    box, order = [root], []
    while box:
        node = box.pop() if use_stack else box.pop(0)    # ← 唯一的区别
        order.append(node)
        box.extend(tree[node])
    return order
print("\n树:  1 / (2,3) / (4,5,6,7)")
print("用栈  (LIFO) → DFS:", traverse(1, True))
print("用队列(FIFO) → BFS:", traverse(1, False), "← 逐层扫描")

# 4. deque(maxlen) 自动丢弃最旧的 —— 适合"最近 N 条"
recent = deque(maxlen=3)
for x in [1, 2, 3, 4, 5]: recent.append(x)
print("\ndeque(maxlen=3) 保留最近三条:", list(recent))

# 5. 优先队列：出队顺序由优先级决定（heapq 是最小堆）
pq = []
for pri, name in [(3, "低优先级"), (1, "紧急"), (2, "普通")]:
    heapq.heappush(pq, (pri, name))
print("\n入队顺序: 低优先级 → 紧急 → 普通")
print("优先队列出队:", [heapq.heappop(pq)[1] for _ in range(3)], "← 按优先级，与入队顺序无关")

# 6. BFS 求最短路径（必须标记已访问）
graph = {"A": ["B", "C"], "B": ["D"], "C": ["D"], "D": ["E"], "E": []}
def bfs_path(start, goal):
    queue = deque([[start]])
    visited = {start}
    while queue:
        path = queue.popleft()
        if path[-1] == goal: return path
        for nxt in graph[path[-1]]:
            if nxt not in visited:
                visited.add(nxt)
                queue.append(path + [nxt])
    return None
print("\nBFS 最短路径 A→E:", " → ".join(bfs_path("A", "E")), "← BFS 保证最短")
