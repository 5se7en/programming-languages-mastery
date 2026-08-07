// 第 27 章 · 多态 —— C# 示例
// 运行：dotnet run
// C# 要求 virtual 与 override 都显式声明，是这几门语言里最严格的

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

// ---------- ① 抽象类：定义契约 + 提供部分实现 ----------
public abstract class Shape
{
    public abstract double Area();                          // 无实现，子类必须提供
    public abstract string Name();
    public virtual string Describe() => $"{Name()} 面积 = {Area():F2}";  // 有默认实现，可选重写
}

public class Circle : Shape
{
    private readonly double _r;
    public Circle(double r) => _r = r;
    public override double Area() => Math.PI * _r * _r;
    public override string Name() => "圆形";
}

public class Rect : Shape
{
    private readonly double _w, _h;
    public Rect(double w, double h) { _w = w; _h = h; }
    public override double Area() => _w * _h;
    public override string Name() => "矩形";
    public override string Describe() => $"矩形 {_w}×{_h} = {Area():F2}";  // 重写默认实现
}

// sealed：禁止继续继承 → JIT 可以去虚化
public sealed class Square : Shape
{
    private readonly double _s;
    public Square(double s) => _s = s;
    public override double Area() => _s * _s;
    public override string Name() => "正方形";
}

// ---------- ② new 隐藏 vs override 重写 ----------
public class HideBase { public virtual string Who() => "Base"; }
public class ByNew : HideBase { public new string Who() => "ByNew"; }          // ⚠️ 隐藏
public class ByOverride : HideBase { public override string Who() => "ByOverride"; }  // ✓ 重写

// ---------- ③ 接口默认实现（C# 8+）----------
public interface ILogger
{
    void Log(string msg);
    void LogError(string msg) => Log($"[错误] {msg}");      // 默认实现
}

public class ConsoleLogger : ILogger
{
    public void Log(string msg) => Console.WriteLine($"    {msg}");
}

// ---------- ④ 性能测试用 ----------
public interface ICompute { int Compute(int v); }
public class CompA : ICompute { public int Compute(int v) => (v * 33 + 11) & 0xFFFFFF; }
public class CompB : ICompute { public int Compute(int v) => (v * 37 + 13) & 0xFFFFFF; }
public sealed class CompSealed { public int Compute(int v) => (v * 33 + 11) & 0xFFFFFF; }

