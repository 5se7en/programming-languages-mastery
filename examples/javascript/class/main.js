// 第 23 章 · 类 —— JavaScript 示例
// 运行：node main.js

console.log("=== 1. 不用类的痛点：数据分散、关联脆弱 ===");
{
  const names = ["Alice", "Bob"];
  const scores = [92, 75];
  const ages = [16, 17];
  console.log("  平行数组:", names, scores, ages);
  console.log("  ⚠️ 三个数组必须严格保持顺序一致，一旦排序其中一个就全乱了");

  function isPassing(score) { return score >= 60; }
  console.log("  isPassing(ages[0]) =", isPassing(ages[0]), " ← 传错参数，语法却完全合法");
}

console.log("\n=== 2. 用类打包：数据和行为待在一起 ===");

class Student {
  static school = "第一中学"; // 静态属性：属于类，只存一份
  static #count = 0;          // 静态私有字段

  #id;                        // 实例私有字段（ES2022）

  constructor(name, score, age) {
    this.name = name;
    this.score = score;       // 三个数据打包在一起，永远不会错位
    this.age = age;
    this.#id = ++Student.#count;
  }

  isPassing() {
    return this.score >= 60;  // 只看自己的 score，不可能传错
  }

  get id() { return this.#id; }

  static count() { return Student.#count; }
  static create(name) { return new Student(name, 0, 0); }
}

const alice = new Student("Alice", 92, 16);
const bob = new Student("Bob", 45, 17);
console.log(`  ${alice.name}: 分数 ${alice.score}, 及格? ${alice.isPassing()}`);
console.log(`  ${bob.name}: 分数 ${bob.score}, 及格? ${bob.isPassing()}`);
console.log("  静态属性 Student.school =", Student.school, " ← 所有实例共享");
console.log("  已创建实例数 =", Student.count());

console.log("\n=== 3. ⚠️ class 只是原型的语法糖（实测）===");
console.log("  typeof Student                    →", typeof Student, " ← 竟然是函数！");
console.log("  方法定义在哪里？                   →",
  Object.getOwnPropertyNames(Student.prototype).filter((n) => n !== "constructor"));
console.log("  实例自己有 isPassing 吗？          →", Object.hasOwn(alice, "isPassing"), " ← 没有");
console.log("  实例自己有什么？                   →", Object.getOwnPropertyNames(alice));
console.log("  getPrototypeOf(alice) === Student.prototype →",
  Object.getPrototypeOf(alice) === Student.prototype);
console.log("  → 方法只存一份（在原型上），所有实例共享；实例内存里只有自己的数据");

console.log("\n  用 ES5 原型写法完全还原：");
function OldStudent(name, score) {
  this.name = name;
  this.score = score;
}
OldStudent.prototype.isPassing = function () { return this.score >= 60; };
console.log("    ES5 原型写法:", new OldStudent("Carol", 88).isPassing());
console.log("    ES6 class 写法:", new Student("Carol", 88, 16).isPassing(), " ← 行为完全一致");

console.log("\n=== 4. 引用语义：b = a 只是起了个别名 ===");
{
  const a = new Student("Alice", 90, 16);
  const b = a;              // 不是拷贝！
  b.name = "Bob";
  console.log(`  赋值后: a.name=${a.name}  b.name=${b.name}  ← a 也变了！`);
  console.log("  a === b ?", a === b, " ← 根本就是同一个对象");

  // 想要拷贝必须显式说
  const c = { ...a };       // 浅拷贝（注意：拷出来的是普通对象，不再是 Student）
  c.name = "Carol";
  console.log(`  展开拷贝后: a.name=${a.name}  c.name=${c.name}  ← 这才是拷贝`);
  console.log("  c instanceof Student ?", c instanceof Student, " ← ⚠️ 浅拷贝丢了类型");
}

console.log("\n=== 5. this 由「调用方式」决定，不是定义位置 ===");
{
  const s = new Student("Dave", 70, 18);
  console.log("  正常调用 s.isPassing() =", s.isPassing());

  const fn = s.isPassing;   // 把方法取出来单独放
  try {
    fn();
  } catch (e) {
    console.log("  取出来直接调用 fn() → 抛错:", e.constructor.name);
    console.log("    （class 内部是严格模式，this 为 undefined）");
  }
  console.log("  用 bind 绑定后 =", s.isPassing.bind(s)());
  console.log("  → this 由调用方式决定，这是 JS 与其他语言的重要差异");
}

console.log("\n=== 6. 私有字段：# 是真正的私有（ES2022）===");
{
  console.log("  alice.id (通过 getter) =", alice.id);
  console.log("  能直接访问 #id 吗？ → 语法上就不允许，会是编译期错误");
  console.log("  Object.keys(alice) =", Object.keys(alice), " ← 私有字段不出现");
  console.log("  JSON.stringify(alice) =", JSON.stringify(alice), " ← 也不会被序列化");
}

console.log("\n=== 7. 类声明不会被提升 ===");
try {
  new NotYetDefined();
} catch (e) {
  console.log("  在定义前使用 →", e.constructor.name, " ← 与 function 的行为不同");
}
class NotYetDefined {}
