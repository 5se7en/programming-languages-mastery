-- 包管理：数据库世界的版本管理——schema 迁移（Flyway/Alembic 的原理，手写一遍）。

-- ① 数据库的「版本」问题: 代码有 git，schema 有什么？
SELECT '① 代码的版本靠 git 管，数据库的 schema 靠什么管？' AS r;
SELECT '   答案: 迁移(migration)——把 schema 的每次变更写成【带编号的脚本】' AS r;
SELECT '   V1__create_users.sql / V2__add_email.sql / V3__create_orders.sql' AS r;
SELECT '   → schema 的当前状态 = 按序应用所有迁移的结果（事件溯源的思想）' AS r;

-- ② 迁移器的核心: 一张版本表
CREATE TABLE schema_migrations (
  version   INTEGER PRIMARY KEY,
  name      TEXT NOT NULL,
  checksum  TEXT NOT NULL,             -- 脚本内容的指纹（防篡改）
  applied_at TEXT DEFAULT (datetime('now'))
);

-- 「迁移脚本仓库」: 版本号 + 名字 + 内容（真实工具里是文件，这里用表模拟）
CREATE TABLE migration_scripts (version INTEGER, name TEXT, body TEXT);
INSERT INTO migration_scripts VALUES
  (1, 'create_users',  'CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT)'),
  (2, 'add_email',     'ALTER TABLE users ADD COLUMN email TEXT'),
  (3, 'create_orders', 'CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER)');

-- 玩具校验和: 长度 + 首尾字符（真实工具用 CRC32/SHA）
-- 应用迁移 1
CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO schema_migrations (version, name, checksum)
SELECT version, name, length(body) || '-' || substr(body, 1, 6)
FROM migration_scripts WHERE version = 1;

-- 应用迁移 2
ALTER TABLE users ADD COLUMN email TEXT;
INSERT INTO schema_migrations (version, name, checksum)
SELECT version, name, length(body) || '-' || substr(body, 1, 6)
FROM migration_scripts WHERE version = 2;

SELECT '② 版本表——迁移器的全部状态:' AS r;
SELECT '   V' || version || ' ' || name || '  checksum=' || checksum AS r
FROM schema_migrations ORDER BY version;
SELECT '   当前 schema 版本: ' || (SELECT MAX(version) FROM schema_migrations) AS r;

-- ③ 幂等: 重跑迁移器 = 跳过已应用的，只补缺的
SELECT '③ 重跑迁移器（比如换了台机器、CI 里重新部署）:' AS r;
SELECT '   待应用: ' || COALESCE(group_concat('V' || version, ', '), '无')
FROM migration_scripts
WHERE version NOT IN (SELECT version FROM schema_migrations);
SELECT '   → V1、V2 已在版本表里 → 跳过；只有 V3 会被应用' AS r;
-- 应用 V3
CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER);
INSERT INTO schema_migrations (version, name, checksum)
SELECT version, name, length(body) || '-' || substr(body, 1, 6)
FROM migration_scripts WHERE version = 3;
SELECT '   应用后版本: ' || (SELECT MAX(version) FROM schema_migrations)
       || '（再跑一遍就是 0 个待应用——幂等，第 52 章的必测性质）' AS r;

-- ④ 防篡改: checksum 对不上 = 有人改了历史迁移
SELECT '④ 校验历史迁移是否被篡改:' AS r;
UPDATE migration_scripts
SET body = 'CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, is_admin INTEGER)'
WHERE version = 1;                     -- 有人偷偷改了 V1 的脚本！
SELECT '   V' || s.version || ' ' || s.name || ': ' ||
       CASE WHEN m.checksum = length(s.body) || '-' || substr(s.body, 1, 6)
            THEN '✓ 一致'
            ELSE '✗ 【校验失败】——脚本被改过，与已应用的版本不符' END AS r
FROM migration_scripts s JOIN schema_migrations m ON m.version = s.version
ORDER BY s.version;
SELECT '   → 已应用的迁移是【历史】: 改历史 = 新环境和老环境执行不同的脚本' AS r;
SELECT '   → 正确做法: 永远新增 V4 去修正，绝不回头改 V1——和 git 不改已推送提交同理' AS r;

-- ⑤ 迁移与包管理的同构
SELECT '⑤ 迁移器与包管理器是同一个问题的两个面:' AS r;
SELECT '   迁移脚本      ↔ 包的版本      （带编号的不可变产物）' AS r;
SELECT '   schema_migrations ↔ lockfile   （已应用/已解析状态的记录）' AS r;
SELECT '   checksum      ↔ 锁文件里的 hash（防篡改，Python 版 ⑤）' AS r;
SELECT '   幂等重跑      ↔ npm ci        （从记录精确重建状态）' AS r;
SELECT '   → 核心思想相同: 【声明目标状态 + 记录已达状态 + 校验不可变性】' AS r;

-- ⑥ 数据库迁移特有的三条纪律
SELECT '⑥ 三条纪律（数据库比代码难在: 状态回滚不掉）:' AS r;
SELECT '   ① 向后兼容地改: 先加新列 → 双写 → 迁数据 → 切读 → 删旧列（五步走）' AS r;
SELECT '      —— 因为部署期间【新老代码同时在跑】，schema 必须同时伺候两代' AS r;
SELECT '   ② 破坏性操作(DROP/RENAME)与代码发布【分开两次】上线' AS r;
SELECT '   ③ 迁移在生产数据副本上演练（第 52 章: 最容易跳过、事故最疼的测试）' AS r;
