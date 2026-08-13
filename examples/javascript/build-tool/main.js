// 构建工具：手写一个微型打包器——依赖图、bundle 生成、tree-shaking，全部真实运行验证。
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const WORK = fs.mkdtempSync(path.join(os.tmpdir(), 'pl-mastery-bundle-'));
const write = (name, s) => fs.writeFileSync(path.join(WORK, name), s.trim() + '\n');

// ---------- 一个小项目（ESM 风格，静态 import/export）----------
write('math.js', `
export function add(a, b) { return a + b; }
export function sub(a, b) { return a - b; }
export function matrixMultiply(a, b) {          // 没人用的大函数（模拟拖进来的重库）
  let out = []; for (let i = 0; i < 1000; i++) out.push(i * 2);
  return out;
}
`);
write('format.js', `
import { add } from './math.js';
export function formatSum(a, b) { return '结果: ' + add(a, b); }
export function formatCsv(rows) { return rows.map(r => r.join(',')).join('\\n'); }
`);
write('main.js', `
import { formatSum } from './format.js';
console.log(formatSum(40, 2));
`);

// ---------- ① 构建依赖图 ----------
function parseImports(source) {
  const out = [];
  for (const m of source.matchAll(/import\s+\{([^}]*)\}\s+from\s+'([^']+)'/g))
    out.push({ names: m[1].split(',').map(s => s.trim()), from: m[2] });
  return out;
}

function buildGraph(entry) {
  const graph = new Map();                       // 文件 → {source, imports}
  const visit = (file) => {
    if (graph.has(file)) return;
    const source = fs.readFileSync(path.join(WORK, file), 'utf8');
    const imports = parseImports(source);
    graph.set(file, { source, imports });
    for (const imp of imports) visit(path.normalize(imp.from));
  };
  visit(entry);
  return graph;
}

console.log('== ① 打包的第一步：从入口出发构建依赖图 ==');
const graph = buildGraph('main.js');
for (const [file, { imports }] of graph)
  console.log(`  ${file} → ${imports.map(i => `${i.from}{${i.names.join(',')}}`).join(' ') || '（无依赖）'}`);
console.log('  → 与第 53 章包管理的依赖图同构，只是节点从「包」细化到「模块」');
console.log('  → esbuild/webpack 的第一步都是它: 静态解析 import，递归到闭包');

// ---------- ② 打包: 把模块图变成单文件 ----------
function bundle(graph, entry) {
  let out = '// ---- bundle（模块注册表 + 运行时）----\n';
  out += 'const __modules = {}, __cache = {};\n';
  out += 'function __require(id) {\n';
  out += '  if (__cache[id]) return __cache[id];\n';
  out += '  const exports = {}; __cache[id] = exports;\n';
  out += '  __modules[id](exports, __require);\n';
  out += '  return exports;\n}\n';
  for (const [file, { source }] of graph) {
    // 把 ESM 语法改写成注册表形式（真实打包器在 AST 上做，这里用正则示意）
    let body = source
      .replace(/import\s+\{([^}]*)\}\s+from\s+'([^']+)';?/g,
               (_, names, from) => `const {${names}} = __require('${path.normalize(from)}');`)
      .replace(/export\s+function\s+(\w+)/g, 'exports.$1 = function $1');
    out += `__modules['${file}'] = (exports, __require) => {\n${body}\n};\n`;
  }
  out += `__require('${entry}');\n`;
  return out;
}

console.log('\n== ② 生成 bundle 并真实运行 ==');
const full = bundle(graph, 'main.js');
const fullPath = path.join(WORK, 'bundle.full.js');
fs.writeFileSync(fullPath, full);
const out1 = execFileSync(process.execPath, [fullPath]).toString().trim();
console.log(`  bundle 运行输出: "${out1}"（三个模块合成了一个文件，浏览器一次请求就能拿全)`);
console.log(`  bundle 体积: ${full.length} 字节`);
console.log('  → 打包器的运行时(__require/__modules)就是第 14 章模块系统的用户态复刻');

