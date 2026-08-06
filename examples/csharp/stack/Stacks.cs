// 第 18 章 · 栈 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Collections.Generic;

class Stacks
{
    static bool IsBalanced(string s)
    {
        var pairs = new Dictionary<char, char> { [')'] = '(', [']'] = '[', ['}'] = '{' };
        var st = new Stack<char>();
        foreach (var ch in s)
        {
            if ("([{".IndexOf(ch) >= 0) st.Push(ch);
            else if (pairs.ContainsKey(ch))
            {
                if (st.Count == 0 || st.Pop() != pairs[ch]) return false;
            }
        }
        return st.Count == 0;
    }

    static double EvalRPN(string expr)
    {
        var st = new Stack<double>();
        foreach (var t in expr.Split(' '))
        {
            if (t is "+" or "-" or "*" or "/")
            {
                double b = st.Pop(), a = st.Pop();      // 先弹出的是右操作数
                st.Push(t switch { "+" => a + b, "-" => a - b, "*" => a * b, _ => a / b });
            }
            else st.Push(double.Parse(t));
        }
        return st.Peek();
    }

    static void Main()
    {
        // 1. Stack<T> 是专门的栈类型，设计干净（不像 Java 继承自列表）
        var stack = new Stack<int>();
        stack.Push(1); stack.Push(2); stack.Push(3);
        Console.WriteLine($"Peek: {stack.Peek()} | Pop: {stack.Pop()} ← Pop 会返回值（比 C++ 方便）");
        Console.WriteLine($"Count: {stack.Count}");

        // 2. TryPop：空栈时不抛异常
        var empty = new Stack<int>();
        Console.WriteLine($"空栈 TryPop 成功? {empty.TryPop(out _)}  ← 安全版本");

        // 3. 遍历顺序是从栈顶到栈底（但不支持按下标访问）
        Console.Write("\nforeach 遍历顺序（顶→底）: ");
        foreach (var x in stack) Console.Write(x + " ");
        Console.WriteLine("← 可遍历但不可索引，语义边界清晰");

        // 4. 括号匹配
        Console.WriteLine();
        foreach (var s in new[] { "(a[b]{c})", "(a[b)]", "(((" })
            Console.WriteLine($"括号匹配 {s,-10} → {IsBalanced(s)}");

        // 5. 后缀表达式求值
        Console.WriteLine($"\n后缀 \"3 4 2 * +\" = {EvalRPN("3 4 2 * +")}  ← 等价 3 + 4*2");
        Console.WriteLine($"后缀 \"5 1 2 + 4 * + 3 -\" = {EvalRPN("5 1 2 + 4 * + 3 -")}");
    }
}
