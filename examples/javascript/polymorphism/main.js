// 第 27 章 · 多态 —— JavaScript 示例
// 运行：node main.js
// JS 的多态 = 原型链属性查找 + 动态类型 = 本质上的鸭子类型

console.log("=== 1. 基于继承的多态 ===");

class Animal {
  speak() {
    return "发出声音";
  }
}
class Dog extends Animal {
  speak() {
    return "汪！";
  }
}
class Cat extends Animal {
  speak() {
    return "喵～";
  }
}

function makeSpeak(animals) {
  return animals.map((a) => a.speak()); // 不关心具体类型
}

console.log("  makeSpeak([Dog, Cat, Animal]) =", makeSpeak([new Dog(), new Cat(), new Animal()]));

console.log("\n=== 2. ⚠️ 但根本不需要继承（鸭子类型）===");
{
  const duck = { speak: () => "嘎嘎" }; // 普通对象，没有任何类
  const robot = { speak: () => "滴滴" };

  console.log("  普通对象也能用:", makeSpeak([new Dog(), duck, robot]));
  console.log("  duck instanceof Animal =", duck instanceof Animal, " ← 毫无关系");
  console.log("  → JS 的「多态」就是属性查找：");
  console.log("     a.speak 沿原型链找到第一个 speak 就调用，完全不检查 a 是什么类型");
  console.log("  → 这既是灵活性的来源，也是错误推迟到运行时的原因");
}

console.log("\n=== 3. 多态的价值：新增类型不改已有代码 ===");
{
  class Circle {
    constructor(r) {
      this.r = r;
    }
    area() {
      return Math.PI * this.r ** 2;
    }
    name() {
      return "圆形";
    }
  }
  class Rect {
    constructor(w, h) {
      this.w = w;
      this.h = h;
    }
    area() {
      return this.w * this.h;
    }
    name() {
      return "矩形";
    }
  }

  const totalArea = (shapes) => shapes.reduce((s, x) => s + x.area(), 0);

  const shapes = [new Circle(2), new Rect(3, 4)];
  shapes.forEach((s) => console.log(`    ${s.name()} 面积 = ${s.area().toFixed(2)}`));
  console.log(`  总面积 = ${totalArea(shapes).toFixed(2)}`);

  // 新增一种图形：totalArea 一个字不用改
  class Triangle {
    constructor(b, h) {
      this.b = b;
      this.h = h;
    }
    area() {
      return (this.b * this.h) / 2;
    }
    name() {
      return "三角形";
    }
  }
  shapes.push(new Triangle(6, 5));
  console.log(`  新增三角形后 → ${totalArea(shapes).toFixed(2)}   ← totalArea 一个字没改`);
  console.log("  → 这就是开闭原则：对扩展开放，对修改关闭");
}

console.log("\n=== 4. 派发的实现：内联缓存 ===");
console.log("  引擎不会每次都老实地沿原型链查找，而是用内联缓存记住结果：");
console.log("    第一次执行 a.speak()  → 沿原型链查找，记下「Dog 形状 → Dog.prototype.speak」");
console.log("    后续执行              → 先检查形状是否还是 Dog，是就直接用缓存的地址");
console.log();
console.log("  缓存的三种状态：");
console.log("    单态   monomorphic  只见过一种形状       → 最快");
console.log("    多态   polymorphic  见过 2-4 种形状      → 较快");
console.log("    超多态 megamorphic  超过 4 种，放弃缓存   → 慢");
console.log();
console.log("  → 实践含义（呼应第 24 章）：保持对象形状稳定，让调用点维持单态");
console.log("  → 这就是为什么「一个函数只处理一两种对象形状」通常比「处理十几种」快得多");

console.log("\n=== 5. JS 没有 abstract，只能手动抛错 ===");
{
  class Shape {
    area() {
      throw new Error("子类必须实现 area()");
    }
    describe() {
      return `面积 ${this.area().toFixed(2)}`; // 模板方法：调用子类实现
    }
  }
  class Square extends Shape {
    constructor(s) {
      super();
      this.s = s;
    }
    area() {
      return this.s ** 2;
    }
  }
  class Incomplete extends Shape {} // 忘了实现 area()

  console.log("  new Square(3).describe() =", new Square(3).describe());
  try {
    new Incomplete().describe();
  } catch (e) {
    console.log("  new Incomplete().describe() →", e.message);
  }
  console.log("  → 对比 Python 的 ABC：实例化时就报错；JS 只能等到调用时");
  console.log("  → 对比 Java/C# 的 abstract：编译期就报错");
}

console.log("\n=== 6. Symbol.iterator：让自定义类型支持 for...of ===");
{
  class Range {
    constructor(start, end) {
      this.start = start;
      this.end = end;
    }
    *[Symbol.iterator]() {
      // 实现这个「协议」就能被 for...of 使用
      for (let i = this.start; i < this.end; i++) yield i;
    }
  }

  console.log("  [...new Range(1, 5)] =", [...new Range(1, 5)]);
  console.log("  for...of 能用 Range 吗？", [...new Range(1, 4)].join(","));
  console.log("  → 这是 JS 版的「结构化契约」：实现约定的方法就能接入语言特性");
  console.log("  → 与 Python 的 __iter__、Protocol 是同一个思路");
}

console.log("\n=== 7. ⚠️ 别把多态退化成 typeof/instanceof 判断 ===");
console.log("  ❌ shapes.forEach(s => {");
console.log("       if (s instanceof Circle) { ... }      // 多态被浪费了");
console.log("       else if (s instanceof Rect) { ... }");
console.log("     });");
console.log("  ✅ shapes.reduce((sum, s) => sum + s.area(), 0);");

console.log("\n=== 8. 小结 ===");
console.log("  · JS 的多态 = 原型链属性查找，本质是鸭子类型");
console.log("  · 完全不需要继承：普通对象只要有同名方法就能用");
console.log("  · 引擎用内联缓存优化：保持形状稳定 → 维持单态 → 最快");
console.log("  · 没有 abstract，强制子类实现只能在基类主动抛错");
console.log("  · Symbol.iterator 等「协议」是 JS 版的结构化契约");
