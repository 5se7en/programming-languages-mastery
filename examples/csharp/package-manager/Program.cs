// 包管理：NuGet 的答案——「最低适用版本」；以及语义化版本的承诺与谎言。
using System.Text;

class Program
{
    // ---------- 一个微型包仓库 ----------
    // 版本: (major, minor, patch)
    record Ver(int Major, int Minor, int Patch) : IComparable<Ver>
    {
        public int CompareTo(Ver? o) => (Major, Minor, Patch).CompareTo((o!.Major, o.Minor, o.Patch));
        public override string ToString() => $"{Major}.{Minor}.{Patch}";
    }

    static readonly Dictionary<string, List<Ver>> Registry = new()
    {
        ["json-lib"] = new()
        {
            new(1, 2, 0), new(1, 5, 0), new(1, 9, 3), new(2, 0, 0), new(2, 3, 1),
        },
    };

    public static void Main()
    {
        Console.WriteLine("== ① 同样的约束，两种求解哲学（手写实测）==");
        // app → lib-a（要求 json-lib >= 1.2.0）；app → lib-b（要求 json-lib >= 1.5.0）
        var constraints = new List<(string from, Ver min)>
        {
            ("lib-a", new Ver(1, 2, 0)),
            ("lib-b", new Ver(1, 5, 0)),
        };
        Console.WriteLine("  约束: lib-a → json-lib >= 1.2.0；lib-b → json-lib >= 1.5.0");
        Console.WriteLine($"  仓库里的版本: {string.Join(", ", Registry["json-lib"])}");

        Ver floor = constraints.Max(c => c.min)!;                       // 所有下界的最大值
        Ver nugetPick = Registry["json-lib"].Where(v => v.CompareTo(floor) >= 0).Min()!;
        Ver npmPick = Registry["json-lib"].Where(v => v.CompareTo(floor) >= 0).Max()!;

        Console.WriteLine($"  NuGet（最低适用版本）: 选 {nugetPick}   ← 满足所有约束的【最老】版本");
        Console.WriteLine($"  npm 风格（最新满足）:  选 {npmPick}   ← 满足所有约束的【最新】版本");
        Console.WriteLine("  → NuGet 的哲学: 「你声明 >= 1.5.0，我就信你【测过】1.5.0」——可复现优先");
        Console.WriteLine("  → npm 的哲学:   「新版本有修复，默认要最新」——时效优先，可复现交给 lockfile");
        Console.WriteLine("  → 没有对错: 前者可能错过安全修复，后者可能引入未测过的行为（见 ②）");

        Console.WriteLine("\n== ② 语义化版本的承诺与谎言（实测一个「补丁」破坏行为）==");
        // json-lib 1.9.2 → 1.9.3: 补丁号 +1，承诺「只修 bug，不改行为」
        string FormatName_v192(string first, string last) => $"{last}, {first}";
        string FormatName_v193(string first, string last) => $"{first} {last}";   // “修复”了格式

        // 调用方的代码【固定不变】: 按 v1.9.2 的输出格式「姓, 名」解析姓氏
        string ExtractLastName(string formatted) => formatted.Split(',')[0].Trim();
        string old192 = FormatName_v192("三", "张");
        Console.WriteLine($"  v1.9.2 的 FormatName: \"{old192}\" → 调用方解析姓氏: \"{ExtractLastName(old192)}\" ✓");
        string new193 = FormatName_v193("三", "张");
        Console.WriteLine($"  v1.9.3「补丁」后:     \"{new193}\" → 同样的解析代码得到: \"{ExtractLastName(new193)}\" ✗（整串当成了姓）");
        Console.WriteLine("  → 补丁号没动 API 签名（semver 意义上「兼容」），却改变了【行为】");
        Console.WriteLine("  → semver 是【作者的自我声明】，不是编译器验证过的契约");
        Console.WriteLine("  → Hyrum 定律: 只要用户够多，你的每一个可观察行为都会被人依赖——");
        Console.WriteLine("     所以「只修 bug」的补丁也可能是某人的 breaking change");

        Console.WriteLine("\n== ③ 锁文件之外，.NET 还锁了什么 ==");
        Console.WriteLine("  packages.lock.json : 锁版本（与 npm/pip 的 lockfile 同义）");
        Console.WriteLine("  <app>.deps.json    : 发布产物里的【运行时清单】——精确到每个程序集的版本与哈希");
        Console.WriteLine("  → .NET Framework 时代靠 bindingRedirect 在【运行时】把 1.x 重定向到 2.0——");
        Console.WriteLine("     一个 XML 配置就能改变加载哪个版本（也因此制造过无数深夜事故）");
        Console.WriteLine("  → .NET Core 起统一交给 deps.json: 构建期决定一切，运行期不再变魔术");

        Console.WriteLine("\n== ④ 传递依赖的可见性：NuGet 的一个好设计 ==");
        Console.WriteLine("  npm 的幽灵依赖（JS 版实测）: 提升让没声明的包也 require 得到");
        Console.WriteLine("  NuGet/.NET: 传递依赖的程序集虽在输出目录，但【编译器默认不让你引用】——");
        Console.WriteLine("     想用 json-lib 就必须自己 PackageReference 声明");
        Console.WriteLine("  → 把「物理够得着」和「逻辑上声明过」分开——pnpm 后来用符号链接达到同样效果");

        Console.WriteLine("\n== ⑤ 中央包管理（CPM）：Monorepo 的版本统一 ==");
        Console.WriteLine("  Directory.Packages.props 把【所有项目的依赖版本】收进一个文件:");
        Console.WriteLine("    <PackageVersion Include=\"json-lib\" Version=\"2.3.1\" />");
        Console.WriteLine("  各项目只写 <PackageReference Include=\"json-lib\" />（不写版本）");
        Console.WriteLine("  → 和 Maven 的 BOM（Java 版 ④）是同一个思想: 版本决策【集中】做一次");
        Console.WriteLine("  → 大仓库里没有它: 30 个项目 30 个 json-lib 版本，升级 = 30 次 PR");

        Console.WriteLine("\n== ⑥ 供应链：装一个包到底装了什么 ==");
        int direct = 3, transitive = 42;
        Console.WriteLine($"  一个典型 Web 项目: 直接依赖 {direct} 个，传递依赖 {transitive} 个");
        Console.WriteLine("  每一个包 = 一段【将以你的权限运行】的代码 + 它的 install 脚本");
        Console.WriteLine("  真实事件: left-pad 下架瘫痪半个 npm、event-stream 被植入偷币代码、");
        Console.WriteLine("            typosquatting（requsts/lodahs）守株待兔等你打错字");
        Console.WriteLine("  防线: 锁文件哈希（Python 版 ⑤）→ 私有镜像/审计 → SBOM + 漏洞扫描（CI 里跑）");
        Console.WriteLine("  → 「装个包」在 2016 年之后就不再是纯技术决策，而是【信任决策】");
    }
}
