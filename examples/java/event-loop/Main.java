import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.*;

public class Main {

    /** 手工实现一个单线程事件循环——Netty/Vert.x 的骨架就是这个。 */
    static class EventLoop implements Runnable {
        private final BlockingQueue<Runnable> tasks = new LinkedBlockingQueue<>();
        private final DelayQueue<DelayedTask> timers = new DelayQueue<>();
        private volatile boolean running = true;

        void post(Runnable r) { tasks.offer(r); }

        void postDelayed(Runnable r, long delayMs) {
            timers.offer(new DelayedTask(r, delayMs));
        }

        void stop() { running = false; tasks.offer(() -> {}); }

        @Override public void run() {
            while (running) {
                try {
                    DelayedTask due;                       // ① 先处理到期的定时器
                    while ((due = timers.poll()) != null) due.task.run();
                    Runnable task = tasks.poll(5, TimeUnit.MILLISECONDS);  // ② 再取普通任务
                    if (task != null) task.run();          // ③ 执行到底，不可抢占
                } catch (InterruptedException e) { break; }
            }
        }
    }

    static class DelayedTask implements Delayed {
        final Runnable task;
        final long deadline;
        DelayedTask(Runnable task, long delayMs) {
            this.task = task;
            this.deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(delayMs);
        }
        @Override public long getDelay(TimeUnit unit) {
            return unit.convert(deadline - System.nanoTime(), TimeUnit.NANOSECONDS);
        }
        @Override public int compareTo(Delayed o) {
            return Long.compare(getDelay(TimeUnit.NANOSECONDS), o.getDelay(TimeUnit.NANOSECONDS));
        }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① Java 没有内置事件循环 ==");
        System.out.println("  标准库提供的是线程池（第 45 章），不是事件循环");
        System.out.println("  事件循环由框架提供: Netty 的 EventLoopGroup、Vert.x 的 Verticle");
        System.out.println("  → 这是 Java 与 JS/Python 最大的模型差异");

        System.out.println("\n== ② 钥匙实验：手工搭一个事件循环 ==");
        EventLoop loop = new EventLoop();
        Thread loopThread = new Thread(loop, "event-loop");
        loopThread.start();

        List<String> order = new CopyOnWriteArrayList<>();
        CountDownLatch done = new CountDownLatch(1);

        loop.post(() -> {
            order.add("1. 第一个任务（线程 " + Thread.currentThread().getName() + "）");
            loop.post(() -> order.add("3. 任务里排的新任务（排到队尾）"));
            order.add("2. 第一个任务的剩余部分（不可抢占，必须跑完）");
        });
        loop.post(() -> order.add("4. 第二个任务"));
        loop.postDelayed(() -> order.add("5. 延时任务（定时器队列）"), 20);
        loop.postDelayed(done::countDown, 60);

        done.await(2, TimeUnit.SECONDS);
        order.forEach(line -> System.out.println("    " + line));
        System.out.println("  ↑ 「取一个任务 → 执行到底 → 取下一个」：与 JS/asyncio 同一台引擎");

        System.out.println("\n== ③ 阻塞任务会卡死整个循环（与 JS 同构）==");
        long t0 = System.nanoTime();
        CountDownLatch blockDone = new CountDownLatch(3);
        for (int i = 0; i < 3; i++) {
            loop.post(() -> {
                try { Thread.sleep(30); } catch (InterruptedException ignored) {}   // ⚠️ 阻塞
                blockDone.countDown();
            });
        }
        blockDone.await(2, TimeUnit.SECONDS);
        System.out.printf("  3 个各阻塞 30ms 的任务: %.0f ms（串行 ❌ 单线程循环被占死）%n",
                (System.nanoTime() - t0) / 1e6);
        System.out.println("  → Netty 的铁律：EventLoop 线程上绝不做阻塞操作（丢给业务线程池）");

        loop.stop();
        loopThread.join(1000);

        System.out.println("\n== ④ Netty 的模型：多个事件循环 ==");
        System.out.println("  bossGroup   : 1 个循环，只负责 accept 新连接");
        System.out.println("  workerGroup : N 个循环（N = 核数×2），每个绑定一条线程");
        System.out.println("  一个连接固定归属一个循环 → 该连接的所有事件都在同一条线程上");
        System.out.println("  → 单个连接内无数据竞争（与 JS 同款收益），整体又能吃满多核");
        System.out.printf("  本机核数 = %d，Netty 默认 worker 数会是 %d%n",
                Runtime.getRuntime().availableProcessors(),
                Runtime.getRuntime().availableProcessors() * 2);

        System.out.println("\n== ⑤ 虚拟线程让 Java 走了另一条路（第 44 章）==");
        System.out.println("  事件循环模型: 少量线程 + 回调/异步 → 高吞吐，但代码风格被改变");
        System.out.println("  虚拟线程模型: 海量廉价线程 + 阻塞代码 → 高吞吐，且代码风格不变");
        System.out.println("  → Java 21+ 的新项目可以不再需要 Netty 式的事件循环编程");
    }
}
