// 第 09 章 · 数据类型 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;

class DataTypes
{
    static void Main()
    {
        // 1. 宽度固定
        Console.WriteLine($"int 范围: {int.MinValue} ~ {int.MaxValue}");
        Console.WriteLine($"宽度(字节): int={sizeof(int)} long={sizeof(long)} decimal={sizeof(decimal)}");

        // 2. 浮点误差
        Console.WriteLine($"0.1 + 0.2 = {0.1 + 0.2} | 等于 0.3 吗: {0.1 + 0.2 == 0.3}");

        // 3. decimal 是语言原生的精确十进制（注意 m 后缀）
        Console.WriteLine($"0.1m + 0.2m = {0.1m + 0.2m} | 等于 0.3m 吗: {0.1m + 0.2m == 0.3m}");

        // 4. 溢出：默认静默回绕，checked 可捕获
        int max = int.MaxValue;
        unchecked { Console.WriteLine($"unchecked 最大值+1 = {max + 1}  ← 回绕"); }
        try { checked { int bad = max + 1; } }
        catch (OverflowException) { Console.WriteLine("checked 块中会抛 OverflowException"); }

        // 5. 字符串长度数的是 UTF-16 码元
        string wave = "👋";
        Console.WriteLine($"\"👋\".Length = {wave.Length} (UTF-16 码元)");
    }
}
