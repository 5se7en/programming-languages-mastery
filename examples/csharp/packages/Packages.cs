// 第 15 章 · 包 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Linq;

class Packages
{
    record SemVer(int Major, int Minor, int Patch, string? Pre) : IComparable<SemVer>
    {
        public static SemVer Parse(string v)
        {
            var parts = v.Split('-', 2);
            var n = parts[0].Split('.').Select(int.Parse).ToArray();
            return new SemVer(n[0], n[1], n[2], parts.Length > 1 ? parts[1] : null);
        }
        public int CompareTo(SemVer? o)
        {
            if (o is null) return 1;
            if (Major != o.Major) return Major.CompareTo(o.Major);
            if (Minor != o.Minor) return Minor.CompareTo(o.Minor);
            if (Patch != o.Patch) return Patch.CompareTo(o.Patch);
            if (Pre is not null && o.Pre is null) return -1;   // 预发布版在前
            if (Pre is null && o.Pre is not null) return 1;
            return 0;
        }
    }

    static bool Satisfies(string version, string spec)
    {
        char op = (spec[0] == '^' || spec[0] == '~') ? spec[0] : '=';
        var baseV = SemVer.Parse(op == '=' ? spec : spec[1..]);
        var v = SemVer.Parse(version);
        if (v.Pre is not null) return false;
        if (v.CompareTo(baseV) < 0) return false;
        if (op == '^') return v.Major == baseV.Major;
        if (op == '~') return v.Major == baseV.Major && v.Minor == baseV.Minor;
        return v.CompareTo(baseV) == 0;
    }

    static void Main()
    {
        var versions = new[] { "1.2.3", "1.2.9", "1.3.0", "1.9.9", "2.0.0" };
        foreach (var spec in new[] { "^1.2.3", "~1.2.3", "1.2.3" })
            Console.WriteLine($"{spec,-8} 匹配 → {string.Join(", ", versions.Where(v => Satisfies(v, spec)))}");

        Console.WriteLine();
        Console.WriteLine($"字符串比较 \"1.10.0\".CompareTo(\"1.9.0\") > 0 → "
            + (string.Compare("1.10.0", "1.9.0", StringComparison.Ordinal) > 0) + " ← 错误！");
        Console.WriteLine($"数字比较   SemVer 1.10.0 > 1.9.0 → "
            + (SemVer.Parse("1.10.0").CompareTo(SemVer.Parse("1.9.0")) > 0) + " ← 正确");

        // .NET 内置的 Version 类型也是按数字比较
        Console.WriteLine($"System.Version 1.10.0 > 1.9.0 → "
            + (new Version("1.10.0") > new Version("1.9.0")));

        Console.WriteLine("\nNuGet 区间记号: [13.0,14.0) 表示 >=13.0 且 <14.0");
        Console.WriteLine("冲突解决: 最近者优先 + 版本降级时给出警告");
    }
}
