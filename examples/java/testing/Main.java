import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/**
 * 测试：手写一个微型 JUnit——自动发现、隔离运行、失败报告，三件事各 10 行。
 * （第 30 章的反射 + 第 51 章的注解扫描，在这里合成了测试框架的骨架。）
 */
public class Main {

    @Retention(RetentionPolicy.RUNTIME) @Target(ElementType.METHOD)
    @interface Test { }

    @Retention(RetentionPolicy.RUNTIME) @Target(ElementType.METHOD)
    @interface Before { }

    // ---------- 微型断言库 ----------
    static void assertEquals(Object expected, Object actual) {
        if (!expected.equals(actual))
            throw new AssertionError("期望 " + expected + "，实际 " + actual);
    }

    // ---------- 微型测试运行器：JUnit 的三件核心工作 ----------
    record TestResult(String name, boolean passed, String message) {}

    static List<TestResult> runTests(Class<?> testClass, boolean freshInstance) throws Exception {
        List<TestResult> results = new ArrayList<>();
        Object shared = testClass.getDeclaredConstructor().newInstance();

        // getDeclaredMethods() 的顺序是【不确定的】——按名字排序保证可复现
        Method[] methods = testClass.getDeclaredMethods();
        java.util.Arrays.sort(methods, java.util.Comparator.comparing(Method::getName));

        for (Method m : methods) {
            if (!m.isAnnotationPresent(Test.class)) continue;        // ① 自动发现: 反射扫 @Test

            // ② 隔离运行: 每个测试一个【新实例】（JUnit 的默认行为）
            Object instance = freshInstance
                    ? testClass.getDeclaredConstructor().newInstance()
                    : shared;

            for (Method b : testClass.getDeclaredMethods())          // @Before 先跑
                if (b.isAnnotationPresent(Before.class)) b.invoke(instance);

            try {
                m.invoke(instance);
                results.add(new TestResult(m.getName(), true, ""));
            } catch (InvocationTargetException e) {                  // ③ 失败报告
                results.add(new TestResult(m.getName(), false, e.getCause().getMessage()));
            }
        }
        return results;
    }

    static void report(List<TestResult> results) {
        long passed = results.stream().filter(TestResult::passed).count();
        for (TestResult r : results)
            System.out.printf("    %s %s%s%n", r.passed() ? "✓" : "✗", r.name(),
                    r.passed() ? "" : " —— " + r.message());
        System.out.printf("    %d/%d 通过%n", passed, results.size());
    }

    // ============ 被测代码 ============
    static class Cart {
        private final List<Integer> items = new ArrayList<>();
        void add(int price) { items.add(price); }
        int total() { return items.stream().mapToInt(Integer::intValue).sum(); }
        int count() { return items.size(); }
    }

    // ============ 测试类 A: 正常的测试 ============
    public static class CartTest {
        Cart cart;

        @Before public void setup() { cart = new Cart(); }

        @Test public void 空购物车总价为零() { assertEquals(0, cart.total()); }

        @Test public void 加两件商品() {
            cart.add(30); cart.add(70);
            assertEquals(100, cart.total());
            assertEquals(2, cart.count());
        }

        @Test public void 故意失败的测试() {
            cart.add(50);
            assertEquals(60, cart.total());              // 故意写错，看失败报告长什么样
        }
    }

    // ============ 测试类 B: 依赖共享状态的测试（用来演示隔离的必要性）============
    public static class SharedStateTest {
        static int seq = 0;                               // ⚠️ 静态字段——实例隔离救不了它
        final Cart cart = new Cart();                     // 实例字段——隔离能救

        @Test public void t1_往购物车加一件() {
            cart.add(10);
            assertEquals(1, cart.count());                // 新实例时必过；共享实例时看运气
        }

        @Test public void t2_购物车应当是空的() {
            assertEquals(0, cart.count());                // 共享实例时: t1 先跑就挂
        }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① 测试框架的三件核心工作（手写版，各约 10 行）==");
        System.out.println("  ① 自动发现: 反射扫描 @Test 注解（第 30/51 章的同一招）");
        System.out.println("  ② 隔离运行: 每个测试方法 new 一个新实例");
        System.out.println("  ③ 失败报告: 捕获 AssertionError，汇总通过/失败");
        System.out.println();
        System.out.println("  跑 CartTest:");
        report(runTests(CartTest.class, true));
        System.out.println("  → 「故意失败的测试」没有中断其他测试——每个测试独立成败");
        System.out.println("  → 这就是 JUnit/pytest/xUnit 的骨架；其余是它们的报告与生态");

        System.out.println("\n== ② 为什么每个测试要一个新实例（实测对比）==");
        System.out.println("  共享一个实例跑 SharedStateTest（已按名字排序: t1 先跑）:");
        report(runTests(SharedStateTest.class, false));
        System.out.println("  每个测试一个新实例跑同样的测试:");
        SharedStateTest.seq = 0;
        report(runTests(SharedStateTest.class, true));
        System.out.println("  → 共享实例时 t2 的成败取决于【t1 是否先跑】——测试顺序耦合");
        System.out.println("  → 顺序耦合的测试是最难缠的一类: 单独跑全过，一起跑就挂（或反过来）");
        System.out.println("  → JUnit 默认每方法新实例，正是用【隔离】买【确定性】");

        System.out.println("\n== ③ 但隔离不是免费的，也不是万能的 ==");
        System.out.println("  不免费: 每个测试 new 实例 + 跑 @Before —— 昂贵的准备要用 @BeforeAll 共享");
        System.out.println("  不万能: static 字段、单例、文件、数据库——【实例外】的状态隔离不掉");
        System.out.println("  → SharedStateTest.seq 是 static 的: 就算每次新实例，它也会跨测试残留");
        System.out.println("  → 测试污染的排查套路: 单独跑挂掉的测试——单独过、一起挂 = 有人污染了共享状态");

        System.out.println("\n== ④ 测试命名：失败信息应该像一句需求 ==");
        System.out.println("  差: test1 / testCart / testTotal   ← 挂了以后你得读代码才知道坏了什么");
        System.out.println("  好: 空购物车总价为零 / VIP享受七折  ← 失败列表本身就是坏掉的需求清单");
        System.out.println("  → 测试有两个读者: 机器（判对错）和【下一个改代码的人】（读行为规格）");

        System.out.println("\n== ⑤ Java 测试生态对号入座 ==");
        System.out.println("  JUnit 5   : @Test/@BeforeEach/@AfterEach —— 本例手写的就是它的骨架");
        System.out.println("  AssertJ   : assertThat(total).isEqualTo(100) —— 流式断言，失败信息更好");
        System.out.println("  Mockito   : mock(Gateway.class) —— Python 版实测过 mock 的两面性");
        System.out.println("  Testcontainers: 在 Docker 里起真数据库 —— 集成层测试的现代标配");
        System.out.println("  JaCoCo    : 覆盖率报告 —— Python 版实测过它能被弱断言骗过");
    }
}
