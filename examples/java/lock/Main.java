import java.lang.management.ManagementFactory;
import java.lang.management.ThreadInfo;
import java.lang.management.ThreadMXBean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReentrantLock;

public class Main {

    // ---- 跨多变量的不变式：转账必须整体原子 ----
    static class Account {
        final String name;
        int balance;
        final ReentrantLock lock = new ReentrantLock();
        Account(String name, int balance) { this.name = name; this.balance = balance; }
    }

    static final Object LOCK_A = new Object();
    static final Object LOCK_B = new Object();

    static int counter = 0;
    static final AtomicInteger atomicCounter = new AtomicInteger();
    static final Object bigLock = new Object();

    public static void main(String[] args) throws Exception {
        System.out.println("== ① 为什么原子操作不够：跨多变量的不变式 ==");
        Account a = new Account("A", 1000), b = new Account("B", 1000);
        int total0 = a.balance + b.balance;
        System.out.println("  转账前总额 = " + total0);
        // 两个 atomic 变量也保证不了「总额守恒」——中间状态会被看到
        System.out.println("  A 扣 100 与 B 加 100 之间，存在「总额 = 1900」的瞬间");
        System.out.println("  要让外界永远看不到这个瞬间，必须把两步锁在一起（临界区）");

        System.out.println("\n== ② synchronized：把任意代码段变成临界区 ==");
        final int N = 200_000;
        counter = 0;
        Thread t1 = new Thread(() -> { for (int i = 0; i < N; i++) synchronized (bigLock) { counter++; } });
        Thread t2 = new Thread(() -> { for (int i = 0; i < N; i++) synchronized (bigLock) { counter++; } });
        long s = System.nanoTime();
        t1.start(); t2.start(); t1.join(); t2.join();
        double lockMs = (System.nanoTime() - s) / 1e6;
        System.out.printf("  加锁结果 = %d（期望 %d）✅，耗时 %.1f ms%n", counter, 2 * N, lockMs);

        atomicCounter.set(0);
        Thread t3 = new Thread(() -> { for (int i = 0; i < N; i++) atomicCounter.incrementAndGet(); });
        Thread t4 = new Thread(() -> { for (int i = 0; i < N; i++) atomicCounter.incrementAndGet(); });
        s = System.nanoTime();
        t3.start(); t4.start(); t3.join(); t4.join();
        double atomMs = (System.nanoTime() - s) / 1e6;
        System.out.printf("  原子结果 = %d，耗时 %.1f ms%n", atomicCounter.get(), atomMs);
        System.out.printf("  锁比原子慢 %.1f 倍——表达力更强，代价也更高%n", lockMs / atomMs);

        System.out.println("\n== ③ 钥匙实验：亲手制造死锁 ==");
        Thread d1 = new Thread(() -> {
            synchronized (LOCK_A) {
                sleep(100);
                synchronized (LOCK_B) { System.out.println("  d1 拿到了两把锁"); }
            }
        }, "死锁线程-1");
        Thread d2 = new Thread(() -> {
            synchronized (LOCK_B) {                    // ⚠️ 顺序相反！
                sleep(100);
                synchronized (LOCK_A) { System.out.println("  d2 拿到了两把锁"); }
            }
        }, "死锁线程-2");
        d1.setDaemon(true); d2.setDaemon(true);        // daemon：不阻止 JVM 退出
        d1.start(); d2.start();
        Thread.sleep(500);

        System.out.println("  两个线程都在等对方的锁，永远不会有人打印「拿到了两把锁」");
        System.out.println("  d1 状态 = " + d1.getState() + "，d2 状态 = " + d2.getState());

        System.out.println("\n== ④ JVM 能自动检测死锁 ==");
        ThreadMXBean mx = ManagementFactory.getThreadMXBean();
        long[] deadlocked = mx.findDeadlockedThreads();
        if (deadlocked != null) {
            System.out.println("  findDeadlockedThreads() 检测到 " + deadlocked.length + " 个死锁线程：");
            for (ThreadInfo info : mx.getThreadInfo(deadlocked, true, true)) {
                System.out.printf("    「%s」持有 %d 把锁，正在等待 %s（被「%s」持有）%n",
                        info.getThreadName(), info.getLockedMonitors().length,
                        info.getLockName(), info.getLockOwnerName());
            }
        } else {
            System.out.println("  未检测到死锁（本次调度侥幸避开了）");
        }
        System.out.println("  （命令行用 jstack <pid> 也能看到 \"Found one Java-level deadlock\"——见章节 shell 实测）");

        System.out.println("\n== ⑤ 破解死锁：全局锁顺序 ==");
        transfer(a, b, 100);        // 两个方向的转账都按同一顺序取锁
        transfer(b, a, 50);
        System.out.printf("  双向转账后 A=%d, B=%d，总额 = %d（守恒 ✅）%n",
                a.balance, b.balance, a.balance + b.balance);
        System.out.println("  秘诀：永远按同一顺序获取锁（这里按 name 排序）→ 环等待不可能形成");
    }

    /** 按名字排序取锁——破坏死锁的「循环等待」条件。 */
    static void transfer(Account from, Account to, int amount) {
        Account first = from.name.compareTo(to.name) < 0 ? from : to;
        Account second = first == from ? to : from;
        first.lock.lock();
        try {
            second.lock.lock();
            try {
                from.balance -= amount;
                to.balance += amount;
            } finally { second.lock.unlock(); }
        } finally { first.lock.unlock(); }
    }

    static void sleep(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException ignored) {}
    }
}
