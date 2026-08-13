// 安全：XSS——同一份「转义」，在三种上下文里有三种正确答案，用错等于没转义。
'use strict';

/** 教科书上最常见的「HTML 转义」实现 */
function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

/** 浏览器解析【普通属性值】时会做的事: 把字符引用解码回原字符 */
function htmlAttrDecode(s) {
  return s.replace(/&#(\d+);/g, (_, d) => String.fromCharCode(+d))
    .replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');
}

console.log('== ① 三种上下文，同一个 escapeHtml，三种结果 ==');

console.log('  【上下文 A：HTML 文本】<div>{}</div>');
const evilA = '<img src=x onerror=alert(1)>';
console.log(`    未转义:  <div>${evilA}</div>`);
console.log(`    转义后:  <div>${escapeHtml(evilA)}</div>`);
console.log('    → ✅ 有效: < 变成 &lt;，浏览器不会把它当标签开始');

console.log('\n  【上下文 B：无引号属性】<div class={}>');
const evilB = 'a onmouseover=alert(1)';
console.log(`    未转义:  <div class=${evilB}>`);
console.log(`    转义后:  <div class=${escapeHtml(evilB)}>`);
console.log('    → ❌ 失败: 载荷里【没有一个字符需要转义】，转义前后完全相同');
console.log('       属性没有引号包住，一个空格就足以引入新属性 onmouseover=');

console.log('\n  【上下文 C：事件处理属性】<a onclick="greet(\'{}\')">');
const evilC = "');alert(1);//";
const attrValue = `greet('${escapeHtml(evilC)}')`;
console.log(`    转义后的 HTML:  <a onclick="${attrValue}">`);
console.log('    浏览器解析属性值时【先解码字符引用】，再把结果交给 JS 引擎:');
console.log(`    JS 引擎实际看到: ${htmlAttrDecode(attrValue)}`);
console.log('    → ❌ 失败: &#39; 被解回 \'，字符串被闭合，alert(1) 成为独立语句');
console.log('       这是【双重解码】漏洞: HTML 解析器和 JS 解析器各解析了一次');

console.log('\n  ⚠️ 补充一个常被讲错的点: <script> 标签内部是【原始文本】，');
console.log('     字符引用【不会】被解码。所以在 <script> 里做 HTML 转义不是「有漏洞」，');
console.log('     而是「数据被损坏」—— 用户名会真的显示成 &#39;。正确做法是 JSON.stringify。');
console.log('  → 三种上下文，三种结果，同一个转义函数——这就是 XSS 难防的根源');

console.log('\n== ② 每种上下文的正确转义各不相同 ==');
const rules = [
  ['HTML 文本',        '&<>"\' → 实体',              'escapeHtml 即可'],
  ['HTML 属性',        '同上 + 【必须用引号包住】',   '无引号属性无法安全转义'],
  ['URL 参数',         'encodeURIComponent',         'escapeHtml 完全不适用'],
  ['JS 字符串',        'JSON.stringify 或 \\x 编码',  'HTML 实体在这里是错的'],
  ['CSS 值',           '严格白名单',                 'expression()/url() 可执行'],
  ['href/src 属性',    '再加协议白名单',             'javascript: 伪协议'],
];
for (const [ctx, how, note] of rules) {
  console.log(`  ${ctx.padEnd(12)} → ${how.padEnd(26)} （${note}）`);
}
console.log('  → 「转义」不是一个动作，而是【六个不同的动作】，取决于数据落在哪里');
console.log('  → 这就是为什么手工拼 HTML 字符串几乎必然出错');

console.log('\n== ③ javascript: 伪协议——转义全对，依然中招 ==');
const url = 'javascript:alert(document.cookie)';
console.log(`  用户输入的链接: ${url}`);
console.log(`  escapeHtml 后:  ${escapeHtml(url)}`);
console.log(`  生成的 HTML:    <a href="${escapeHtml(url)}">click</a>`);
console.log('  → 转义完全正确（没有任何需要转义的字符），但点击就执行 JS');
console.log('  → 正确做法是【协议白名单】:');
const allowProto = (u) => {
  try {
    const p = new URL(u, 'https://example.com').protocol;
    return ['http:', 'https:', 'mailto:'].includes(p) ? u : '#';
  } catch { return '#'; }
};
for (const u of ['https://ok.example/a', 'javascript:alert(1)', '//evil.example', 'DaTa:text/html,x']) {
  console.log(`    ${u.padEnd(24)} → ${allowProto(u)}`);
}
console.log('  → 注意大小写与空白绕过（DaTa: / java\\tscript:），所以要【解析后判断】而不是字符串匹配');
console.log('  ⚠️ 但注意 //evil.example 【通过了】协议白名单: 它继承了当前页面的 https:');
console.log('     协议对了，主机却是攻击者的 —— 这是开放重定向，需要【再加一层主机白名单】');
console.log('     教训: 一个检查通过 ≠ 安全，要问清楚它到底检查了什么');

console.log('\n== ④ 根本解法：别拼字符串，用结构化 API ==');
console.log('  危险: el.innerHTML = "<div>" + userInput + "</div>"');
console.log('  安全: el.textContent = userInput          ← 数据永远是数据');
console.log('        el.setAttribute("class", userInput) ← 属性值不参与解析');
console.log('  → 与 Python 版的参数化查询【完全同构】: 让数据走一条不会被解析的通道');
console.log('  → React/Vue 默认转义 {expr}，这就是它们「默认安全」的来源');
console.log('     而 dangerouslySetInnerHTML / v-html 的名字里就写着代价');

console.log('\n== ⑤ 原型链污染：JS 独有的一类注入 ==');
function unsafeMerge(target, src) {
  for (const k in src) {
    if (typeof src[k] === 'object' && src[k] !== null) {
      target[k] = target[k] || {};
      unsafeMerge(target[k], src[k]);                 // ⚠️ 没有过滤 __proto__
    } else target[k] = src[k];
  }
  return target;
}
const evil = JSON.parse('{"__proto__":{"isAdmin":true}}');
const before = ({}).isAdmin;
unsafeMerge({}, evil);
const after = ({}).isAdmin;
console.log(`  合并前，一个【全新的空对象】的 isAdmin: ${before}`);
console.log(`  合并后，一个【全新的空对象】的 isAdmin: ${after}`);
console.log('  → 攻击者从未碰过这个对象，却让它有了 isAdmin === true');
console.log('  → 第 26 章讲过原型链: 改 Object.prototype 会影响【所有】对象');
console.log('  → 后果: 任何 `if (user.isAdmin)` 的检查全部失守');
console.log('  → 防御: 过滤 __proto__/constructor/prototype，或用 Object.create(null)、Map');
delete Object.prototype.isAdmin;                      // 清理，别污染后续实验

console.log('\n== ⑥ 依赖也是攻击面 ==');
console.log('  第 53 章实测过: 一个 create-react-app 装出 1000+ 个传递依赖');
console.log('  → 你审计了自己的 200 行代码，但运行的是几十万行别人的代码');
console.log('  → event-stream(2018): 一个 200 万周下载的包被转手，新维护者植入了盗币代码');
console.log('  → 防御: lockfile 锁版本、npm audit、最小依赖、CI 里跑 SCA');
console.log('  → 关键认识: 【供应链安全是依赖管理问题，不是代码审计问题】');

console.log('\n== ⑦ 浏览器给的纵深防御 ==');
console.log("  CSP:              Content-Security-Policy: script-src 'self'");
console.log('                    → 即使被注入，外部脚本与内联脚本也不执行');
console.log('  HttpOnly Cookie:  → document.cookie 读不到，XSS 偷不走会话');
console.log('  SameSite=Lax:     → 跨站请求不带 Cookie，挡住大部分 CSRF');
console.log('  Subresource Integrity: <script integrity="sha384-...">');
console.log('                    → CDN 被攻破也不会加载被篡改的脚本');
console.log('  → 这些都是【第二道防线】: 假设第一道会失守，仍然限制损失');
console.log('  → 安全设计的基本假设不是「不会出错」，而是【出错时代价有多大】');
