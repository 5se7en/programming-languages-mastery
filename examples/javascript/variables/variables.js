// 第 08 章 · 变量 — JavaScript 示例
// 运行：node variables.js

// 1. 声明
let studentName = "Alice";
const MAX_SCORE = 100;
let age = 20;
let score = 92;
console.log(studentName, age, score, MAX_SCORE);

// 2. 类型跟着值走
console.log("typeof score:", typeof score);
score = "A+";
console.log("typeof score:", typeof score);

// 3. 赋值语义：原始类型复制值
let a = 92;
let b = a;
b = 60;
console.log("原始类型 值复制:", a, b);        // 92 60

// 4. 赋值语义：对象复制引用
let s1 = { name: "Alice", score: 92 };
let s2 = s1;
s2.score = 60;
console.log("对象 引用复制:", s1.score, s2.score);  // 60 60

// 5. const 锁定绑定而非内容
const student = { score: 92 };
student.score = 60;            // 合法
console.log("const 对象可改内容:", student.score);
