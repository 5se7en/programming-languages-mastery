"""包管理：手写一个依赖求解器——复现依赖地狱、老 pip 的静默覆盖、新 pip 的回溯求解。"""
import itertools
import time


# ---------- 一个微型「包仓库」 ----------
# 版本号简化为浮点数；约束形如 (">=", 2.0) 或 ("<", 2.0)
def satisfies(version, constraint):
    op, target = constraint
    return {"<": version < target, ">=": version >= target, "==": version == target}[op]


REGISTRY = {
    # 包名: {版本: {依赖包: 约束}}
    "web-framework": {
        2.0: {"http-lib": (">=", 2.0)},        # 新版要新的 http-lib
        1.5: {"http-lib": (">=", 1.0)},        # 老版随便
    },
    "auth-kit": {
        1.0: {"http-lib": ("<", 2.0)},         # 还没适配 http-lib 2.x
    },
    "http-lib": {2.1: {}, 2.0: {}, 1.4: {}},
}


def versions_desc(pkg):
    return sorted(REGISTRY[pkg], reverse=True)


if __name__ == "__main__":
    print("== ① 依赖地狱的诞生现场：钻石依赖 ==")
    print("  app 依赖: web-framework(任意) + auth-kit(任意)")
    print("  web-framework 2.0 → http-lib >= 2.0")
    print("  auth-kit      1.0 → http-lib <  2.0     ← 两条约束交集为空")
    print("  这就是「钻石依赖冲突」: 你没做错任何事，冲突来自依赖的依赖")

    print("\n== ② 老 pip（2020 年前）的行为：不求解，后装的赢 ==")
    installed = {}
    for pkg in ["web-framework", "auth-kit"]:            # 按声明顺序逐个安装
        ver = versions_desc(pkg)[0]                      # 各自贪心拿最新
        installed[pkg] = ver
        for dep, cons in REGISTRY[pkg][ver].items():
            dep_ver = max(v for v in versions_desc(dep) if satisfies(v, cons))
            if dep in installed and installed[dep] != dep_ver:
                print(f"  ⚠️ {dep} 已装 {installed[dep]}，现在被【静默覆盖】成 {dep_ver}")
            installed[dep] = dep_ver
    print(f"  最终安装: {installed}")
    wf_dep = REGISTRY["web-framework"][installed["web-framework"]]["http-lib"]
    ok = satisfies(installed["http-lib"], wf_dep)
    print(f"  检查 web-framework 2.0 的约束 http-lib >= 2.0: {'满足' if ok else '【已被破坏】'}")
    print("  → 装完不报错，运行时才炸——这就是「依赖地狱」在 pip 老解析器下的形态")
    print("  → 老 pip 根本不做全局求解: 谁最后装，谁的依赖版本就留下")

    print("\n== ③ 新 pip（2020.3+）的行为：回溯求解 ==")
    steps = []

    def resolve(requirements, chosen):
        """回溯求解: 逐个满足约束，冲突就退回上一个选择点换版本。"""
        reqs = {k: list(v) for k, v in requirements.items()}
        pending = [(p, c) for p, cs in reqs.items() for c in cs]
        if not pending:
            return chosen
        # 找第一个未定版本的包
        for pkg in reqs:
            if pkg in chosen:
                bad = [c for c in reqs[pkg] if not satisfies(chosen[pkg], c)]
                if bad:
                    steps.append(f"冲突: {pkg}={chosen[pkg]} 不满足 {bad[0]}，回溯")
                    return None
                continue
            for ver in versions_desc(pkg):
                if all(satisfies(ver, c) for c in reqs[pkg]):
                    steps.append(f"尝试 {pkg} = {ver}")
                    new_reqs = {k: list(v) for k, v in reqs.items()}
                    for dep, cons in REGISTRY[pkg][ver].items():
                        new_reqs.setdefault(dep, []).append(cons)
                    result = resolve(new_reqs, {**chosen, pkg: ver})
                    if result is not None:
                        return result
                    steps.append(f"  {pkg} = {ver} 走不通，换下一个版本")
            steps.append(f"{pkg} 所有版本都失败")
            return None
        return chosen

    solution = resolve({"web-framework": [(">=", 0)], "auth-kit": [(">=", 0)]}, {})
    for s in steps:
        print(f"    {s}")
    print(f"  求解结果: {solution}")
    print("  → 回溯放弃了 web-framework 2.0，退回 1.5——【所有约束同时满足】")
    print("  → 这就是 2020 年 pip 重写解析器的原因: 从「后装的赢」到「全局求解」")

    print("\n== ④ 求解为什么慢：这是一个 NP 完全问题（实测指数增长）==")
    def build_pathological(n):
        """构造最坏情况: 每个包的高版本都与最后的约束冲突 → 强制回溯所有组合。"""
        reg = {}
        for i in range(n):
            reg[f"pkg{i}"] = {2.0: {"anchor": (">=", 2.0)}, 1.0: {"anchor": (">=", 1.0)}}
        reg["anchor"] = {1.0: {}}                        # 只有 1.0 → 所有 2.0 分支必然失败
        return reg

    for n in [6, 9, 12]:
        REGISTRY.clear()
        REGISTRY.update(build_pathological(n))
        steps.clear()
        t0 = time.perf_counter()
        resolve({f"pkg{i}": [(">=", 0)] for i in range(n)}, {})
        ms = (time.perf_counter() - t0) * 1000
        print(f"  {n:2d} 个包（各 2 版本）最坏情况: 求解 {ms:8.1f} ms，尝试 {len(steps)} 步")
    print("  → 步数随包数【指数】增长——依赖求解是 SAT 问题（NP 完全）")
    print("  → 真实工具靠启发式 + 缓存把常见情况压到秒级；但最坏情况谁也救不了")
    print("  → 「pip 卡在 resolving dependencies」的那几分钟，它就在做这件事")

    print("\n== ⑤ 锁文件到底锁住了什么 ==")
    print("  requirements.txt / pyproject.toml: 【约束】——「我能接受什么」(>=2.0,<3)")
    print("  lockfile（poetry.lock/uv.lock）:   【解】——「上次求解出的精确版本 + 哈希」")
    print("  → 约束是给求解器的输入，锁是求解器的输出快照")
    print("  → 没锁文件: 同一份代码，今天装和明天装可能得到【不同的依赖树】")
    print("  → 锁文件里的 hash 还兼防篡改: 仓库里的包被换掉时安装直接失败（供应链防线）")

    print("\n== ⑥ Python 特有的一条约束：一个环境一个版本 ==")
    print("  sys.modules 按【模块名】缓存——import http_lib 只能有一个胜出者")
    print("  → 所以 Python 必须全局求解出【单一版本】(② ③ 的全部原因)")
    print("  → npm 的答案完全相反: 同一个包可以多版本共存（JS 版实测它的代价）")
    print("  → 虚拟环境(venv)是另一维度的隔离: 每个项目一套依赖，项目间互不干扰")
