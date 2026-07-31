# 第 08 章 · 变量 — Python 示例
# 运行：python3 variables.py

# 1. 赋值即绑定
student_name = "Alice"
MAX_SCORE = 100        # 约定的"常量"
age = 20
score = 92
print(student_name, age, score, MAX_SCORE)

# 2. 类型跟着值走
print("type:", type(score).__name__)
score = "A+"
print("type:", type(score).__name__)

# 3. 不可变对象：重新绑定
a = 92
b = a
b = 60
print("不可变对象 重新绑定:", a, b)          # 92 60

# 4. 可变对象：共享同一对象
s1 = {"name": "Alice", "score": 92}
s2 = s1
s2["score"] = 60
print("可变对象 共享绑定:", s1["score"])      # 60
print("是同一对象吗:", s1 is s2)             # True

# 5. 正确的复制方式
s3 = s1.copy()
s3["score"] = 100
print("复制后互不影响:", s1["score"], s3["score"])  # 60 100
