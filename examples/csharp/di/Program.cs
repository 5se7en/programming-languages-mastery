// 依赖注入：三种注入方式的取舍，以及生命周期最经典的事故——被俘获的依赖。
using System.Diagnostics;

// ============ 依赖与实现 ============
interface IClock { DateTime Now { get; } }
interface IRepo { string Find(int id); }

class SystemClock : IClock { public DateTime Now => DateTime.Now; }

/// 「每请求一个」的依赖: 它持有本次请求的上下文（连接、事务、用户身份）
class RequestContext
{
    static int _seq;
    public int RequestId { get; } = ++_seq;
    public override string ToString() => $"Request#{RequestId}";
}

class ScopedRepo : IRepo
{
    private readonly RequestContext _ctx;
    public ScopedRepo(RequestContext ctx) => _ctx = ctx;
    public string Find(int id) => $"用户{id}（来自 {_ctx}）";
}

// ============ 三种注入方式 ============
class CtorInjected                      // ① 构造器注入（推荐）
{
    private readonly IRepo _repo;
    public CtorInjected(IRepo repo) => _repo = repo ?? throw new ArgumentNullException(nameof(repo));
    public string Run() => _repo.Find(1);
}

class PropInjected                      // ② 属性注入
{
    public IRepo? Repo { get; set; }    // ⚠️ 可空——用之前没人保证它被设过
    public string Run() => Repo is null ? "✗ NullReference: Repo 从没被设置" : Repo.Find(2);
}

class MethodInjected                    // ③ 方法注入
{
    public string Run(IRepo repo) => repo.Find(3);   // 依赖只在这个方法里用
}

// ============ 微型容器（带生命周期）============
enum Life { Transient, Scoped, Singleton }

class MiniContainer
{
    record Reg(Type Impl, Life Life);
    readonly Dictionary<Type, Reg> _regs = new();
    readonly Dictionary<Type, object> _singletons = new();

    public void Register<TService, TImpl>(Life life) where TImpl : TService =>
        _regs[typeof(TService)] = new Reg(typeof(TImpl), life);
    public void Register<T>(Life life) => _regs[typeof(T)] = new Reg(typeof(T), life);

    /// scope = 一次请求的容器；Scoped 对象在同一个 scope 内共享
    public object Resolve(Type type, Dictionary<Type, object> scope)
    {
        if (!_regs.TryGetValue(type, out var reg)) reg = new Reg(type, Life.Transient);

        if (reg.Life == Life.Singleton && _singletons.TryGetValue(type, out var s)) return s;
        if (reg.Life == Life.Scoped && scope.TryGetValue(type, out var sc)) return sc;

        var ctor = reg.Impl.GetConstructors()[0];
        var args = ctor.GetParameters().Select(p => Resolve(p.ParameterType, scope)).ToArray();
        var instance = ctor.Invoke(args);

        if (reg.Life == Life.Singleton) _singletons[type] = instance;
        if (reg.Life == Life.Scoped) scope[type] = instance;
        return instance;
    }

    public T Resolve<T>(Dictionary<Type, object> scope) => (T)Resolve(typeof(T), scope);
}

// ============ 被俘获依赖的受害者 ============
class SingletonService                   // 注册成 Singleton，却依赖 Scoped 的 Repo
{
    public IRepo Repo { get; }
    public SingletonService(IRepo repo) => Repo = repo;
}

