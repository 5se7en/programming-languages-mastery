// 第 22 章 · 图 —— JavaScript 示例
// 运行：node main.js

console.log("=== 1. 邻接表：用 Map + 数组构造图 ===");

class Graph {
  constructor() {
    this.adj = new Map();
  }
  addEdge(from, to, weight = 1) {
    if (!this.adj.has(from)) this.adj.set(from, []);
    if (!this.adj.has(to)) this.adj.set(to, []); // 保证终点也在图里
    this.adj.get(from).push({ to, weight });
  }
  neighbors(node) {
    return this.adj.get(node) ?? [];
  }
}

const g = new Graph();
// A 到 D 有两条路：直达，或绕经 B、C
g.addEdge("A", "B");
g.addEdge("A", "D");
g.addEdge("B", "C");
g.addEdge("C", "D");

console.log("图: A→B, A→D, B→C, C→D");
for (const [node, edges] of g.adj) {
  console.log(`  ${node} → [${edges.map((e) => e.to).join(", ")}]`);
}

console.log("\n=== 2. ⚠️ DFS 找到的不是最短路径！===");

function dfsPath(graph, start, goal, path = [start], seen = new Set([start])) {
  if (start === goal) return path;
  for (const { to } of graph.neighbors(start)) {
    if (!seen.has(to)) {
      seen.add(to);
      const r = dfsPath(graph, to, goal, [...path, to], seen);
      if (r) return r;
    }
  }
  return null;
}

function bfsPath(graph, start, goal) {
  // 用下标指针代替 shift()，避免 O(n) 的数组前移（第 19 章）
  const queue = [[start]];
  let head = 0;
  const seen = new Set([start]);
  while (head < queue.length) {
    const path = queue[head++];
    const node = path[path.length - 1];
    if (node === goal) return path;
    for (const { to } of graph.neighbors(node)) {
      if (!seen.has(to)) {
        seen.add(to);
        queue.push([...path, to]);
      }
    }
  }
  return null;
}

console.log("DFS 找到:", dfsPath(g, "A", "D").join(" → "), " (先钻进了 B 这条深路)");
console.log("BFS 找到:", bfsPath(g, "A", "D").join(" → "), "          ← 才是最短路径");
console.log("→ 无权图求最短路径必须用 BFS，DFS 只保证「找得到」");

console.log("\n=== 3. 拓扑排序：构建顺序 + 循环依赖检测 ===");

function topoSort(nodes, edges) {
  const indeg = new Map(nodes.map((n) => [n, 0]));
  const adj = new Map(nodes.map((n) => [n, []]));
  for (const [a, b] of edges) {
    adj.get(a).push(b);
    indeg.set(b, indeg.get(b) + 1);
  }

  const queue = nodes.filter((n) => indeg.get(n) === 0);
  let head = 0;
  const order = [];
  while (head < queue.length) {
    const n = queue[head++];
    order.push(n);
    for (const m of adj.get(n)) {
      indeg.set(m, indeg.get(m) - 1);
      if (indeg.get(m) === 0) queue.push(m);
    }
  }
  if (order.length !== nodes.length) {
    // 没排完 → 剩下的节点入度降不到 0，它们在环里
    return { order: null, cycle: nodes.filter((n) => indeg.get(n) > 0) };
  }
  return { order, cycle: null };
}

console.log("场景 A：正常的模块依赖");
const nodesA = ["utils", "config", "db", "api", "ui", "app"];
const edgesA = [
  ["utils", "db"], ["config", "db"], ["db", "api"],
  ["api", "ui"], ["ui", "app"], ["utils", "api"],
];
const a = topoSort(nodesA, edgesA);
console.log("  依赖:", edgesA.map(([x, y]) => `${x}→${y}`).join(", "));
console.log("  构建顺序:", a.order.join(" → "));
console.log("  ✓ 无环");

