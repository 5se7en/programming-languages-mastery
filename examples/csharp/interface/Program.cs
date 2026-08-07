// 第 28 章 · 接口 —— C# 示例
// 运行：dotnet run
// C# 独有的显式接口实现，是解决同名冲突的另一条路

using System;
using System.Collections.Generic;
using System.Linq;

// ---------- ① 接口 + 默认实现（C# 8+）----------
public interface IStorage
{
    string Save(string data);
    string SaveAll(IEnumerable<string> items) =>          // 默认实现
        string.Join("; ", items.Select(Save));
}

public class FileStorage : IStorage
{
    public string Save(string data) => $"文件: {data}";
    // 不用实现 SaveAll —— 用接口的默认实现
}

public class S3Storage : IStorage
{
    public string Save(string data) => $"S3: {data}";
}

public class MemoryStorage : IStorage
{
    private readonly List<string> _items = new();
    public string Save(string data) { _items.Add(data); return $"内存: {data}"; }
    public int Count => _items.Count;
}

// ---------- ② ⚠️ 显式接口实现：C# 独有 ----------
public interface IFlyable   { string Move(); }
public interface ISwimmable { string Move(); }

public class Duck : IFlyable, ISwimmable
{
    string IFlyable.Move()   => "飞行";       // 显式实现，只能通过 IFlyable 访问
    string ISwimmable.Move() => "游泳";       // 显式实现，只能通过 ISwimmable 访问
    public string Move()     => "走路";       // 类自己的公开方法
}

// ---------- ③ 显式实现用于「隐藏」实现细节型接口 ----------
public class UserRepo : IDisposable
{
    public string Query(int id) => $"查询用户 {id}";
    void IDisposable.Dispose() => Console.WriteLine("    → 已释放数据库连接");
}

// ---------- ④ 依赖倒置 ----------
public class ReportService
{
    private readonly IStorage _storage;                    // 依赖契约
    public ReportService(IStorage storage) => _storage = storage;
    public string Generate(string content) => _storage.Save($"报表[{content}]");
}

// ---------- ⑤ 接口隔离原则 ----------
public interface IWorkable { string Work(); }
public interface IFeedable { string Eat(); }

public class Robot : IWorkable                             // 只实现需要的
{
    public string Work() => "机器人在工作";
}

public class Human : IWorkable, IFeedable
{
    public string Work() => "人在工作";
    public string Eat()  => "人在吃饭";
}

