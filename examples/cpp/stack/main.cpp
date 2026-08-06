// 第 18 章 · 栈 — C++ 示例
// 运行：g++ -std=c++17 -o main *.cpp && ./main
#include <iostream>
#include <stack>
#include <vector>
#include <string>
#include <map>
#include <sstream>

// 括号匹配
bool isBalanced(const std::string& s) {
    std::map<char, char> pairs{{')','('}, {']','['}, {'}','{'}};
    std::stack<char> st;
    for (char ch : s) {
        if (ch=='('||ch=='['||ch=='{') st.push(ch);
        else if (pairs.count(ch)) {
            if (st.empty() || st.top() != pairs[ch]) return false;
            st.pop();                          // 注意：先 top() 取值，再 pop()
        }
    }
    return st.empty();
}

// 后缀表达式求值
double evalRPN(const std::string& expr) {
    std::stack<double> st;
    std::istringstream iss(expr);
    std::string t;
    while (iss >> t) {
        if (t=="+"||t=="-"||t=="*"||t=="/") {
            double b = st.top(); st.pop();     // 先弹出的是右操作数
            double a = st.top(); st.pop();
            st.push(t=="+"?a+b : t=="-"?a-b : t=="*"?a*b : a/b);
        } else st.push(std::stod(t));
    }
    return st.top();
}

int main() {
    // 1. std::stack 是「容器适配器」——把底层容器的其他能力全部封死
    std::stack<int> s;
    s.push(1); s.push(2); s.push(3);
    std::cout << "栈顶 top(): " << s.top() << "\n";
    s.pop();                                   // ⚠️ pop() 不返回值！
    std::cout << "pop() 后栈顶: " << s.top() << "  ← pop() 返回 void，取值要先 top()\n";
    std::cout << "size: " << s.size() << " | empty: " << std::boolalpha << s.empty() << "\n";
    std::cout << "→ std::stack 无法按下标访问、无法遍历：限制即保证\n";

    // 2. 可以指定底层容器
    std::stack<int, std::vector<int>> sv;      // 用 vector 更省内存
    sv.push(42);
    std::cout << "\n用 vector 作底层容器: top = " << sv.top() << "\n";

    // 3. 括号匹配
    std::cout << "\n";
    for (const std::string& t : {"(a[b]{c})", "(a[b)]", "((("})
        std::cout << "括号匹配 " << t << " → " << isBalanced(t) << "\n";

    // 4. 后缀表达式
    std::cout << "\n后缀 \"3 4 2 * +\" = " << evalRPN("3 4 2 * +") << "  ← 等价 3 + 4*2\n";
    std::cout << "后缀 \"5 1 2 + 4 * + 3 -\" = " << evalRPN("5 1 2 + 4 * + 3 -") << "\n";

    // 5. ⚠️ 空栈调用 top()/pop() 是未定义行为，必须先判 empty()
    std::stack<int> empty;
    std::cout << "\n⚠️ 空栈调用 top() 是未定义行为（不是抛异常），务必先判 empty(): "
              << (empty.empty() ? "已判空，安全" : "") << "\n";
    return 0;
}
