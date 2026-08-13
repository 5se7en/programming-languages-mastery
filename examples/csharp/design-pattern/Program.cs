// 设计模式：C# 的语言特性吃掉了多少模式——委托、事件、迭代器、模式匹配，逐个实测。
using System.Diagnostics;
using System.Text;

class Program
{
    // ============ ① 策略：GoF 版 vs 委托版 ============
    interface IDiscount { decimal Apply(decimal price); }                 // GoF: 需要接口
    class VipDiscount : IDiscount { public decimal Apply(decimal p) => p * 0.7m; }
    class NewUserDiscount : IDiscount { public decimal Apply(decimal p) => p - 10; }

    class GofCheckout                                                      // GoF: 需要上下文类
    {
        private readonly IDiscount _d;
        public GofCheckout(IDiscount d) => _d = d;
        public decimal Total(decimal p) => _d.Apply(p);
    }

    // 委托版: 策略就是 Func<decimal, decimal>
    static decimal Checkout(decimal price, Func<decimal, decimal> discount) => discount(price);

    // ============ ③ 观察者：event 是语言内建的观察者 ============
    class OrderService
    {
        public event Action<string>? OrderPlaced;                          // 一个内建的观察者
        public void Place(string id) => OrderPlaced?.Invoke(id);
    }

    // ============ ④ 迭代器：yield return 让迭代器模式消失 ============
    static IEnumerable<int> Fibonacci(int n)
    {
        int a = 0, b = 1;
        for (int i = 0; i < n; i++) { yield return a; (a, b) = (b, a + b); }
    }

    // ============ ⑤ 访问者模式 vs 模式匹配 ============
    abstract record Shape;
    record Circle(double R) : Shape;
    record Rect(double W, double H) : Shape;
    record Triangle(double B, double H) : Shape;

    // GoF 访问者: 需要 IVisitor 接口 + 每个 Shape 实现 Accept + 每个操作一个 Visitor 类
    interface IShapeVisitor<T> { T Visit(Circle c); T Visit(Rect r); T Visit(Triangle t); }
    class AreaVisitor : IShapeVisitor<double>
    {
        public double Visit(Circle c) => Math.PI * c.R * c.R;
        public double Visit(Rect r) => r.W * r.H;
        public double Visit(Triangle t) => t.B * t.H / 2;
    }
    static double VisitArea(Shape s, IShapeVisitor<double> v) => s switch
    {
        Circle c => v.Visit(c), Rect r => v.Visit(r), Triangle t => v.Visit(t),
        _ => throw new NotSupportedException()
    };

    // 模式匹配版: 一个 switch 表达式
    static double Area(Shape s) => s switch
    {
        Circle c => Math.PI * c.R * c.R,
        Rect r => r.W * r.H,
        Triangle t => t.B * t.H / 2,
        _ => throw new NotSupportedException()
    };

