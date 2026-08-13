"""测试：金字塔的成本结构、mock 掩盖的缺陷、100% 覆盖率下的 bug——三笔账全部实测。"""
import os
import sqlite3
import subprocess
import sys
import tempfile
import time
import trace
import unittest
from unittest.mock import Mock


def timed(fn, n=1):
    t0 = time.perf_counter()
    for _ in range(n):
        fn()
    return (time.perf_counter() - t0) * 1000 / n


# ============ 被测代码 ============

def parse_amount(s: str) -> int:
    """把 '12.50' 这样的金额解析成分。纯函数——单元测试的理想对象。"""
    yuan, _, cents = s.partition(".")
    return int(yuan) * 100 + int((cents + "00")[:2] or 0)


class RealGateway:
    """真实的支付网关（模拟）: 有一条 mock 不知道的业务规则。"""
    def charge(self, cents: int) -> str:
        if cents <= 0:
            raise ValueError("网关拒绝: 金额必须为正")   # ← mock 不知道这条规则
        return "ok"


def checkout(gateway, amount_str: str) -> str:
    """业务代码: 解析金额并扣款。bug 在这: 没有校验 0 元订单。"""
    return gateway.charge(parse_amount(amount_str))


def discount(price: float, is_vip: bool) -> float:
    """故意埋一个 bug: VIP 应打 7 折，写成了 8 折。"""
    return price * 0.8 if is_vip else price * 0.95


