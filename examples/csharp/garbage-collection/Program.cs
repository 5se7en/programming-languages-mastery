using System;
using System.Runtime.CompilerServices;

class Student
{
    public string Name;
    public Student? Partner;
    public byte[] Payload = new byte[1024];
    public Student(string name) { Name = name; }
}

class Program
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static (WeakReference, WeakReference) MakeCycle()
    {
        var x = new Student("小红");
        var y = new Student("小刚");
        x.Partner = y;
        y.Partner = x;                          // 互指成环（Python 的死角）
        return (new WeakReference(x), new WeakReference(y));
    }

    static void Main()
    {
        Console.WriteLine("== ① 亲眼看对象晋升：gen0 -> gen1 -> gen2 ==");
        var s = new Student("小明");
        Console.WriteLine($"刚出生:        第 {GC.GetGeneration(s)} 代");
        GC.Collect();
        Console.WriteLine($"熬过一次 GC:   第 {GC.GetGeneration(s)} 代");
        GC.Collect();
        Console.WriteLine($"熬过两次 GC:   第 {GC.GetGeneration(s)} 代");
        GC.Collect();
        Console.WriteLine($"熬过三次 GC:   第 {GC.GetGeneration(s)} 代   <- 到顶了（gen2 = 老年代）");
        GC.KeepAlive(s);

        Console.WriteLine("\n== ② 可达性 + 循环引用：与 Java 同款结论 ==");
        var (wx, wy) = MakeCycle();             // 环在方法内创建——返回后栈上引用必然消失
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Console.WriteLine($"互指的两个对象 GC 后: wx.IsAlive = {wx.IsAlive}, wy.IsAlive = {wy.IsAlive}");
        Console.WriteLine("（追踪式 GC 不数引用数——从根标记，环照收不误）");

        Console.WriteLine("\n== ③ 三代的回收频率：越老越少收 ==");
        int g0 = GC.CollectionCount(0), g1 = GC.CollectionCount(1), g2 = GC.CollectionCount(2);
        long sum = 0;
        for (int i = 0; i < 5_000_000; i++)
        {
            var tmp = new Student("t");         // 朝生夕死的分配压力
            sum += tmp.Payload.Length;
        }
        Console.WriteLine($"分配五百万个临时对象后（校验和 {sum % 10}）:");
        Console.WriteLine($"  gen0 回收: +{GC.CollectionCount(0) - g0} 次   <- 新生代最忙");
        Console.WriteLine($"  gen1 回收: +{GC.CollectionCount(1) - g1} 次");
        Console.WriteLine($"  gen2 回收: +{GC.CollectionCount(2) - g2} 次   <- 老年代几乎不动");

        Console.WriteLine("\n== ④ 内存账本 ==");
        Console.WriteLine($"GC.GetTotalMemory(false) = {GC.GetTotalMemory(false) / 1024 / 1024} MB");
        Console.WriteLine($"已收集总代数计数: gen0={GC.CollectionCount(0)}, gen1={GC.CollectionCount(1)}, gen2={GC.CollectionCount(2)}");
    }
}
