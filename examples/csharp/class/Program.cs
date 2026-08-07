// 第 23 章 · 类 —— C# 示例
// 运行：dotnet run
// C# 的特色：属性(property)、record、以及 class/struct 让你自己选值/引用语义

using System;

public class Student
{
    public string Name { get; set; }              // 属性：看起来像字段，实际是方法
    public int Score { get; private set; }         // 外部只读，内部可写
    public int Id { get; }

    private static int _count = 0;                 // 静态字段：整个类共享
    public const int PassLine = 60;                // 常量
    public static string School = "第一中学";

    public Student(string name, int score)         // 构造函数
    {
        if (score < 0 || score > 100)              // 构造函数保证对象从诞生起就合法
            throw new ArgumentException("分数必须在 0..100 之间");
        Name = name;
        Score = score;
        Id = ++_count;
    }

    public bool IsPassing() => Score >= PassLine;  // 表达式体成员
    public static int Count => _count;

    public override string ToString() => $"Student({Name}, {Score})";
}

// record：不可变数据类，基于值比较（C# 9+）
public record Point(int X, int Y);

// struct 是值类型，class 是引用类型 —— C# 让你自己选
public struct PointValue { public int X; public int Y; }
public class PointRef { public int X; public int Y; }

class Program
{
    static void Main()
    {
        Console.WriteLine("=== 1. 用类打包：数据和行为待在一起 ===");
        var alice = new Student("Alice", 92);
        var bob = new Student("Bob", 45);
        Console.WriteLine($"  {alice.Name}: 分数 {alice.Score}, 及格? {alice.IsPassing()}");
        Console.WriteLine($"  {bob.Name}: 分数 {bob.Score}, 及格? {bob.IsPassing()}");
        Console.WriteLine($"  静态字段 Student.School = {Student.School}  ← 所有实例共享");
        Console.WriteLine($"  已创建实例数 = {Student.Count}");

        Console.WriteLine("\n=== 2. 属性：看起来像字段，实际是方法 ===");
        Console.WriteLine($"  alice.Score = {alice.Score}   ← 读起来像字段");
        Console.WriteLine("  但 Score 有 private set，外部无法赋值：");
        Console.WriteLine("    alice.Score = 100;  // 编译错误");
        Console.WriteLine("  → 属性让你在「像字段一样使用」和「背后能加校验」之间兼得");

        Console.WriteLine("\n=== 3. 构造函数保证对象合法 ===");
        try
        {
            new Student("Invalid", 150);
        }
        catch (ArgumentException e)
        {
            Console.WriteLine($"  new Student(\"Invalid\", 150) → {e.Message}");
            Console.WriteLine("  → 非法对象根本无法被创建出来");
        }

        Console.WriteLine("\n=== 4. class 是引用语义：b = a 只是起了个别名 ===");
        var a = new Student("Alice", 90);
        var b = a;                                  // 引用赋值，不是拷贝
        b.Name = "Bob";
        Console.WriteLine($"  赋值后: a.Name={a.Name}  b.Name={b.Name}  ← a 也变了！");
        Console.WriteLine($"  ReferenceEquals(a, b) = {ReferenceEquals(a, b)}  ← 同一个对象");

        Console.WriteLine("\n=== 5. ⚠️ C# 独有：struct 是值语义，可以自己选 ===");
        var v1 = new PointValue { X = 1, Y = 2 };
        var v2 = v1;                                // struct：拷贝！
        v2.X = 99;
        Console.WriteLine($"  struct: v1.X={v1.X}  v2.X={v2.X}  ← v1 没变（值语义，像 C++）");

        var r1 = new PointRef { X = 1, Y = 2 };
        var r2 = r1;                                // class：别名！
        r2.X = 99;
        Console.WriteLine($"  class : r1.X={r1.X}  r2.X={r2.X}  ← r1 也变了（引用语义）");
        Console.WriteLine("  → C# 是这几门语言里唯一让你自己选择语义的");
        Console.WriteLine("  → struct 适合小而不可变的数据；大对象用 struct 反而更慢（每次传递都要完整拷贝）");

        Console.WriteLine("\n=== 6. record：不可变数据类 + 基于值比较（C# 9+）===");
        var p1 = new Point(1, 2);
        var p2 = new Point(1, 2);
        Console.WriteLine($"  new Point(1, 2)  → {p1}  ← 自动生成的 ToString");
        Console.WriteLine($"  p1 == p2         → {p1 == p2}  ← 比较的是值，不是引用！");
        Console.WriteLine($"  p1.GetHashCode() == p2.GetHashCode() → {p1.GetHashCode() == p2.GetHashCode()}");
        Console.WriteLine();
        Console.WriteLine("  对比普通 class：");
        var c1 = new PointRef { X = 1, Y = 2 };
        var c2 = new PointRef { X = 1, Y = 2 };
        Console.WriteLine($"    两个内容相同的 class 实例 c1 == c2 → {c1 == c2}  ← 比较引用，所以是 false");
        Console.WriteLine("  → record 自动实现基于值的 Equals 和 GetHashCode");
        Console.WriteLine("  → 回顾第 20 章：这从设计上防止了「存进哈希表却查不到」的坑");

        Console.WriteLine("\n=== 7. 实例成员 vs 静态成员：存几份 ===");
        Console.WriteLine($"  alice.Id = {alice.Id}   bob.Id = {bob.Id}   ← 实例属性，每个对象一份");
        Student.School = "第二中学";
        Console.WriteLine($"  改静态字段后，所有实例看到的都是: {Student.School}");
        Console.WriteLine("  → 静态成员整个类只有一份");

        Console.WriteLine("\n=== 8. with 表达式：基于已有 record 造一个改了几个字段的新对象 ===");
        var p3 = p1 with { Y = 99 };
        Console.WriteLine($"  p1 = {p1}");
        Console.WriteLine($"  p1 with {{ Y = 99 }} = {p3}   ← 原对象不变，返回新对象");
        Console.WriteLine("  → 不可变数据的标准修改方式");
    }
}
