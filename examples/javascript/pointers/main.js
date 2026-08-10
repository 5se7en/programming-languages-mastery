// 指针：JS 连地址都不给看——但 ArrayBuffer 是一块"自己管字节"的手动内存。

console.log("== ① 对象变量都是引用：比较即地址比较 ==");
const a = { name: "小明" };
const b = a;
const c = { name: "小明" };
b.name = "小红";
console.log(`b = a 后改 b.name，a.name = ${a.name}   <- 同一个对象`);
console.log(`a === b: ${a === b}（同一引用）；a === c: ${a === c}（内容相同也不等）`);

console.log("\n== ② ArrayBuffer：一块裸内存，offset 就是你的指针 ==");
const buf = new ArrayBuffer(16); // 16 字节，无类型
const view = new DataView(buf);
view.setInt32(0, 42, true); // "指针" offset=0，写 int32
view.setFloat64(8, 3.14, true); // "指针" offset=8，写 double
console.log(`offset 0 读 int32:   ${view.getInt32(0, true)}`);
console.log(`offset 8 读 float64: ${view.getFloat64(8, true)}`);
console.log(`同一块内存换个类型读: offset 0 按 uint8 = ${view.getUint8(0)}`);
console.log("（手动算 offset、手动选类型——这就是把指针的工作自己做一遍）");

console.log("\n== ③ TypedArray：同一块内存的多种视图 ==");
const nums = new Int32Array(buf); // 与 DataView 共享同一块 ArrayBuffer
console.log(`Int32Array 视图看 buf: [${nums[0]}, ${nums[1]}, ...]   <- nums[0] 就是刚才写的 42`);
nums[0] = 100;
console.log(`改 nums[0] 后 DataView 再读 offset 0: ${view.getInt32(0, true)}   <- 同一块内存`);

console.log("\n== ④ 越界：沙箱里的指针不会伤人 ==");
console.log(`nums[999] = ${nums[999]}   <- 越界读返回 undefined，不是别人的内存`);
console.log("（C++ 的越界是未定义行为，这里是被沙箱拦下的 undefined——能力换安全）");
