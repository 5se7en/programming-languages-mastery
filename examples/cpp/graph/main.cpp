// 第 22 章 · 图 —— C++ 示例
// 运行：g++ -std=c++17 -O2 main.cpp -o graph && ./graph

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Graph = std::map<std::string, std::vector<std::string>>;

// ---------- BFS：用队列，保证最短路径 ----------
std::vector<std::string> bfsPath(const Graph& g, const std::string& start,
                                 const std::string& goal) {
    std::queue<std::vector<std::string>> q;
    q.push({start});
    std::set<std::string> seen{start};
    while (!q.empty()) {
        auto path = q.front();
        q.pop();
        if (path.back() == goal) return path;
        auto it = g.find(path.back());
        if (it == g.end()) continue;
        for (const auto& next : it->second) {
            if (seen.insert(next).second) {      // insert 的 .second 表示是否真的插入了
                auto np = path;
                np.push_back(next);
                q.push(np);
            }
        }
    }
    return {};
}

// ---------- DFS：用递归，只保证「找得到」 ----------
bool dfsPath(const Graph& g, const std::string& cur, const std::string& goal,
             std::vector<std::string>& path, std::set<std::string>& seen) {
    if (cur == goal) return true;
    auto it = g.find(cur);
    if (it == g.end()) return false;
    for (const auto& next : it->second) {
        if (seen.insert(next).second) {
            path.push_back(next);
            if (dfsPath(g, next, goal, path, seen)) return true;
            path.pop_back();
        }
    }
    return false;
}

// ---------- Kahn 拓扑排序 ----------
struct TopoResult {
    std::vector<std::string> order;
    std::vector<std::string> cycle;
    bool ok = false;
};

TopoResult topoSort(const std::vector<std::string>& nodes,
                    const std::vector<std::pair<std::string, std::string>>& edges) {
    std::map<std::string, int> indeg;
    std::map<std::string, std::vector<std::string>> adj;
    for (const auto& n : nodes) { indeg[n] = 0; adj[n] = {}; }
    for (const auto& [a, b] : edges) { adj[a].push_back(b); indeg[b]++; }

    std::queue<std::string> q;
    for (const auto& n : nodes)
        if (indeg[n] == 0) q.push(n);

    TopoResult r;
    while (!q.empty()) {
        auto n = q.front();
        q.pop();
        r.order.push_back(n);
        for (const auto& m : adj[n])
            if (--indeg[m] == 0) q.push(m);
    }
    if (r.order.size() != nodes.size()) {
        // 没排完 → 剩下节点的入度降不到 0，它们在环里
        for (const auto& n : nodes)
            if (indeg[n] > 0) r.cycle.push_back(n);
        r.order.clear();
        return r;
    }
    r.ok = true;
    return r;
}

// ---------- 环检测：三色标记 ----------
enum Color { WHITE, GRAY, BLACK };

bool dfsCycle(const Graph& g, const std::string& n, std::map<std::string, Color>& color) {
    color[n] = GRAY;                             // 灰色 = 正在访问的路径上
    auto it = g.find(n);
    if (it != g.end())
        for (const auto& m : it->second) {
            if (color[m] == GRAY) return true;   // 回边 → 有环
            if (color[m] == WHITE && dfsCycle(g, m, color)) return true;
        }
    color[n] = BLACK;
    return false;
}

bool hasCycle(const Graph& g) {
    std::map<std::string, Color> color;
    for (const auto& [n, _] : g) color[n] = WHITE;
    for (const auto& [n, _] : g)
        if (color[n] == WHITE && dfsCycle(g, n, color)) return true;
    return false;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += sep;
        s += v[i];
    }
    return s;
}

