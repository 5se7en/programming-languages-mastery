// ORM：手写一个「表达式树 → SQL」翻译器——EF Core 的核心原理，一百行讲清楚。
using System.Diagnostics;
using System.Linq.Expressions;
using System.Text;

record User(int Id, string Name, string City, int Score);

/// 把 LINQ 表达式树翻译成 SQL —— 这正是 IQueryable 背后发生的事
static class SqlTranslator
{
    public static string Translate(Expression expr) => expr switch
    {
        BinaryExpression b =>
            $"({Translate(b.Left)} {Op(b.NodeType)} {Translate(b.Right)})",
        MemberExpression m when m.Expression is ParameterExpression =>
            m.Member.Name,                                     // u.Score → Score
        ConstantExpression c =>
            c.Value is string s ? $"'{s}'" : c.Value?.ToString() ?? "NULL",
        MethodCallExpression mc when mc.Method.Name == "StartsWith" =>
            $"{Translate(mc.Object!)} LIKE {Translate(mc.Arguments[0])[..^1]}%'",
        MethodCallExpression mc when mc.Method.Name == "Contains" =>
            $"{Translate(mc.Object!)} LIKE '%{Translate(mc.Arguments[0]).Trim('\'')}%'",
        UnaryExpression u => Translate(u.Operand),
        LambdaExpression l => Translate(l.Body),
        _ => throw new NotSupportedException($"翻译不了: {expr.NodeType} ({expr})")
    };

    static string Op(ExpressionType t) => t switch
    {
        ExpressionType.Equal => "=",
        ExpressionType.NotEqual => "<>",
        ExpressionType.GreaterThan => ">",
        ExpressionType.GreaterThanOrEqual => ">=",
        ExpressionType.LessThan => "<",
        ExpressionType.AndAlso => "AND",
        ExpressionType.OrElse => "OR",
        _ => throw new NotSupportedException(t.ToString())
    };
}

