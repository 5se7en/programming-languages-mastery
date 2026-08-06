# 第 15 章 · 包 — Python 示例
# 运行：python3 main.py
# 说明：离线实现 SemVer 逻辑，不真去下载包。

def parse(v):
    core, _, pre = v.partition("-")
    major, minor, patch = (int(x) for x in core.split("."))
    return (major, minor, patch, pre or None)

def compare(a, b):
    """按数字比较；预发布版排在正式版之前。"""
    xa, xb = parse(a), parse(b)
    if xa[:3] != xb[:3]:
        return -1 if xa[:3] < xb[:3] else 1
    if xa[3] and not xb[3]: return -1
    if not xa[3] and xb[3]: return 1
    return 0

def satisfies(version, spec):
    op = spec[0] if spec[0] in "^~" else "="
    base = parse(spec.lstrip("^~"))
    v = parse(version)
    if v[3]: return False
    if compare(version, spec.lstrip("^~")) < 0: return False
    if op == "^": return v[0] == base[0]
    if op == "~": return v[0] == base[0] and v[1] == base[1]
    return compare(version, spec) == 0

versions = ["1.2.3", "1.2.9", "1.3.0", "1.9.9", "2.0.0"]
for spec in ["^1.2.3", "~1.2.3", "1.2.3"]:
    print(f"{spec:<8} 匹配 → " + ", ".join(v for v in versions if satisfies(v, spec)))

print()
print('字符串比较 "1.10.0" > "1.9.0" →', "1.10.0" > "1.9.0", "← 错误！")
print('数字比较   compare("1.10.0","1.9.0") > 0 →', compare("1.10.0", "1.9.0") > 0, "← 正确")
print('预发布版   compare("1.0.0-beta","1.0.0") < 0 →', compare("1.0.0-beta", "1.0.0") < 0)

# 依赖解析：菱形依赖场景
print("\n菱形依赖：库 A 要 utils^1.0，库 B 要 utils^2.0")
deps = {"A": "^1.0.0", "B": "^2.0.0"}
available = ["1.5.0", "2.1.0"]
for lib, spec in deps.items():
    ok = [v for v in available if satisfies(v, spec)]
    print(f"  库 {lib} 需要 {spec:<8} → 可用 {ok}")
print("  npm 策略：两个版本都装（嵌套）")
print("  pip 策略：只能有一个版本 → 冲突时报错")
print("  Maven 策略：最近者优先，强制统一")
