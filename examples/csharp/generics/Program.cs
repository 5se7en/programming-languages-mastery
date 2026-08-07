using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

// 泛型类：类型参数 T 在使用时才确定
class Box<T>
{
    public static int Count;             // 静态字段：每个构造类型各一份（具化的证据）
    private readonly T _value;
    public Box(T value) { _value = value; Count++; }
    public T Get() => _value;
}

class Student
{
    public string Name = "未命名";
    public int Score = 0;
}

class Program
{
    // 约束 where T : new() → 可以 new T()（Java 因擦除做不到）
    static T Create<T>() where T : new() => new T();

    static long SumGeneric(List<int> list)
    {
        long s = 0;
        foreach (int x in list) s += x;          // 无装箱
        return s;
    }

    static long SumBoxed(ArrayList list)
    {
        long s = 0;
        foreach (object x in list) s += (int)x;  // 每个元素都要拆箱
        return s;
    }

    static void Main()
    {
        Console.WriteLine("== ① 具化：List<int> 和 List<string> 是不同的类型 ==");
        Console.WriteLine($"typeof(List<int>) == typeof(List<string>): {typeof(List<int>) == typeof(List<string>)}");
        var scores = new List<int>();
        Console.WriteLine($"scores.GetType() = {scores.GetType()}");

        Console.WriteLine("\n== ② 静态字段：每个构造类型各一份 ==");
        _ = new Box<int>(90);
        _ = new Box<int>(85);
        _ = new Box<string>("小明");
        Console.WriteLine($"Box<int>.Count = {Box<int>.Count}, Box<string>.Count = {Box<string>.Count}"
                + "   <- 与 Java 相反！");

        Console.WriteLine("\n== ③ 运行时类型完整 → new T() / typeof(T) / default(T) 都可用 ==");
        Student s = Create<Student>();
        Console.WriteLine($"Create<Student>() -> Name = {s.Name}, Score = {s.Score}");
        Console.WriteLine($"default(int) = {default(int)}, default(string) = {default(string) ?? "null"}");

        Console.WriteLine("\n== ④ 变体：接口可以声明 out / in ==");
        IEnumerable<string> strs = new List<string> { "小明", "小红" };
        IEnumerable<object> objs = strs;              // ✓ IEnumerable<out T> 协变
        Console.WriteLine($"IEnumerable<string> -> IEnumerable<object>: {objs.Count()} 个元素");
        // List<object> l = new List<string>();       // ✗ 编译错误：List<T> 不变

        Console.WriteLine("\n== ⑤ 数组协变的坑（与 Java 相同） ==");
        object[] arr = new string[1];                 // 数组协变：编译通过
        try
        {
            arr[0] = 42;                              // 运行时才爆
        }
        catch (ArrayTypeMismatchException)
        {
            Console.WriteLine("ArrayTypeMismatchException: 尝试把 int 存进 string[]");
        }

        Console.WriteLine("\n== ⑥ 装箱的代价（1000 万元素求和） ==");
        const int n = 10_000_000;
        var generic = new List<int>(n);
        var nonGeneric = new ArrayList(n);
        for (int i = 0; i < n; i++) { generic.Add(i); nonGeneric.Add(i); }
        for (int r = 0; r < 3; r++) { SumGeneric(generic); SumBoxed(nonGeneric); }   // 预热
        var sw = Stopwatch.StartNew();
        long s1 = SumGeneric(generic);
        double tGeneric = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        long s2 = SumBoxed(nonGeneric);
        double tBoxed = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"List<int>（无装箱）    求和: {tGeneric,6:F1} ms（结果 {s1}）");
        Console.WriteLine($"ArrayList（元素装箱）  求和: {tBoxed,6:F1} ms（结果 {s2}）");
    }
}
