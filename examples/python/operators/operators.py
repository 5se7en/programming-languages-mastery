# 第 10 章 · 运算符 — Python 示例
# 运行：python3 operators.py

# 1. == 比较值，is 比较身份
a, b = [1, 2], [1, 2]
print("a == b →", a == b, "| a is b →", a is b)

# 2. 绝不要用 is 比较数值（缓存是实现细节）
#    注意：运行时 Python 会打印 SyntaxWarning: "is" with a literal —— 这是故意触发的，
#    正说明连解释器都在劝你改用 ==。
print("int('256') is 256 →", int("256") is 256, " ← 小整数被缓存")
print("int('257') is 257 →", int("257") is 257, "← 超出缓存，结果相反")
print("int('257') == 257 →", int("257") == 257, " ← 永远可靠")

# 3. is 的正确用途：判 None
value = None
print("value is None →", value is None)

# 4. 短路求值
def boom():
    print("   ← 这行不该出现！")
    return True
print("False and boom() →", False and boom())
print("True  or  boom() →", True or boom())

# 5. Python 独有：链式比较、整除、字符串乘法
print("1 < 5 < 10 →", 1 < 5 < 10)
print("7 // 2 =", 7 // 2, "| 7 / 2 =", 7 / 2)
print("'ab' * 3 =", "ab" * 3)

# 6. 运算符重载
class Score:
    def __init__(self, v): self.v = v
    def __add__(self, o): return Score(self.v + o.v)
    def __eq__(self, o): return self.v == o.v
print("重载 + →", (Score(90) + Score(5)).v, "| 重载 == →", Score(95) == Score(95))
