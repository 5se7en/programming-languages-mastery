import java.lang.management.ManagementFactory;
import java.lang.management.ThreadInfo;
import java.lang.management.ThreadMXBean;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReentrantLock;

/**
 * 数据库锁：造一个真死锁，让 JVM 自己检测出来——并对比第 48 章「检测不到」的那一种。
 */
public class Main {

    static final ReentrantLock rowA = new ReentrantLock();
    static final ReentrantLock rowB = new ReentrantLock();

    public static void main(String[] args) throws Exception {
        ThreadMXBean mx = ManagementFactory.getThreadMXBean();

        System.out.println("== ① 造一个死锁：两个「事务」以相反顺序加锁 ==");
        System.out.println("  T1: 锁 行A → 想要 行B      T2: 锁 行B → 想要 行A");
        CountDownLatch bothHold = new CountDownLatch(2);

        Thread t1 = new Thread(() -> {
            rowA.lock();
            bothHold.countDown();
            try { bothHold.await(); } catch (InterruptedException ignored) { }
            rowB.lock();                          // ← 永远等不到
            rowB.unlock();
            rowA.unlock();
        }, "事务-T1");

        Thread t2 = new Thread(() -> {
            rowB.lock();
            bothHold.countDown();
            try { bothHold.await(); } catch (InterruptedException ignored) { }
            rowA.lock();                          // ← 永远等不到
            rowA.unlock();
            rowB.unlock();
        }, "事务-T2");

        t1.setDaemon(true); t2.setDaemon(true);
        t1.start(); t2.start();
        Thread.sleep(500);                        // 给它们时间走进死锁

        System.out.println("\n== ② JVM 自动检测：findDeadlockedThreads() ==");
        long[] ids = mx.findDeadlockedThreads();
        if (ids != null) {
            System.out.printf("  ⚠️ 检测到 %d 个线程互相死锁:%n", ids.length);
            for (ThreadInfo info : mx.getThreadInfo(ids, true, true)) {
                System.out.printf("    %s 状态=%s%n", info.getThreadName(), info.getThreadState());
                System.out.printf("      正在等待: %s%n", info.getLockInfo());
                System.out.printf("      被谁持有: %s%n", info.getLockOwnerName());
            }
            System.out.println("  → 等待图里有环 → 死锁成立。这正是 C++ 版手写的那个算法");
        } else {
            System.out.println("  没检测到（不应该）");
        }

        System.out.println("\n== ③ 与第 48 章的对比：两种「卡住」，只有一种能被检测 ==");
        System.out.println("  本章的死锁   : 两个线程各持一把锁，互等对方 → 等待图【有环】→ 可检测 ✓");
        System.out.println("  第 45 章的饥饿: 线程在等【任务】，任务在等【线程】→ 没有锁的环 → 检测不到 ✗");
        System.out.println("  第 48 章的写偏斜: 两个事务根本没冲突，只是逻辑上违反了约束 → 更不可检测 ✗");
        System.out.println("  → 「数据库能自动处理死锁」这句话只覆盖第一种；后两种要靠你自己");

        System.out.println("\n== ④ 破坏循环等待：固定加锁顺序（实测无死锁）==");
        // ⚠️ 必须用【新的锁】: 上面那两把已被死锁线程永久持有，谁碰谁挂
        final ReentrantLock rowC = new ReentrantLock(), rowD = new ReentrantLock();
        final int ROUNDS = 20000;
        AtomicInteger done = new AtomicInteger();
        Runnable ordered = () -> {
            for (int i = 0; i < ROUNDS; i++) {
                rowC.lock();                      // ← 所有线程都【先 C 后 D】
                try {
                    rowD.lock();
                    try { done.incrementAndGet(); } finally { rowD.unlock(); }
                } finally { rowC.unlock(); }
            }
        };
        Thread o1 = new Thread(ordered), o2 = new Thread(ordered);
        o1.setDaemon(true); o2.setDaemon(true);
        long t0 = System.nanoTime();
        o1.start(); o2.start();
        o1.join(5000); o2.join(5000);
        double ms = (System.nanoTime() - t0) / 1e6;
        System.out.printf("  两线程各做 %d 次「双行加锁」，固定顺序: %.0f ms，完成 %d 次%n",
                ROUNDS, ms, done.get());
        System.out.printf("  期间新增死锁: %s%n",
                mx.findDeadlockedThreads().length == ids.length
                        ? "✓ 无（顺序一致就不可能有环；仍在的是 ① 里那两条）" : "有");
        System.out.println("  → 转账场景的正确写法: 永远【按账号 id 从小到大】加锁");
        System.out.println("     而不是「先扣款方，后收款方」——后者的顺序取决于数据，必然出现相反的组合");

        System.out.println("\n== ⑤ 超时退出：拿不到锁就放弃（另一种活路）==");
        ReentrantLock busy = new ReentrantLock();
        busy.lock();
        Thread waiter = new Thread(() -> {
            try {
                boolean got = busy.tryLock(200, TimeUnit.MILLISECONDS);
                System.out.printf("  tryLock(200ms) 结果: %s%n",
                        got ? "拿到了" : "✓ 超时放弃（不会永久卡住）");
                if (got) busy.unlock();
            } catch (InterruptedException ignored) { }
        });
        waiter.start(); waiter.join();
        busy.unlock();
        System.out.println("  → 对应数据库的 innodb_lock_wait_timeout（MySQL 默认 50 秒）");
        System.out.println("  → 超时后你会收到一个【可重试】的错误，而不是永久挂起");

        System.out.println("\n== ⑥ JDBC 侧的锁 API ==");
        System.out.println("  SELECT ... FOR UPDATE  → 加 X 锁（读的时候就锁住，防写偏斜，第 48 章）");
        System.out.println("  SELECT ... FOR SHARE   → 加 S 锁（允许别人也读，但不许写）");
        System.out.println("  ⚠️ 两者都【必须在事务里】用，否则语句一结束锁就没了");
        System.out.println("  Statement.setQueryTimeout(n) → 语句级超时，与锁超时是两回事");
        System.out.println("  捕获死锁: SQLException.getSQLState() == \"40001\"（序列化失败，可重试）");
        System.out.println("  → 与第 48 章呼应: 只重试【可重试】的错误，且要指数退避");
    }
}
