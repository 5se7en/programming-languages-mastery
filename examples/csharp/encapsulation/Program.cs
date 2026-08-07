// 第 25 章 · 封装 —— C# 示例
// 运行：dotnet run
// C# 有最多的访问修饰符，属性(property)让封装写起来最省事

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Reflection;

public class Account
{
    private int _balance = 100;              // 仅本类（默认）
    protected int ForSubclass;                // 本类 + 派生类
    internal int SameAssembly;                // 同一程序集（对应 Java 的包级）

    public int Balance => _balance;           // 只读属性（表达式体）

    // ✅ 有业务含义的操作，而不是裸 setter
    public void Deposit(int n)
    {
        if (n <= 0) throw new ArgumentException("金额必须为正");
        _balance += n;
    }

    public void Withdraw(int n)
    {
        if (n > _balance) throw new InvalidOperationException("余额不足");
        _balance -= n;                        // 唯一的修改入口，不变式得到保证
    }
}

public class Temperature
{
    private double _celsius;

    public double Celsius
    {
        get => _celsius;
        set
        {
            if (value < -273.15) throw new ArgumentOutOfRangeException(nameof(value), "低于绝对零度");
            _celsius = value;
        }
    }

    public double Fahrenheit => _celsius * 9 / 5 + 32;   // 只读计算属性
}

// C# 11 的 required + init：必须初始化，之后只读
public class Student
{
    public required string Name { get; init; }
    public int Score { get; init; }
}

// 封装泄漏的演示
public class BadRoster
{
    private readonly List<string> _items = new() { "Alice", "Bob" };
    public List<string> GetItems() => _items;                        // ✗ 返回内部列表本身
}

public class GoodRoster
{
    private readonly List<string> _items = new() { "Alice", "Bob" };
    public IReadOnlyList<string> GetItems() => _items.AsReadOnly();   // ✓ 只读视图
    public int Size => _items.Count;
}

class Program
{
    static void Main()
    {
        Console.WriteLine("=== 1. 封装后：编译器挡住直接访问 ===");
        var acc = new Account();
        Console.WriteLine($"  acc.Balance = {acc.Balance}");
        try { acc.Withdraw(1000); }
        catch (InvalidOperationException e)
        {
            Console.WriteLine($"  acc.Withdraw(1000) → {e.Message}  ← 不变式 balance >= 0 得到保证");
        }
        // acc._balance = -999;   // 编译错误：无法访问私有字段
        // acc.Balance = -999;    // 编译错误：属性只有 getter
        Console.WriteLine("  写 acc._balance = -999 → 编译错误，编译器直接挡住");

        Console.WriteLine("\n=== 2. ⚠️ 但反射能突破 private（实测）===");
        var f = typeof(Account).GetField("_balance",
            BindingFlags.NonPublic | BindingFlags.Instance);
        f!.SetValue(acc, -999);
        Console.WriteLine("  GetField(..., BindingFlags.NonPublic) + SetValue(acc, -999)");
        Console.WriteLine($"  → acc.Balance = {acc.Balance}  ← 突破成功");
        Console.WriteLine("  → 与 Java 完全一样：编译期强制，运行时反射能绕过（第 30 章）");
        Console.WriteLine("  → 这是刻意保留的：序列化、依赖注入、ORM 全靠它");

        Console.WriteLine("\n=== 3. C# 的六个访问修饰符 ===");
        Console.WriteLine("  private              仅本类（默认）");
        Console.WriteLine("  protected            本类 + 派生类");
        Console.WriteLine("  internal             同一程序集（一个 DLL）");
        Console.WriteLine("  protected internal   派生类 或 同程序集");
        Console.WriteLine("  private protected    同程序集内的派生类（两者都要满足）");
        Console.WriteLine("  public               所有人");
        Console.WriteLine("  → internal 对应 Java 的包级私有，但粒度是程序集（.NET 的部署单元）");

        Console.WriteLine("\n=== 4. 属性：C# 的核心优势 ===");
        var t = new Temperature();
        t.Celsius = 25;                       // 看起来像赋值，实际调用了 setter
        Console.WriteLine($"  t.Celsius = 25 后:");
        Console.WriteLine($"    t.Celsius    = {t.Celsius}");
        Console.WriteLine($"    t.Fahrenheit = {t.Fahrenheit}  ← 计算属性，永远不会不一致");
        try { t.Celsius = -300; }
        catch (ArgumentOutOfRangeException)
        {
            Console.WriteLine("    t.Celsius = -300 → ArgumentOutOfRangeException");
        }

        Console.WriteLine("\n=== 5. 自动属性：不需要校验时的简写 ===");
        Console.WriteLine("  public int Score { get; set; }         // 编译器自动生成后备字段");
        Console.WriteLine("  public int Id { get; }                  // 只读，只能在构造函数里赋值");
        Console.WriteLine("  public int Count { get; private set; }  // 外部只读，内部可写");
        Console.WriteLine("  public string Name { get; init; }       // C# 9+：只能在初始化时赋值");
        Console.WriteLine();
        Console.WriteLine("  → 和 Python 的 @property 是同一个思路：");
        Console.WriteLine("     先写简单的自动属性，需要校验时再改成完整属性，调用方完全不用动");
        Console.WriteLine("  → 这让「预防性地写 getter/setter」变得没有必要");

        Console.WriteLine("\n=== 6. required + init：C# 11 的不可变写法 ===");
        var s = new Student { Name = "Alice", Score = 92 };
        Console.WriteLine($"  new Student {{ Name = \"Alice\", Score = 92 }} → {s.Name}, {s.Score}");
        // s.Name = "Bob";           // 编译错误：init 属性只能在初始化时赋值
        // var bad = new Student();  // 编译错误：required 属性 Name 必须初始化
        Console.WriteLine("  s.Name = \"Bob\"    → 编译错误（init 只能在初始化时赋值）");
        Console.WriteLine("  new Student()      → 编译错误（required 属性必须初始化）");

        Console.WriteLine("\n=== 7. ⚠️ 封装泄漏：返回可变的内部集合 ===");
        var bad = new BadRoster();
        bad.GetItems().Add("入侵者");
        Console.WriteLine($"  BadRoster:  外部 Add 后内部变成 [{string.Join(", ", bad.GetItems())}]");

        var good = new GoodRoster();
        Console.WriteLine($"  GoodRoster: 返回 IReadOnlyList，外部根本没有 Add 方法");
        Console.WriteLine($"              内部仍是 [{string.Join(", ", good.GetItems())}]");
        Console.WriteLine("  → 这是最隐蔽的封装泄漏：字段是 private 的，但可变引用漏出去了");

        Console.WriteLine("\n=== 8. 暴露操作，而不是暴露状态 ===");
        Console.WriteLine("  ❌ public int Balance { get; set; }  —— 等于公开字段");
        Console.WriteLine("  ✅ public int Balance { get; }  +  Deposit() / Withdraw()");
        Console.WriteLine("  → 设计时先问「调用方需要做什么」，而不是「这个对象有什么数据」");

        Console.WriteLine("\n=== 9. 小结 ===");
        Console.WriteLine("  · C# 有六个访问修饰符，粒度最细");
        Console.WriteLine("  · 属性让「先简单、需要时加校验」成为无痛操作（同 Python 的 @property）");
        Console.WriteLine("  · private 同样能被反射突破（实测已验证）");
        Console.WriteLine("  · 返回集合用 IReadOnlyList，避免封装泄漏");
    }
}
