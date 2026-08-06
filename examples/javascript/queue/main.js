// 第 19 章 · 队列 — JavaScript 示例
// 运行：node main.js

// 1. ⚠️ JS 没有内置队列；用 Array.shift() 是 O(n)
const naive = [1, 2, 3];
console.log("Array.shift() 出队:", naive.shift(), "← ⚠️ O(n)，要搬移所有元素");

// 2. 正确做法：用双指针避免搬移
class Queue {
  constructor() { this.items = {}; this.head = 0; this.tail = 0; }
  enqueue(x) { this.items[this.tail++] = x; }
  dequeue() {
    if (this.head === this.tail) return undefined;
    const x = this.items[this.head];
    delete this.items[this.head++];
    return x;
  }
  peek() { return this.items[this.head]; }
  get size() { return this.tail - this.head; }
}
const q = new Queue();
q.enqueue("A"); q.enqueue("B"); q.enqueue("C");
console.log("双指针队列 出队:", q.dequeue(), "| 队首:", q.peek(), "| 剩余:", q.size);

// 3. 性能对比：shift() vs 双指针
const N = 50000;
let t = process.hrtime.bigint();
const arr = []; for (let i = 0; i < N; i++) arr.push(i);
while (arr.length) arr.shift();                       // O(n) 每次
const shiftMs = Number(process.hrtime.bigint() - t) / 1e6;
t = process.hrtime.bigint();
const q2 = new Queue(); for (let i = 0; i < N; i++) q2.enqueue(i);
while (q2.size) q2.dequeue();                          // O(1) 每次
const ptrMs = Number(process.hrtime.bigint() - t) / 1e6;
console.log(`\n${N} 个元素进出: shift() ${shiftMs.toFixed(1)}ms vs 双指针 ${ptrMs.toFixed(1)}ms`
  + ` → 慢 ${(shiftMs / ptrMs).toFixed(0)} 倍`);

// 4. 栈 vs 队列 = DFS vs BFS（只改一行）
const tree = { 1: [2, 3], 2: [4, 5], 3: [6, 7], 4: [], 5: [], 6: [], 7: [] };
function traverse(root, useStack) {
  const box = [root], order = [];
  while (box.length) {
    const node = useStack ? box.pop() : box.shift();   // ← 唯一的区别
    order.push(node);
    box.push(...tree[node]);
  }
  return order;
}
console.log("\n用栈  (LIFO) → DFS:", traverse(1, true).join(" "));
console.log("用队列(FIFO) → BFS:", traverse(1, false).join(" "), "← 逐层扫描");
