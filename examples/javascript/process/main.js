// 进程：Node 单线程，想用多核只能靠多进程（child_process / cluster）。

const { fork, execSync } = require("child_process");
const os = require("os");
const path = require("path");
const fs = require("fs");

let counter = 100; // 主进程变量——子进程完全看不到

console.log("== ① 进程身份 ==");
console.log(`  我是进程 ${process.pid}，父进程 ${process.ppid}`);
console.log(`  CPU 核数 = ${os.cpus().length}，平台 = ${process.platform}`);
console.log(`  Node 是单线程事件循环（第 43 章）——多核只能靠多进程`);

console.log("\n== ② 钥匙实验：子进程是另一个世界 ==");
console.log(`  父进程 counter = ${counter}`);

// 写一个临时子进程脚本
const childScript = path.join(os.tmpdir(), `plm-child-${process.pid}.js`);
fs.writeFileSync(
  childScript,
  `let counter = 100;
counter += 1;
console.log(\`  [子进程 \${process.pid}] 我的 counter = \${counter}，父进程是 \${process.ppid}\`);
process.send && process.send({ from: process.pid, text: "隔离归隔离，话还是要讲的" });
process.exit(0);
`
);

const child = fork(childScript, [], { stdio: "inherit", silent: false });

child.on("message", (msg) => {
  console.log(`  父进程收到 IPC 消息: "${msg.text}"（来自进程 ${msg.from}）`);
});

child.on("exit", (code) => {
  console.log(`  子进程退出码 = ${code}，父进程 counter 仍是 ${counter}   <- 纹丝不动`);
  fs.unlinkSync(childScript);

  console.log("\n== ③ IPC：fork() 自带消息通道 ==");
  console.log("  child.send() / process.send() —— 结构化克隆后传输");
  console.log("  （与 C++ 的裸管道相比，Node 把序列化封装好了）");

  console.log("\n== ④ execSync：同步调用外部命令 ==");
  const out = execSync("echo '  外部命令的输出'").toString().trimEnd();
  console.log(out);

  console.log("\n== ⑤ cluster：多进程共享同一个端口 ==");
  console.log("  Node 的标准扩展方式：主进程 fork 出 N 个 worker（N = 核数）");
  console.log("  内核负载均衡把连接分给各 worker —— 单机吃满多核的经典模型");
  console.log("  （每个 worker 是独立进程：内存隔离、崩溃不互相拖累）");
});
