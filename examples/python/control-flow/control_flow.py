# 第 11 章 · 流程控制 — Python 示例
# 运行：python3 control_flow.py

scores = [92, 75, 50]

# 1. 分支：用缩进表示层级，关键字是 elif
def grade(score):
    if score >= 90:
        return "A"
    elif score >= 60:
        return "B"
    else:
        return "C"
print("分支:", " ".join(grade(s) for s in scores))

# 2. Python 只有 for-each；需要下标用 enumerate，需要计数用 range
print("直接遍历:", [s for s in scores])
print("带下标:  ", [(i, s) for i, s in enumerate(scores)])
print("计数:    ", list(range(3)))

# 3. Python 独有的 for-else：循环正常跑完（没 break）才执行 else
def find(target, items):
    for x in items:
        if x == target:
            return f"找到 {x}"
    else:
        return "循环跑完也没找到（else 分支执行了）"
print("for-else:", find(75, scores))
print("for-else:", find(99, scores))

# 4. 卫语句
def process(user):
    if user is None:
        return "无用户"
    if not user.get("active"):
        return "未激活"
    return "处理完成"
print("卫语句:", process(None), "|", process({"active": True}))

# 5. 多分支：3.10 前用 dict 分派代替 switch
#    （Python 3.10+ 可用 match-case，本示例为兼容旧版未使用）
handler = {"A": "优秀", "B": "及格"}
print("dict 分派:", handler.get("A", "不及格"), handler.get("C", "不及格"))
