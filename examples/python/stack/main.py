# 第 18 章 · 栈 — Python 示例
# 运行：python3 main.py

# 1. Python 官方推荐用 list 作栈
stack = []
stack.append(1); stack.append(2); stack.append(3)
print("栈:", stack, "| 栈顶(peek):", stack[-1])
print("pop():", stack.pop(), "← LIFO")

# 2. 括号匹配
def is_balanced(s):
    pairs = {")": "(", "]": "[", "}": "{"}
    st = []
    for ch in s:
        if ch in "([{": st.append(ch)
        elif ch in pairs:
            if not st or st.pop() != pairs[ch]: return False
    return not st

for s in ["(a[b]{c})", "(a[b)]", "((("]:
    print(f"括号匹配 {s:<10} → {is_balanced(s)}")

# 3. 后缀表达式求值
def eval_rpn(tokens):
    st = []
    for t in tokens:
        if t in "+-*/":
            b, a = st.pop(), st.pop()          # 先弹出的是右操作数
            st.append({"+": a+b, "-": a-b, "*": a*b, "/": a/b}[t])
        else:
            st.append(float(t))
    return st[0]

print(f'\n后缀 "3 4 2 * +" = {eval_rpn("3 4 2 * +".split())}  ← 等价中缀 3 + 4*2')
print(f'后缀 "5 1 2 + 4 * + 3 -" = {eval_rpn("5 1 2 + 4 * + 3 -".split())}  ← 等价 5+((1+2)*4)-3')

# 4. 递归 ⇄ 显式栈：等价改写
class Node:
    def __init__(self, v, l=None, r=None): self.v, self.left, self.right = v, l, r

tree = Node(1, Node(2, Node(4), Node(5)), Node(3))

def dfs_recursive(node, out):
    if not node: return
    out.append(node.v); dfs_recursive(node.left, out); dfs_recursive(node.right, out)

def dfs_iterative(root):
    out, st = [], [root]
    while st:
        node = st.pop()
        if not node: continue
        out.append(node.v)
        st.append(node.right)     # 后压的先处理 → 保证左子树先访问
        st.append(node.left)
    return out

rec = []; dfs_recursive(tree, rec)
print(f"\n递归遍历:   {rec}")
print(f"显式栈遍历: {dfs_iterative(tree)}  ← 结果一致，且不会栈溢出")

# 5. ⚠️ pop(0) 是 O(n) 的队列语义，不是栈
print("\n⚠️ stack.pop() 是 O(1) 栈语义；stack.pop(0) 是 O(n) 队列语义")
