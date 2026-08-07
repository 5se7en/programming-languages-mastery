// 反射：JS 对象本来就是可枚举的字典，ES6 又补上了标准的 Reflect / Proxy。

class Student {
  #secret; // ES2022 真私有字段
  constructor(name = "未命名", score = 0) {
    this.name = name;
    this.score = score;
    this.#secret = `${name} 的真实分数是 ${score}`;
  }
  getName() {
    return this.name;
  }
}

console.log("== ① 对象即字典：结构随时可枚举 ==");
const s = new Student("小明", 90);
console.log(`Object.keys(s) = ${JSON.stringify(Object.keys(s))}`);
console.log(`s["score"] = ${s["score"]}   <- 字符串就是成员名`);

console.log("\n== ② 运行时改结构：加字段、加方法 ==");
s.motto = "好好学习";
Student.prototype.hello = function () {
  return `${this.name} 说你好`;
};
console.log(`动态加的方法: ${s.hello()}`);

console.log("\n== ③ Reflect：标准化的反射 API（ES6） ==");
console.log(`Reflect.ownKeys(s) = ${JSON.stringify(Reflect.ownKeys(s))}`);
console.log(`Reflect.get(s, "name") = ${Reflect.get(s, "name")}`);
const s2 = Reflect.construct(Student, ["小红", 85]);
console.log(`Reflect.construct -> ${s2.getName()}`);

console.log("\n== ④ #private 是真私有：反射也拿不到 ==");
console.log(`Object.keys 看不到 #secret: ${JSON.stringify(Object.keys(s))}`);
console.log(`Reflect.ownKeys 也看不到: ${JSON.stringify(Reflect.ownKeys(s))}`);
// console.log(s.#secret);   // ✗ 类外访问直接是语法错误，连运行的机会都没有

console.log("\n== ⑤ Proxy：拦截对对象的每一次访问（元编程） ==");
const audited = new Proxy(s, {
  get(target, prop, receiver) {
    if (typeof prop === "string" && prop !== "name") {
      console.log(`  [审计] 有人读取了 ${String(prop)}`);
    }
    return Reflect.get(target, prop, receiver);
  },
});
console.log(`通过 Proxy 读 score: ${audited.score}`);
