// 泛型：JavaScript 没有类型参数——动态类型让一切天然"泛型"。

console.log('== ① 动态类型天然"泛型"：一个容器装一切 ==');
const stack = [];
stack.push(90, "小明", { name: "小红" });
console.log(stack);

console.log("\n== ② 同一个函数天然适用于一切类型 ==");
const first = (arr) => arr[0];
console.log(first([90, 85]), first(["小明", "小红"]));

console.log("\n== ③ 自由的代价：类型混入后不报错，悄悄算错 ==");
const scores = [90, 85, "九十八"];
const total = scores.reduce((a, b) => a + b, 0);
console.log(`[90, 85, "九十八"] 求和 = ${JSON.stringify(total)}  <- 不报错，结果却成了字符串！`);

console.log("\n== ④ 运行时的自卫：手工检查 ==");
function sumScores(arr) {
  return arr.reduce((acc, x) => {
    if (typeof x !== "number") {
      throw new TypeError(`期望 number，拿到 ${typeof x}: ${x}`);
    }
    return acc + x;
  }, 0);
}
try {
  sumScores(scores);
} catch (e) {
  console.log(`${e.name}: ${e.message}`);
}

// ⑤ TypeScript 的泛型（编译期检查，运行时消失）：
//   function first<T>(arr: T[]): T { return arr[0]; }
//   first<number>([90, 85]);        // ✓ 推断出返回 number
//   first<number>([90, "八十五"]);  // ✗ 编译错误：string 不能赋给 number
