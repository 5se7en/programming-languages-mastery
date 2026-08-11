import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * 事务：手写 undo log 实现原子性与保存点——搞懂 ROLLBACK 到底回滚了什么。
 * （JDBC 需要第三方驱动 jar，故用手写引擎演示原理；末尾给出 JDBC 的对应 API。）
 */
public class Main {

    /** undo 记录：撤销一次写所需的全部信息 */
    record Undo(String key, Integer oldValue) {}   // oldValue 为 null 表示「这个键原本不存在」

    static class TxnStore {
        private final Map<String, Integer> data = new HashMap<>();
        private final Deque<Undo> undoLog = new ArrayDeque<>();
        private final Map<String, Integer> savepoints = new HashMap<>();
        private boolean inTxn = false;

        void put(String k, int v) {
            if (inTxn) undoLog.push(new Undo(k, data.get(k)));   // 先记下旧值，再改
            data.put(k, v);
        }

        Integer get(String k) { return data.get(k); }

        void begin() { inTxn = true; undoLog.clear(); savepoints.clear(); }

        void commit() { inTxn = false; undoLog.clear(); savepoints.clear(); }

        /** 回滚 = 按【逆序】重放 undo 记录 —— 顺序错了结果就是错的 */
        int rollback() {
            int n = undoLog.size();
            while (!undoLog.isEmpty()) applyUndo(undoLog.pop());
            inTxn = false;
            savepoints.clear();
            return n;
        }

        void savepoint(String name) { savepoints.put(name, undoLog.size()); }

        /** 回滚到保存点 = 只撤销「保存点之后」的那些写 */
        int rollbackTo(String name) {
            Integer mark = savepoints.get(name);
            if (mark == null) throw new IllegalArgumentException("没有这个保存点: " + name);
            int n = 0;
            while (undoLog.size() > mark) { applyUndo(undoLog.pop()); n++; }
            return n;
        }

        private void applyUndo(Undo u) {
            if (u.oldValue() == null) data.remove(u.key());
            else data.put(u.key(), u.oldValue());
        }

        int undoSize() { return undoLog.size(); }
    }

    public static void main(String[] args) {
        TxnStore db = new TxnStore();
        db.put("甲", 100);
        db.put("乙", 100);

        System.out.println("== ① 原子性的实现：undo log ==");
        System.out.printf("  初始: 甲=%d 乙=%d%n", db.get("甲"), db.get("乙"));
        db.begin();
        db.put("甲", 40);                       // 转账第一步
        System.out.printf("  事务中转账一半: 甲=%d 乙=%d，undo log 里有 %d 条记录%n",
                db.get("甲"), db.get("乙"), db.undoSize());
        int undone = db.rollback();             // 「崩溃」→ 回滚
        System.out.printf("  ROLLBACK 撤销了 %d 条: 甲=%d 乙=%d%n", undone, db.get("甲"), db.get("乙"));
        System.out.println("  → 「原子」的实现极其朴素: 改之前把旧值抄下来，要回滚就抄回去");
        System.out.println("  → 与第 46 章的 WAL 是【一体两面】: WAL 存新值(redo)，undo log 存旧值");

        System.out.println("\n== ② 回滚必须【逆序】重放（很多人手写事务时踩的坑）==");
        db.begin();
        db.put("甲", 1);
        db.put("甲", 2);
        db.put("甲", 3);
        System.out.printf("  同一个键连改三次: 甲=%d，undo 里 3 条旧值 [100, 1, 2]%n", db.get("甲"));
        db.rollback();
        System.out.printf("  逆序重放 → 甲=%d ✓（若顺序重放会得到 2，错的）%n", db.get("甲"));

        System.out.println("\n== ③ 保存点：事务内的部分回滚 ==");
        db.begin();
        db.put("甲", 50);
        db.savepoint("sp1");
        db.put("乙", 50);
        db.put("丙", 999);                      // 一个原本不存在的键
        System.out.printf("  sp1 之后又改了两处: 甲=%d 乙=%d 丙=%d%n",
                db.get("甲"), db.get("乙"), db.get("丙"));
        int n = db.rollbackTo("sp1");
        System.out.printf("  ROLLBACK TO sp1 撤销 %d 条: 甲=%d 乙=%d 丙=%s%n",
                n, db.get("甲"), db.get("乙"), db.get("丙"));
        db.commit();
        System.out.println("  → sp1【之前】的修改保留，【之后】的被撤销；丙 被删回「不存在」");
        System.out.println("  → 实现只是记住「保存点时 undo log 有多深」，回滚到那个深度为止");

        System.out.println("\n== ④ ACID 四个字母各自靠什么实现 ==");
        System.out.println("  A 原子性 Atomicity   → undo log（本例）/ 回滚段：失败能撤干净");
        System.out.println("  C 一致性 Consistency → 约束（第 46 章实测 CHECK 拦下 -999）+ 你的业务逻辑");
        System.out.println("  I 隔离性 Isolation   → MVCC + 锁（C++ 版手写了 MVCC，第 50 章讲锁）");
        System.out.println("  D 持久性 Durability  → WAL + fsync（第 46 章实测三档: 1.86/26/4399 μs）");
        System.out.println("  → 只有 C 有你的份: A/I/D 是数据库的工作，C 需要你把业务规则声明清楚");

        System.out.println("\n== ⑤ 四个隔离级别与它们放过的异常 ==");
        String[][] table = {
            {"级别", "脏读", "不可重复读", "幻读", "写偏斜"},
            {"READ UNCOMMITTED", "可能", "可能", "可能", "可能"},
            {"READ COMMITTED", "不会", "可能", "可能", "可能"},
            {"REPEATABLE READ", "不会", "不会", "标准允许*", "可能"},
            {"SERIALIZABLE", "不会", "不会", "不会", "不会"},
        };
        for (String[] row : table)
            System.out.printf("  %-18s %-6s %-12s %-12s %s%n", row[0], row[1], row[2], row[3], row[4]);
        System.out.println("  * SQL 标准允许 RR 出现幻读，但 MySQL/InnoDB 的 RR 用间隙锁挡住了幻读");
        System.out.println("    而 PostgreSQL 的 RR 其实是【快照隔离】，也挡住了幻读");
        System.out.println("  → 同一个名字「REPEATABLE READ」，三家的实际行为各不相同");
        System.out.println("  → 写偏斜【只有】SERIALIZABLE 挡得住（JS 版详细演示了它）");

        System.out.println("\n== ⑥ JDBC 里对应的 API ==");
        System.out.println("  conn.setAutoCommit(false)                    ← 关掉自动提交才叫开事务");
        System.out.println("  conn.setTransactionIsolation(Connection.TRANSACTION_REPEATABLE_READ)");
        System.out.println("  Savepoint sp = conn.setSavepoint(\"sp1\"); conn.rollback(sp)");
        System.out.println("  conn.commit() / conn.rollback()             ← 必须放在 finally 或 try-with-resources");
        System.out.println("  ⚠️ 最常见的事故: 忘了 setAutoCommit(false)，每条语句自成一个事务，");
        System.out.println("     转账的两步之间没有任何保护——和第 46 章实测的文件版一样脆");
        System.out.println("  → Spring 的 @Transactional 是这段样板的注解版，但自调用不生效（代理机制）");

        List<String> keys = new ArrayList<>(List.of("甲", "乙"));
        System.out.printf("%n  最终状态: 甲=%d 乙=%d（总额守恒: %s）%n",
                db.get("甲"), db.get("乙"), db.get("甲") + db.get("乙") == 100 ? "是" : "否");
        keys.clear();
    }
}
