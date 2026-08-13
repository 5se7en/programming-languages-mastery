import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Constructor;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * 依赖注入：手写一个 DI 容器——反射构造 + 自动装配 + 单例 + 循环依赖检测。
 * （第 30 章的反射 + 第 51 章的对象图构建，在这里合成了 Spring 的骨架。）
 */
public class Main {

    @Retention(RetentionPolicy.RUNTIME) @Target(ElementType.TYPE)
    @interface Singleton { }

    // ============ 被注入的业务代码 ============
    interface Repo { String find(int id); }
    interface Mailer { void send(String to, String msg); List<String> sent(); }

    @Singleton
    static class SqlRepo implements Repo {
        static int instances = 0;
        SqlRepo() { instances++; }
        public String find(int id) { return "用户" + id; }
    }

    static class FakeMailer implements Mailer {
        private final List<String> log = new ArrayList<>();
        public void send(String to, String msg) { log.add(to + ": " + msg); }
        public List<String> sent() { return log; }
    }

    /** 注意: 它只声明【需要什么】，从不自己 new —— 这就是「控制反转」 */
    static class UserService {
        private final Repo repo;
        private final Mailer mailer;
        UserService(Repo repo, Mailer mailer) { this.repo = repo; this.mailer = mailer; }
        String notify(int id) {
            String name = repo.find(id);
            mailer.send(name, "你好");
            return name;
        }
    }

    static class ReportService {
        private final Repo repo;
        ReportService(Repo repo) { this.repo = repo; }
        String header() { return "报表: " + repo.find(1); }
    }

    // ============ 微型 DI 容器（约 40 行）============
    static class Container {
        private final Map<Class<?>, Class<?>> bindings = new HashMap<>();  // 接口 → 实现
        private final Map<Class<?>, Object> singletons = new HashMap<>();
        private final Set<Class<?>> building = new LinkedHashSet<>();      // 正在构造 → 用于查环

        <T> void bind(Class<T> iface, Class<? extends T> impl) { bindings.put(iface, impl); }

        @SuppressWarnings("unchecked")
        <T> T get(Class<T> type) {
            Class<?> impl = bindings.getOrDefault(type, type);
            if (singletons.containsKey(impl)) return (T) singletons.get(impl);

            if (!building.add(impl)) {                                     // ③ 循环依赖检测
                List<String> cycle = building.stream().map(Class::getSimpleName).toList();
                throw new IllegalStateException("循环依赖: " + String.join(" → ", cycle)
                        + " → " + impl.getSimpleName());
            }
            try {
                Constructor<?> ctor = impl.getDeclaredConstructors()[0];    // ① 找构造器
                ctor.setAccessible(true);
                Object[] args = new Object[ctor.getParameterCount()];
                for (int i = 0; i < args.length; i++)
                    args[i] = get(ctor.getParameterTypes()[i]);             // ② 递归解析每个参数
                Object instance = ctor.newInstance(args);
                if (impl.isAnnotationPresent(Singleton.class)) singletons.put(impl, instance);
                return (T) instance;
            } catch (IllegalStateException e) {
                throw e;
            } catch (Exception e) {
                throw new RuntimeException("构造 " + impl.getSimpleName() + " 失败", e);
            } finally {
                building.remove(impl);
            }
        }
    }

    // ============ 循环依赖的两个类 ============
    static class A { A(B b) { } }
    static class B { B(A a) { } }

