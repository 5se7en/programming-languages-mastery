using System;
using System.Diagnostics;
using System.Threading.Tasks;

class Program
{
    static int counter = 100;              // 进程内的静态变量——子进程看不到

    static async Task Main()
    {
        Console.WriteLine("== ① 进程身份 ==");
        var self = Process.GetCurrentProcess();
        Console.WriteLine($"  我是进程 {self.Id}，进程名 {self.ProcessName}");
        Console.WriteLine($"  处理器数 = {Environment.ProcessorCount}"
                + $"，工作集 = {self.WorkingSet64 / 1024 / 1024} MB");

        Console.WriteLine("\n== ② 钥匙实验：子进程是另一个世界 ==");
        Console.WriteLine($"  父进程 counter = {counter}");
        var psi = new ProcessStartInfo("sh", "-c \"echo '  [子进程 '$$'] 我看不到 C# 的 counter 变量'\"")
        {
            RedirectStandardOutput = true,
            UseShellExecute = false
        };
        using (var child = Process.Start(psi)!)
        {
            Console.Write(await child.StandardOutput.ReadToEndAsync());
            await child.WaitForExitAsync();
            Console.WriteLine($"  子进程退出码 = {child.ExitCode}，父进程 counter 仍是 {counter}");
        }

        Console.WriteLine("\n== ③ 进程间通信：重定向标准流 ==");
        var grepPsi = new ProcessStartInfo("grep", "并发")
        {
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            UseShellExecute = false
        };
        using (var grep = Process.Start(grepPsi)!)
        {
            await grep.StandardInput.WriteAsync("第 39 章 进程\n第 40 章 线程\nPart 6 并发\n");
            grep.StandardInput.Close();
            Console.Write($"  grep 返回: {await grep.StandardOutput.ReadToEndAsync()}");
            await grep.WaitForExitAsync();
        }

        Console.WriteLine("\n== ④ 进程列表：系统全局视野 ==");
        var all = Process.GetProcesses();
        Console.WriteLine($"  系统上共有 {all.Length} 个可见进程");
        Console.WriteLine($"  当前进程已运行 {(DateTime.Now - self.StartTime).TotalMilliseconds:F0} ms");

        Console.WriteLine("\n== ⑤ .NET 的立场：与 Java 相同 ==");
        Console.WriteLine("  进程用于调用外部程序；并行计算交给线程池与 Task（第 45 章）");
        Console.WriteLine("  跨平台 API 统一（Windows 的 CreateProcess / Unix 的 fork+exec 被抹平）");
        Console.WriteLine("  WaitForExitAsync 让等待子进程也能 await（第 42 章）");
    }
}