console.log("\n场景 B：⚠️ 循环依赖");
const nodesB = ["auth", "user", "order", "payment"];
const edgesB = [
  ["auth", "user"], ["user", "order"],
  ["order", "payment"], ["payment", "user"],
];
const b = topoSort(nodesB, edgesB);
console.log("  依赖:", edgesB.map(([x, y]) => `${x}→${y}`).join(", "));
console.log("  拓扑排序结果:", b.order);
console.log("  ⚠️ 检测到循环依赖，涉及模块:", b.cycle);
console.log("  → 这就是 npm/Maven 报 circular dependency 的原理");

console.log("\n=== 4. 环检测：三色标记法 ===");

function hasCycle(adjMap) {
  const WHITE = 0, GRAY = 1, BLACK = 2;
  const color = new Map([...adjMap.keys()].map((n) => [n, WHITE]));
  function dfs(n) {
    color.set(n, GRAY); // 灰色 = 正在访问的路径上
    for (const m of adjMap.get(n) ?? []) {
      if (color.get(m) === GRAY) return true; // 回边 → 有环
      if (color.get(m) === WHITE && dfs(m)) return true;
    }
    color.set(n, BLACK);
    return false;
  }
  return [...adjMap.keys()].some((n) => color.get(n) === WHITE && dfs(n));
}

const cyclic = new Map([["A", ["B"]], ["B", ["C"]], ["C", ["A"]]]);
const diamond = new Map([["A", ["B", "C"]], ["B", ["D"]], ["C", ["D"]], ["D", []]]);
console.log("A→B→C→A            有环?", hasCycle(cyclic));
console.log("A→B, A→C, B→D, C→D 有环?", hasCycle(diamond), " ← D 被访问两次，但这不是环（是 DAG）");
console.log("→ 关键：区分「重复访问」和「回到正在访问的路径上」");

console.log("\n=== 5. Dijkstra：带权最短路径 ===");

function dijkstra(weighted, start) {
  const dist = new Map([...weighted.keys()].map((n) => [n, Infinity]));
  dist.set(start, 0);
  const prev = new Map();
  const visited = new Set();

  // 没有内置堆，这里用「线性找最小」代替（小图足够；大图应手写堆）
  while (visited.size < weighted.size) {
    let cur = null;
    for (const [n, d] of dist)
      if (!visited.has(n) && (cur === null || d < dist.get(cur))) cur = n;
    if (cur === null || dist.get(cur) === Infinity) break;
    visited.add(cur);
    for (const { to, weight } of weighted.get(cur) ?? []) {
      if (dist.get(cur) + weight < dist.get(to)) {
        dist.set(to, dist.get(cur) + weight);
        prev.set(to, cur);
      }
    }
  }
  return { dist, prev };
}

const roads = new Map([
  ["北京", [{ to: "天津", weight: 120 }, { to: "济南", weight: 400 }]],
  ["天津", [{ to: "济南", weight: 320 }, { to: "青岛", weight: 550 }]],
  ["济南", [{ to: "青岛", weight: 360 }]],
  ["青岛", []],
]);

const { dist, prev } = dijkstra(roads, "北京");
console.log("路网: 北京-天津 120, 北京-济南 400, 天津-济南 320,");
console.log("      天津-青岛 550, 济南-青岛 360");
for (const [city, d] of dist) console.log(`  北京 → ${city}: ${d} km`);

const path = ["青岛"];
let cur = "青岛";
while (prev.has(cur)) {
  cur = prev.get(cur);
  path.push(cur);
}
console.log("最短路径:", path.reverse().join(" → "), `= ${dist.get("青岛")} km`);
console.log("穷举验证: 天津路线 120+550=670 ✓  济南路线 400+360=760");

console.log("\n=== 6. 邻接矩阵 vs 邻接表：为什么默认用表 ===");
for (const [V, deg] of [[100, 4], [1000, 4], [10000, 4]]) {
  const cells = V * V, entries = V * deg;
  console.log(
    `  ${String(V).padStart(5)} 顶点(平均度${deg}): 矩阵 ${String(cells).padStart(11)} 格` +
    `  表 ${String(entries).padStart(6)} 项  → 矩阵是表的 ${(cells / entries).toFixed(0)} 倍`
  );
}
console.log("→ 真实的图几乎都是稀疏的，矩阵里 99% 以上存的都是「没有边」");
