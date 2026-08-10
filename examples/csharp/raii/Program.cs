using System;
using System.Threading.Tasks;

class FileHandle : IDisposable
{
    private readonly string _name;
    public FileHandle(string name)
    {
        _name = name;
        Console.WriteLine($"    [获取] 打开 {_name}");
    }
    public void Dispose() => Console.WriteLine($"    [释放] 关闭 {_name}");
}

class AsyncFileHandle : IAsyncDisposable
{
    private readonly string _name;
    public AsyncFileHandle(string name)
    {
        _name = name;
        Console.WriteLine($"    [获取] 异步打开 {_name}");
    }
    public async ValueTask DisposeAsync()
    {
        await Task.Delay(1);                       // 真正的异步清理（刷盘、发关闭帧……）
        Console.WriteLine($"    [释放] 异步关闭 {_name}");
    }
}

class Program
{
    static void ManualStyle()
    {
        var f = new FileHandle("manual.txt");
        throw new InvalidOperationException("中途出错");
        // f.Dispose() 永远执行不到
    }

    static void UsingStyle()
    {
        using var f = new FileHandle("using.txt");
        throw new InvalidOperationException("中途出错");
    }

    static async Task Main()
    {
        Console.WriteLine("== ① using 语句：作用域即资源生命周期 ==");
        using (var f = new FileHandle("data.txt"))
        {
            Console.WriteLine("    使用中……");
        }
        Console.WriteLine("    块结束——无需手写 Dispose");

        Console.WriteLine("\n== ② 钥匙实验：异常安全 ==");
        Console.WriteLine("  手动风格:");
        try { ManualStyle(); }
        catch (Exception e) { Console.WriteLine($"    捕获: {e.Message}   <- 没有 [释放] 打印！泄漏"); }
        Console.WriteLine("  using 风格:");
        try { UsingStyle(); }
        catch (Exception e) { Console.WriteLine($"    捕获: {e.Message}   <- [释放] 已在上一行打印"); }

        Console.WriteLine("\n== ③ 多个资源：逆序释放 ==");
        try
        {
            using var a = new FileHandle("第一个");
            using var b = new FileHandle("第二个");
            using var c = new FileHandle("第三个");
            throw new InvalidOperationException("三个都开着的时候出错了");
        }
        catch (Exception) { }
        Console.WriteLine("    三个全部释放，顺序是 3-2-1（声明的逆序）");

        Console.WriteLine("\n== ④ using 声明（C# 8）：少一层缩进 ==");
        Console.WriteLine("    using (var f = ...) { ... }  → 花括号界定作用域");
        Console.WriteLine("    using var f = ...;           → 作用域延伸到方法末尾");

        Console.WriteLine("\n== ⑤ C# 独有：异步 RAII（await using）==");
        await using (var af = new AsyncFileHandle("async.txt"))
        {
            Console.WriteLine("    异步使用中……");
        }
        Console.WriteLine("    （C++ 析构、Java close、Python __exit__ 都不能 await——");
        Console.WriteLine("      C# 的 IAsyncDisposable 是五门语言里唯一的异步资源释放）");
    }
}