class Program
{
    static void Main()
    {
        Console.WriteLine("=== 1. 默认接口实现（C# 8+）===");
        IStorage fs = new FileStorage();
        Console.WriteLine("  FileStorage 只实现了 Save()：");
        Console.WriteLine($"    Save(\"a\")          = {fs.Save("a")}");
        Console.WriteLine($"    SaveAll([a, b, c]) = {fs.SaveAll(new[] { "a", "b", "c" })}");
        Console.WriteLine("  → SaveAll 用的是接口的默认实现");
        Console.WriteLine();
        Console.WriteLine("  ⚠️ 与 Java 8 的关键差异：");
        Console.WriteLine("     C# 的默认实现只能通过接口类型访问，不出现在类的公开 API 里");
        Console.WriteLine("     new FileStorage().SaveAll(...)  → 编译错误");
        Console.WriteLine("     ((IStorage)new FileStorage()).SaveAll(...)  → 可以");
        Console.WriteLine("  → 这让它比 Java 的默认方法更「干净」");

        Console.WriteLine("\n=== 2. ⚠️ 显式接口实现：C# 独有的冲突解法 ===");
        var d = new Duck();
        Console.WriteLine("  class Duck : IFlyable, ISwimmable（两个接口都有 Move()）");
        Console.WriteLine();
        Console.WriteLine($"    ((IFlyable)d).Move()   = {((IFlyable)d).Move()}");
        Console.WriteLine($"    ((ISwimmable)d).Move() = {((ISwimmable)d).Move()}");
        Console.WriteLine($"    d.Move()               = {d.Move()}   ← 类自己的公开实现");
        Console.WriteLine();
        Console.WriteLine("  → 同一个对象，三种不同结果，取决于「通过哪个接口访问」");
        Console.WriteLine();
        Console.WriteLine("  对比 Java：");
        Console.WriteLine("                    Java                      C#");
        Console.WriteLine("    同名冲突        只能给一个实现             可以给各自不同的实现");
        Console.WriteLine("    解决语法        A.super.hello() 手工合并   string IFlyable.Move() 分别实现");
        Console.WriteLine("    调用时          结果唯一                   取决于通过哪个接口访问");
        Console.WriteLine("  → 当两个接口的同名方法「语义本来就不同」时，C# 的方案更彻底");

        Console.WriteLine("\n=== 3. 显式实现还能「隐藏」方法 ===");
        var repo = new UserRepo();
        Console.WriteLine($"    repo.Query(42)  = {repo.Query(42)}   ← 类的公开 API");
        Console.WriteLine("    repo.Dispose()  → 编译错误（类的公开 API 里没有）");
        Console.Write("    ((IDisposable)repo).Dispose() → ");
        Console.WriteLine();
        ((IDisposable)repo).Dispose();
        Console.WriteLine("  → 让「实现细节型」接口不污染类的公开 API");
        Console.WriteLine("  → IDisposable、IEnumerator 这类接口的方法通常不希望用户直接调用");

        Console.WriteLine("\n=== 4. 依赖倒置：换实现不用改代码 ===");
        var impls = new (string name, IStorage impl)[]
        {
            ("FileStorage",   new FileStorage()),
            ("S3Storage",     new S3Storage()),
            ("MemoryStorage", new MemoryStorage()),
        };
        foreach (var (name, impl) in impls)
            Console.WriteLine($"    {name,-14} → {new ReportService(impl).Generate("月度")}");
        Console.WriteLine("  → ReportService 的代码一个字都不用改");
        Console.WriteLine("  → 生产注入 S3Storage，测试注入 MemoryStorage");

        Console.WriteLine("\n=== 5. 用内存实现做单元测试 ===");
        var mem = new MemoryStorage();
        var svc = new ReportService(mem);
        svc.Generate("一月");
        svc.Generate("二月");
        Console.WriteLine($"    生成 2 份报表后，MemoryStorage.Count = {mem.Count}");
        Console.WriteLine("  → 不碰真实资源就能验证业务逻辑 —— 这是接口最实际的价值");

        Console.WriteLine("\n=== 6. 接口隔离原则 ===");
        Console.WriteLine("  ❌ 胖接口：interface Worker { Work(); Eat(); Sleep(); }");
        Console.WriteLine("     class Robot : Worker {");
        Console.WriteLine("         public void Eat() => throw new NotSupportedException();  // 机器人不吃饭");
        Console.WriteLine("     }");
        Console.WriteLine();
        Console.WriteLine("  ✅ 拆成小接口：");
        var workers = new IWorkable[] { new Robot(), new Human() };
        foreach (var w in workers)
            Console.WriteLine($"    {w.GetType().Name,-6} → {w.Work()}");
        Console.WriteLine($"    Human 还能: {new Human().Eat()}");
        Console.WriteLine("  → 判断信号：实现类里出现 NotSupportedException，说明接口太胖了");

        Console.WriteLine("\n=== 7. 接口 vs 抽象类 ===");
        Console.WriteLine("                      抽象类            接口");
        Console.WriteLine("    能有几个          1 个（单继承）     任意多个");
        Console.WriteLine("    实例字段          ✅                ❌");
        Console.WriteLine("    构造函数          ✅                ❌");
        Console.WriteLine("    表达的关系        is-a（是一种）     can-do（能做什么）");

        Console.WriteLine("\n=== 8. 小结 ===");
        Console.WriteLine("  · C# 的默认接口实现比 Java 更干净：只能通过接口类型访问");
        Console.WriteLine("  · 显式接口实现是 C# 独有：同名方法可以有各自不同的实现");
        Console.WriteLine("  · 它还能隐藏实现细节型接口，不污染类的公开 API");
        Console.WriteLine("  · 依赖倒置 + 内存实现 = 不碰真实资源就能测试业务逻辑");
    }
}
