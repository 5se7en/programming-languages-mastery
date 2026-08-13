"""构建工具：手写一个增量构建系统——实测时间戳的两种失效，以及内容哈希为什么赢。"""
import hashlib
import os
import shutil
import tempfile
import time

WORK = tempfile.mkdtemp(prefix="pl-mastery-build-")


def write(name, content):
    path = os.path.join(WORK, name)
    with open(path, "w") as f:
        f.write(content)
    return path


def read(name):
    with open(os.path.join(WORK, name)) as f:
        return f.read()


# ---------- 一个微型构建系统 ----------
# 规则: 目标文件 ← 依赖文件列表 + 构建动作（这里的"编译"= 大写转换，代表任意昂贵操作）
RULES = {
    "a.obj":   (["a.src", "common.hdr"], lambda: write("a.obj", read("a.src").upper() + read("common.hdr").upper())),
    "b.obj":   (["b.src", "common.hdr"], lambda: write("b.obj", read("b.src").upper() + read("common.hdr").upper())),
    "c.obj":   (["c.src"],               lambda: write("c.obj", read("c.src").upper())),
    "app.bin": (["a.obj", "b.obj", "c.obj"],
                lambda: write("app.bin", read("a.obj") + read("b.obj") + read("c.obj"))),
}


def topo_order(targets):
    """拓扑排序: 先构建依赖，再构建目标（构建系统的第一块基石: DAG）"""
    order, seen = [], set()
    def visit(t):
        if t in seen or t not in RULES:
            return
        seen.add(t)
        for dep in RULES[t][0]:
            visit(dep)
        order.append(t)
    for t in targets:
        visit(t)
    return order


def build_mtime(targets):
    """Make 的判断: 目标不存在，或任一依赖的 mtime 比目标新 → 重建"""
    rebuilt = []
    for t in topo_order(targets):
        deps, action = RULES[t]
        tpath = os.path.join(WORK, t)
        if not os.path.exists(tpath) or any(
                os.path.getmtime(os.path.join(WORK, d)) > os.path.getmtime(tpath) for d in deps):
            action()
            rebuilt.append(t)
    return rebuilt


HASH_DB = {}                       # Bazel 式: 记住每个目标上次构建时【输入的内容哈希】

def fingerprint(deps):
    h = hashlib.sha256()
    for d in sorted(deps):
        with open(os.path.join(WORK, d), "rb") as f:
            h.update(d.encode())
            h.update(f.read())
    return h.hexdigest()[:16]


def build_hash(targets):
    """Bazel 的判断: 输入的内容哈希变了 → 重建；没变 → 跳过（时间戳无关）"""
    rebuilt = []
    for t in topo_order(targets):
        deps, action = RULES[t]
        fp = fingerprint(deps)
        if HASH_DB.get(t) != fp or not os.path.exists(os.path.join(WORK, t)):
            action()
            HASH_DB[t] = fp
            rebuilt.append(t)
    return rebuilt


