// SQL：C# 把 SQL 直接【编进了语言】——LINQ 是五语言里最彻底的致敬。
using System.Diagnostics;
using System.Linq.Expressions;

record User(int Id, string Name, string City, int Score);
record Order(int Id, int UserId, int Amount);

class Program
{
    static void Main()
    {
        const int NU = 10_000, NO = 50_000;
        var users = Enumerable.Range(0, NU)
            .Select(i => new User(i, $"user-{i}", $"city-{i % 10}", i % 100)).ToList();
        var orders = Enumerable.Range(0, NO)
            .Select(i => new Order(i, (i * 7919) % 12000, i % 500)).ToList();

        Console.WriteLine("== ① C# 有【两套】写法，编译成同一个东西 ==");
        var querySyntax =                                   // 查询语法：几乎就是 SQL
            from u in users
            where u.Score >= 95
            orderby u.Score descending
            select u.Name;
        var methodSyntax = users                            // 方法语法：链式调用
            .Where(u => u.Score >= 95)
            .OrderByDescending(u => u.Score)
            .Select(u => u.Name);
        Console.WriteLine($"  查询语法 from/where/orderby/select 与方法语法结果一致: "
                          + $"{querySyntax.Take(3).SequenceEqual(methodSyntax.Take(3))}");
        Console.WriteLine($"  前 3 名: {string.Join(", ", querySyntax.Take(3))}");
        Console.WriteLine("  → 查询语法是【语法糖】，编译器直接翻译成方法调用——C# 3.0(2007) 为 LINQ 而生");

        Console.WriteLine("\n== ② LINQ 的真正杀手锏：表达式树 ==");
        Func<User, bool> compiled = u => u.Score > 90;                    // 委托: 编译成 IL，只能【执行】
        Expression<Func<User, bool>> tree = u => u.Score > 90;            // 表达式树: 保留【结构】
        Console.WriteLine($"  委托只能调用: compiled(users[95]) = {compiled(users[95])}");
        Console.WriteLine($"  表达式树可以【读】: {tree}");
        Console.WriteLine($"    根节点类型: {tree.Body.NodeType}");
        var be = (BinaryExpression)tree.Body;
        Console.WriteLine($"    左边: {be.Left}（成员访问）  右边: {be.Right}（常量）");
        Console.WriteLine("  → EF Core 正是【读】这棵树，把它翻译成 SQL 的 WHERE score > 90");
        Console.WriteLine("  → 这是 C# 相对 Java Stream 的独有能力: 查询能被【转译】而不只是被执行");

        Console.WriteLine("\n== ③ IEnumerable vs IQueryable：在哪儿执行的分水岭 ==");
        Console.WriteLine("  IEnumerable<T>.Where(谓词是 Func)   → 数据先拉到内存，在【本地】过滤");
        Console.WriteLine("  IQueryable<T>.Where(谓词是 Expression) → 翻译成 SQL，在【数据库】过滤");
        Console.WriteLine("  → 一个 .AsEnumerable() 放错位置，就把「数据库过滤」变成「全表拉回内存过滤」");
        Console.WriteLine("  → 第 46 章实测过这个代价: 拉全表 109ms + 46MB vs 让数据库查 9μs");

        Console.WriteLine("\n== ④ JOIN：LINQ 的 join 是哈希连接 ==");
        var sw = Stopwatch.StartNew();
        var nested = orders.Where(o => users.Any(u => u.Id == o.UserId)).Count();   // O(N×M)
        double msNested = sw.Elapsed.TotalMilliseconds;
        sw = Stopwatch.StartNew();
        var joined = orders.Join(users, o => o.UserId, u => u.Id, (o, u) => o).Count();
        double msJoin = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  Any() 嵌套循环写法: {msNested:F0} ms");
        Console.WriteLine($"  Join() 哈希连接:    {msJoin:F1} ms（快 {msNested / msJoin:F0}x，结果一致: {nested == joined}）");
        Console.WriteLine("  → LINQ 的 Join 内部建哈希表——与 C++ 版实测的 Hash Join 同一个算法");
        Console.WriteLine("  → 但它【只会】哈希连接；SQL 优化器会在三种算法间按数据量选（C++ 版三种全实现）");

        Console.WriteLine("\n== ⑤ GroupBy 与延迟执行 ==");
        var byCity = users.GroupBy(u => u.City)
                          .Select(g => new { City = g.Key, Avg = g.Average(u => u.Score) })
                          .OrderByDescending(x => x.Avg).Take(3);
        foreach (var x in byCity) Console.WriteLine($"  {x.City} 平均分 {x.Avg:F2}");
        int probed = 0;
        var lazy = users.Select(u => { probed++; return u; }).Where(u => u.Score == 99);
        Console.WriteLine($"  构造查询后还没执行: 已摸 {probed} 行");
        var firstHit = lazy.First();
        Console.WriteLine($"  调用 First() 才执行，且只摸了 {probed} 行就命中 {firstHit.Name}");
        Console.WriteLine("  → 延迟执行 + 短路，与 SQL 的 LIMIT 1 同一个思想（Java Stream 版实测同款）");

        Console.WriteLine("\n== ⑥ 五语言的声明式查询谱系 ==");
        Console.WriteLine("  SQL    : 声明式的原点（1974 年 SEQUEL）——作用于【磁盘上的表】");
        Console.WriteLine("  C#     : LINQ 把 SQL 编进语言 + 表达式树可【转译回 SQL】（2007）");
        Console.WriteLine("  Java   : Stream 借走了动词，但 lambda 无法内省 → 转译不回 SQL（2014）");
        Console.WriteLine("  JS     : 数组的 filter/map/reduce（早于两者），无 GROUP BY/JOIN 内建");
        Console.WriteLine("  Python : 列表推导 + itertools/pandas；pandas 的 API 明显是 SQL 的形状");
        Console.WriteLine("  → SQL 影响了所有语言的集合 API——它是唯一「反向输出」范式的领域语言");
    }
}
