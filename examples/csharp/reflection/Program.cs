using System;
using System.Diagnostics;
using System.Reflection;

class Student
{
    private string name;
    private int score;

    public Student() : this("未命名", 0) { }
    public Student(string name, int score) { this.name = name; this.score = score; }

    public string GetName() => name;
    public int GetScore() => score;

    private string Secret() => $"{name} 的真实分数是 {score}";
}

class Program
{
    static void Main()
    {
        Console.WriteLine("== ① Type 对象：类型信息的运行时入口 ==");
        Type t1 = typeof(Student);                    // 编译期字面量
        Type t2 = new Student().GetType();            // 从对象上问
        Type t3 = Type.GetType("Student");            // 从字符串加载！
        Console.WriteLine($"三种方式拿到同一个 Type 对象: {t1 == t2 && t2 == t3}");

        Console.WriteLine("\n== ② 枚举成员（注意 BindingFlags） ==");
        var flags = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance;
        foreach (var f in t1.GetFields(flags))
            Console.WriteLine($"  字段: {f.FieldType.Name} {f.Name}");
        foreach (var m in t1.GetMethods(flags | BindingFlags.DeclaredOnly))
            Console.WriteLine($"  方法: {m.Name}");

        Console.WriteLine("\n== ③ 动态创建 + 动态调用 ==");
        object obj = Activator.CreateInstance(t1, "小明", 90)!;
        MethodInfo getName = t1.GetMethod("GetName")!;
        Console.WriteLine($"Invoke(GetName) = {getName.Invoke(obj, null)}");

        Console.WriteLine("\n== ④ 击穿封装：private 形同虚设 ==");
        FieldInfo name = t1.GetField("name", BindingFlags.NonPublic | BindingFlags.Instance)!;
        name.SetValue(obj, "被改名");
        MethodInfo secret = t1.GetMethod("Secret", BindingFlags.NonPublic | BindingFlags.Instance)!;
        Console.WriteLine($"私有字段已改，私有方法照调: {secret.Invoke(obj, null)}");

        Console.WriteLine("\n== ⑤ BindingFlags 的经典坑：忘写 NonPublic 就拿不到 ==");
        FieldInfo? missed = t1.GetField("name");      // 默认只找 public
        Console.WriteLine($"t1.GetField(\"name\") = {(missed == null ? "null   <- 不报错，静默返回 null" : missed.Name)}");

        Console.WriteLine("\n== ⑥ 性能：直接调用 vs 反射调用（1000 万次） ==");
        var s = new Student("小红", 85);
        MethodInfo mi = t1.GetMethod("GetScore")!;
        long sink = 0;
        for (int i = 0; i < 3_000_000; i++)           // 预热
        {
            sink += s.GetScore();
            sink += (int)mi.Invoke(s, null)!;
        }
        const int n = 10_000_000;
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < n; i++) sink += s.GetScore();
        double tDirect = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        for (int i = 0; i < n; i++) sink += (int)mi.Invoke(s, null)!;
        double tReflect = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"直接调用:          {tDirect,6:F1} ms");
        Console.WriteLine($"MethodInfo.Invoke: {tReflect,6:F1} ms");
        if (sink == 42) Console.WriteLine();          // 防止死代码消除
    }
}
