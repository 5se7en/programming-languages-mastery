using System;
using System.Runtime.CompilerServices;

class Student
{
    public string Name;
    public Student? Partner;
    public Student(string name) { Name = name; }
}

class OwnedResource : IDisposable          // C# 的"唯一所有权"只能靠约定 + IDisposable
{
    private bool _disposed;
    public string Name { get; }
    public OwnedResource(string name) { Name = name; Console.WriteLine($"    [获取] {name}"); }
    public void Dispose()
    {
        if (_disposed) return;             // 幂等——对应 unique_ptr 的"只 delete 一次"
        _disposed = true;
        Console.WriteLine($"    [释放] {Name}");
    }
}

class Program
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference MakeStudent(string name) => new WeakReference(new Student(name));

    [MethodImpl(MethodImplOptions.NoInlining)]
    static (WeakReference, WeakReference) MakeCycle()
    {
        var x = new Student("环-甲");
        var y = new Student("环-乙");
        x.Partner = y;
        y.Partner = x;                     // 成环（C++ 在这里泄漏）
        return (new WeakReference(x), new WeakReference(y));
    }

    static void Main()
    {
        Console.WriteLine("== ① C# 的引用 ≈ shared_ptr（但由 GC 追踪，不计数）==");
        var s = new Student("小明");
        var weakAlive = new WeakReference(s);
        Console.WriteLine($"    强引用在: IsAlive = {weakAlive.IsAlive}");
        GC.KeepAlive(s);
        var weakDead = MakeStudent("小强");        // 引用只活在方法里——返回即不可达
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Console.WriteLine($"    强引用断: IsAlive = {weakDead.IsAlive}   <- 无需 delete");

        Console.WriteLine("\n== ② 钥匙实验：同样的环，C# 毫无压力 ==");
        var (wx, wy) = MakeCycle();
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Console.WriteLine($"    成环对象 GC 后: wx.IsAlive = {wx.IsAlive}, wy.IsAlive = {wy.IsAlive}");
        Console.WriteLine("    （追踪式 GC 不数引用——C++ 需要 weak_ptr，C# 什么都不用做）");

        Console.WriteLine("\n== ③ WeakReference ≈ weak_ptr ==");
        var owner = new Student("被观察者");
        var observer = new WeakReference(owner);
        Console.WriteLine($"    对象活着: Target = {((Student?)observer.Target)?.Name}");
        GC.KeepAlive(owner);
        var gone = MakeStudent("已逝者");
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Console.WriteLine($"    对象死后: Target = {((Student?)gone.Target)?.Name ?? "null"}"
                + "   <- 与 weak_ptr::lock() 返回空同义");

        Console.WriteLine("\n== ④ 最接近 unique_ptr 的：IDisposable + using ==");
        using (var r = new OwnedResource("独占资源"))
        {
            Console.WriteLine($"    使用 {r.Name} 中……");
        }
        Console.WriteLine("    （确定性释放做到了，但「唯一所有权」编译器不检查——");
        Console.WriteLine("      C++ 的 unique_ptr 拷贝会编译报错，C# 的引用随便复制）");

        Console.WriteLine("\n== ⑤ 两种世界的分工 ==");
        Console.WriteLine("    内存      -> GC 全自动（连环都不怕，实测 ②）");
        Console.WriteLine("    非内存资源 -> IDisposable + using 手动界定（第 37 章）");
        Console.WriteLine("    C++ 则是同一套工具（智能指针）统管两者");
    }
}
