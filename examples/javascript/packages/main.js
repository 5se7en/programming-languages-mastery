// 第 15 章 · 包 — JavaScript 示例
// 运行：node main.js
// 说明：为保证离线可跑，这里自己实现 SemVer 逻辑，而不真去下载包。

function parse(v) {
  const [core, pre] = v.split("-");
  const [major, minor, patch] = core.split(".").map(Number);
  return { major, minor, patch, pre: pre || null };
}

// 版本比较：必须按「数字」比较，不能按字符串
function compare(a, b) {
  const x = parse(a), y = parse(b);
  for (const k of ["major", "minor", "patch"]) {
    if (x[k] !== y[k]) return x[k] - y[k];
  }
  if (x.pre && !y.pre) return -1;      // 预发布版排在正式版之前
  if (!x.pre && y.pre) return 1;
  return 0;
}

function satisfies(version, range) {
  const op = range[0] === "^" || range[0] === "~" ? range[0] : "=";
  const base = parse(range.replace(/^[\^~]/, ""));
  const v = parse(version);
  if (v.pre) return false;
  if (compare(version, range.replace(/^[\^~]/, "")) < 0) return false;
  if (op === "^") return v.major === base.major;              // 不跨 MAJOR
  if (op === "~") return v.major === base.major && v.minor === base.minor;  // 只放 PATCH
  return compare(version, range) === 0;
}

const versions = ["1.2.3", "1.2.9", "1.3.0", "1.9.9", "2.0.0"];
for (const range of ["^1.2.3", "~1.2.3", "1.2.3"]) {
  console.log(range.padEnd(8), "匹配 →", versions.filter(v => satisfies(v, range)).join(", "));
}

// 经典陷阱：字符串比较会得出错误结论
console.log('\n字符串比较 "1.10.0" > "1.9.0" →', "1.10.0" > "1.9.0", "← 错误！");
console.log('数字比较   compare("1.10.0","1.9.0") > 0 →', compare("1.10.0", "1.9.0") > 0, "← 正确");
console.log('预发布版   compare("1.0.0-beta","1.0.0") < 0 →', compare("1.0.0-beta", "1.0.0") < 0);

// package.json 的核心字段
const manifest = {
  name: "my-lib", version: "1.0.0", main: "index.js",
  dependencies: { lodash: "^4.17.21" },
  devDependencies: { jest: "^29.0.0" },
  license: "MIT",
};
console.log("\n清单文件核心字段:", Object.keys(manifest).join(", "));
console.log("生产依赖:", Object.keys(manifest.dependencies), "| 开发依赖:", Object.keys(manifest.devDependencies));
