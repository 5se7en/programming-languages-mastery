// 测试：测试替身的三张面孔——stub、mock、fake 各验证什么，以及 fake 测不出的那类 bug。
using System.Diagnostics;

// ============ 被测系统: 订单服务，依赖「仓储」和「通知」 ============

interface IOrderRepo
{
    void Save(string id, int cents);
    int? Find(string id);
}

interface INotifier
{
    void Send(string message);
}

class OrderService
{
    private readonly IOrderRepo _repo;
    private readonly INotifier _notifier;
    public OrderService(IOrderRepo repo, INotifier notifier) { _repo = repo; _notifier = notifier; }

    public void PlaceOrder(string id, int cents)
    {
        if (cents <= 0) throw new ArgumentException("金额必须为正");
        _repo.Save(id, cents);
        if (cents >= 10000) _notifier.Send($"大额订单 {id}: {cents / 100.0:F2} 元");
    }
}

// ============ 三种测试替身 ============

/// ① stub: 只提供【固定返回值】——用来喂数据，什么都不验证
class StubRepo : IOrderRepo
{
    public void Save(string id, int cents) { }
    public int? Find(string id) => 9999;             // 永远返回固定值
}

/// ② mock: 记录【交互】——验证「有没有以正确的方式调用依赖」
class MockNotifier : INotifier
{
    public List<string> Sent = new();
    public void Send(string message) => Sent.Add(message);
}

/// ③ fake: 一个【能工作的轻量实现】——行为真实，但省掉了昂贵的部分
class FakeRepo : IOrderRepo
{
    private readonly Dictionary<string, int> _rows = new();
    public void Save(string id, int cents) => _rows[id] = cents;
    public int? Find(string id) => _rows.TryGetValue(id, out var v) ? v : null;
}

/// 「真实」仓储: 存文件系统——藏着一条 fake 不知道的现实规则
class FileRepo : IOrderRepo
{
    private readonly string _dir;
    public FileRepo(string dir) { _dir = dir; Directory.CreateDirectory(dir); }
    public void Save(string id, int cents) =>
        File.WriteAllText(Path.Combine(_dir, id + ".txt"), cents.ToString());
    public int? Find(string id)
    {
        var p = Path.Combine(_dir, id + ".txt");
        return File.Exists(p) ? int.Parse(File.ReadAllText(p)) : null;
    }
}

class Program
{
    static int passed = 0, failed = 0;
    static void Check(string name, Action test)
    {
        try { test(); Console.WriteLine($"    ✓ {name}"); passed++; }
        catch (Exception e) { Console.WriteLine($"    ✗ {name} —— {e.Message}"); failed++; }
    }

