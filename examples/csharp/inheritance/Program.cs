// 第 26 章 · 继承 —— C# 示例
// 运行：dotnet run
// C# 与 Java 一样单继承 + 多接口，但「重写必须显式声明」—— 对症脆弱基类问题

using System;
using System.Collections.Generic;

// ---------- ① 默认不可重写：基类必须主动标记扩展点 ----------
public class Animal
{
    protected string Name { get; }
    public Animal(string name) => Name = name;

    public virtual string Speak() => $"{Name} 发出声音";    // 必须标 virtual 才能被重写
    public string Identity() => $"我是 {Name}";              // 不标 → 不能被重写
}

public class Dog : Animal
{
    public Dog(string name) : base(name) { }
    public override string Speak() => base.Speak() + "：汪！";   // 必须标 override
}

// ---------- ② sealed：禁止继续重写 / 禁止被继承 ----------
public class Cat : Animal
{
    public Cat(string name) : base(name) { }
    public sealed override string Speak() => base.Speak() + "：喵～";  // 子类不能再重写
}

public sealed class Immutable { }   // 整个类禁止被继承

// ---------- ③ new 关键字：方法隐藏（不是重写）----------
public class HideBase
{
    public virtual void M() => Console.WriteLine("    HideBase.M()");
}

public class HideDerived : HideBase
{
    public new void M() => Console.WriteLine("    HideDerived.M()");   // ⚠️ 隐藏，不是重写
}

public class OverrideDerived : HideBase
{
    public override void M() => Console.WriteLine("    OverrideDerived.M()");  // ✓ 真正的重写
}

// ---------- ④ 脆弱基类与组合修复 ----------
public class CountingSetBad : HashSet<string>
{
    public int AddCount { get; private set; }
    public new bool Add(string item)      // ⚠️ HashSet.Add 不是 virtual，只能 new 隐藏
    {
        AddCount++;
        return base.Add(item);
    }
}

public class CountingSetGood
{
    private readonly HashSet<string> _inner = new();     // 组合：has-a
    public int AddCount { get; private set; }

    public bool Add(string item)
    {
        AddCount++;
        return _inner.Add(item);
    }

    public void AddAll(IEnumerable<string> items)
    {
        foreach (var item in items)
        {
            AddCount++;
            _inner.Add(item);
        }
    }

    public int Count => _inner.Count;
}

// ---------- ⑤ abstract：不能实例化，必须被继承 ----------
public abstract class Shape
{
    public abstract double Area();                        // 子类必须实现
    public virtual string Describe() => $"面积 = {Area():F2}";
}

public class Circle : Shape
{
    private readonly double _r;
    public Circle(double r) => _r = r;
    public override double Area() => Math.PI * _r * _r;
}