    public static void main(String[] args) {
        System.out.println("== ① 没有 DI 时，依赖是怎么写死的 ==");
        System.out.println("  class UserService {");
        System.out.println("      private Repo repo = new SqlRepo();      // ⚠️ 依赖写死在内部");
        System.out.println("      private Mailer mailer = new SmtpMailer(); // ⚠️ 测试时换不掉");
        System.out.println("  }");
        System.out.println("  → 谁来决定用哪个实现？【类自己】—— 这就是「控制」在类手里");
        System.out.println("  → 后果: 第 52 章实测过——换不进替身，就测不了");

        System.out.println("\n== ② 控制反转：把「决定权」交出去 ==");
        System.out.println("  class UserService {");
        System.out.println("      UserService(Repo repo, Mailer mailer) { ... }  // 只声明【需要什么】");
        System.out.println("  }");
        System.out.println("  → 谁来决定用哪个实现？【调用方/容器】—— 控制反转了");
        System.out.println("  → 「依赖注入」是实现控制反转的一种手法: 通过参数把依赖【递进来】");

        System.out.println("\n== ③ 手工注入（Poor Man's DI）：不用容器也能做 DI ==");
        Repo repo = new SqlRepo();
        FakeMailer mailer = new FakeMailer();
        UserService manual = new UserService(repo, mailer);
        System.out.println("  new UserService(new SqlRepo(), new FakeMailer())");
        System.out.println("  notify(42) → " + manual.notify(42) + "，邮件记录: " + mailer.sent());
        System.out.println("  → 【DI 不等于 DI 容器】: 手工传参就是最纯粹的依赖注入");
        System.out.println("  → 容器解决的是「对象图太大时手工装配太啰嗦」，不是 DI 本身");

        System.out.println("\n== ④ 手写 DI 容器：反射自动装配（实测）==");
        SqlRepo.instances = 0;
        Container c = new Container();
        c.bind(Repo.class, SqlRepo.class);
        c.bind(Mailer.class, FakeMailer.class);

        UserService svc = c.get(UserService.class);
        System.out.println("  container.get(UserService.class) —— 容器做了三件事:");
        System.out.println("    ① 反射找到构造器 UserService(Repo, Mailer)");
        System.out.println("    ② 递归解析每个参数类型（Repo→SqlRepo，Mailer→FakeMailer）");
        System.out.println("    ③ newInstance 把它们装配起来");
        System.out.println("  结果: notify(7) → " + svc.notify(7));

        System.out.println("\n== ⑤ 生命周期：单例 vs 每次新建（实测）==");
        ReportService r1 = c.get(ReportService.class);
        ReportService r2 = c.get(ReportService.class);
        System.out.println("  SqlRepo 标了 @Singleton，被 UserService/ReportService 共用");
        System.out.println("  SqlRepo 实例数: " + SqlRepo.instances + "（三次注入只造了一个）");
        System.out.println("  ReportService 未标注 → 每次新建: r1 == r2 ? " + (r1 == r2));
        System.out.println("  → 生命周期是容器的核心职责之一: 谁该共享、谁该独立");
        System.out.println("  → Spring 默认 singleton，ASP.NET Core 分 Singleton/Scoped/Transient");

        System.out.println("\n== ⑥ 循环依赖：构造器注入让它【构造期】就暴露（实测）==");
        Container c2 = new Container();
        try {
            c2.get(A.class);
            System.out.println("  竟然成功了（不应该）");
        } catch (IllegalStateException e) {
            System.out.println("  container.get(A.class) → ✗ " + e.getMessage());
        }
        System.out.println("  → A 需要 B，B 又需要 A —— 构造器注入下【根本造不出来】，容器立刻报错");
        System.out.println("  → 对比字段注入(@Autowired 打在字段上): 先 new 空对象再塞字段，");
        System.out.println("     循环依赖能「成功」启动，但对象在某一刻是【半成品】——问题推迟到运行时");
        System.out.println("  → 这是构造器注入相对字段注入的最大优势: 把设计问题变成启动失败");

        System.out.println("\n== ⑦ 为什么 Spring 的 @Transactional 自调用不生效（本章解释第 48 章的坑）==");
        Repo raw = new SqlRepo();
        Repo proxied = (Repo) Proxy.newProxyInstance(
                Main.class.getClassLoader(), new Class<?>[]{Repo.class},
                (p, m, a) -> "【代理拦截】" + m.invoke(raw, a));
        System.out.println("  容器注入给你的往往不是原对象，而是一个【代理】:");
        System.out.println("    直接调 raw.find(1)     → " + raw.find(1));
        System.out.println("    调代理 proxied.find(1) → " + proxied.find(1));
        System.out.println("  → 事务/日志/缓存这些横切能力，就是代理在方法前后插入的");
        System.out.println("  → 但类【内部】的 this.method() 走的是原对象，绕过了代理:");
        System.out.println("     所以第 48 章说的「@Transactional 自调用不生效」不是 bug，是代理的必然");
        System.out.println("  → 解法: 拆成两个 bean 互相注入，或注入自己的代理（AopContext）");

        System.out.println("\n== ⑧ DI 与服务定位器：只差一个方向 ==");
        System.out.println("  DI:       UserService(Repo repo)          —— 依赖【被推进来】");
        System.out.println("  服务定位器: Repo repo = Locator.get(Repo.class); —— 依赖【被拉出来】");
        System.out.println("  → 表面上都实现了「不 new 具体类」，但:");
        System.out.println("     DI 的依赖写在【构造器签名】里 —— 编译期可见，测试时必须传");
        System.out.println("     定位器的依赖藏在【方法体】里 —— 看签名不知道它要什么，测试时会漏");
        System.out.println("  → 后者被视为反模式的原因: 它把依赖【隐藏】了，而 DI 把依赖【公开】了");
    }
}
