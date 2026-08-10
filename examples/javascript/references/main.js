// 引用：JS 的传参同样是"按值传递引用"——解构赋值是语言给 swap 的补偿。

function swap(a, b) {
  [a, b] = [b, a]; // 只交换了函数内的两个参数名
}

function mutate(obj) {
  obj.score = 100; // 修改内容——穿透
}

function rebind(obj) {
  obj = { name: "换人", score: 0 }; // 重绑定——不穿透
}

console.log("== ① swap 测试：传参失败，解构成功 ==");
let x = 1,
  y = 2;
swap(x, y);
console.log(`swap(x, y) 之后: x = ${x}, y = ${y}   <- 没换成`);
[x, y] = [y, x]; // 语言层面的答案
console.log(`[x, y] = [y, x] 之后: x = ${x}, y = ${y}   <- 解构赋值，语言替你换`);

console.log("\n== ② 改内容穿透，重绑定不穿透 ==");
const stu = { name: "小明", score: 90 };
mutate(stu);
console.log(`mutate(stu) 之后 score = ${stu.score}   <- 穿透`);
rebind(stu);
console.log(`rebind(stu) 之后 name = ${stu.name}   <- 不穿透`);

console.log("\n== ③ const 锁的是引用，不是内容 ==");
stu.score = 61; // const 对象改字段——合法！
console.log(`const stu 改字段: score = ${stu.score}   <- 引用没变，内容随便`);
try {
  eval("stu = {}"); // 重绑定 const——才是被禁止的
} catch (e) {
  console.log(`const stu 重绑定 -> ${e.constructor.name}: 引用不可变，内容可变`);
}
console.log("（真要锁内容: Object.freeze——第 21 章实测过）");

console.log("\n== ④ 拷贝的三个层次 ==");
const src = { name: "小明", tags: ["A", "B"] };
const shared = src; // 共享：同一对象
const shallow = { ...src }; // 浅拷贝：第一层新建，tags 仍共享
const deep = structuredClone(src); // 深拷贝：全新
shallow.tags.push("C");
console.log(`浅拷贝改 tags 后，src.tags = [${src.tags}]   <- 第二层仍共享！`);
deep.tags.push("D");
console.log(`深拷贝改 tags 后，src.tags = [${src.tags}]   <- 深拷贝才真隔离`);
