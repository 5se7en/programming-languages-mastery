// 第 24 章 · 对象 —— C# 示例
// 运行：dotnet run
// C# 同时有引用类型（class，有对象头）和值类型（struct，无对象头）

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

// 值类型：无对象头，就是纯粹的数据
public struct PointStruct { public int X, Y; }

// 引用类型：堆上分配，有对象头
public class PointClass { public int X, Y; }

// 显式控制布局（与本地代码互操作时必需）
[StructLayout(LayoutKind.Sequential)]
public struct Packet { public byte A; public int B; }

// 联合体：两个字段共用同一块内存
[StructLayout(LayoutKind.Explicit)]
public struct Union
{
    [FieldOffset(0)] public int AsInt;
    [FieldOffset(0)] public float AsFloat;
}

class Program
{
    static void Main()
    {
        Console.WriteLine("=== 1. struct 无对象头，class 有 ===");
        Console.WriteLine($"  Unsafe.SizeOf<PointStruct>() = {Unsafe.SizeOf<PointStruct>()} 字节");
        Console.WriteLine("    → 就是两个 int，一个字节都不多（像 C++）");
        Console.WriteLine("  PointClass 实例 = 16 字节对象头 + 8 字节数据 = 24 字节");
        Console.WriteLine("    → 对象头买到了 GC、锁、类型信息");
        Console.WriteLine($"  Unsafe.SizeOf<Packet>() = {Unsafe.SizeOf<Packet>()} 字节");
        Console.WriteLine("    → byte + int，中间有 3 字节填充（对齐，与 C++ 一样）");

        Console.WriteLine("\n=== 2. ⚠️ 装箱：值类型放进 object 会发生什么 ===");
        int n = 42;
        object boxed = n;                  // 装箱：堆上分配对象，把值拷进去
        int unboxed = (int)boxed;          // 拆箱：拷回来
        Console.WriteLine($"  int n = 42        → 栈上 4 字节");
        Console.WriteLine($"  object boxed = n  → 堆上分配一个对象，把 42 拷进去");
        Console.WriteLine($"  拆箱回来 = {unboxed}");

        int m = 42;
        object b1 = m, b2 = m;
        Console.WriteLine($"  装两次: ReferenceEquals(b1, b2) = {ReferenceEquals(b1, b2)}");
        Console.WriteLine("    → 装箱会拷贝，装两次得到两个不同对象");

        Console.WriteLine("\n=== 3. 装箱的性能代价（实测）===");
        const int Iter = 10_000_000;

        // 预热，让 JIT 先编译好
        for (int w = 0; w < 3; w++) { long t = 0; for (int i = 0; i < 100_000; i++) t += i; }

        var sw = Stopwatch.StartNew();
        long sum1 = 0;
        for (int i = 0; i < Iter; i++) sum1 += i;
        sw.Stop();
        double direct = sw.Elapsed.TotalMilliseconds;

        sw.Restart();
        long sum2 = 0;
        for (int i = 0; i < Iter; i++) { object o = i; sum2 += (int)o; }
        sw.Stop();
        double boxing = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  {Iter:N0} 次循环:");
        Console.WriteLine($"    直接累加   {direct,7:F0} ms");
        Console.WriteLine($"    装箱后累加 {boxing,7:F0} ms   → 慢约 {boxing / direct:F1} 倍");
        Console.WriteLine($"  (校验和 {sum1} == {sum2})");
        Console.WriteLine("  ⚠️ 倍数会随环境波动，记住「装箱有实实在在的代价」这个结论");

        Console.WriteLine("\n=== 4. 装箱容易在意想不到的地方发生 ===");
        var arrayList = new ArrayList();
        var genericList = new List<int>();

        sw.Restart();
        for (int i = 0; i < 1_000_000; i++) arrayList.Add(i);      // ✗ 一百万次装箱
        sw.Stop();
        double alTime = sw.Elapsed.TotalMilliseconds;

        sw.Restart();
        for (int i = 0; i < 1_000_000; i++) genericList.Add(i);    // ✓ 无装箱
        sw.Stop();
        double glTime = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  ArrayList.Add(int)  一百万次: {alTime,6:F0} ms  ← 每次都装箱");
        Console.WriteLine($"  List<int>.Add(int)  一百万次: {glTime,6:F0} ms  ← 无装箱");
        Console.WriteLine($"  → 泛型快约 {alTime / glTime:F1} 倍");
        Console.WriteLine("  → List<int> 内部是真正的 int[]，ArrayList 存的是 object[]");
        Console.WriteLine("  → 泛型的一大价值就是消除装箱（第 29 章的伏笔）");

        Console.WriteLine("\n=== 5. struct 是值语义：赋值即拷贝 ===");
        var s1 = new PointStruct { X = 1, Y = 2 };
        var s2 = s1;                       // 拷贝
        s2.X = 99;
        Console.WriteLine($"  struct: s1.X={s1.X}  s2.X={s2.X}  ← s1 没变（第 23 章的值语义）");

        var c1 = new PointClass { X = 1, Y = 2 };
        var c2 = c1;                       // 别名
        c2.X = 99;
        Console.WriteLine($"  class : c1.X={c1.X}  c2.X={c2.X}  ← c1 也变了（引用语义）");

        Console.WriteLine("\n=== 6. ⚠️ 大对象用 struct 反而更慢 ===");
        Console.WriteLine("  struct 每次传递都要完整拷贝：");
        Console.WriteLine("    小 struct（8-16 字节）→ 拷贝很便宜，还省了堆分配   ✅");
        Console.WriteLine("    大 struct（上百字节）  → 每次传参都拷贝一大坨      ❌");
        Console.WriteLine("  → 经验规则：struct 控制在 16 字节以内，且应该是不可变的");

        Console.WriteLine("\n=== 7. 显式布局：与本地代码互操作 ===");
        var u = new Union { AsInt = 1065353216 };
        Console.WriteLine($"  Union {{ AsInt=1065353216 }} 的 AsFloat = {u.AsFloat}");
        Console.WriteLine("    → 两个字段共用同一块内存，解释方式不同");
        Console.WriteLine("  ⚠️ 默认情况下 CLR 对 class 可能重排字段（LayoutKind.Auto）");
        Console.WriteLine("     需要确定布局时必须显式声明 Sequential 或 Explicit");

        Console.WriteLine("\n=== 8. 小结 ===");
        Console.WriteLine("  · class 有对象头（16 字节），struct 没有");
        Console.WriteLine("  · C# 是唯一让你自己选布局的语言（第 23 章的值/引用语义同理）");
        Console.WriteLine("  · 装箱把值类型搬到堆上，有实实在在的代价");
        Console.WriteLine("  · 用泛型集合避免装箱，小数据用 struct");
    }
}