if __name__ == "__main__":
    write("a.src", "aaa")
    write("b.src", "bbb")
    write("c.src", "ccc")
    write("common.hdr", "hdr-v1")

    print("== ① 构建系统的骨架：依赖图 + 拓扑排序 ==")
    print("  规则: a.obj ← [a.src, common.hdr]   b.obj ← [b.src, common.hdr]")
    print("        c.obj ← [c.src]               app.bin ← [a.obj, b.obj, c.obj]")
    print(f"  拓扑序: {topo_order(['app.bin'])}")
    r = build_mtime(["app.bin"])
    print(f"  首次构建（全量）: 重建 {len(r)} 个 → {r}")

    print("\n== ② 增量构建：只重建受影响的目标（mtime 版实测）==")
    r = build_mtime(["app.bin"])
    print(f"  什么都没改，再构建: 重建 {len(r)} 个 → {r or '无'} ✓")
    time.sleep(0.01)
    write("c.src", "ccc-changed")
    r = build_mtime(["app.bin"])
    print(f"  改 c.src: 重建 {len(r)} 个 → {r}（a.obj/b.obj 没动）✓")
    time.sleep(0.01)
    write("common.hdr", "hdr-v2")
    r = build_mtime(["app.bin"])
    print(f"  改 common.hdr: 重建 {len(r)} 个 → {r}")
    print("  → 一个「头文件」牵动两个 obj——依赖图的扇出决定改动的爆炸半径（C++ 版实测真实编译）")

    print("\n== ③ 时间戳的失效一：假阳性（touch 没改内容的文件）==")
    time.sleep(0.01)
    os.utime(os.path.join(WORK, "common.hdr"))        # touch: 内容没变，mtime 变了
    r = build_mtime(["app.bin"])
    print(f"  touch common.hdr（内容一个字没改）: 重建 {len(r)} 个 → {r}")
    print("  → 白白重建了 3 个目标——git checkout/分支切换会大量制造这种假阳性")

    print("\n== ④ 时间戳的失效二：假阴性（更危险）==")
    time.sleep(0.01)
    write("common.hdr", "hdr-v3-IMPORTANT-FIX")
    past = time.time() - 3600
    os.utime(os.path.join(WORK, "common.hdr"), (past, past))   # 内容变了，mtime 却在过去
    r = build_mtime(["app.bin"])
    print(f"  改了 common.hdr 但 mtime 被拨回一小时前: 重建 {len(r)} 个 → {r or '无'}")
    print(f"  产物里的头文件版本: {'HDR-V3' in read('app.bin') and 'v3 ✓' or '【还是旧的 v2】✗'}")
    print("  → 改动【没进产物】——时钟漂移、解压旧档、构建缓存恢复都会造出这种局面")
    print("  → 假阳性只是浪费时间，假阴性是【产物悄悄过期】——你以为修了，其实没有")

    print("\n== ⑤ Bazel 的答案：内容哈希（同样四个实验重跑一遍）==")
    for f_ in ["a.obj", "b.obj", "c.obj", "app.bin"]:
        p = os.path.join(WORK, f_)
        if os.path.exists(p):
            os.remove(p)
    HASH_DB.clear()
    r = build_hash(["app.bin"])
    print(f"  首次构建: 重建 {len(r)} 个")
    r = build_hash(["app.bin"])
    print(f"  无改动:   重建 {len(r)} 个 → {r or '无'} ✓")
    os.utime(os.path.join(WORK, "common.hdr"))
    r = build_hash(["app.bin"])
    print(f"  touch（假阳性场景）:   重建 {len(r)} 个 → {r or '无'} ✓ 内容没变就不重建")
    write("common.hdr", "hdr-v4")
    os.utime(os.path.join(WORK, "common.hdr"), (past, past))
    r = build_hash(["app.bin"])
    print(f"  改内容+回拨 mtime（假阴性场景）: 重建 {len(r)} 个 → {r} ✓ 哈希变了就重建")
    print("  → 内容哈希同时消灭两种失效——代价是每次都要读文件算哈希（用缓存摊薄）")

    print("\n== ⑥ 再往前一步：哈希不只判断「要不要重建」，还能【复用别人的产物】==")
    print("  action cache: key = hash(所有输入 + 命令行)，value = 产物")
    print("  → 同样的输入在【任何机器】上都算出同一个 key —— 同事/CI 构建过的直接下载")
    print("  → 这就是 Bazel/Buck 远程缓存的原理: 「构建」退化成「查表」")
    print("  → 前提是【确定性构建】: 相同输入必须产出逐字节相同的输出")
    print("     （时间戳、随机数、绝对路径、并行顺序——都是确定性的敌人，C# 版展开）")
    print("  → 第 53 章锁文件保证「装一样的依赖」，确定性构建保证「产出一样的产物」——")
    print("     供应链可验证性的两块基石")

    shutil.rmtree(WORK, ignore_errors=True)