    static void Main()
    {
        Console.WriteLine("== ① 策略模式：接口版 vs 委托版（实测同样结果）==");
        decimal price = 100m;
        Console.WriteLine($"  GoF 版 new GofCheckout(new VipDiscount()).Total(100) = "
                          + $"{new GofCheckout(new VipDiscount()).Total(price)}");
        Console.WriteLine($"  委托版 Checkout(100, p => p * 0.7m)               = "
                          + $"{Checkout(price, p => p * 0.7m)}");
        Console.WriteLine($"  委托版换策略 Checkout(100, p => p - 10)            = "
                          + $"{Checkout(price, p => p - 10)}");
        Console.WriteLine("  → GoF 版: 1 个接口 + N 个实现类 + 1 个上下文类");
        Console.WriteLine("  → 委托版: 0 个额外类型 —— Func<T,R> 就是「只有一个方法的接口」的语言化");
        Console.WriteLine("  → C# 从 1.0 就有委托: 它是 C# 相对早期 Java 的最大设计差异之一");

        Console.WriteLine("\n== ② 委托与接口的性能（实测——C# 里这不是性能决策）==");
        const int N = 50_000_000;
        IDiscount iface = new VipDiscount();
        Func<decimal, decimal> del = p => p * 0.7m;
        decimal sink = 0;

        var sw = Stopwatch.StartNew();
        for (int i = 0; i < N; i++) sink += iface.Apply(1m);
        double msIface = sw.Elapsed.TotalMilliseconds;

        sw = Stopwatch.StartNew();
        for (int i = 0; i < N; i++) sink += del(1m);
        double msDel = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  接口调用 {N} 次: {msIface,7:F1} ms（{msIface * 1e6 / N:F2} ns/次）");
        Console.WriteLine($"  委托调用 {N} 次: {msDel,7:F1} ms（{msDel * 1e6 / N:F2} ns/次）");
        Console.WriteLine($"  → 差距 {Math.Max(msIface, msDel) / Math.Min(msIface, msDel):F2}x —— 基本等价");
        Console.WriteLine("  → 对比 C++ 版实测的 3.7x（模板 vs 虚函数）: 有 JIT 的语言里，");
        Console.WriteLine("     两种写法都会被优化到相近水平——所以选哪个纯粹看【可读性】");

        Console.WriteLine("\n== ③ 观察者：event 是语言内建的（实测）==");
        var svc = new OrderService();
        var log = new List<string>();
        Action<string> stock = id => log.Add($"库存服务处理 {id}");
        svc.OrderPlaced += stock;
        svc.OrderPlaced += id => log.Add($"邮件服务处理 {id}");
        svc.Place("A1");
        svc.OrderPlaced -= stock;                                          // 退订
        svc.Place("A2");
        Console.WriteLine($"  两个订阅者 → Place(A1) → 退订一个 → Place(A2)");
        Console.WriteLine($"  {string.Join(" | ", log)}");
        Console.WriteLine("  → `event` 关键字 = 语言内建的观察者模式（+= 订阅、-= 退订、Invoke 发布）");
        Console.WriteLine("  → 它甚至比手写更安全: event 字段【只能】在声明类内部 Invoke");
        Console.WriteLine("  ⚠️ 但有个经典内存泄漏: 订阅者不退订，发布者就一直持有它的引用（第 36 章 GC）");

        Console.WriteLine("\n== ④ 迭代器：yield return 让整个模式消失（实测）==");
        Console.WriteLine($"  Fibonacci(10) → {string.Join(", ", Fibonacci(10))}");
        Console.WriteLine("  → GoF 迭代器模式: IIterator 接口 + HasNext/Next + 一个具体迭代器类");
        Console.WriteLine("  → C# 版: 一个 yield return —— 编译器生成状态机（第 44 章实测过 <Counter>d__0）");
        Console.WriteLine("  → 第 44 章讲协程时的那个状态机，正是「迭代器模式」被语言吸收的产物");

        Console.WriteLine("\n== ⑤ 访问者模式 vs 模式匹配（实测同样结果）==");
        Shape[] shapes = { new Circle(1), new Rect(2, 3), new Triangle(4, 5) };
        var visitor = new AreaVisitor();
        var byVisitor = shapes.Select(s => VisitArea(s, visitor)).ToArray();
        var byPattern = shapes.Select(Area).ToArray();
        Console.WriteLine($"  访问者版: [{string.Join(", ", byVisitor.Select(a => a.ToString("F2")))}]");
        Console.WriteLine($"  模式匹配: [{string.Join(", ", byPattern.Select(a => a.ToString("F2")))}]");
        Console.WriteLine($"  结果一致: {byVisitor.SequenceEqual(byPattern)}");
        Console.WriteLine("  → GoF 访问者: 1 个 IVisitor 接口 + 每个类型一个 Visit + 每个类型实现 Accept");
        Console.WriteLine("  → 模式匹配版: 一个 switch 表达式");
        Console.WriteLine("  → 访问者模式解决的是「在不改类的前提下加操作」——即所谓【表达式问题】");
        Console.WriteLine("     函数式语言用代数数据类型 + 模式匹配天然解决它（record + switch 就是它的 C# 化）");

        Console.WriteLine("\n== ⑥ C# 吃掉的模式清单 ==");
        var eaten = new (string, string)[]
        {
            ("策略 Strategy", "Func<T,R> / 委托（① 实测）"),
            ("命令 Command", "Action / 委托 + 闭包"),
            ("观察者 Observer", "event / IObservable（③ 实测）"),
            ("迭代器 Iterator", "yield return（④ 实测）"),
            ("访问者 Visitor", "switch 模式匹配（⑤ 实测）"),
            ("装饰器 Decorator", "扩展方法 / 高阶函数"),
            ("适配器 Adapter", "扩展方法（给别人的类型加方法）"),
            ("空对象 Null Object", "可空引用类型 + ?? 运算符"),
            ("原型 Prototype", "record 的 with 表达式"),
        };
        foreach (var (gof, cs) in eaten) Console.WriteLine($"  {gof,-20} → {cs}");
        Console.WriteLine("  → C# 二十年演化史，某种程度上就是【一部把设计模式吸收进语言的历史】");

        Console.WriteLine("\n== ⑦ 剩下的模式为什么还在 ==");
        Console.WriteLine("  仓储/工作单元 —— 架构边界（第 51 章 ORM 实测过它们的职责）");
        Console.WriteLine("  适配器/外观   —— 隔离外部系统，与语言无关");
        Console.WriteLine("  状态机        —— 领域复杂度，语言消不掉");
        Console.WriteLine("  → 一条清晰的分界: 【模拟语言缺失特性】的模式会消失，");
        Console.WriteLine("     【组织系统边界】的模式会留下");
        _ = sink;
    }
}
