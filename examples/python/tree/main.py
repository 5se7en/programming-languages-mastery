"""第 21 章 · 树 —— Python 示例
运行：python3 main.py
"""

import bisect
import heapq
import math
import random

print("=== 1. 二叉搜索树：左小右大 ===")


class Node:
    __slots__ = ("v", "left", "right")

    def __init__(self, v):
        self.v, self.left, self.right = v, None, None


def insert(root, v):
    """迭代插入，避免深树时递归爆栈"""
    if root is None:
        return Node(v)
    cur = root
    while True:
        if v < cur.v:
            if cur.left is None:
                cur.left = Node(v)
                return root
            cur = cur.left
        else:
            if cur.right is None:
                cur.right = Node(v)
                return root
            cur = cur.right


def inorder(node, out=None):
    """中序遍历：左 → 根 → 右，结果必然有序"""
    if out is None:
        out = []
    if node:
        inorder(node.left, out)
        out.append(node.v)
        inorder(node.right, out)
    return out


def height(root):
    """树高决定查找的最坏代价（迭代版，避免爆栈）"""
    if root is None:
        return 0
    h, stack = 0, [(root, 1)]
    while stack:
        node, d = stack.pop()
        h = max(h, d)
        if node.left:
            stack.append((node.left, d + 1))
        if node.right:
            stack.append((node.right, d + 1))
    return h


values = [50, 30, 70, 20, 40, 60, 80]
root = None
for v in values:
    root = insert(root, v)

print("插入顺序:", values)
print("中序遍历:", inorder(root), "← 自动有序！这是 BST 的定义性质")
print("树高:", height(root))

print("\n=== 2. ⚠️ BST 的退化：有序插入会变成链表 ===")
N = 2000

random_tree = None
for v in random.sample(range(N), N):
    random_tree = insert(random_tree, v)

sorted_tree = None
for v in range(N):
    sorted_tree = insert(sorted_tree, v)

print(f"随机插入 {N} 个数 → 树高 {height(random_tree)}")
print(f"有序插入 {N} 个数 → 树高 {height(sorted_tree)}   ← 完全退化成链表！")
print(f"理想树高 log2({N}) ≈ {math.log2(N):.0f}")
print("→ 这就是「平衡树」（AVL / 红黑树）存在的全部理由")

print("\n=== 3. 四种遍历 ===")


def traverse(node, order, out=None):
    if out is None:
        out = []
    if node is None:
        return out
    if order == "pre":
        out.append(node.v)
    traverse(node.left, order, out)
    if order == "in":
        out.append(node.v)
    traverse(node.right, order, out)
    if order == "post":
        out.append(node.v)
    return out


def level_order(node):
    """层序遍历用队列（第 19 章的 BFS）"""
    from collections import deque

    out, queue = [], deque([node] if node else [])
    while queue:
        cur = queue.popleft()
        out.append(cur.v)
        if cur.left:
            queue.append(cur.left)
        if cur.right:
            queue.append(cur.right)
    return out


print("前序 (根左右):", traverse(root, "pre"))
print("中序 (左根右):", traverse(root, "in"), "← 有序")
print("后序 (左右根):", traverse(root, "post"))
print("层序 (逐层)  :", level_order(root))

print("\n=== 4. Python 没有内置平衡树，但有 bisect ===")
scores = [60, 75, 88, 92]
print("有序列表:", scores)

bisect.insort(scores, 80)  # 插入并保持有序
print("insort(80) 后:", scores)

pos = bisect.bisect_left(scores, 80)
print(f"bisect_left(80) → 下标 {pos}   查找是 O(log n)")
print("⚠️ 但插入仍是 O(n)（要搬移元素），适合读多写少")

# 范围查询：用两次二分定位区间
lo = bisect.bisect_left(scores, 75)
hi = bisect.bisect_right(scores, 88)
print(f"范围查询 [75, 88] → {scores[lo:hi]}")

print("\n=== 5. heapq：堆（用数组存的完全二叉树）===")
h = [5, 3, 8, 1, 9]
heapq.heapify(h)  # 建堆 O(n)
print("堆的内部数组:", h, "← 不是有序数组！只保证堆顶最小")
print("堆顶（最小值）:", h[0], "  O(1)")

popped = [heapq.heappop(h) for _ in range(len(h))]
print("逐个 heappop:", popped, "← 这样才有序")

# 完全二叉树的父子关系可以直接用下标算出来，不需要指针
print("下标关系: 节点 i 的左子=2i+1, 右子=2i+2, 父=(i-1)//2")

print("\n=== 6. 用堆求 Top-K（比排序整个列表快）===")
data = [random.randint(1, 1000) for _ in range(100)]
print("最大的 3 个:", heapq.nlargest(3, data))
print("最小的 3 个:", heapq.nsmallest(3, data))
print("→ Top-K 用堆是 O(n log k)，排序整个列表是 O(n log n)")

print("\n=== 7. Python 里随处可见的树 ===")
import ast

code = "x = 1 + 2 * 3"
tree = ast.parse(code)
print(f"源码 {code!r} 解析成语法树（第 03 章）:")
print(" ", ast.dump(tree.body[0].value)[:70] + "...")
print("os.walk() 遍历目录树、JSON 的嵌套结构 —— 都是树")
