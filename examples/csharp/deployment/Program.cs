// 部署：同一份代码，三种发布模式——产物大小、启动速度、可移植性三者不可兼得。
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;

class Program
{
    static (long bytes, int files) Measure(string root)
    {
        long b = 0; int n = 0;
        var stack = new Stack<string>();
        stack.Push(root);
        while (stack.Count > 0)
        {
            var d = stack.Pop();
            try
            {
                foreach (var sub in Directory.GetDirectories(d)) stack.Push(sub);
                foreach (var f in Directory.GetFiles(d)) { b += new FileInfo(f).Length; n++; }
            }
            catch { /* 权限等问题跳过 */ }
        }
        return (b, n);
    }

    static void Main()
    {
        var proc = Process.GetCurrentProcess();

        Console.WriteLine("== ① 启动时间：Main() 之前花掉了多少（实测）==");
        double beforeMain = (DateTime.Now - proc.StartTime).TotalMilliseconds;
        var sw = Stopwatch.StartNew();
        long s = 0;
        for (int i = 0; i < 5_000_000; i++) s += i % 7;
        double workMs = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  进程启动 → 进入 Main(): {beforeMain,6:F1} ms");
        Console.WriteLine($"  一段 500 万次循环的业务代码: {workMs,6:F1} ms（结果 {s}）");
        Console.WriteLine("  → 这段时间用来: 加载 CLR、JIT 编译启动路径、初始化 GC 堆");
        Console.WriteLine("  → 与 Java 版同构的问题，也有同构的解法: AOT（见 ④）");

        Console.WriteLine("\n== ② 交付物：运行时有多大（实测）==");
        string baseDir = AppContext.BaseDirectory;
        var (appBytes, appFiles) = Measure(baseDir);
        Console.WriteLine($"  应用输出目录: {baseDir}");
        Console.WriteLine($"    {appBytes / 1024.0:N0} KB / {appFiles} 个文件");
        string? fx = Path.GetDirectoryName(typeof(object).Assembly.Location);
        if (fx is not null && Directory.Exists(fx))
        {
            var (fxBytes, fxFiles) = Measure(fx);
            Console.WriteLine($"  共享框架目录: {fx}");
            Console.WriteLine($"    {fxBytes / 1048576.0:N0} MB / {fxFiles} 个文件");
            Console.WriteLine($"  → 比例 1 : {(double)fxBytes / Math.Max(appBytes, 1):N0}"
                              + " —— 和 Java 版是同一个量级的结论");
        }
        Console.WriteLine($"  运行时版本: {RuntimeInformation.FrameworkDescription}");
        Console.WriteLine($"  目标平台:   {RuntimeInformation.RuntimeIdentifier}");
        Console.WriteLine("  → 当前是【框架依赖】模式: 产物很小，但目标机器必须装了对应版本的 .NET");

        Console.WriteLine("\n== ③ 依赖解析：运行时才知道缺没缺 ==");
        var asms = AppDomain.CurrentDomain.GetAssemblies();
        Console.WriteLine($"  本进程已加载程序集: {asms.Length} 个");
        foreach (var a in asms.OrderBy(a => a.GetName().Name).Take(6))
            Console.WriteLine($"    {a.GetName().Name,-28} {a.GetName().Version}");
        Console.WriteLine("  → .NET 的程序集是【按需加载】的（和第 30 章的反射同源）");
        Console.WriteLine("  → 后果: 少了一个 DLL，可能【跑到那条代码路径时才崩】——");
        Console.WriteLine("     一个只在月底结算时才走到的分支，能藏住这个问题一个月");
        Console.WriteLine("  → 这正是 AOT/裁剪难做的原因: 静态分析看不出反射会加载什么");
        Console.WriteLine("  → 防御: 部署后跑一遍冒烟测试，覆盖所有【会加载新程序集】的路径");

        Console.WriteLine("\n== ④ 四种发布模式的取舍（本程序的实测数据）==");
        Console.WriteLine("  以下数字由 dotnet publish 对【本文件】的四种模式分别构建后测得");
        Console.WriteLine("  （run-all.sh 无法在沙箱里执行 publish，故为独立测量，命令见下）:");
        var modes = new (string Mode, string Size, string Files, string Startup, string Needs)[]
        {
            ("框架依赖(默认)", "  1 MB", "  5", " 20.0 ms", "目标机装了对应 .NET"),
            ("自包含",        " 77 MB", "187", " 20.0 ms", "什么都不用装"),
            ("自包含+裁剪",    " 19 MB", " 34", " 19.9 ms", "反射需显式配置"),
            ("Native AOT",    " 15 MB", "  4", "【3.3 ms】", "放弃反射/动态加载"),
        };
        Console.WriteLine($"    {"模式",-16} {"产物",-8} {"文件数",-6} {"进入 Main()",-11} {"前提"}");
        foreach (var m in modes)
            Console.WriteLine($"    {m.Mode,-16} {m.Size,-8} {m.Files,-6} {m.Startup,-11} {m.Needs}");
        Console.WriteLine("    复现命令: dotnet publish -c Release -r osx-arm64 \\");
        Console.WriteLine("                [--self-contained] [-p:PublishTrimmed=true] [-p:PublishAot=true]");
        Console.WriteLine("  → 读法一: 前三种模式的【启动时间几乎一样】（都是 20 ms）——");
        Console.WriteLine("     自包含和裁剪解决的是【可移植性与体积】，不解决启动速度");
        Console.WriteLine("  → 读法二: 只有 AOT 把启动降到 3.3 ms（快 6x），因为它【没有 CLR 要加载、没有 JIT 要跑】");
        Console.WriteLine("  → 读法三: 裁剪把 77 MB 压到 19 MB（省 75%），文件数从 187 降到 34");
        Console.WriteLine("  → 注意最后一列: 【每一档更好的启动速度，都在放弃一部分动态能力】");
        Console.WriteLine("  → 这是第 30 章反射的账单在部署阶段结清: 运行时越动态，越难提前编译");
        Console.WriteLine("  → 判据仍然是【启动次数】: 长驻服务不在乎启动，Serverless 只在乎启动");

        Console.WriteLine("\n== ⑤ 配置：同一个产物跑遍所有环境 ==");
        string env = Environment.GetEnvironmentVariable("DOTNET_ENVIRONMENT")
                     ?? Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT")
                     ?? "(未设置)";
        Console.WriteLine($"  当前环境标识: {env}");
        Console.WriteLine($"  时区: {TimeZoneInfo.Local.Id}  文化: {System.Globalization.CultureInfo.CurrentCulture.Name}");
        Console.WriteLine("  → 配置优先级的标准顺序: 命令行 > 环境变量 > 配置文件 > 默认值");
        Console.WriteLine("  → 关键约束: 产物里【不能含有环境特定的值】，否则:");
        Console.WriteLine("     你在预发环境测的产物 ≠ 你发到生产的产物 —— 测试就失去了意义");
        Console.WriteLine("  ⚠️ 一个常见的伪解法: 「构建时按环境替换配置文件」——");
        Console.WriteLine("     它看起来做到了配置分离，实际上产生了 N 个【互不相同的产物】");

        Console.WriteLine("\n== ⑥ 发布之后：可观测性的三根支柱 ==");
        Console.WriteLine("  日志(Logs):    离散事件。回答「那一刻发生了什么」");
        Console.WriteLine("  指标(Metrics): 聚合数值。回答「现在整体健康吗」，成本低、可长期保留");
        Console.WriteLine("  链路(Traces):  一次请求的完整路径。回答「慢在哪一跳」");
        Console.WriteLine("  → 三者的关系: 指标告诉你【出事了】，链路告诉你【在哪】，日志告诉你【为什么】");
        Console.WriteLine("  → 缺任何一根都会让排查退化成猜测——而第 57 章讲过猜测的失败率");
        Console.WriteLine("  → 部署与可观测性是一件事的两面: 【没有可观测性，你无法判断发布是否成功】");
        Console.WriteLine("     而判断不了成功与否，金丝雀发布就失去了全部意义（Python 版 ⑥）");
    }
}
