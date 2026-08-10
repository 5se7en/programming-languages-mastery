// RAII：JS 是五门语言里唯一没有作用域绑定资源管理的——只能靠 try/finally。

class FileHandle {
  constructor(name) {
    this.name = name;
    console.log(`    [获取] 打开 ${name}`);
  }
  close() {
    console.log(`    [释放] 关闭 ${this.name}`);
  }
  [Symbol.dispose]() {
    this.close();
  } // 提案就绪，但引擎语义未实现（见 ④）
}

console.log("== ① try/finally：JS 当下唯一可靠的手段 ==");
{
  const f = new FileHandle("data.txt");
  try {
    console.log("    使用中……");
  } finally {
    f.close(); // 必须自己写——语言不帮你
  }
}
console.log("    块结束");

console.log("\n== ② 钥匙实验：异常安全 ==");
console.log("  裸风格（忘了 try/finally）:");
try {
  const f = new FileHandle("manual.txt");
  throw new Error("中途出错");
  f.close(); // eslint-disable-line no-unreachable
} catch (e) {
  console.log(`    捕获: ${e.message}   <- 没有 [释放] 打印！句柄泄漏`);
}
console.log("  try/finally 风格:");
try {
  const f = new FileHandle("finally.txt");
  try {
    throw new Error("中途出错");
  } finally {
    f.close();
  }
} catch (e) {
  console.log(`    捕获: ${e.message}   <- [释放] 已在上一行打印`);
}

console.log("\n== ③ 多个资源：嵌套的丑陋（对比其他语言的一行）==");
try {
  const a = new FileHandle("第一个");
  try {
    const b = new FileHandle("第二个");
    try {
      const c = new FileHandle("第三个");
      try {
        throw new Error("三个都开着的时候出错了");
      } finally {
        c.close();
      }
    } finally {
      b.close();
    }
  } finally {
    a.close();
  }
} catch (e) {
  console.log(`    捕获: ${e.message}（逆序释放对了，但代码嵌了三层）`);
}

console.log("\n== ④ 提案现状：Symbol.dispose 有了，语义还没有 ==");
console.log(`    typeof Symbol.dispose = ${typeof Symbol.dispose}   <- well-known symbol 已就位`);
console.log("    但 using 声明在本机 Node 上：语法要 flag，且开了 flag 也不触发 dispose");
console.log("    （见章节 shell 实测：非 disposable 对象不抛 TypeError = 语义未实现）");

console.log("\n== ⑤ 折中方案：高阶函数包住资源 ==");
function withResource(name, fn) {
  const res = new FileHandle(name);
  try {
    return fn(res);
  } finally {
    res.close(); // 释放逻辑写一次，调用方拿不到忘记的机会
  }
}
try {
  withResource("高阶函数.txt", () => {
    throw new Error("中途出错");
  });
} catch (e) {
  console.log(`    捕获: ${e.message}   <- 释放已自动完成（把 finally 封进库里）`);
}
