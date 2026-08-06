// 第 13 章 · 作用域 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;

class Student
{
    private int score;
    public void SetScore(int score) => this.score = score;   // 参数遮蔽字段，用 this 区分
    public int GetScore() => score;
}

class Scope
{
    static void Main()
    {
        // 1. 块作用域
        int local = 1;
        if (true)
        {
            int inside = 2;
            Console.WriteLine($"块内可见: inside={inside} local={local}");
        }
        // Console.WriteLine(inside);   // ✗ 编译错误：块外不可见
        Console.WriteLine("块外不可见 inside；C# 还禁止在嵌套块中遮蔽局部变量");

        // 2. 字段遮蔽需用 this
        var s = new Student();
        s.SetScore(92);
        Console.WriteLine($"this 区分字段与参数: score={s.GetScore()}");

        // 3. C# 闭包捕获的是「变量本身」，不是当时的值（与 Java 不同）
        int count = 0;
        Action print = () => Console.WriteLine($"闭包捕获变量本身: count={count}");
        count = 42;
        print();      // 输出 42，而不是 0

        // 4. 循环变量不泄漏
        for (int i = 0; i < 3; i++) { }
        // Console.WriteLine(i);   // ✗ 编译错误
        Console.WriteLine("循环变量 i 不会泄漏到循环外");
    }
}
