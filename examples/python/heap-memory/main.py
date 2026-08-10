"""堆内存：CPython 的对象池与驻留——分配器在你看不见的地方拼命复用。"""
import sys
import tracemalloc

print("== ① 小整数池：-5..256 全程只有一份 ==")
a, b = 256, 256
c, d = 257, 257
print(f"a = 256, b = 256: id 相同? {a is b}")
e = 200 + 56
print(f"200 + 56 算出来的 256 也是同一个对象? {e is a}")
f_ = int("257")
print(f"运行期造出的 257 与字面量 257: id 相同? {f_ is c}   <- 池外对象各是各的")

print("\n== ② 字符串驻留：相同内容，共享一份 ==")
s1 = "student_name"
s2 = "student_name"
print(f'同一文件里两个 "student_name": 同一对象? {s1 is s2}（编译期常量合并）')
r1 = "student" + str(len("x")) * 0 + " name!"      # 运行期拼出来的
r2 = "".join(["student", " name!"])                 # 另一条路拼出来的
print(f"运行期拼接的两份 'student name!': 同一对象? {r1 is r2}   <- 各是各的堆对象")
u1, u2 = sys.intern(r1), sys.intern(r2)
print(f"sys.intern 之后: {u1 is u2}   <- 手动驻留，全进程共享一份")

print("\n== ③ free list：刚死对象的地址立刻被复用 ==")
x = [1, 2, 3]
addr = id(x)
del x
y = [4, 5, 6]
print(f"del 一个列表后立刻建新列表，地址复用? {id(y) == addr}")
print("（pymalloc 把小对象的坑位留着——分配常常就是从池里捡现成的）")

print("\n== ④ tracemalloc：内存都花在了哪一行 ==")
tracemalloc.start()
data = [bytes(1000) for _ in range(10_000)]        # 故意分配 10 MB
strings = ["row-%d" % i for i in range(10_000)]
snapshot = tracemalloc.take_snapshot()
for stat in snapshot.statistics("lineno")[:2]:
    print(f"  {stat}")
tracemalloc.stop()
print("（定位 Python 内存问题的标准工具——按行号列出分配大户）")
