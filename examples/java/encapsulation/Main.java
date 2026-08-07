// 第 25 章 · 封装 —— Java 示例
// 运行：javac Main.java && java Main
// Java 有最细致的四级访问控制，但 private 能被反射突破

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class Main {

    // ---------- 不封装的反面教材 ----------
    static class BadAccount {
        public int balance = 100;            // ⚠️ 公开字段

        public void deposit(int n) {
            if (n <= 0) throw new IllegalArgumentException("金额必须为正");
            balance += n;
        }
    }

    // ---------- 封装良好的版本 ----------
    static class Account {
        private int balance = 100;           // 只有本类
        protected int forSubclass;           // 本类 + 子类 + 同包（注意包含同包！）
        int packagePrivate;                  // 不写修饰符 = 包内可见

        public int getBalance() { return balance; }

        // ✅ 有业务含义的操作，而不是裸 setter
        public void deposit(int n) {
            if (n <= 0) throw new IllegalArgumentException("金额必须为正");
            balance += n;
        }

        public void withdraw(int n) {
            if (n > balance) throw new IllegalStateException("余额不足");
            balance -= n;                     // 唯一的修改入口，不变式得到保证
        }
    }

    // ---------- 封装泄漏的演示 ----------
    static class BadRoster {
        private final List<String> items = new ArrayList<>(List.of("Alice", "Bob"));
        public List<String> getItems() { return items; }        // ✗ 返回内部列表本身
    }

    static class GoodRoster {
        private final List<String> items = new ArrayList<>(List.of("Alice", "Bob"));
        public List<String> getItems() {
            return Collections.unmodifiableList(items);          // ✓ 返回不可变视图
        }
        public int size() { return items.size(); }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("=== 1. 不封装的后果：校验形同虚设 ===");
        BadAccount bad = new BadAccount();
        try {
            bad.deposit(-50);
        } catch (IllegalArgumentException e) {
            System.out.println("  bad.deposit(-50)   → " + e.getMessage() + "  ← 正门的校验生效");
        }
        bad.balance = -999;                   // 直接绕过
        System.out.println("  bad.balance = -999 → 余额变成 " + bad.balance + "  ← 从墙上的洞进来了");

        System.out.println("\n=== 2. 封装后：编译器挡住直接访问 ===");
        Account acc = new Account();
        System.out.println("  acc.getBalance() = " + acc.getBalance());
        try {
            acc.withdraw(1000);
        } catch (IllegalStateException e) {
            System.out.println("  acc.withdraw(1000) → " + e.getMessage()
                    + "  ← 不变式 balance >= 0 得到保证");
        }
        // acc.balance = -999;   // 编译错误：balance has private access in Account
        System.out.println("  写 acc.balance = -999 → 编译错误，编译器直接挡住");

        System.out.println("\n=== 3. ⚠️ 但反射能突破 private（实测）===");
        Field f = Account.class.getDeclaredField("balance");
        f.setAccessible(true);                // 关掉访问检查
        f.setInt(acc, -999);
        System.out.println("  Field.setAccessible(true) + setInt(acc, -999)");
        System.out.println("  → acc.getBalance() = " + acc.getBalance() + "  ← 突破成功");
        System.out.println("  → private 是「编译期」强制，运行时反射能绕过（第 30 章）");

        System.out.println("\n  这不是 bug，而是刻意保留的能力：");
        System.out.println("    Spring 的依赖注入、Jackson 的 JSON 序列化、Hibernate 的 ORM");
        System.out.println("    全都依赖反射。封死它等于废掉半个生态。");
        System.out.println("  → 代价就是 private 只能防「意外」，防不了「蓄意」");
        System.out.println("  → Java 9 的模块系统提供了更强封装：未 exports 的包反射也进不去");

        System.out.println("\n=== 4. 四个访问级别 ===");
        System.out.println("  修饰符        本类  同包  子类  其他");
        System.out.println("  private        ✅    ❌    ❌    ❌");
        System.out.println("  (默认/包级)     ✅    ✅    ❌    ❌");
        System.out.println("  protected      ✅    ✅    ✅    ❌   ← 注意：包含同包！");
        System.out.println("  public         ✅    ✅    ✅    ✅");
        System.out.println("  → protected 比「本类+子类」更宽松，这是很多人记错的地方");

        System.out.println("\n=== 5. ⚠️ 封装泄漏：字段私有，但引用漏出去了 ===");
        BadRoster br = new BadRoster();
        br.getItems().add("入侵者");           // 外部直接改了内部列表
        System.out.println("  BadRoster:  外部 add 后内部变成 " + br.getItems());

        GoodRoster gr = new GoodRoster();
        try {
            gr.getItems().add("入侵者");
        } catch (UnsupportedOperationException e) {
            System.out.println("  GoodRoster: 外部 add → UnsupportedOperationException");
            System.out.println("              内部仍是 " + gr.getItems());
        }
        System.out.println("  → 这是最隐蔽的封装泄漏：字段是 private 的，但可变引用漏出去了");

        System.out.println("\n=== 6. getter/setter 的争议 ===");
        System.out.println("  ❌ public void setBalance(int b) { this.balance = b; }");
        System.out.println("     → 没有校验、没有副作用，和公开字段的区别只是多包了一层");
        System.out.println("  ✅ public void deposit(int n) / withdraw(int n)");
        System.out.println("     → 表达业务意图，且能保护不变式");
        System.out.println();
        System.out.println("  判断标准：如果 setter 只是 this.x = x，它就没有存在价值");
        System.out.println("  更好的做法往往是：根本不提供 setter（用 record 做不可变数据）");

        System.out.println("\n=== 7. record：不可变，从根上不需要 setter ===");
        record Point(int x, int y) { }
        Point p = new Point(1, 2);
        System.out.println("  new Point(1, 2) = " + p);
        System.out.println("  → 字段自动是 private final，只有访问器没有 setter");
        System.out.println("  → 不可变对象天生就是封装良好的：没有任何方式能破坏它的状态");

        System.out.println("\n=== 8. 小结 ===");
        System.out.println("  · Java 有最细致的四级访问控制");
        System.out.println("  · 但 private 只是编译期强制，反射能突破（实测已验证）");
        System.out.println("  · 封装防的是意外和误用，并划清可维护的边界");
        System.out.println("  · 暴露操作（deposit/withdraw）优于暴露状态（setBalance）");
        System.out.println("  · 小心封装泄漏：返回内部集合要用 unmodifiableList 或拷贝");
    }
}