class Program
{
    static void Main()
    {
        Console.WriteLine("=== 1. 多态：新增类型不改已有代码（开闭原则）===");
        var shapes = new List<Shape> { new Circle(2), new Rect(3, 4), new Square(5) };
        foreach (var s in shapes)
            Console.WriteLine($"    {s.Describe()}");
        Console.WriteLine($"  总面积 = {shapes.Sum(s => s.Area()):F2}");
        Console.WriteLine("  → 新增一种图形只需加一个类，这段代码一个字不用改");

        Console.WriteLine("\n=== 2. C# 最严格：virtual 与 override 都必须显式 ===");
        Console.WriteLine("  public virtual string Speak()      // 基类必须标 virtual");
        Console.WriteLine("  public override string Speak()     // 子类必须标 override");
        Console.WriteLine();
        Console.WriteLine("              Java              C++              C#");
        Console.WriteLine("  默认虚      是                 否（需 virtual）  否（需 virtual）");
        Console.WriteLine("  重写标记    @Override 可选     override 可选     override 强制");
        Console.WriteLine("  → C# 的严格是对「脆弱基类」问题的直接回应（第 26 章）");
        Console.WriteLine("  → 基类作者必须主动决定哪些方法是扩展点");

        Console.WriteLine("\n=== 3. ⚠️ new 隐藏不是多态 ===");
        HideBase byNewAsBase = new ByNew();
        ByNew byNewAsSelf = new ByNew();
        HideBase byOverrideAsBase = new ByOverride();

        Console.WriteLine($"  HideBase b = new ByNew();      b.Who() = {byNewAsBase.Who()}");
        Console.WriteLine($"  ByNew    b = new ByNew();      b.Who() = {byNewAsSelf.Who()}");
        Console.WriteLine("  → 同一个对象，结果取决于「变量的静态类型」！这不是多态");
        Console.WriteLine();
        Console.WriteLine($"  HideBase b = new ByOverride(); b.Who() = {byOverrideAsBase.Who()}");
        Console.WriteLine("  → 无论变量什么类型都执行实际对象的方法 ← 这才是多态");
        Console.WriteLine("  ⚠️ new 隐藏几乎总是设计错误的信号");

        Console.WriteLine("\n=== 4. 接口默认实现（C# 8+）===");
        ILogger logger = new ConsoleLogger();
        logger.Log("普通日志");
        logger.LogError("出错了");
        Console.WriteLine("  → ConsoleLogger 只实现了 Log，LogError 用的是接口的默认实现");
        Console.WriteLine("  → 与 Java 8 的默认方法同理：让接口能演进而不破坏已有实现类");

        Console.WriteLine("\n=== 5. 模式匹配：更现代，但要小心 ===");
        Console.WriteLine("  double Area(Shape s) => s switch {");
        Console.WriteLine("      Circle c => Math.PI * c.R * c.R,");
        Console.WriteLine("      Rect r   => r.W * r.H,");
        Console.WriteLine("      _        => throw new ArgumentException()");
        Console.WriteLine("  };");
        Console.WriteLine();
        Console.WriteLine("  ⚠️ 但这是「反多态」的写法：");
        Console.WriteLine("     它把行为从类里搬到了外面，重新引入了「新增类型要改这个函数」的问题");
        Console.WriteLine("  → 只在处理外部类型（你无法修改的类）时才用它");

        // ---------- 性能测试 ----------
        const int N = 50_000_000;

        // 预热，让 JIT 完成编译
        for (int w = 0; w < 3; w++)
        {
            ICompute c = new CompA();
            int v = 1;
            for (int i = 0; i < 5_000_000; i++) v = c.Compute(v);
            if (v == 0) Console.Write("");
        }

        Console.WriteLine("\n=== 6. 虚调用的开销（串行依赖链，防止优化）===");

        // ⚠️ 微基准测试的关键：每组跑多轮取「最小值」
        //    首轮常因 JIT 尚未完全优化、CPU 频率未爬升而偏慢，
        //    单轮测量会得出「接口调用比 sealed 方法还快」这类噪声结论。
        const int Rounds = 5;
        double d1 = double.MaxValue, d2 = double.MaxValue, d3 = double.MaxValue;
        int v1 = 1, v2 = 1, v3 = 1;

        var f = new CompSealed();
        ICompute mono = new CompA();                      // 只有一个实现类
        var poly = new ICompute[1024];                     // 两种实现随机交替
        var rng = new Random(42);
        for (int i = 0; i < 1024; i++) poly[i] = rng.Next(2) == 0 ? new CompA() : new CompB();

        var sw = new Stopwatch();
        for (int round = 0; round < Rounds; round++)
        {
            sw.Restart();
            for (int i = 0; i < N; i++) v1 = f.Compute(v1);
            sw.Stop(); d1 = Math.Min(d1, sw.Elapsed.TotalMilliseconds);

            sw.Restart();
            for (int i = 0; i < N; i++) v2 = mono.Compute(v2);
            sw.Stop(); d2 = Math.Min(d2, sw.Elapsed.TotalMilliseconds);

            sw.Restart();
            for (int i = 0; i < N; i++) v3 = poly[i & 1023].Compute(v3);
            sw.Stop(); d3 = Math.Min(d3, sw.Elapsed.TotalMilliseconds);
        }

        Console.WriteLine($"  （每组跑 {Rounds} 轮取最小值，排除 JIT 预热和调度干扰）");
        Console.WriteLine($"  {N / 1_000_000} 百万次调用（每次输入依赖上次输出）:");
        Console.WriteLine($"    sealed 类的方法        {d1,5:F0} ms   → 1.00 倍");
        Console.WriteLine($"    接口调用 · 单一实现     {d2,5:F0} ms   → {d2 / d1:F2} 倍");
        Console.WriteLine($"    接口调用 · 两种实现     {d3,5:F0} ms   → {d3 / d1:F2} 倍");
        Console.WriteLine($"  (校验值 {v1} {v2} {v3}，确保循环真的执行了)");
        Console.WriteLine();
        Console.WriteLine("  → 与 Java 同理：.NET 的 JIT 也能做去虚化");
        Console.WriteLine("  → sealed 让 JIT 确定「这里不会再有子类」，从而去虚化并内联");
        Console.WriteLine("  ⚠️ 数字依赖环境，记住「开销有限且与类型多样性相关」这个结论");

        Console.WriteLine("\n=== 7. 小结 ===");
        Console.WriteLine("  · C# 要求 virtual + override 双显式，最严格");
        Console.WriteLine("  · new 隐藏不是多态：结果取决于变量的静态类型");
        Console.WriteLine("  · 接口默认实现让接口能演进而不破坏实现类");
        Console.WriteLine("  · 模式匹配虽然现代，但是「反多态」的，只用于外部类型");
        Console.WriteLine("  · sealed 既是设计约束，也是给 JIT 的优化提示");
    }
}