int main() {
    std::cout << "=== 1. 邻接表：map<string, vector<string>> ===\n";
    Graph g{{"A", {"B", "D"}}, {"B", {"C"}}, {"C", {"D"}}, {"D", {}}};
    std::cout << "图: A→B, A→D, B→C, C→D\n";
    for (const auto& [k, v] : g)
        std::cout << "  " << k << " → [" << join(v, ", ") << "]\n";

    std::cout << "\n=== 2. ⚠️ DFS 找到的不是最短路径！===\n";
    std::vector<std::string> dpath{"A"};
    std::set<std::string> dseen{"A"};
    dfsPath(g, "A", "D", dpath, dseen);
    std::cout << "DFS 找到: " << join(dpath, " → ") << "  (先钻进了 B 这条深路)\n";
    std::cout << "BFS 找到: " << join(bfsPath(g, "A", "D"), " → ")
              << "          ← 才是最短路径\n";
    std::cout << "→ 无权图求最短路径必须用 BFS，DFS 只保证「找得到」\n";

    std::cout << "\n=== 3. 拓扑排序：构建顺序 + 循环依赖检测 ===\n";
    std::cout << "场景 A：正常的模块依赖\n";
    auto a = topoSort({"utils", "config", "db", "api", "ui", "app"},
                      {{"utils","db"},{"config","db"},{"db","api"},
                       {"api","ui"},{"ui","app"},{"utils","api"}});
    std::cout << "  构建顺序: " << join(a.order, " → ") << "\n  ✓ 无环\n";

    std::cout << "\n场景 B：⚠️ 循环依赖\n";
    auto b = topoSort({"auth", "user", "order", "payment"},
                      {{"auth","user"},{"user","order"},
                       {"order","payment"},{"payment","user"}});
    std::cout << "  依赖: auth→user, user→order, order→payment, payment→user\n";
    std::cout << "  排序成功? " << (b.ok ? "是" : "否") << "\n";
    std::cout << "  ⚠️ 检测到循环依赖，涉及模块: [" << join(b.cycle, ", ") << "]\n";

    std::cout << "\n=== 4. 环检测：三色标记法 ===\n";
    Graph cyclic{{"A", {"B"}}, {"B", {"C"}}, {"C", {"A"}}};
    Graph diamond{{"A", {"B", "C"}}, {"B", {"D"}}, {"C", {"D"}}, {"D", {}}};
    std::cout << "A→B→C→A            有环? " << (hasCycle(cyclic) ? "true" : "false") << "\n";
    std::cout << "A→B, A→C, B→D, C→D 有环? " << (hasCycle(diamond) ? "true" : "false")
              << "  ← D 被访问两次，但这不是环（是 DAG）\n";
    std::cout << "→ 关键：区分「重复访问」和「回到正在访问的路径上」\n";

    std::cout << "\n=== 5. Dijkstra：⚠️ priority_queue 默认是大顶堆！===\n";
    std::map<std::string, std::vector<std::pair<std::string, int>>> roads{
        {"北京", {{"天津", 120}, {"济南", 400}}},
        {"天津", {{"济南", 320}, {"青岛", 550}}},
        {"济南", {{"青岛", 360}}},
        {"青岛", {}}};

    std::map<std::string, int> dist;
    for (const auto& [n, _] : roads) dist[n] = INT32_MAX;
    dist["北京"] = 0;
    std::map<std::string, std::string> prev;

    // 必须显式指定 std::greater 才是小顶堆 —— 忘了加是极常见的 bug
    std::priority_queue<std::pair<int, std::string>,
                        std::vector<std::pair<int, std::string>>,
                        std::greater<>> pq;
    pq.push({0, "北京"});
    while (!pq.empty()) {
        auto [d, n] = pq.top();
        pq.pop();
        if (d > dist[n]) continue;               // 过期条目
        for (const auto& [m, w] : roads[n])
            if (d + w < dist[m]) {
                dist[m] = d + w;
                prev[m] = n;
                pq.push({d + w, m});
            }
    }

    std::cout << "路网: 北京-天津 120, 北京-济南 400, 天津-济南 320,\n";
    std::cout << "      天津-青岛 550, 济南-青岛 360\n";
    for (const auto& name : {"北京", "天津", "济南", "青岛"})
        std::cout << "  北京 → " << name << ": " << dist[name] << " km\n";

    std::vector<std::string> path{"青岛"};
    std::string cur = "青岛";
    while (prev.count(cur)) { cur = prev[cur]; path.push_back(cur); }
    std::reverse(path.begin(), path.end());
    std::cout << "最短路径: " << join(path, " → ") << " = " << dist["青岛"] << " km\n";
    std::cout << "穷举验证: 天津路线 " << 120 + 550 << " ✓   济南路线 " << 400 + 360 << "\n";

    std::cout << "\n=== 6. 邻接矩阵 vs 邻接表：为什么默认用表 ===\n";
    int cases[3][2] = {{100, 4}, {1000, 4}, {10000, 4}};
    for (auto& c : cases) {
        long long cells = (long long) c[0] * c[0], entries = (long long) c[0] * c[1];
        std::cout << "  " << std::setw(5) << c[0] << " 顶点(平均度" << c[1] << "): 矩阵 "
                  << std::setw(11) << cells << " 格   表 " << std::setw(6) << entries
                  << " 项  → 矩阵是表的 " << std::fixed << std::setprecision(0)
                  << (double) cells / entries << " 倍\n";
    }
    std::cout << "→ 真实的图几乎都是稀疏的，矩阵里 99% 以上存的都是「没有边」\n";
    return 0;
}
