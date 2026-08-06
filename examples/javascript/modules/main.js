// 第 14 章 · 模块 — JavaScript 示例（ESM）
// 运行：node main.js   （同目录 package.json 设了 "type": "module"）

import describe, { average, MAX_SCORE } from "./mathutil.js";
import * as util from "./mathutil.js";          // 整体导入成命名空间对象

console.log("具名导入:", average([92, 75, 50]).toFixed(2), "| 常量:", MAX_SCORE);
console.log("默认导入:", describe());
console.log("命名空间导入:", Object.keys(util).sort().join(", "));

// 模块只加载一次：再次导入不会重新执行 mathutil.js
import("./mathutil.js").then(() => console.log("再次导入 → 没有重复执行（已缓存）"));

// 未导出的名字外部访问不到
console.log("SECRET 可见吗:", "SECRET" in util ? "可见" : "不可见 ← 未 export 就是私有");
