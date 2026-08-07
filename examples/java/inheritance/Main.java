// 第 26 章 · 继承 —— Java 示例
// 运行：javac Main.java && java Main
// Java 的选择：单继承 + 多接口，从语言层面回避菱形问题

import java.util.*;

public class Main {

    // ---------- ① 脆弱基类问题：本章最重要的实测 ----------
    /** 想统计一共 add 过多少元素，于是继承 HashSet 重写两个方法 */
    static class CountingSetBad<E> extends HashSet<E> {
        int addCount = 0;

        @Override
        public boolean add(E e) {
            addCount++;
            return super.add(e);
        }

        @Override
        public boolean addAll(Collection<? extends E> c) {
            addCount += c.size();
            return super.addAll(c);      // ⚠️ 父类的 addAll 内部又逐个调用了 add！
        }
    }

    /** 组合版本：不继承 HashSet，而是「持有」一个 Set */
    static class CountingSetGood<E> {
        private final Set<E> inner = new HashSet<>();   // 组合：has-a
        private int addCount = 0;

        public boolean add(E e) {
            addCount++;
            return inner.add(e);
        }

        public boolean addAll(Collection<? extends E> c) {
            addCount += c.size();
            boolean changed = false;
            for (E e : c) changed |= inner.add(e);      // 直接操作 inner，不经过自己的 add
            return changed;
        }

        public int getAddCount() { return addCount; }
        public int size() { return inner.size(); }
    }

    // ---------- ② 构造函数调用可重写方法的陷阱 ----------
    static class Base {
        Base() { init(); }               // ⚠️ 危险：调用了可被重写的方法
        void init() { }
    }

    static class Derived extends Base {
        private int value = 42;
        @Override
        void init() {
            System.out.println("    子类 init() 看到的 value = " + value + "  ← 期望 42！");
        }
    }

    // ---------- ③ 正常的继承：is-a 关系成立 ----------
    static class Animal {
        protected final String name;
        Animal(String name) { this.name = name; }
        String speak() { return name + " 发出声音"; }
        final String identity() { return "我是 " + name; }   // final：禁止重写
    }

    static class Dog extends Animal {
        Dog(String name) { super(name); }
        @Override                                   // 编译器会检查确实覆盖了父类方法
        String speak() { return super.speak() + "：汪！"; }
    }

    // ---------- ④ 为了复用而继承的反面教材 ----------
    /** Java 早期 java.util.Stack extends Vector 的真实设计错误 */
    static class BadStack<E> extends ArrayList<E> {
        public void push(E e) { add(e); }
        public E pop() { return remove(size() - 1); }
        // ⚠️ 但同时继承来了 get(i)、add(i, e)、remove(i)... 全都破坏栈语义
    }

