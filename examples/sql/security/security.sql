-- 安全：数据库侧的纵深防御——假设应用层已经失守，还能剩下什么。

CREATE TABLE users(
  id INTEGER PRIMARY KEY, tenant_id INTEGER, name TEXT,
  email TEXT, pw_hash TEXT, role TEXT, deleted_at TEXT
);
INSERT INTO users VALUES
 (1,100,'alice','alice@a.com','$argon2id$v=19$m=65536...','user',NULL),
 (2,100,'bob','bob@a.com','$argon2id$v=19$m=65536...','user',NULL),
 (3,200,'carol','carol@b.com','$argon2id$v=19$m=65536...','user',NULL),
 (4,100,'root','root@a.com','$argon2id$v=19$m=65536...','admin',NULL);

CREATE TABLE orders(id INTEGER PRIMARY KEY, tenant_id INTEGER, user_id INTEGER, amount INTEGER);
INSERT INTO orders VALUES (1,100,1,500),(2,100,2,300),(3,200,3,900),(4,100,4,1200);

-- ① 注入之后：攻击者拿到执行权限，能做多少事取决于【这个连接的权限】
SELECT '① 同一条注入，损失取决于数据库账号的权限' AS r;
SELECT '   如果应用用超级用户连接: 可读全部表、可 DROP、可读文件、可能可执行命令' AS r;
SELECT '   如果应用用最小权限账号: 只能碰它被授权的那几张表的那几列' AS r;
SELECT '   → GRANT SELECT, INSERT, UPDATE ON app.orders TO app_user;' AS r;
SELECT '   → 【不要授予 DROP / CREATE / FILE / SUPERUSER】，迁移用另一个账号' AS r;
SELECT '   → 这是纵深防御: 第一道防线（参数化）失守时，第二道决定损失有多大' AS r;

-- ② 视图 + 列级权限：让敏感列根本不在可达范围内
CREATE VIEW users_public AS SELECT id, tenant_id, name, role FROM users WHERE deleted_at IS NULL;
SELECT '② 用视图限制可见列（pw_hash / email 不在视图里）:' AS r;
SELECT '   users_public 的列: ' || group_concat(name, ', ') AS r
  FROM pragma_table_info('users_public');
SELECT '   → 即使这个查询被注入，攻击者也拿不到 pw_hash——它不在授权范围内' AS r;
SELECT '   → PostgreSQL 还支持列级 GRANT: GRANT SELECT (id, name) ON users TO app_user;' AS r;

-- ③ 多租户越权：最常见、也最少被测试的漏洞
SELECT '③ 越权访问（IDOR）: 比注入更常见，且【完全不需要任何特殊字符】' AS r;
SELECT '   租户 100 的用户请求 GET /orders/3（这是租户 200 的订单）:' AS r;
SELECT '   ⓐ 只按 id 查（漏洞写法）→ 返回 ' || COUNT(*) || ' 行，跨租户数据泄露' AS r
  FROM orders WHERE id = 3;
SELECT '   ⓑ 同时按 tenant_id 过滤 → 返回 ' || COUNT(*) || ' 行' AS r
  FROM orders WHERE id = 3 AND tenant_id = 100;
SELECT '   → 参数化查询【完全防不住这个】: 输入是合法的数字 3，没有任何注入' AS r;
SELECT '   → 授权检查必须发生在【每一次数据访问】上，不能只在路由入口检查一次' AS r;
SELECT '   → PostgreSQL 的行级安全(RLS)把这个约束下沉到数据库，应用忘写也不会漏:' AS r;
SELECT '      ALTER TABLE orders ENABLE ROW LEVEL SECURITY;' AS r;
SELECT '      CREATE POLICY t ON orders USING (tenant_id = current_setting(''app.tenant'')::int);' AS r;

-- ④ 密码字段：数据库能帮你把错误变得显眼
SELECT '④ 密码存储的数据库侧约束:' AS r;
SELECT '   存的是 ' || substr(pw_hash,1,20) || '...（算法+参数+盐+哈希，自描述格式）' AS r
  FROM users WHERE name='alice';
SELECT '   → 好的哈希格式【自带算法与参数】，将来升级迭代次数时可以逐个用户平滑迁移' AS r;
SELECT '   → 绝不要存明文，也不要存 MD5/SHA 裸哈希（Python/Java 版实测: 快 6 个数量级）' AS r;
SELECT '   → 也不要给 pw_hash 建索引: 索引不需要，还多一份泄露途径' AS r;

-- ⑤ 审计：谁在什么时候看了什么
CREATE TABLE audit_log(
  id INTEGER PRIMARY KEY, at TEXT, actor INTEGER, action TEXT, target TEXT
);
CREATE TRIGGER audit_role_change AFTER UPDATE OF role ON users
BEGIN
  INSERT INTO audit_log(at, actor, action, target)
  VALUES ('2026-08-13T10:00:00', NEW.id, 'role: '||OLD.role||' → '||NEW.role, NEW.name);
END;
UPDATE users SET role='admin' WHERE name='bob';       -- 一次提权
SELECT '⑤ 用触发器把提权操作记进审计日志:' AS r;
SELECT '   ' || action || '（目标 ' || target || '，时间 ' || at || '）' AS r FROM audit_log;
SELECT '   → 审计日志的价值不在阻止，而在【事后能回答「发生了什么」】' AS r;
SELECT '   → 关键: 审计表要让应用账号【只能 INSERT，不能 UPDATE/DELETE】，否则可被清理' AS r;

-- ⑥ 软删除的安全含义
UPDATE users SET deleted_at='2026-08-13' WHERE name='carol';
SELECT '⑥ 软删除 = 数据还在:' AS r;
SELECT '   users 表里 carol 仍然存在: ' || COUNT(*) || ' 行' AS r FROM users WHERE name='carol';
SELECT '   users_public 视图里已经看不到: ' || COUNT(*) || ' 行' AS r
  FROM users_public WHERE name='carol';
SELECT '   → 任何【忘记加 deleted_at IS NULL】的查询都会泄露已删除数据' AS r;
SELECT '   → 而 GDPR 的「被遗忘权」要求真删除——软删除会让合规变成一个持续的债务' AS r;
SELECT '   → 实践: 用视图或 RLS 强制过滤，别指望每个查询都记得写' AS r;

-- ⑦ 数据库侧安全清单
SELECT '⑦ 清单（按「第一道失守后还剩什么」排序）:' AS r;
SELECT '   ⓐ 应用账号最小权限，迁移账号分离' AS r;
SELECT '   ⓑ 敏感列走视图/列级权限，应用账号根本看不到' AS r;
SELECT '   ⓒ 多租户用 RLS 兜底，不依赖每个查询都记得过滤（③）' AS r;
SELECT '   ⓓ 连接串里的密码走密钥管理服务，不进代码库、不进环境变量明文' AS r;
SELECT '   ⓔ 传输加密(TLS) + 静态加密(TDE)，备份同样要加密' AS r;
SELECT '   ⓕ 审计日志只进不出（⑤）' AS r;
SELECT '   → 注意这六条【没有一条是「防注入」】——那是应用层的事（Python 版实测）' AS r;
SELECT '   → 数据库层的职责是: 假设应用层会出错，把损失控制住' AS r;