    static void Main()
    {
        string work = Directory.CreateTempSubdirectory("pl-mastery-test-").FullName;

        Console.WriteLine("== ① 三种替身各验证什么（同一个被测系统）==");
        Console.WriteLine("  stub —— 喂数据: 「当依赖返回 X 时，我的逻辑对不对」");
        Check("stub: 只关心返回值", () =>
        {
            var svc = new OrderService(new StubRepo(), new MockNotifier());
            svc.PlaceOrder("A1", 500);               // stub 不验证任何东西，只是不碍事
        });

        Console.WriteLine("  mock —— 验交互: 「我有没有【正确地调用】依赖」");
        Check("mock: 大额订单必须发通知", () =>
        {
            var notifier = new MockNotifier();
            var svc = new OrderService(new FakeRepo(), notifier);
            svc.PlaceOrder("A2", 20000);
            if (notifier.Sent.Count != 1) throw new Exception($"期望 1 条通知，实际 {notifier.Sent.Count}");
            if (!notifier.Sent[0].Contains("200.00")) throw new Exception("通知内容错误");
        });
        Check("mock: 小额订单不该发通知", () =>
        {
            var notifier = new MockNotifier();
            new OrderService(new FakeRepo(), notifier).PlaceOrder("A3", 500);
            if (notifier.Sent.Count != 0) throw new Exception("小额订单不应发通知");
        });

        Console.WriteLine("  fake —— 真行为: 「存进去的东西能【原样取出来】吗」");
        Check("fake: 保存后能读回", () =>
        {
            var repo = new FakeRepo();
            new OrderService(repo, new MockNotifier()).PlaceOrder("A4", 1250);
            if (repo.Find("A4") != 1250) throw new Exception("读回的值不对");
        });
        Console.WriteLine("  → 三种替身回答三种问题: 逻辑对吗 / 交互对吗 / 往返对吗");
        Console.WriteLine("  → 术语常被混用成「mock」，但选错类型会让测试【验证不到你想验证的】");

        Console.WriteLine("\n== ② fake 测不出的 bug（实测）==");
        Console.WriteLine("  订单号带斜杠「2026/08/001」——fake 的 Dictionary 毫不在意:");
        Check("fake: 斜杠订单号照常工作", () =>
        {
            var repo = new FakeRepo();
            new OrderService(repo, new MockNotifier()).PlaceOrder("2026/08/001", 100);
            if (repo.Find("2026/08/001") != 100) throw new Exception("读回失败");
        });
        Console.WriteLine("  同样的用例换成【真实文件系统】:");
        Check("真实 FileRepo: 斜杠订单号", () =>
        {
            var repo = new FileRepo(Path.Combine(work, "orders"));
            new OrderService(repo, new MockNotifier()).PlaceOrder("2026/08/001", 100);
            if (repo.Find("2026/08/001") != 100) throw new Exception("读回失败");
        });
        Console.WriteLine("  → fake 过了，真实现挂了: 「/」在文件路径里是目录分隔符！");
        Console.WriteLine("  → fake 的保真度边界: 它复刻了【接口语义】，复刻不了【底层介质的规则】");
        Console.WriteLine("  → 与 Python 版 ②（mock 掩盖网关规则）同一个教训的另一张面孔");
        Console.WriteLine("  → 处方: fake 支撑日常快速反馈，再留【少量集成测试】跑真实介质");

        Console.WriteLine("\n== ③ 替身的速度红利（实测）==");
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < 2000; i++)
        {
            var repo = new FakeRepo();
            new OrderService(repo, new MockNotifier()).PlaceOrder($"F{i}", 100 + i);
        }
        double msFake = sw.Elapsed.TotalMilliseconds;

        var fileRepo = new FileRepo(Path.Combine(work, "bench"));
        sw = Stopwatch.StartNew();
        for (int i = 0; i < 2000; i++)
            new OrderService(fileRepo, new MockNotifier()).PlaceOrder($"R{i}", 100 + i);
        double msReal = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  2000 个测试用 fake:      {msFake,7:F1} ms");
        Console.WriteLine($"  2000 个测试用真实文件:   {msReal,7:F1} ms（慢 {msReal / msFake:F0}x）");
        Console.WriteLine("  → 这就是替身存在的理由——快到能【每次保存都跑全部测试】");
        Console.WriteLine("  → 与 ② 合起来是完整结论: 快与真不可兼得，所以金字塔要分层（Python 版 ①）");

        Console.WriteLine("\n== ④ 被测系统的可测性来自哪里 ==");
        Console.WriteLine("  OrderService 依赖的是【接口】IOrderRepo/INotifier，不是具体类");
        Console.WriteLine("  → 所以才【换得进】替身——这就是依赖注入（第 55 章的主角）");
        Console.WriteLine("  → 反例: 在方法里 new FileRepo() 或调静态方法 → 什么都换不掉 → 测不了");
        Console.WriteLine("  → 「难测」几乎总是设计问题的信号: 依赖写死、副作用糊进逻辑、全局状态");

        Console.WriteLine($"\n  本示例测试汇总: {passed} 通过，{failed} 失败（斜杠那个失败是【演示用的】）");
        Console.WriteLine("  → .NET 生态: xUnit（框架）+ Moq/NSubstitute（mock 库）+ coverlet（覆盖率）");

        Directory.Delete(work, true);
    }
}
