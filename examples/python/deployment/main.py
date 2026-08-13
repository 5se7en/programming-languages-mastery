"""部署：Python 的交付物——解释器不在你手里，依赖解析发生在【部署那一刻】。"""
import importlib
import os
import platform
import sys
import sysconfig
import time


def dir_size(root, limit_s=4.0):
    """统计目录总大小与文件数（带超时，避免在超大目录上卡住）"""
    t0 = time.perf_counter()
    total = files = 0
    for dirpath, _dirnames, filenames in os.walk(root):
        if time.perf_counter() - t0 > limit_s:
            return total, files, True
        for f in filenames:
            try:
                total += os.path.getsize(os.path.join(dirpath, f))
                files += 1
            except OSError:
                pass
    return total, files, False


if __name__ == "__main__":
    print("== ① 交付物：解释器与标准库有多大（实测）==")
    stdlib = sysconfig.get_path("stdlib")
    size, count, truncated = dir_size(stdlib)
    print(f"  标准库目录: {stdlib}")
    print(f"    {size/1048576:.0f} MB / {count:,} 个文件{'（统计超时）' if truncated else ''}")
    try:
        exe_size = os.path.getsize(os.path.realpath(sys.executable))
        print(f"  解释器可执行文件: {exe_size/1048576:.1f} MB（{sys.executable}）")
    except OSError:
        print(f"  解释器: {sys.executable}")
    print("  → 与 Java/Node 对照: Python 的运行时是【一个小可执行文件 + 一大堆 .py 源码】")
    print(f"  → 后果: 容器里光是标准库就要复制 {count:,} 个文件，"
          f"平均每个 {size/max(count,1)/1024:.0f} KB")
    print("     加上第三方包就是几万个 —— 和 Node 版 ③ 实测的同一个问题")
    print("  → 也因此 python:3.x-slim 与 python:3.x 的镜像能差好几倍")

    print("\n== ② 导入成本：每次启动都要重新付（实测）==")
    mods = ["json", "sqlite3", "email", "unittest", "xml.etree.ElementTree"]
    total_ms = 0.0
    for m in mods:
        for key in [k for k in list(sys.modules) if k == m or k.startswith(m + ".")]:
            del sys.modules[key]                       # 清掉，测真实的首次导入
        t0 = time.perf_counter()
        importlib.import_module(m)
        ms = (time.perf_counter() - t0) * 1000
        total_ms += ms
        print(f"    import {m:24} {ms:7.1f} ms")
    print(f"  五个标准库模块合计: {total_ms:.1f} ms")
    print("  → Python 的 import 是【运行时行为】: 查找文件 → 编译成字节码 → 执行模块顶层代码")
    print("  → .pyc 缓存能省掉编译，但【查找与执行仍在每次启动时发生】")
    print("  → 所以 Python 服务的冷启动优化，第一刀往往是砍 import（尤其是顶层的重依赖）")

    print("\n== ③ 「在我机器上是好的」：Python 的三种成因 ==")
    print(f"  当前环境: Python {platform.python_version()} / "
          f"{platform.system()}-{platform.machine()} / {sys.implementation.name}")
    print(f"  sys.prefix   = {sys.prefix}")
    print(f"  在虚拟环境中 = {sys.prefix != sys.base_prefix}")
    print("  成因一【解释器版本】: 3.8 写的 walrus/match 在更早版本上是语法错误；")
    print("     反过来，依赖某个版本才有的 stdlib 行为则【不会报错，只是结果不同】")
    print("  成因二【依赖在部署时才解析】: requirements.txt 写 `flask>=2.0` 意味着")
    print("     今天装到 2.3、下个月装到 2.4 —— 【同一份代码，不同的运行结果】")
    print("     必须用锁文件（pip freeze / poetry.lock / uv.lock）+ 哈希校验")
    print("  成因三【含 C 扩展的包】: numpy/psycopg2 等按【本机架构与 libc】编译")
    print("     manylinux wheel 解决了大部分情况，但 musl(Alpine) 常常没有预编译轮子，")
    print("     于是构建时才现场编译 —— 镜像变大、构建变慢、且可能失败")
    print("  → 注意这三条【都不会在开发机上暴露】: 开发机的解释器、依赖、libc 全都是对的")

    print("\n== ④ 依赖锁定：可复现构建的最小要求 ==")
    demo = [
        ("requirements.txt 写 flask>=2.0", "今天 2.3.3，下月 2.4.0", "❌ 不可复现"),
        ("requirements.txt 写 flask==2.3.3", "总是 2.3.3", "⚠️ 但它的【依赖】仍未锁"),
        ("pip freeze 全量锁定", "整棵依赖树都固定", "✅ 可复现"),
        ("再加 --require-hashes", "内容也被校验", "✅ 可复现 + 抗篡改"),
    ]
    for how, what, verdict in demo:
        print(f"    {how:34} → {what:22} {verdict}")
    print("  → 第二行是最常见的误解: 锁住直接依赖【不等于】锁住传递依赖")
    print("  → 第四行防的是第 58 章讲的供应链攻击: 版本号对了，内容也可能被换过")

    print("\n== ⑤ 配置：三种做法与它们的失败模式 ==")
    print("  ⓐ 硬编码在代码里    → 每个环境一份代码，必然出现「测试环境是对的」")
    print("  ⓑ 配置文件随镜像打包 → 同一份代码，但【每个环境一个镜像】= 测过的不是发出去的")
    print("  ⓒ 环境变量/配置中心 → 【构建一次，到处部署】，测过的产物就是发出去的产物")
    shown = {k: os.environ.get(k, "(未设置)") for k in ("TZ", "LANG", "PYTHONPATH")}
    for k, v in shown.items():
        print(f"    当前 {k:12} = {v}")
    print("  → ⓑ 的问题很隐蔽: 它看起来「配置和代码分开了」，")
    print("     但只要产物里含环境信息，你在预发环境测的就【不是】要上生产的那个产物")
    print("  → 判据: 【同一个构建产物能否原封不动地跑在所有环境】")

    print("\n== ⑥ 发布策略：回滚速度 vs 资源成本 ==")
    strategies = [
        ("重建部署", "1x", "有停机", "快（重来一次）", "内部工具"),
        ("滚动发布", "1.2x", "无停机", "慢（要滚回去）", "默认选择"),
        ("蓝绿部署", "2x", "无停机", "【最快，切流量即可】", "关键系统"),
        ("金丝雀发布", "1.1x", "无停机", "快（先切 1% 回来）", "高风险变更"),
    ]
    print(f"    {'策略':<10} {'资源':<6} {'停机':<8} {'回滚速度':<20} {'适用'}")
    for n, r, d, rb, use in strategies:
        print(f"    {n:<10} {r:<6} {d:<8} {rb:<20} {use}")
    print("  → 核心权衡: 【回滚速度是用资源换来的】。蓝绿最快，因为它一直养着另一套环境")
    print("  → 金丝雀的独特价值不是回滚快，而是【故障只影响 1% 的用户】——")
    print("     它把「回滚快慢」问题降级成了「影响面大小」问题")
    print("  ⚠️ 但所有策略都有一个共同前提: 【新旧版本能同时运行】")
    print("     数据库 schema 变更会打破这个前提 —— 这正是 SQL 版要解决的问题")
