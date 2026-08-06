// 第 10 章 · 运算符 — JavaScript 示例
// 运行：node operators.js

// 1. == 会隐式转换，=== 不会
console.log("1 == '1'          →", 1 == "1");
console.log("1 === '1'         →", 1 === "1");
console.log("[] == false       →", [] == false);
console.log("null == undefined →", null == undefined);
console.log("null === undefined→", null === undefined);

// 2. NaN 不等于自己
console.log("NaN === NaN       →", NaN === NaN, "| 正确判断:", Number.isNaN(NaN));

// 3. 短路求值：右边根本不执行
function boom() { console.log("   ← 这行不该出现！"); return true; }
console.log("false && boom()   →", false && boom());
console.log("true  || boom()   →", true || boom());

// 4. ?? 与 || 的区别（数字默认值场景）
console.log("0 || 8080         →", 0 || 8080, "  ← 0 被当成假值");
console.log("0 ?? 8080         →", 0 ?? 8080, "     ← 只在 null/undefined 时兜底");

// 5. 可选链防空
const user = { address: null };
console.log("user?.address?.city →", user?.address?.city);