    public static void main(String[] args) {
        System.out.println("=== 1. ⚠️ 脆弱基类问题（Effective Java Item 18）===");
        CountingSetBad<String> bad = new CountingSetBad<>();
        bad.addAll(List.of("x", "y", "z"));
        System.out.println("  继承版本: addAll 3 个元素");
        System.out.println("    期望 addCount = 3，实际 addCount = " + bad.addCount + "  ← 翻倍了！");
        System.out.println();
        System.out.println("  执行过程：");
        System.out.println("    调用 addAll(3 个元素)");
        System.out.println("      → 子类的 addAll: addCount += 3          (现在是 3)");
        System.out.println("      → super.addAll()");
        System.out.println("          → HashSet 内部逐个调用 add() × 3");
        System.out.println("          → 但方法查找找到的是子类重写的 add！");
        System.out.println("          → 子类的 add: addCount++ × 3         (现在是 6)");
        System.out.println();
        System.out.println("  ⚠️ 致命之处：HashSet.addAll 内部是否调用 add 是「父类的实现细节」");
        System.out.println("     它没写在文档里，也随时可能在下个版本改掉。");
        System.out.println("     你的子类正确与否，取决于一件你无法控制、甚至无法得知的事。");

        System.out.println("\n=== 2. 组合修复了这个问题 ===");
        CountingSetGood<String> good = new CountingSetGood<>();
        good.addAll(List.of("x", "y", "z"));
        System.out.println("  组合版本: addAll 3 个元素 → addCount = " + good.getAddCount() + "  ✓ 正确");
        System.out.println();
        System.out.println("  为什么组合能解决：");
        System.out.println("    继承：子类依赖父类的「实现细节」  → 细节一变，子类就坏");
        System.out.println("    组合：我只依赖 inner 的「公开接口」→ 它内部怎么实现与我无关");
        System.out.println("  → 这就是「组合优于继承」的实质");

        System.out.println("\n=== 3. ⚠️ 构造函数调用可重写方法 ===");
        System.out.println("  class Base { Base() { init(); } }");
        System.out.println("  class Derived extends Base { int value = 42; init() {...} }");
        System.out.println("  new Derived() 的输出：");
        new Derived();
        System.out.println("  → 打印的是 0，不是 42！");
        System.out.println("  → 构造顺序：父类字段 → 父类构造体 → 子类字段 → 子类构造体");
        System.out.println("     父类构造函数执行时，子类的字段还没初始化");
        System.out.println("  → 规则：永远不要在构造函数里调用可被重写的方法（Item 19）");

        System.out.println("\n=== 4. 正常的继承：is-a 成立 ===");
        Animal a = new Dog("旺财");        // 用父类类型持有子类对象
        System.out.println("  new Dog(\"旺财\").speak() = " + a.speak());
        System.out.println("  a instanceof Dog    = " + (a instanceof Dog));
        System.out.println("  a instanceof Animal = " + (a instanceof Animal));
        System.out.println("  → 这才是继承的正当用途：Dog 确实「是一种」Animal");
        System.out.println("  → 而且在任何用 Animal 的地方都能安全替换（里氏替换原则）");

        System.out.println("\n=== 5. final：明确表达「不要继承/重写」 ===");
        System.out.println("  final class Immutable { }         // 禁止被继承");
        System.out.println("  public final String identity()     // 禁止被重写");
        System.out.println("  → Animal.identity() 是 final 的，子类改不了：" + a.identity());
        System.out.println("  ⚠️ Java 的方法默认「可重写」，每个 public 方法都成了潜在契约");
        System.out.println("     C# 的选择相反（默认 virtual 才能重写），更安全");

        System.out.println("\n=== 6. ⚠️ 为了复用而继承的反面教材 ===");
        BadStack<String> stack = new BadStack<>();
        stack.push("a");
        stack.push("b");
        stack.add(0, "插队的");             // ⚠️ 继承来的方法，完全破坏了栈语义
        System.out.println("  BadStack extends ArrayList：");
        System.out.println("    push(a), push(b) 后，调用继承来的 add(0, \"插队的\")");
        System.out.println("    → 栈内容变成 " + stack + "  ← 栈怎么能从中间插入？");
        System.out.println("  → 这正是 Java 早期 java.util.Stack extends Vector 的真实错误");
        System.out.println("  → 现在推荐用 Deque（第 18 章）");

        System.out.println("\n=== 7. 判断该不该继承的三个问题 ===");
        System.out.println("  ① 是 is-a 还是 has-a？");
        System.out.println("     「Dog 是 Animal」✓     「CountingSet 是 HashSet」✗（只是「用了」）");
        System.out.println("  ② 满足里氏替换吗？");
        System.out.println("     任何用父类的代码，换成子类还正确吗？");
        System.out.println("  ③ 父类会变吗？");
        System.out.println("     第三方库的类随时可能改实现 → 脆弱基类风险");
        System.out.println("  → 只要有一个答案不理想，就该用组合");

        System.out.println("\n=== 8. 小结 ===");
        System.out.println("  · 继承 = 代码复用 + is-a 承诺；只想要复用时，用组合");
        System.out.println("  · 脆弱基类：实测 addAll 3 个元素得到 6，组合版本正确得到 3");
        System.out.println("  · 构造函数里绝不调用可重写方法");
        System.out.println("  · Java 单继承 + 多接口，从语言层面回避了菱形问题");
        System.out.println("  · @Override 不是装饰，它能在签名不匹配时报错，务必标注");
    }
}