class Program
{
    static void Main()
    {
        var users = Enumerable.Range(0, 100_000)
            .Select(i => new User(i, $"user-{i}", $"city-{i % 50}", i % 100)).ToList();

        Console.WriteLine("== ① 把 C# 的 lambda 翻译成 SQL（实测）==");
        Console.WriteLine("  你写的 LINQ           →  ORM 生成的 SQL");
        var cases = new (string desc, Expression<Func<User, bool>> pred)[]
        {
            ("u => u.Score > 90",                        u => u.Score > 90),
            ("u => u.City == \"city-3\"",                u => u.City == "city-3"),
            ("u => u.Score > 90 && u.City == \"city-3\"",u => u.Score > 90 && u.City == "city-3"),
            ("u => u.Name.StartsWith(\"user-1\")",       u => u.Name.StartsWith("user-1")),
        };
        foreach (var (desc, pred) in cases)
            Console.WriteLine($"  {desc,-42} → WHERE {SqlTranslator.Translate(pred)}");
        Console.WriteLine("  → 这一百行就是 EF Core 的核心: 【读】表达式树，而不是【执行】它");
        Console.WriteLine("  → 第 47 章讲过它的前提: 只有 Expression<Func<>> 保留了语法结构");

        Console.WriteLine("\n== ② 翻译不了的时候会发生什么 ==");
        Expression<Func<User, bool>> hard = u => MyCustomCheck(u.Name);
        try
        {
            Console.WriteLine("  尝试翻译 u => MyCustomCheck(u.Name) ...");
            Console.WriteLine("  " + SqlTranslator.Translate(hard));
        }
        catch (NotSupportedException e)
        {
            Console.WriteLine($"  ✗ 抛出异常: {e.Message.Split('(')[0].Trim()}");
        }
        Console.WriteLine("  → 真实 ORM 有两种反应:");
        Console.WriteLine("     EF Core 3.0+ : 直接【报错】——逼你自己决定怎么办（好设计）");
        Console.WriteLine("     EF Core 2.x  : 【静默降级】为客户端求值 = 悄悄把全表拉回内存");
        Console.WriteLine("     → 后者制造过无数生产事故，所以 3.0 把它改成了默认报错");

        Console.WriteLine("\n== ③ IQueryable vs IEnumerable：过滤发生在哪一端 ==");
        Console.WriteLine("  ⚠️ 用内存集合测不出这个差别（那里没有「传输」这一步，IQueryable 只有开销）");
        Console.WriteLine("  → 所以这里模拟一个【真实数据源】: 每返回一行都要付传输 + 反序列化的成本");

        var source = new FakeDbSet(users);

        // 好写法: 谓词被【翻译成 SQL】，只有命中的行才会被传输
        source.Reset();
        var sw = Stopwatch.StartNew();
        var good = source.Where(u => u.Score > 98).Take(5).ToList();
        double msGood = sw.Elapsed.TotalMilliseconds;
        int rowsGood = source.RowsTransferred;

        // 坏写法: 先 ToList() 把【全表】拉回来，再在内存里过滤
        source.Reset();
        sw = Stopwatch.StartNew();
        var bad = source.ToList().Where(u => u.Score > 98).Take(5).ToList();
        double msBad = sw.Elapsed.TotalMilliseconds;
        int rowsBad = source.RowsTransferred;

        Console.WriteLine($"  .Where(...).ToList()   ← 过滤在【服务端】: "
                          + $"{msGood,7:F1} ms，传输 {rowsGood,6} 行");
        Console.WriteLine($"  .ToList().Where(...)   ← 过滤在【客户端】: "
                          + $"{msBad,7:F1} ms，传输 {rowsBad,6} 行");
        Console.WriteLine($"  结果一致: {good.Count == bad.Count}；"
                          + $"慢 {msBad / msGood:F0}x，多传输 {(double)rowsBad / rowsGood:F0}x 的行");
        Console.WriteLine("  → 一个 .ToList() 放错位置，就把「数据库过滤」变成了「全表拉回内存过滤」");
        Console.WriteLine("  → 第 46 章实测过真实数据库上的同一笔账: 109 ms + 46 MB 堆 vs 9 μs");

        Console.WriteLine("\n== ④ 变更跟踪：ORM 怎么知道你改了什么 ==");
        Console.WriteLine("  两种实现方式:");
        Console.WriteLine("    快照法(EF Core 默认/Hibernate): 加载时存一份副本，SaveChanges 时逐字段比对");
        Console.WriteLine("      → 简单可靠，代价是【每个实体一份内存副本】");
        Console.WriteLine("    代理法(EF 的 Change Proxies): 生成子类拦截 setter，改了就标记");
        Console.WriteLine("      → 省内存，代价是实体属性必须是 virtual，且对象不再是你的类型");
        Console.WriteLine("  → Python 版实测了快照法: 5 行代码实现，只更新真正变了的列");
        Console.WriteLine("  → 大批量操作时把跟踪关掉: EF 的 .AsNoTracking()（只读查询的标配）");

        var swT = Stopwatch.StartNew();
        var snapshots = users.Take(50_000).Select(u => new User(u.Id, u.Name, u.City, u.Score)).ToList();
        double msTrack = swT.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  实测快照成本: 为 {snapshots.Count} 个实体各存一份副本 = {msTrack:F0} ms");
        Console.WriteLine("  → 这就是「查一万行只为了展示，却慢得莫名其妙」的常见原因");

        Console.WriteLine("\n== ⑤ 五语言的 ORM 与它们依赖的语言特性 ==");
        Console.WriteLine("  C#     : EF Core —— 靠【表达式树】把 LINQ 翻译成 SQL（本例实现的就是它）");
        Console.WriteLine("  Java   : Hibernate/JPA —— 靠【注解 + 反射】读类型结构（Java 版实现的就是它）");
        Console.WriteLine("  Python : SQLAlchemy —— 靠【描述符 + 元类】拦截属性访问（Python 版实现的就是它）");
        Console.WriteLine("  JS     : Prisma/TypeORM —— 靠【代码生成 + 装饰器】");
        Console.WriteLine("  C++    : ❌ 没有主流 ORM —— 因为它【没有运行时反射】（C++ 版展开）");
        Console.WriteLine("  → 三种技术路线，对应三种语言特性；哪种特性最强，哪家的 ORM 就最自然");

        Console.WriteLine("\n== ⑥ 用 ORM 的三条纪律 ==");
        Console.WriteLine("  ① 打开 SQL 日志: 你写的是对象操作，执行的是你没看过的 SQL");
        Console.WriteLine("     EF Core: optionsBuilder.LogTo(Console.WriteLine)");
        Console.WriteLine("  ② 只读查询加 .AsNoTracking(): 省掉快照成本（④ 实测）");
        Console.WriteLine("  ③ 关联查询显式 .Include(): 否则就是 N+1（Python 版实测 201 条 SQL）");
        Console.WriteLine("  → ORM 不是「不用懂 SQL」的借口，而是「懂 SQL 之后少写样板」的工具");
    }

    static bool MyCustomCheck(string s) => s.Length > 3;
}

/// 模拟一个远程数据源: 每传回一行都要付出「网络 + 反序列化」的成本
class FakeDbSet
{
    readonly List<User> _rows;
    public int RowsTransferred { get; private set; }
    public FakeDbSet(List<User> rows) => _rows = rows;
    public void Reset() => RowsTransferred = 0;

    /// 接受【表达式树】→ 翻译成 SQL → 只把命中的行传回来
    public IEnumerable<User> Where(Expression<Func<User, bool>> predicate)
    {
        _ = SqlTranslator.Translate(predicate);        // 真的翻译一遍（① 的翻译器）
        var filter = predicate.Compile();              // 服务端执行，不计入传输
        return Materialize(filter);
    }

    /// 没有谓词 → 全表传回来
    public List<User> ToList() => Materialize(null).ToList();

    IEnumerable<User> Materialize(Func<User, bool>? serverFilter)
    {
        foreach (var r in _rows)
        {
            if (serverFilter != null && !serverFilter(r)) continue;   // 服务端过滤掉，不传输
            RowsTransferred++;
            Thread.SpinWait(100);                                      // 传输 + 反序列化的成本
            yield return r;
        }
    }
}
