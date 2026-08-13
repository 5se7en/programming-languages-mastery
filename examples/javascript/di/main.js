// 依赖注入：函数式 DI——闭包就是最轻的容器；以及部分应用与 DI 的同构。
'use strict';

// ============ 依赖 ============
const realClock = () => Date.now();
const realMailer = {
  send: () => { throw new Error('真的发邮件了！（测试里绝不该走到这）'); },
};

const frozenClock = (t) => () => t;
const fakeMailer = () => {
  const sent = [];
  return { send: (to, msg) => sent.push([to, msg]), sent };
};

// ---- 写法 A: 依赖写死（不可测） ----
function greetHardCoded(user) {
  const stamp = realClock();
  realMailer.send(user, `你好 @${stamp}`);
  return stamp;
}

// ---- 写法 B: 参数注入（最直白） ----
function greetInjected(user, { clock, mailer }) {
  const stamp = clock();
  mailer.send(user, `你好 @${stamp}`);
  return stamp;
}

// ---- 写法 C: 闭包工厂（函数式 DI 的主流写法） ----
function makeGreeter({ clock, mailer }) {
  // 依赖被【闭包捕获】，返回的函数签名里再也看不到它们
  return function greet(user) {
    const stamp = clock();
    mailer.send(user, `你好 @${stamp}`);
    return stamp;
  };
}

console.log('== ① 三种写法，同一个业务逻辑 ==');
console.log('  A 写死:   greetHardCoded(user)              —— 测试里会真发邮件');
console.log('  B 参数:   greetInjected(user, {clock, mailer}) —— 每次调用都要传');
console.log('  C 闭包:   makeGreeter({clock, mailer})(user)   —— 装配一次，之后像普通函数');

console.log('\n== ② 闭包工厂：装配与使用分离（实测）==');
const fm = fakeMailer();
const greet = makeGreeter({ clock: frozenClock(1_000_000), mailer: fm });
console.log(`  组装期: makeGreeter({...}) —— 只做一次（相当于容器的 Resolve）`);
console.log(`  使用期: greet('甲') → ${greet('甲')}，greet('乙') → ${greet('乙')}`);
console.log(`  邮件记录: ${JSON.stringify(fm.sent)}`);
console.log('  → greet 的签名是 (user) —— 调用方【完全不知道】它依赖时钟和邮件');
console.log('  → 这就是构造器注入的函数式版本: 闭包环境 = 对象的字段');
console.log('  → 第 13 章讲闭包时说「函数 + 它捕获的环境」——这里那个环境就是【依赖容器】');

console.log('\n== ③ 部分应用：DI 的另一个名字（实测）==');
const partial = (fn, ...bound) => (...rest) => fn(...bound, ...rest);
function sendReport(clock, mailer, title) {      // 依赖在前，参数在后
  const stamp = clock();
  mailer.send('报表组', `${title} @${stamp}`);
  return `${title}@${stamp}`;
}
const fm2 = fakeMailer();
const report = partial(sendReport, frozenClock(2_000_000), fm2);   // 绑定依赖
console.log(`  partial(sendReport, clock, mailer) → 得到 (title) => ...`);
console.log(`  report('日报') → ${report('日报')}`);
console.log(`  report('周报') → ${report('周报')}`);
console.log('  → 「把依赖绑定好，返回一个更简单的函数」——这与 DI 容器做的事完全同构');
console.log('  → 函数式世界里 DI 不需要专门的名字，它就是【部分应用】');

console.log('\n== ④ 组装根（Composition Root）：整个应用只有一处知道真实依赖 ==');
function buildApp(env) {
  // 唯一知道「用哪个实现」的地方 —— 相当于容器的注册代码
  const deps = env === 'test'
    ? { clock: frozenClock(3_000_000), mailer: fakeMailer() }
    : { clock: realClock, mailer: realMailer };
  return {
    greet: makeGreeter(deps),
    deps,                                     // 暴露给测试断言用
  };
}
const testApp = buildApp('test');
console.log(`  buildApp('test').greet('丙') → ${testApp.greet('丙')}`);
console.log(`  测试里可以直接断言: ${JSON.stringify(testApp.deps.mailer.sent)}`);
console.log('  → 组装根应该在【程序最外层】（main/入口），业务代码一律只接收依赖');
console.log('  → 判断 DI 做得好不好的一个信号: 全项目搜 `new Xxx()`，应该只在组装根出现');

console.log('\n== ⑤ 为什么 JS 生态很少用 DI 容器 ==');
console.log('  ① 模块本身就是单例容器: import { db } from "./db.js" —— 天然的全局装配');
console.log('  ② 函数是一等公民: 传函数比传对象轻得多（③ 的部分应用）');
console.log('  ③ 但代价是: import 的依赖【换不掉】——测试里只能用 jest.mock 改模块（Python 版 ② 同款问题）');
console.log('  → 所以 JS 的实践是: 业务逻辑写成【接收依赖的纯函数/工厂】，');
console.log('     只在组装根 import 真实实现——用结构自律代替容器');
console.log('  → 例外: Angular（有完整 DI 容器）、NestJS（照搬 Spring 的装饰器 + 容器）');

console.log('\n== ⑥ 三种语言的 DI，同一个内核 ==');
console.log('  Java/C#: 构造器参数 + 容器反射装配（对象图）');
console.log('  Python : 构造器参数 / 默认参数 / 猴子补丁（一切可写）');
console.log('  JS     : 闭包捕获 / 部分应用 / 工厂函数（函数是一等公民）');
console.log('  → 内核完全相同: 【不要自己找依赖，让依赖被送进来】');
console.log('  → 差别只在「送」的语法: 参数、字段、闭包环境——本质都是把控制权交出去');
