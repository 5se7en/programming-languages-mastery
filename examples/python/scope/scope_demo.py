# 第 13 章 · 作用域 — Python 示例
# 运行：python3 scope_demo.py

# 1. LEGB 规则：Local → Enclosing → Global → Built-in
x = "全局 G"

def outer():
    y = "外层 E"
    def inner():
        z = "局部 L"
        return f"{z} → {y} → {x} → {len.__name__}(内置 B)"
    return inner()
print("LEGB:", outer())

# 2. ⚠️ Python 没有块作用域：if/for 里定义的变量在外面仍可见
if True:
    inside_if = "if 里定义的"
for i in range(3):
    pass
print("没有块作用域:", inside_if, "| 循环变量 i 泄漏 =", i)

# 3. UnboundLocalError：函数内有赋值 → 该名字全程为局部
def broken():
    try:
        print(x)          # 报错！因为下面有 x = ...
        x = "局部"
    except UnboundLocalError as e:
        return f"UnboundLocalError: {e}"
print("函数内有赋值:", broken())

# 4. global / nonlocal：显式声明才能修改外层
def use_global():
    global x
    x = "被 global 修改"
    return x
print("用 global:", use_global())

def use_nonlocal():
    count = 0
    def inc():
        nonlocal count     # 修改外层函数的变量
        count += 1
        return count
    inc(); inc()
    return inc()
print("用 nonlocal:", use_nonlocal())

# 5. 局部变量比全局变量快（LOAD_FAST 数组索引 vs LOAD_GLOBAL 字典查找）
import timeit
N = 200000
g_var = 1

def access_global():
    for _ in range(N):
        g_var                # LOAD_GLOBAL

def access_local():
    l_var = 1
    for _ in range(N):
        l_var                # LOAD_FAST

t_g = timeit.timeit(access_global, number=20)
t_l = timeit.timeit(access_local, number=20)
print(f"全局访问 {t_g:.4f}s vs 局部访问 {t_l:.4f}s → 局部快约 {t_g/t_l:.2f} 倍")