if __name__ == "__main__":
    print("== ① 测试金字塔的成本结构（实测三层各自的单次耗时）==")

    # --- 单元层: 纯函数，无 I/O ---
    def unit_test():
        assert parse_amount("12.50") == 1250
        assert parse_amount("0.99") == 99
        assert parse_amount("100") == 10000
    ms_unit = timed(unit_test, n=2000)

    # --- 集成层: 真实数据库文件（建库→写入→查询→删库）---
    def integration_test():
        p = os.path.join(tempfile.gettempdir(), f"t-{os.getpid()}.db")
        con = sqlite3.connect(p)
        con.execute("CREATE TABLE orders(id INTEGER PRIMARY KEY, cents INTEGER CHECK(cents > 0))")
        con.execute("INSERT INTO orders VALUES (1, ?)", (parse_amount("12.50"),))
        con.commit()
        assert con.execute("SELECT cents FROM orders WHERE id=1").fetchone()[0] == 1250
        con.close()
        os.unlink(p)
    ms_int = timed(integration_test, n=20)

    # --- 端到端层: 启动一个完整进程（模拟「启动整个应用」）---
    def e2e_test():
        r = subprocess.run([sys.executable, "-c",
                            "import sys; sys.exit(0 if 12*100+50 == 1250 else 1)"],
                           capture_output=True)
        assert r.returncode == 0
    ms_e2e = timed(e2e_test, n=5)

    print(f"  单元测试（纯函数）      : {ms_unit*1000:8.1f} μs/个")
    print(f"  集成测试（真实数据库）  : {ms_int*1000:8.0f} μs/个（比单元慢 {ms_int/ms_unit:.0f}x）")
    print(f"  端到端测试（启动进程）  : {ms_e2e*1000:8.0f} μs/个（比单元慢 {ms_e2e/ms_unit:.0f}x）")
    print(f"  → 一套 1000 个测试的套件: 全单元 {ms_unit*1000:.0f} ms，全端到端 {ms_e2e*1000/1000:.0f} 秒")
    print("  → 金字塔不是教条，是成本结构: 越往上一层，每个测试贵一到两个数量级")
    print("  → 所以数量分布应该是金字塔形——大量单元、适量集成、少量端到端")

    print("\n== ② mock 的两面：省下时间，掩盖缺陷（实测）==")
    # mock 版测试: 快、绿
    def mock_test():
        gw = Mock()
        gw.charge.return_value = "ok"
        assert checkout(gw, "0.00") == "ok"          # 0 元订单——mock 欣然接受
        gw.charge.assert_called_once_with(0)
    ms_mock = timed(mock_test, n=200)
    print(f"  mock 版测试「0 元订单结账」: 通过 ✓（{ms_mock*1000:.0f} μs）")

    # 真实版测试: 同一个用例挂了
    try:
        checkout(RealGateway(), "0.00")
        real_result = "通过（不应该）"
    except ValueError as e:
        real_result = f"失败 ✗ —— {e}"
    print(f"  真实网关同一个用例        : {real_result}")
    print("  → mock 测的是【你以为依赖会怎样】，真实测试测的是【依赖实际怎样】")
    print("  → mock 版绿灯掩盖了业务代码「没校验 0 元订单」的真实缺陷")
    print("  → mock 的正确用途: 隔离【慢/贵/不稳定】的依赖；错误用途: 替你【定义】依赖的行为")

    print("\n== ③ 100% 覆盖率下的 bug（实测覆盖率）==")
    tracer = trace.Trace(count=True, trace=False)
    def weak_tests():
        assert discount(100, False) == 95.0          # 非 VIP: 断言正确
        result = discount(100, True)                 # VIP: 调用了，但断言写得太弱
        assert result > 0                            # ← 弱断言: 只要是正数就过
    tracer.runfunc(weak_tests)
    counts = tracer.results().counts
    this_file = os.path.abspath(__file__)
    body_line = discount.__code__.co_firstlineno + 2  # discount 函数体（return 那一行）
    covered = {ln for (f, ln), c in counts.items() if f == this_file and c > 0}
    line_covered = body_line in covered
    print(f"  测试套件: 两个分支都调用了 discount → 行覆盖率 100%: {line_covered}")
    print(f"  测试结果: 全部通过 ✓")
    print(f"  但 bug 还在: discount(100, True) = {discount(100, True)}（应该是 70.0，VIP 七折）")
    print("  → 覆盖率度量的是【代码被执行过】，不是【行为被验证过】")
    print("  → 弱断言（assert result > 0）让覆盖率变成了假安全感")
    print("  → 把覆盖率当目标，团队就会生产「跑过但不验证」的测试——古德哈特定律")

    print("\n== ④ 测试的结构：AAA 与一次只测一件事 ==")
    print("  Arrange（准备）→ Act（执行）→ Assert（断言）")
    print("  def test_vip_gets_30_percent_off():        # 名字说清「期望的行为」")
    print("      price = 100                            # Arrange")
    print("      result = discount(price, is_vip=True)  # Act")
    print("      assert result == 70.0                  # Assert ← 这个断言能抓住 ③ 的 bug")
    print("  → 一个测试一个行为: 挂了就知道【哪条规则】坏了，不用调试测试本身")

    print("\n== ⑤ 用标准库跑一遍真正的测试框架 ==")
    class TestParseAmount(unittest.TestCase):
        def test_normal(self):
            self.assertEqual(parse_amount("12.50"), 1250)
        def test_no_cents(self):
            self.assertEqual(parse_amount("100"), 10000)
        def test_vip_discount_correct(self):
            self.assertEqual(discount(100, True), 70.0)   # ← 强断言，会抓住 bug

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TestParseAmount)
    result = unittest.TestResult()
    suite.run(result)
    print(f"  跑了 {result.testsRun} 个测试: {len(result.failures)} 个失败")
    for test, tb in result.failures:
        last = [l for l in tb.strip().splitlines() if l][-1]
        print(f"    ✗ {test._testMethodName}: {last}")
    print("  → 强断言的测试立刻暴露了 ③ 里覆盖率 100% 也没抓到的 bug")
    print("  → 框架提供的是: 自动发现、隔离运行、失败报告——Java 版会手写这三件事")

    print("\n== ⑥ 三层测试各自的职责（本章总纲）==")
    print("  单元:   验证【一段逻辑】——快到可以每次保存都跑（① 实测微秒级）")
    print("  集成:   验证【与真实依赖的契约】——mock 掩盖的缺陷在这层现形（② 实测）")
    print("  端到端: 验证【整个系统串起来】——最贵，只留关键路径（① 实测慢数千倍）")
    print("  → 三层不可互相替代: ② 证明了快的测不出真的，① 证明了真的贵得跑不起")
