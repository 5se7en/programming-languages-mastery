// 第 21 章 · 树 —— JavaScript 示例
// 运行：node main.js

console.log("=== 1. 二叉搜索树：左小右大 ===");

class BST {
  constructor() {
    this.root = null;
  }

  insert(v) {
    const node = { v, left: null, right: null };
    if (!this.root) {
      this.root = node;
      return;
    }
    let cur = this.root;
    while (true) {
      if (v < cur.v) {
        if (!cur.left) {
          cur.left = node;
          return;
        }
        cur = cur.left;
      } else {
        if (!cur.right) {
          cur.right = node;
          return;
        }
        cur = cur.right;
      }
    }
  }

  // 中序遍历：左 → 根 → 右，结果必然有序
  inorder(node = this.root, out = []) {
    if (node) {
      this.inorder(node.left, out);
      out.push(node.v);
      this.inorder(node.right, out);
    }
    return out;
  }

  // 树高决定查找的最坏代价（用迭代避免深树爆栈）
  height() {
    if (!this.root) return 0;
    let h = 0;
    const stack = [[this.root, 1]];
    while (stack.length) {
      const [n, d] = stack.pop();
      if (d > h) h = d;
      if (n.left) stack.push([n.left, d + 1]);
      if (n.right) stack.push([n.right, d + 1]);
    }
    return h;
  }
}

const bst = new BST();
[50, 30, 70, 20, 40, 60, 80].forEach((v) => bst.insert(v));
console.log("插入顺序:", [50, 30, 70, 20, 40, 60, 80]);
console.log("中序遍历:", bst.inorder(), "← 自动有序！这是 BST 的定义性质");
console.log("树高:", bst.height());

console.log("\n=== 2. ⚠️ BST 的退化：有序插入会变成链表 ===");
const N = 2000;

// 随机排列（Fisher-Yates 洗牌）
const shuffled = Array.from({ length: N }, (_, i) => i);
for (let i = N - 1; i > 0; i--) {
  const j = Math.floor(Math.random() * (i + 1));
  [shuffled[i], shuffled[j]] = [shuffled[j], shuffled[i]];
}

const randomTree = new BST();
shuffled.forEach((v) => randomTree.insert(v));

const sortedTree = new BST();
for (let i = 0; i < N; i++) sortedTree.insert(i);

console.log(`随机插入 ${N} 个数 → 树高 ${randomTree.height()}`);
console.log(`有序插入 ${N} 个数 → 树高 ${sortedTree.height()}   ← 完全退化成链表！`);
console.log(`理想树高 log2(${N}) ≈ ${Math.log2(N).toFixed(0)}`);
console.log("→ 这就是「平衡树」（AVL / 红黑树）存在的全部理由");

console.log("\n=== 3. 四种遍历 ===");
function traverse(node, order, out = []) {
  if (!node) return out;
  if (order === "pre") out.push(node.v);
  traverse(node.left, order, out);
  if (order === "in") out.push(node.v);
  traverse(node.right, order, out);
  if (order === "post") out.push(node.v);
  return out;
}

// 层序遍历用队列（第 19 章的 BFS）
function levelOrder(root) {
  const out = [];
  const queue = root ? [root] : [];
  while (queue.length) {
    const node = queue.shift();
    out.push(node.v);
    if (node.left) queue.push(node.left);
    if (node.right) queue.push(node.right);
  }
  return out;
}

console.log("前序 (根左右):", traverse(bst.root, "pre"));
console.log("中序 (左根右):", traverse(bst.root, "in"), "← 有序");
console.log("后序 (左右根):", traverse(bst.root, "post"));
console.log("层序 (逐层)  :", levelOrder(bst.root));

console.log("\n=== 4. JavaScript 没有内置有序映射 ===");
const map = new Map([
  ["zebra", 1],
  ["apple", 2],
  ["mango", 3],
]);
console.log("Map 的顺序 :", [...map.keys()], "← 是插入顺序，不是排序！");
const sorted = [...map.entries()].sort((a, b) => a[0].localeCompare(b[0]));
console.log("手动排序后:", sorted.map(([k]) => k));

console.log("\n=== 5. 替代方案：有序数组 + 二分查找 ===");
function binarySearch(arr, target) {
  let lo = 0,
    hi = arr.length - 1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (arr[mid] === target) return mid;
    if (arr[mid] < target) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

const scores = [60, 75, 80, 88, 92];
console.log("有序数组:", scores);
console.log("二分查找 88 →  下标", binarySearch(scores, 88), "  O(log n)");
console.log("范围查询 [75, 88]:", scores.filter((s) => s >= 75 && s <= 88));

console.log("\n=== 6. 堆：优先队列的底层（用数组存树）===");
// 完全二叉树可以直接用下标算父子关系，不需要指针
class MinHeap {
  constructor() {
    this.a = [];
  }
  push(v) {
    this.a.push(v);
    let i = this.a.length - 1;
    while (i > 0) {
      const p = (i - 1) >> 1; // 父节点下标
      if (this.a[p] <= this.a[i]) break;
      [this.a[p], this.a[i]] = [this.a[i], this.a[p]];
      i = p;
    }
  }
  pop() {
    const top = this.a[0];
    const last = this.a.pop();
    if (this.a.length) {
      this.a[0] = last;
      let i = 0;
      while (true) {
        const l = 2 * i + 1, // 左子
          r = 2 * i + 2; // 右子
        let min = i;
        if (l < this.a.length && this.a[l] < this.a[min]) min = l;
        if (r < this.a.length && this.a[r] < this.a[min]) min = r;
        if (min === i) break;
        [this.a[min], this.a[i]] = [this.a[i], this.a[min]];
        i = min;
      }
    }
    return top;
  }
}

const heap = new MinHeap();
[5, 3, 8, 1, 9].forEach((v) => heap.push(v));
console.log("堆的内部数组:", heap.a, "← 不是有序数组！只保证堆顶最小");
const popped = [];
while (heap.a.length) popped.push(heap.pop());
console.log("逐个 pop 出来:", popped, "← 这样才有序");

console.log("\n=== 7. DOM 才是 JavaScript 里最常见的树 ===");
console.log("document → html → body → div → p");
console.log("element.parentNode / .children —— 每天都在遍历树");