// ---------- ③ tree-shaking: 摇掉没人用的导出 ----------
console.log('\n== ③ tree-shaking：静态分析「谁被用到」，摇掉其余（实测瘦身）==');
function usedExports(graph, entry) {
  const used = new Map([...graph.keys()].map(k => [k, new Set()]));
  const walk = (file) => {
    for (const imp of graph.get(file).imports) {
      const target = path.normalize(imp.from);
      for (const n of imp.names)
        if (!used.get(target).has(n)) {
          used.get(target).add(n);
          walk(target);                            // 被用到的模块，它的 import 也生效
        }
    }
  };
  walk(entry);
  return used;
}

/** 删掉一个导出函数的完整块: 从 "export function name" 起，数大括号直到平衡 */
function dropFunction(source, name) {
  const start = source.indexOf(`export function ${name}`);
  if (start < 0) return source;
  let i = source.indexOf('{', start), depth = 0;
  for (; i < source.length; i++) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}' && --depth === 0) { i++; break; }
  }
  return source.slice(0, start) + source.slice(i);
}

function shake(graph, entry) {
  const used = usedExports(graph, entry);
  const shaken = new Map();
  for (const [file, mod] of graph) {
    let source = mod.source;
    if (file !== entry) {
      // 删掉没被 import 的导出函数（真实工具基于 AST + 作用域分析，这里数括号示意）
      for (const m of [...mod.source.matchAll(/export function (\w+)/g)])
        if (!used.get(file).has(m[1])) source = dropFunction(source, m[1]);
    }
    shaken.set(file, { source, imports: parseImports(source) });
  }
  return shaken;
}

const used = usedExports(graph, 'main.js');
for (const [file, names] of used)
  if (file !== 'main.js')
    console.log(`  ${file} 被用到的导出: {${[...names].join(', ') || ''}}`);
const shaken = bundle(shake(graph, 'main.js'), 'main.js');
const shakenPath = path.join(WORK, 'bundle.shaken.js');
fs.writeFileSync(shakenPath, shaken);
const out2 = execFileSync(process.execPath, [shakenPath]).toString().trim();
console.log(`  摇树后 bundle 运行输出: "${out2}"（结果一致: ${out1 === out2}）`);
console.log(`  体积: ${full.length} → ${shaken.length} 字节（瘦身 ${(100 - 100 * shaken.length / full.length).toFixed(0)}%）`);
console.log('  → matrixMultiply/sub/formatCsv 没人 import → 从产物里【整块消失】');
console.log('  → 死代码消除的模块级版本——它靠的是【静态可分析】的 import/export 结构');

console.log('\n== ④ 为什么 CommonJS 摇不动，ESM 可以 ==');
console.log("  CommonJS: require(变量)、module.exports[key] = ... —— 【运行时】才知道导入导出什么");
console.log("  ESM:      import { add } from './math.js' —— 【语法层面】固定，不执行就能分析");
console.log('  → 这就是 ESM 规范坚持静态结构的原因: 不是为了好看，是为了让工具能做 ③');
console.log('  → 第 47 章的既视感: 声明式（静态可分析）让优化器有牌可打——SQL 与 ESM 同一个道理');

console.log('\n== ⑤ 现代前端构建器在比什么 ==');
console.log('  webpack : JS 写的，插件生态最全——大项目冷启动分钟级');
console.log('  esbuild : Go 写的，并行 + 少遍历——快 10~100x（本例的算法，工业级实现）');
console.log('  vite    : 开发态干脆【不打包】——浏览器原生 ESM 按需加载，秒级启动');
console.log('            生产态仍用 Rollup 打包（HTTP 请求数与压缩率的取舍又回来了）');
console.log('  → 「打包」本身是 HTTP/1.1 时代的产物；工具在快与不做之间演化');

fs.rmSync(WORK, { recursive: true, force: true });