class Program
{
    static void Main()
    {
        Console.WriteLine("=== 1. C# 与 Java 的关键差异：默认不可重写 ===");
        Animal a = new Dog("旺财");
        Console.WriteLine($"  new Dog(\"旺财\").Speak() = {a.Speak()}");
        Console.WriteLine($"  Identity() 没标 virtual → 子类改不了: {a.Identity()}");
        Console.WriteLine();
        Console.WriteLine("                  Java              C#");
        Console.WriteLine("  默认可否重写    可以（除非 final） 不可以（必须标 virtual）");
        Console.WriteLine("  重写标记        @Override 可选     override 强制");
        Console.WriteLine("  禁止继承        final class        sealed class");
        Console.WriteLine("  禁止继续重写    final 方法         sealed override");
        Console.WriteLine();
        Console.WriteLine("  → C# 的选择更安全：基类作者必须主动决定哪些方法是扩展点");
        Console.WriteLine("  → 这正好对症「脆弱基类」问题");
        Console.WriteLine("  → Java 的默认可重写让每个 public 方法都成了潜在契约");

        Console.WriteLine("\n=== 2. sealed override：到此为止，不许再重写 ===");
        Animal cat = new Cat("咪咪");
        Console.WriteLine($"  Cat.Speak() 标了 sealed override: {cat.Speak()}");
        Console.WriteLine("  → Cat 的子类无法再重写 Speak()");
        Console.WriteLine("  → sealed class Immutable：整个类禁止被继承");

        Console.WriteLine("\n=== 3. ⚠️ new 关键字：方法隐藏，不是多态 ===");
        HideBase b1 = new HideDerived();      // 用基类变量持有子类对象
        HideDerived d1 = new HideDerived();
        HideBase b2 = new OverrideDerived();

        Console.WriteLine("  HideBase b1 = new HideDerived();  b1.M():");
        b1.M();
        Console.WriteLine("  HideDerived d1 = new HideDerived();  d1.M():");
        d1.M();
        Console.WriteLine("  → 同一个对象，调用结果取决于「变量的静态类型」！");
        Console.WriteLine();
        Console.WriteLine("  对比真正的 override：");
        Console.WriteLine("  HideBase b2 = new OverrideDerived();  b2.M():");
        b2.M();
        Console.WriteLine("  → 无论变量是什么类型，都执行实际对象的方法 ← 这才是多态（第 27 章）");
        Console.WriteLine();
        Console.WriteLine("  ⚠️ new 造成的方法隐藏几乎总是设计错误的信号");
        Console.WriteLine("     编译器会对未标 new 的隐藏发出警告，");
        Console.WriteLine("     不要用「加 new」的方式消除警告，应该反思设计");

        Console.WriteLine("\n=== 4. 脆弱基类：C# 的表现与 Java 不同 ===");
        var bad = new CountingSetBad();
        bad.Add("x");
        bad.Add("y");
        bad.Add("z");
        Console.WriteLine($"  继承版本: 直接调 3 次 Add → AddCount = {bad.AddCount}");

        HashSet<string> asBase = bad;          // ⚠️ 当成基类使用时
        asBase.Add("w");                        // 调用的是 HashSet.Add，不是我的 Add！
        Console.WriteLine($"  但当成 HashSet 用时调 Add(\"w\") → AddCount 仍是 {bad.AddCount}");
        Console.WriteLine($"  而实际元素数已经是 {bad.Count} 个 → 计数漏了！");
        Console.WriteLine();
        Console.WriteLine("  ⚠️ 因为 HashSet.Add 不是 virtual，子类只能用 new 隐藏，");
        Console.WriteLine("     而 new 隐藏对基类类型的变量完全无效（见第 3 节）");
        Console.WriteLine("  → 继承一个「没有为继承而设计」的类，就是这种下场");

        Console.WriteLine("\n=== 5. 组合修复了这个问题 ===");
        var good = new CountingSetGood();
        good.AddAll(new[] { "x", "y", "z" });
        Console.WriteLine($"  组合版本: AddAll 3 个 → AddCount = {good.AddCount}  ✓ 正确");
        Console.WriteLine($"           元素数 = {good.Count}");
        Console.WriteLine();
        Console.WriteLine("  为什么组合能解决：");
        Console.WriteLine("    继承：子类依赖父类的实现细节，还被迫「是一个」HashSet");
        Console.WriteLine("    组合：只依赖 _inner 的公开接口，也不会被当成 HashSet 到处传");
        Console.WriteLine("  → 这就是「组合优于继承」的实质");

        Console.WriteLine("\n=== 6. abstract：定义必须实现的契约 ===");
        Shape s = new Circle(2);
        Console.WriteLine($"  new Circle(2).Describe() = {s.Describe()}");
        Console.WriteLine("  → abstract 方法强制子类实现，abstract 类不能实例化");
        Console.WriteLine("  → 这是「为继承而设计」的正确方式：明确哪些必须实现、哪些可选重写");

        Console.WriteLine("\n=== 7. 判断该不该继承的三个问题 ===");
        Console.WriteLine("  ① 是 is-a 还是 has-a？");
        Console.WriteLine("     「Dog 是 Animal」✓     「CountingSet 是 HashSet」✗");
        Console.WriteLine("  ② 满足里氏替换吗？");
        Console.WriteLine("     任何用父类的代码，换成子类还正确吗？");
        Console.WriteLine("  ③ 父类会变吗？");
        Console.WriteLine("     第三方库的类随时可能改实现 → 脆弱基类风险");

        Console.WriteLine("\n=== 8. 小结 ===");
        Console.WriteLine("  · C# 单继承 + 多接口，与 Java 相同");
        Console.WriteLine("  · 但重写必须显式（virtual + override），默认不可重写更安全");
        Console.WriteLine("  · new 是方法隐藏不是多态，调用结果取决于变量静态类型");
        Console.WriteLine("  · 实测：继承没为继承设计的类（HashSet），计数会漏");
        Console.WriteLine("  · 语言演进线：C++（全给）→ Java（砍多继承）→ C#（再砍默认可重写）");
    }
}
