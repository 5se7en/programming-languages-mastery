"""第 22 章 · 图 —— Python 示例
运行：python3 main.py
"""

import heapq
import sys
from collections import defaultdict, deque

print("=== 1. 邻接表：dict + list ===")

graph = {"A": ["B", "D"], "B": ["C"], "C": ["D"], "D": []}
print("图: A→B, A→D, B→C, C→D")
for node, nbrs in graph.items():
    print(f"  {node} → {nbrs}")

print("\n=== 2. ⚠️ DFS 找到的不是最短路径！===")


def dfs_path(g, start, goal, path=None, seen=None):
    if path is None:
        path, seen = [start], {start}
    if start == goal:
        return path
    for nxt in g[start]:
        if nxt not in seen:
            seen.add(nxt)
            r = dfs_path(g, nxt, goal, path + [nxt], seen)
            if r:
                return r
    return None


def bfs_path(g, start, goal):
    queue, seen = deque([[start]]), {start}
    while queue:
        path = queue.popleft()          # O(1)，别用 list.pop(0)
        if path[-1] == goal:
            return path
        for nxt in g[path[-1]]:
            if nxt not in seen:
                seen.add(nxt)
                queue.append(path + [nxt])
    return None


print("DFS 找到:", " → ".join(dfs_path(graph, "A", "D")), " (先钻进了 B 这条深路)")
print("BFS 找到:", " → ".join(bfs_path(graph, "A", "D")), "          ← 才是最短路径")
print("→ 无权图求最短路径必须用 BFS，DFS 只保证「找得到」")

print("\n=== 3. Python 内置拓扑排序：graphlib（3.9+）===")
print("   —— 六门语言中唯一开箱即用的")

from graphlib import CycleError, TopologicalSorter

print("\n场景 A：正常的模块依赖")
# 键是节点，值是「它依赖谁」
deps = {"db": {"utils", "config"}, "api": {"db"}, "ui": {"api"}, "app": {"ui"}}
print("  依赖:", deps)
print("  构建顺序:", " → ".join(TopologicalSorter(deps).static_order()))
print("  ✓ 无环")

print("\n场景 B：⚠️ 循环依赖")
bad = {"user": {"payment"}, "order": {"user"}, "payment": {"order"}}
print("  依赖:", bad)
try:
    TopologicalSorter(bad).prepare()
    print("  没有环")
except CycleError as e:
    print("  ⚠️ CycleError:", e.args[0])
    print("  环的位置:", " → ".join(e.args[1]), " ← 直接告诉你环在哪")
print("  → 这就是 npm/Maven 报 circular dependency 的原理")

print("\n=== 4. 手写 Kahn 算法（理解拓扑排序的原理）===")


def kahn(nodes, edges):
    """反复取出入度为 0 的节点"""
    indeg = {n: 0 for n in nodes}
    g = defaultdict(list)
    for a, b in edges:              # a 必须在 b 之前
        g[a].append(b)
        indeg[b] += 1

    queue = deque(n for n in nodes if indeg[n] == 0)
    order = []
    while queue:
        n = queue.popleft()
        order.append(n)
        for m in g[n]:
            indeg[m] -= 1
            if indeg[m] == 0:
                queue.append(m)

    if len(order) != len(nodes):
        # 没排完 → 剩下节点的入度降不到 0，它们在环里
        return None, [n for n in nodes if indeg[n] > 0]
    return order, None


nodes = ["auth", "user", "order", "payment"]
edges = [("auth", "user"), ("user", "order"), ("order", "payment"), ("payment", "user")]
order, cycle = kahn(nodes, edges)
print("  依赖:", ", ".join(f"{a}→{b}" for a, b in edges))
print("  排序结果:", order)
print("  ⚠️ 环中的节点:", cycle)

print("\n=== 5. 环检测：三色标记法 ===")


def has_cycle(g):
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {n: WHITE for n in g}

    def dfs(n):
        color[n] = GRAY             # 灰色 = 正在访问的路径上
        for m in g.get(n, []):
            if color[m] == GRAY:    # 回边 → 有环
                return True
            if color[m] == WHITE and dfs(m):
                return True
        color[n] = BLACK
        return False

    return any(dfs(n) for n in g if color[n] == WHITE)


print("A→B→C→A            有环?", has_cycle({"A": ["B"], "B": ["C"], "C": ["A"]}))
print("A→B, A→C, B→D, C→D 有环?",
      has_cycle({"A": ["B", "C"], "B": ["D"], "C": ["D"], "D": []}),
      " ← D 被访问两次，但这不是环（是 DAG）")
print("→ 关键：区分「重复访问」和「回到正在访问的路径上」")

print("\n=== 6. Dijkstra：用 heapq 求带权最短路径 ===")


def dijkstra(g, start):
    dist = {n: float("inf") for n in g}
    dist[start] = 0
    prev = {}
    pq = [(0, start)]
    while pq:
        d, n = heapq.heappop(pq)    # 贪心取当前最近的（堆，第 19/21 章）
        if d > dist[n]:
            continue                 # 过期条目，跳过
        for m, w in g[n]:
            if d + w < dist[m]:
                dist[m] = d + w
                prev[m] = n
                heapq.heappush(pq, (d + w, m))
    return dist, prev


roads = {
    "北京": [("天津", 120), ("济南", 400)],
    "天津": [("济南", 320), ("青岛", 550)],
    "济南": [("青岛", 360)],
    "青岛": [],
}
dist, prev = dijkstra(roads, "北京")
print("路网: 北京-天津 120, 北京-济南 400, 天津-济南 320,")
print("      天津-青岛 550, 济南-青岛 360")
for city, d in dist.items():
    print(f"  北京 → {city}: {d} km")

path, cur = ["青岛"], "青岛"
while cur in prev:
    cur = prev[cur]
    path.append(cur)
print("最短路径:", " → ".join(reversed(path)), "=", dist["青岛"], "km")
print("穷举验证: 天津路线", 120 + 550, "✓   济南路线", 400 + 360,
      "  天津→济南路线", 120 + 320 + 360)

print("\n=== 7. 邻接矩阵 vs 邻接表：实测内存 ===")
V, avg_deg = 2000, 4

matrix = [[0] * V for _ in range(V)]
m_size = sys.getsizeof(matrix) + sum(sys.getsizeof(row) for row in matrix)

adj = {i: [(i * 7 + k) % V for k in range(avg_deg)] for i in range(V)}
l_size = sys.getsizeof(adj) + sum(sys.getsizeof(k) + sys.getsizeof(v) for k, v in adj.items())

print(f"{V} 顶点、{V * avg_deg} 条边（稀疏图）:")
print(f"  邻接矩阵 {m_size / 1024 / 1024:>7.2f} MB")
print(f"  邻接表   {l_size / 1024 / 1024:>7.2f} MB")
print(f"  → 矩阵占用是表的 {m_size / l_size:.0f} 倍")

print("\n规模放大后的结构性差距（纯算术，与环境无关）:")
for v, deg in [(100, 4), (1000, 4), (10000, 4), (10000, 100)]:
    cells, entries = v * v, v * deg
    print(f"  {v:>6} 顶点(平均度{deg:>4}): 矩阵 {cells:>11,} 格  "
          f"表 {entries:>9,} 项  → {cells / entries:>7.0f} 倍")
print("→ 真实的图几乎都是稀疏的，矩阵里 99% 以上存的都是「没有边」")
