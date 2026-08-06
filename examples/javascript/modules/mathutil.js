// 被导入的模块：只导出想公开的东西
console.log("   [mathutil.js 被执行了]");

const SECRET = "内部实现，未导出";        // 没有 export → 外部看不见

export function average(scores) {
  return scores.length ? scores.reduce((a, b) => a + b, 0) / scores.length : 0;
}
export const MAX_SCORE = 100;
export default function describe() { return "这是默认导出"; }
