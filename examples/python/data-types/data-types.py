# 第 09 章 · 数据类型 — Python 示例
# 运行：python3 data-types.py

from decimal import Decimal

# 1. int 是任意精度，永不溢出
score = 92
big = 2 ** 200
print("2**200 有", len(str(big)), "位数字，且完全精确")

# 2. float 就是 C 的 double，同样有 IEEE 754 误差
print("0.1 + 0.2 =", 0.1 + 0.2)
print("等于 0.3 吗:", 0.1 + 0.2 == 0.3)

# 3. 需要精确小数用 Decimal（必须从字符串构造）
print("Decimal 精确:", Decimal("0.1") + Decimal("0.2"),
      "| 等于 0.3 吗:", Decimal("0.1") + Decimal("0.2") == Decimal("0.3"))
print("从浮点构造是错的:", str(Decimal(0.1))[:30], "...")

# 4. 字符串长度数的是码点
wave = "👋"
print("len('👋') =", len(wave), "| UTF-8 字节数 =", len(wave.encode("utf-8")))

# 5. bool 是 int 的子类
print("True + True =", True + True, "| bool 是 int 吗:", isinstance(True, int))

# 6. 空值只有一个
print("空值:", None)
