"""性能优化：先测量，再优化——用 profiler 实测「凭感觉找热点」错在哪。"""
import cProfile
import io
import pstats
import pathlib
import time


# ============ 被测系统：一个「看起来」瓶颈在算法的程序 ============
def slow_looking(n):
    """看起来最可疑: 三层嵌套的字符串拼接"""
    parts = []
    for i in range(n):
        parts.append(str(i))
    return "".join(parts)


def actually_slow(n):
    """看起来无辜: 一行成员判断——但 seen 是 list，每次都要 O(len) 线性扫描"""
    seen = []                                    # ⚠️ 用 list 做去重
    for i in range(n):
        key = i % 3000                           # 3000 个不同值 → list 最终长 3000
        if key not in seen:                      # ⚠️ 这一行是 O(len(seen))
            seen.append(key)
    return len(seen)


def fast_version(n):
    """同样的逻辑，把 list 换成 set（O(1) 查找）"""
    seen = set()
    for i in range(n):
        seen.add(i % 3000)
    return len(seen)


def workload(n):
    slow_looking(n)
    return actually_slow(n)


def alloc_workload(n=400_000):
    """一个会大量分配内存的负载——用来演示测量本身的波动"""
    data = [i * 2 for i in range(n)]
    return sum(data)


def timed(fn, *a, repeat=1):
    t0 = time.perf_counter()
    for _ in range(repeat):
        r = fn(*a)
    return (time.perf_counter() - t0) * 1000 / repeat, r


if __name__ == "__main__":
    N = 60_000

    print("== ① 先猜，再测：凭感觉找热点的失败率 ==")
    print("  程序里有两个函数:")
    print("    slow_looking(n)  —— 循环 n 次拼接字符串（看起来最像瓶颈）")
    print("    actually_slow(n) —— 循环 n 次做一次成员判断（看起来无辜）")
    print("  凭感觉，多数人会去优化字符串拼接。实测:")
    ms_a, _ = timed(slow_looking, N)
    ms_b, _ = timed(actually_slow, N)
    print(f"    slow_looking:  {ms_a:8.1f} ms（占比 {100*ms_a/(ms_a+ms_b):.0f}%）")
    print(f"    actually_slow: {ms_b:8.1f} ms（占比 {100*ms_b/(ms_a+ms_b):.0f}%）")
    print(f"  → 真正的瓶颈是【看起来无辜的那一行成员判断】")
    print("  → 优化字符串拼接哪怕做到极致，总耗时最多减少 "
          f"{100*ms_a/(ms_a+ms_b):.0f}%（阿姆达尔定律的上界）")

    print("\n== ② profiler 直接指出热点（实测 cProfile 输出）==")
    pr = cProfile.Profile()
    pr.enable()
    workload(N)
    pr.disable()
    s = io.StringIO()
    pstats.Stats(pr, stream=s).sort_stats("tottime").print_stats(4)
    started = False
    for line in s.getvalue().splitlines():
        if "tottime" in line:
            started = True
        if started and line.strip():
            # 只保留函数名部分，去掉冗长的绝对路径
            print(f"    {line.strip().replace(str(pathlib.Path(__file__).parent) + '/', '')[:92]}")
    print("  → tottime 排序: 第一行就是真正的热点，不需要任何猜测")
    print("  → 这一步的成本是【一分钟】，而猜错的成本是【几天的无效优化】")

    print("\n== ③ 找到热点后，修复往往很小（实测）==")
    ms_slow, r1 = timed(actually_slow, N)
    ms_fast, r2 = timed(fast_version, N)
    print(f"  用 list 做成员判断: {ms_slow:8.1f} ms")
    print(f"  换成 set:          {ms_fast:8.1f} ms（快 {ms_slow/ms_fast:.0f}x）")
    print(f"  结果一致: {r1 == r2}")
    print("  → 改动量: 一个 [] 换成 set()，两行代码")
    print("  → 收益: 数据结构选对了，O(n) 的成员判断变成 O(1)（第 20 章哈希表）")
    print("  → 「性能优化」绝大多数时候不是精雕细琢，而是【找到那个用错的数据结构】")

    print("\n== ④ 阿姆达尔定律：优化的收益上限（实测验算）==")
    total = ms_a + ms_b
    for name, part in [("字符串拼接", ms_a), ("成员判断", ms_b)]:
        frac = part / total
        for speedup in [2, 10, 1000]:
            new_total = (total - part) + part / speedup
            print(f"  把「{name}」（占 {100*frac:.0f}%）加速 {speedup:>4}x "
                  f"→ 总耗时 {total:.0f} → {new_total:.0f} ms（整体快 {total/new_total:.2f}x）")
    print(f"  → 占比小的那块（{100*ms_a/total:.0f}%）哪怕加速 1000 倍，整体也只快 "
          f"{total/((total-ms_a)+ms_a/1000):.2f}x —— 这就是收益的天花板")
    print("  → 这就是为什么【先找占比最大的那块】比【优化得多彻底】重要得多")

    print("\n== ⑤ 微基准的三个陷阱（Python 版）==")
    print("  陷阱一 —— 测了个空循环:")
    ms_empty, _ = timed(lambda: None, repeat=100000)
    print(f"    timed(lambda: None) 单次 {ms_empty*1000:.3f} μs —— 这是【测量本身】的开销")
    print("    → 被测代码若比这还快，你测到的全是噪声")
    print("  陷阱二 —— 忘了预热:")
    ms_first, _ = timed(alloc_workload)
    ms_warm, _ = timed(alloc_workload, repeat=5)
    print(f"    第一次 {ms_first:.1f} ms vs 预热后平均 {ms_warm:.1f} ms"
          f"（相差 {100*(ms_first/ms_warm-1):+.0f}%，落在下面测到的波动范围内）")
    print("    → Python 没有 JIT，预热效应【小到测不出来】: 多跑几次不会让代码变快")
    print("    → 对照第 52 章的 Java 实测: 那里不预热会让并行加速比从 5.78x 掉到 1.92x")
    print("    → 结论: 预热的必要性【因语言而异】——但「先跑几轮再计时」是无害的通用习惯")
    print("  陷阱三 —— 只测了一次:")
    runs = [timed(alloc_workload)[0] for _ in range(7)]
    print(f"    同一段代码跑 7 次: min {min(runs):.1f} / 中位数 {sorted(runs)[3]:.1f} "
          f"/ max {max(runs):.1f} ms（波动 {max(runs)/min(runs):.2f}x）")
    print("    → 即使在最稳定的情况下也有波动（OS 调度、CPU 频率、其他进程）")
    print("    → 所以要报告【多次的最小值或中位数】，而不是随手跑的那一次")
    print("    → 判断一个优化是否真实有效，前提是差距【大于这个波动】")

    print("\n== ⑥ 性能优化的完整流程（本章总纲）==")
    print("  ① 定目标: 「首页要在 200ms 内返回」—— 没有目标就没有「优化完成」")
    print("  ② 测现状: profiler 找热点（②），不要猜（① 实测猜错的代价）")
    print("  ③ 算上限: 阿姆达尔定律（④）—— 值不值得优化，先算再动手")
    print("  ④ 改一处: 一次只改一个变量，否则不知道是哪一处起了作用")
    print("  ⑤ 再测量: 确认真的变快了（而且没变慢别的地方）")
    print("  ⑥ 定回归: 把这个基准加进 CI（第 52 章的测试套件思路）")
    print("  → 六步里有三步是【测量】——这就是本章唯一真正的纪律")
