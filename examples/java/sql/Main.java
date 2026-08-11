import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

/**
 * SQL：声明式思想反哺命令式语言——Java Stream 就是「内存里的 SQL」。
 * 本例不引 JDBC 驱动（第三方 jar），而是证明 SQL 的五个动词已经长进了现代语言。
 */
public class Main {

    record User(int id, String name, String city, int score) {}
    record Order(int id, int userId, int amount) {}

    public static void main(String[] args) {
        final int NU = 10_000, NO = 50_000;
        List<User> users = new ArrayList<>(NU);
        for (int i = 0; i < NU; i++) users.add(new User(i, "user-" + i, "city-" + (i % 10), i % 100));
        List<Order> orders = new ArrayList<>(NO);
        for (int i = 0; i < NO; i++) orders.add(new Order(i, (i * 7919) % 12000, i % 500));

        System.out.println("== Stream 就是内存里的 SQL：五个动词一一对应 ==");
        System.out.println("  WHERE→filter  SELECT→map  ORDER BY→sorted  LIMIT→limit  GROUP BY→groupingBy");

        // ① WHERE + ORDER BY + LIMIT：命令式循环 vs 声明式 Stream
        System.out.println("\n== ① 同一个查询，命令式 vs 声明式 ==");
        long t0 = System.nanoTime();
        List<User> imperative = new ArrayList<>();
        for (User u : users) if (u.score() >= 95) imperative.add(u);      // WHERE
        imperative.sort(Comparator.comparingInt(User::score).reversed()); // ORDER BY DESC
        if (imperative.size() > 3) imperative = imperative.subList(0, 3); // LIMIT 3
        double msImp = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        List<User> declarative = users.stream()
                .filter(u -> u.score() >= 95)                             // WHERE
                .sorted(Comparator.comparingInt(User::score).reversed())  // ORDER BY DESC
                .limit(3)                                                 // LIMIT 3
                .collect(Collectors.toList());
        double msDecl = (System.nanoTime() - t0) / 1e6;
        System.out.printf("  命令式 12 行: %.2f ms；声明式 4 行: %.2f ms（结果一致: %s）%n",
                msImp, msDecl, imperative.equals(declarative));
        System.out.println("  → 「怎么做」写死在循环里 vs 「要什么」交给流水线——SQL 的分野搬到了内存");

        // ② GROUP BY：SQL 里一句话，命令式要一个 Map + 手工累加
        System.out.println("\n== ② GROUP BY city 求平均分 ==");
        Map<String, Double> avgByCity = users.stream()
                .collect(Collectors.groupingBy(User::city, Collectors.averagingInt(User::score)));
        avgByCity.entrySet().stream()
                .sorted(Map.Entry.<String, Double>comparingByValue().reversed()).limit(3)
                .forEach(e -> System.out.printf("  %s 平均分 %.2f%n", e.getKey(), e.getValue()));
        System.out.println("  ↑ 对应 SQL: SELECT city, AVG(score) FROM users GROUP BY city ORDER BY 2 DESC");

        // ③ JOIN：Stream 也能，但要你【亲手选算法】——这正是 SQL 优于 Stream 之处
        System.out.println("\n== ③ JOIN：Stream 暴露了 SQL 替你藏起来的东西 ==");
        t0 = System.nanoTime();
        long nestedCount = orders.stream()
                .filter(o -> users.stream().anyMatch(u -> u.id() == o.userId()))  // 嵌套循环 O(N×M)
                .count();
        double msNested = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        var userIds = users.stream().map(User::id).collect(Collectors.toSet());   // 先建哈希
        long hashCount = orders.stream().filter(o -> userIds.contains(o.userId())).count();
        double msHash = (System.nanoTime() - t0) / 1e6;
        System.out.printf("  嵌套循环写法 anyMatch: %.0f ms%n", msNested);
        System.out.printf("  先建 Set 再 contains:  %.1f ms（快 %.0fx，结果一致: %s）%n",
                msHash, msNested / msHash, nestedCount == hashCount);
        System.out.println("  → Stream 里【你】得选嵌套循环还是哈希；SQL 里【优化器】替你选（C++ 版三种全实现）");
        System.out.println("  → 这就是声明式的赢面: 你只说 JOIN，算法留给看得见统计信息的优化器");

        // ④ 惰性求值：Stream 与 SQL 共享的执行模型
        System.out.println("\n== ④ 惰性求值：filter 不会扫全表就停 ==");
        int[] probed = {0};
        var first = users.stream()
                .peek(u -> probed[0]++)                       // 记录「摸了几行」
                .filter(u -> u.score() == 99)
                .findFirst();
        System.out.printf("  findFirst 只摸了 %d 行就命中（共 %d 行），拿到 %s%n",
                probed[0], NU, first.map(User::name).orElse("无"));
        System.out.println("  → 对应 SQL 的 LIMIT 1：优化器同样会「够了就停」，不会算完再截断");

        // ⑤ 边界：Stream 是内存的，SQL 是磁盘的
        System.out.println("\n== ⑤ 为什么不能用 Stream 取代数据库 ==");
        System.out.println("  Stream 处理的是【已在内存里】的集合——数据得先全捞进来（第 46 章实测很贵）");
        System.out.println("  SQL 处理的是【磁盘上】的表——只把结果捞回来，且有索引/事务/持久化");
        System.out.println("  → Stream 学到了 SQL 的【声明式表达】，学不到它的【存储与优化】");
        System.out.println("  → 生产: JDBC(java.sql.*) 把查询下推给数据库，只用 Stream 处理返回的小结果集");
    }
}
