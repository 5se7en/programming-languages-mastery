// 第 11 章 · 流程控制 — JavaScript 示例
// 运行：node control-flow.js

const scores = [92, 75, 50];

// 1. 分支
function grade(score) {
  if (score >= 90) return "A";
  else if (score >= 60) return "B";
  else return "C";
}
console.log("分支:", scores.map(grade).join(" "));

// 2. switch 需要 break，否则穿透
function describe(g) {
  switch (g) {
    case "A": return "优秀";
    case "B": return "及格";
    default:  return "不及格";
  }
}
console.log("switch:", describe("A"), describe("C"));

// 3. for...in 取键，for...of 取值 —— 最易混淆的一对
const keys = [], values = [];
for (const k in scores) keys.push(k);
for (const v of scores) values.push(v);
console.log("for...in 得到键:", keys, "| for...of 得到值:", values);

// 4. 经典陷阱：闭包捕获循环变量
const fnsVar = [], fnsLet = [];
for (var i = 0; i < 3; i++) fnsVar.push(() => i);
for (let j = 0; j < 3; j++) fnsLet.push(() => j);
console.log("用 var →", fnsVar.map(f => f()), "← 全是 3！");
console.log("用 let →", fnsLet.map(f => f()), "← 正确");

// 5. 卫语句：消除嵌套
function process(user) {
  if (!user) return "无用户";
  if (!user.active) return "未激活";
  return "处理完成";
}
console.log("卫语句:", process(null), "|", process({ active: true }));
