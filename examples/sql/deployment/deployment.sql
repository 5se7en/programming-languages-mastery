-- 部署：数据库是唯一【无法回滚】的那一部分——所以变更必须设计成向前兼容的。

-- ① 迁移的最小机制：一张记录「已经跑到哪」的表
CREATE TABLE schema_migrations(version TEXT PRIMARY KEY, applied_at TEXT, checksum TEXT);
CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, email TEXT);
INSERT INTO users VALUES (1,'alice','alice@a.com'),(2,'bob','bob@a.com');
INSERT INTO schema_migrations VALUES
  ('001_create_users','2026-08-01T10:00:00','a1b2c3'),
  ('002_add_email',   '2026-08-05T14:30:00','d4e5f6');

SELECT '① 迁移状态由数据库自己记录，而不是由「谁记得跑过」决定:' AS r;
SELECT '   已应用 ' || version || ' @ ' || applied_at AS r FROM schema_migrations ORDER BY version;
SELECT '   → 部署时工具比对「文件里有哪些迁移」与「表里记了哪些」，只跑差集' AS r;
SELECT '   → checksum 的作用: 检测【已应用的迁移文件被改动过】——' AS r;
SELECT '     那意味着开发机和生产库的 schema 已经悄悄分叉了' AS r;
SELECT '   → 这和第 53 章的 lockfile 是同一个思路: 把「当前状态」变成可校验的事实' AS r;

-- ② 为什么数据库变更不能像代码一样回滚
SELECT '② 代码回滚 = 换回旧产物；数据库回滚【没有这么便宜】:' AS r;
SELECT '   ⓐ DROP COLUMN 之后，那一列的数据【已经没了】，回滚只能恢复结构，恢复不了内容' AS r;
SELECT '   ⓑ 大表上的 ALTER 可能跑几十分钟，回滚也要同样久（期间系统处于中间状态）' AS r;
SELECT '   ⓒ 回滚脚本几乎从不被测试 —— 需要它的时候是你第一次运行它' AS r;
SELECT '   → 所以真正的策略不是「写好回滚脚本」，而是【让变更不需要回滚】' AS r;
SELECT '   → 即: 每一步都同时兼容【新旧两个版本的应用代码】' AS r;

-- ③ 扩展-收缩（expand-contract）：一次重命名要分四次发布
SELECT '③ 把 users.name 改名为 users.full_name —— 危险做法与安全做法:' AS r;
SELECT '   ❌ 危险: ALTER TABLE users RENAME COLUMN name TO full_name;' AS r;
SELECT '      滚动发布期间新旧代码同时在跑（Python 版 ⑥），旧代码找不到 name 列 → 全部报错' AS r;
SELECT '   ✅ 安全: 拆成四次发布，每一次都【新旧代码都能工作】:' AS r;

-- 发布 1：扩展（只加不减）
ALTER TABLE users ADD COLUMN full_name TEXT;
SELECT '      发布 1【扩展】 加列 full_name（可空）。旧代码看不见它，照常工作' AS r;

-- 发布 2：双写 + 回填
UPDATE users SET full_name = name WHERE full_name IS NULL;
SELECT '      发布 2【双写】 新代码同时写 name 和 full_name，并回填历史数据' AS r;
SELECT '        回填后两列一致的行数: ' || COUNT(*) AS r FROM users WHERE full_name = name;

-- 发布 3：切读
SELECT '      发布 3【切读】 新代码改为只读 full_name（仍然双写，保证可回滚到发布 2）' AS r;
SELECT '        按新列读到: ' || group_concat(full_name, ', ') AS r FROM users;

-- 发布 4：收缩
SELECT '      发布 4【收缩】 确认没有代码再用 name，才 DROP COLUMN name' AS r;
SELECT '   → 四次发布，每一次的【前后两个状态都能同时服务新旧代码】' AS r;
SELECT '   → 代价是慢（要等几个发布周期），收益是【任何一步都可以停下来或回退】' AS r;
SELECT '   → 这个模式适用于一切破坏性变更: 改列名、改类型、拆表、改约束' AS r;

-- ④ 哪些变更是安全的
SELECT '④ 变更的安全等级:' AS r;
SELECT '   ✅ 安全（旧代码不受影响）: 加可空列、加新表、加索引(并发方式)、放宽约束' AS r;
SELECT '   ⚠️ 危险（旧代码会崩）:    删列、改列名、改类型、收紧约束、加 NOT NULL 无默认值' AS r;
SELECT '   → 判据很简单: 【旧版本的代码在变更之后还能不能正常工作？】' AS r;
SELECT '   → 加索引也要小心: 不带 CONCURRENTLY 的 CREATE INDEX 会锁表（第 49/50 章）' AS r;

-- ⑤ 数据迁移与 schema 迁移是两回事
CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER, amount_cents INTEGER);
INSERT INTO orders VALUES (1,1,50000),(2,2,30000);
SELECT '⑤ schema 迁移（改结构）vs 数据迁移（改内容）:' AS r;
SELECT '   schema 迁移: 快、原子、可以在发布流程里同步跑' AS r;
SELECT '   数据迁移:   可能要处理上亿行，【必须分批、可中断、可重入】' AS r;
SELECT '   → 反例: 一条 UPDATE 扫全表 —— 锁住整张表几十分钟，等于计划内的停机' AS r;
SELECT '   → 正确做法: 后台任务按主键分批推进，记录进度，失败可从断点续跑' AS r;
SELECT '     UPDATE orders SET ... WHERE id BETWEEN ? AND ? （每批几千行，跑完记录位置）' AS r;
SELECT '   → 当前数据: ' || COUNT(*) || ' 行订单，金额以【分】为单位存储' AS r FROM orders;
SELECT '   → 顺带一条设计经验: 钱用整数存分，别用浮点（第 9 章实测过 0.1+0.2）' AS r;

-- ⑥ 发布顺序：谁先谁后
SELECT '⑥ 一次含数据库变更的发布，正确顺序:' AS r;
SELECT '   ⓐ 先跑【扩展类】迁移（加列/加表）——此时线上还是旧代码，不受影响' AS r;
SELECT '   ⓑ 再滚动发布新代码 —— 新旧代码此时同时在跑，两者都能工作' AS r;
SELECT '   ⓒ 观察一段时间（金丝雀/指标），确认没问题' AS r;
SELECT '   ⓓ 最后才跑【收缩类】迁移（删列），且要在【下一个发布周期】' AS r;
SELECT '   → 关键: ⓐ 和 ⓓ 之间隔着至少一个完整的发布周期，' AS r;
SELECT '     这段时间就是你的【回滚窗口】——在它关闭之前，随时可以退回旧代码' AS r;
SELECT '   ⚠️ 最常见的事故: 把 ⓐ 和 ⓓ 合并成一次发布，回滚窗口直接消失' AS r;

-- ⑦ 备份：唯一真正的兜底
SELECT '⑦ 关于备份，只有一条规则:' AS r;
SELECT '   【没有演练过恢复的备份，不叫备份】' AS r;
SELECT '   要定期回答的三个问题:' AS r;
SELECT '     RPO（能接受丢多少数据）→ 决定备份频率与是否需要 WAL 归档（第 46 章）' AS r;
SELECT '     RTO（能接受多久恢复）  → 决定用全量还是增量、要不要热备' AS r;
SELECT '     恢复演练做过没有       → 决定前两个数字是不是真的' AS r;
SELECT '   → 与第 58 章的密钥轮换同理: 没演练过的流程，在真正需要时不会成功' AS r;
SELECT '   → 也与第 52 章同理: 【没被执行过的代码路径，就是没写过】' AS r;