class Program
{
    static void Main()
    {
        Console.WriteLine("== ① 三种注入方式的取舍（实测各自的失败形态）==");
        var repo = new ScopedRepo(new RequestContext());

        var a = new CtorInjected(repo);
        Console.WriteLine($"  构造器注入: {a.Run()}");
        try { _ = new CtorInjected(null!); }
        catch (ArgumentNullException) { Console.WriteLine("    忘了传依赖 → 【构造时】就抛异常 ✓"); }

        var b = new PropInjected();
        Console.WriteLine($"  属性注入（忘了设置）: {b.Run()}");
        b.Repo = repo;
        Console.WriteLine($"  属性注入（设置后）:   {b.Run()}");

        Console.WriteLine($"  方法注入: {new MethodInjected().Run(repo)}");
        Console.WriteLine("  → 构造器: 依赖【必填且不可变】——对象一旦存在就是完整的（推荐默认）");
        Console.WriteLine("  → 属性:   依赖可选可换——代价是对象可能处于【半成品】状态（实测 NullReference）");
        Console.WriteLine("  → 方法:   依赖只服务单个方法——不该升级成字段的临时依赖");

        Console.WriteLine("\n== ② 三种生命周期（实测同一次/不同次请求的实例数）==");
        var c = new MiniContainer();
        c.Register<IClock, SystemClock>(Life.Singleton);
        c.Register<RequestContext>(Life.Scoped);
        c.Register<IRepo, ScopedRepo>(Life.Scoped);

        var scope1 = new Dictionary<Type, object>();
        var r1a = c.Resolve<IRepo>(scope1);
        var r1b = c.Resolve<IRepo>(scope1);
        var scope2 = new Dictionary<Type, object>();
        var r2 = c.Resolve<IRepo>(scope2);

        Console.WriteLine($"  同一个 scope 内两次解析 IRepo: 同一实例? {ReferenceEquals(r1a, r1b)}");
        Console.WriteLine($"  不同 scope 解析 IRepo:        同一实例? {ReferenceEquals(r1a, r2)}");
        Console.WriteLine($"    scope1 → {r1a.Find(1)}");
        Console.WriteLine($"    scope2 → {r2.Find(1)}");
        Console.WriteLine($"  Singleton 的 IClock: 跨 scope 同一实例? " +
                          $"{ReferenceEquals(c.Resolve<IClock>(scope1), c.Resolve<IClock>(scope2))}");
        Console.WriteLine("  → Transient 每次新建 / Scoped 每请求一个 / Singleton 全局一个");
        Console.WriteLine("  → Web 应用里 scope 就是【一次 HTTP 请求】: 连接、事务、用户身份都挂在它上面");

        Console.WriteLine("\n== ③ 被俘获的依赖（Captive Dependency）——DI 最经典的事故（实测）==");
        c.Register<SingletonService>(Life.Singleton);      // ⚠️ Singleton 依赖 Scoped
        var scopeA = new Dictionary<Type, object>();
        var svcA = c.Resolve<SingletonService>(scopeA);
        Console.WriteLine($"  第 1 次请求解析 SingletonService → {svcA.Repo.Find(1)}");

        var scopeB = new Dictionary<Type, object>();
        var svcB = c.Resolve<SingletonService>(scopeB);
        Console.WriteLine($"  第 2 次请求解析 SingletonService → {svcB.Repo.Find(1)}");
        Console.WriteLine($"  两次拿到的是同一个 SingletonService? {ReferenceEquals(svcA, svcB)}");
        Console.WriteLine("  ⚠️ 第 2 次请求【用着第 1 次请求的 RequestContext】——它被单例俘获了");
        Console.WriteLine("  → 真实后果: 数据库连接/事务/当前用户跨请求泄漏——安全与正确性双重事故");
        Console.WriteLine("  → 规则: 长生命周期【不能】依赖短生命周期（Singleton ⊅ Scoped ⊅ Transient）");
        Console.WriteLine("  → ASP.NET Core 的 ValidateScopes 会在启动时把这种组合直接【报错】");
        Console.WriteLine("  → 必须这么用时: 注入 IServiceScopeFactory，用时现开一个 scope");

        Console.WriteLine("\n== ④ 容器的代价：反射解析不是免费的（实测）==");
        const int N = 200_000;
        var scope = new Dictionary<Type, object>();
        c.Register<IRepo, ScopedRepo>(Life.Transient);
        c.Register<RequestContext>(Life.Transient);

        var sw = Stopwatch.StartNew();
        for (int i = 0; i < N; i++) _ = c.Resolve<IRepo>(new Dictionary<Type, object>());
        double msContainer = sw.Elapsed.TotalMilliseconds;

        sw = Stopwatch.StartNew();
        for (int i = 0; i < N; i++) _ = new ScopedRepo(new RequestContext());
        double msManual = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  容器解析 {N} 次: {msContainer,7:F1} ms（{msContainer * 1000 / N:F2} μs/次）");
        Console.WriteLine($"  手工 new {N} 次:  {msManual,7:F1} ms（{msManual * 1000 / N:F2} μs/次）");
        Console.WriteLine($"  → 慢 {msContainer / msManual:F0}x —— 反射查构造器 + 递归解析的开销");
        Console.WriteLine("  → 但注意分母: 每次几微秒，而一次 HTTP 请求是毫秒级——通常可忽略");
        Console.WriteLine("  → 真实容器还会【缓存编译好的构造委托】（表达式树，第 51 章），比本例快得多");
        Console.WriteLine("  → 编译期 DI（Dagger/Micronaut/源生成器）干脆把装配代码生成出来: 零反射");

        Console.WriteLine("\n== ⑤ 什么该注入，什么不该 ==");
        Console.WriteLine("  该注入: 有【外部副作用】或【多实现】的东西——数据库、HTTP 客户端、时钟、随机源");
        Console.WriteLine("          （第 52 章实测: 时间与随机是 flaky 测试的两大来源，注入即驯服）");
        Console.WriteLine("  不该注入: 纯数据结构、值对象、纯函数工具类——它们没有替换的必要");
        Console.WriteLine("  → 判据: 「测试时我需要换掉它吗？」——不需要就别为它加一层间接");
        Console.WriteLine("  → 过度 DI 的味道: 每个类都有接口、接口只有一个实现、名字叫 IFooService/FooService");
    }
}
