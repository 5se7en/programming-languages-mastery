// 第 18 章 · 栈 — JavaScript 示例
// 运行：node main.js

// 1. JS 没有 Stack 类，直接用 Array 的 push/pop（都是 O(1)）
const stack = [];
stack.push(1); stack.push(2); stack.push(3);
console.log("栈:", stack, "| 栈顶(peek):", stack[stack.length - 1]);
console.log("pop():", stack.pop(), "← 最后进的先出（LIFO）");

// 2. 应用一：括号匹配（嵌套 ⇒ 栈）
function isBalanced(s) {
  const pairs = { ")": "(", "]": "[", "}": "{" };
  const st = [];
  for (const ch of s) {
    if ("([{".includes(ch)) st.push(ch);
    else if (ch in pairs) { if (st.pop() !== pairs[ch]) return false; }
  }
  return st.length === 0;
}
for (const s of ["(a[b]{c})", "(a[b)]", "((("])
  console.log(`括号匹配 ${s.padEnd(10)} → ${isBalanced(s)}`);

// 3. 应用二：后缀表达式求值（栈让优先级消失）
function evalRPN(tokens) {
  const st = [];
  for (const t of tokens) {
    if ("+-*/".includes(t) && t.length === 1) {
      const b = st.pop(), a = st.pop();
      st.push({ "+": a + b, "-": a - b, "*": a * b, "/": a / b }[t]);
    } else st.push(Number(t));
  }
  return st[0];
}
console.log(`\n后缀 "3 4 2 * +" = ${evalRPN("3 4 2 * +".split(" "))}  ← 等价中缀 3 + 4*2`);

// 4. 应用三：用两个栈实现撤销/重做
const done = [], undone = [];
function doOp(op) { done.push(op); undone.length = 0; }
function undo() { if (done.length) undone.push(done.pop()); }
function redo() { if (undone.length) done.push(undone.pop()); }
doOp("改A"); doOp("改B"); doOp("改C");
undo(); undo();
console.log(`\n撤销两次后已做: [${done}] 待重做: [${undone}]`);
redo();
console.log(`重做一次后已做: [${done}] 待重做: [${undone}]`);

// 5. ⚠️ 别用 shift 当 pop（那是 O(n) 且语义是队列）
console.log("\n⚠️ pop() 从末尾 O(1)；shift() 从头部 O(n) 且是 FIFO 语义");
