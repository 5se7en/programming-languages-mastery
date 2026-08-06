// 第 13 章 · 作用域 — JavaScript 示例
// 运行：node scope.js

// 1. 提升：var 已登记未赋值，let 落在暂时性死区（TDZ）
try { console.log("var 提前访问 →", a); } catch (e) { console.log("var:", e.constructor.name); }
var a = 1;
try { console.log(b); } catch (e) { console.log("let 提前访问 → " + e.constructor.name + ":", e.message); }
let b = 1;
console.log("函数声明整体提升 →", hoisted());
function hoisted() { return "能在定义前调用"; }

// 2. 块作用域：var 泄漏，let 被限制
if (true) {
  var leaked = "var 定义的";
  let confined = "let 定义的";
}
console.log("块外访问 var →", leaked);
try { console.log(confined); } catch (e) { console.log("块外访问 let →", e.constructor.name); }

// 3. 作用域链：由内向外查找
const outerVar = "外层";
function outer() {
  const middleVar = "中层";
  function inner() {
    const innerVar = "内层";
    return `${innerVar} → ${middleVar} → ${outerVar}`;   // 逐层向外找到
  }
  return inner();
}
console.log("作用域链:", outer());

// 4. 闭包：内层函数捕获外层变量，使其活得更久
function makeCounter() {
  let count = 0;                 // 被捕获 → 移到堆上
  return () => ++count;
}
const c = makeCounter();
c(); c();
console.log("闭包计数器:", c());

// 5. 遮蔽：内层同名变量遮蔽外层
let x = "全局 x";
{
  let x = "块内 x";
  console.log("块内看到:", x);
}
console.log("块外看到:", x);
