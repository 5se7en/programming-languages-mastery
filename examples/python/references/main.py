"""引用：Python 的传参是"按值传递引用"（官方叫 pass by assignment）。"""


def swap(a, b):
    a, b = b, a                 # 只是重绑定了函数内的两个名字


def mutate(lst):
    lst.append(99)              # 修改对象内容——穿透


def rebind(lst):
    lst = [0]                   # 重绑定参数名——不穿透


def augmented(lst):
    lst += [7]                  # += 对 list 是原地修改（等价 extend）——穿透！


def plain_add(lst):
    lst = lst + [8]             # = + 创建新列表再绑定——不穿透


print("== ① swap 测试：失败 ==")
x, y = 1, 2
swap(x, y)
print(f"swap(x, y) 之后: x = {x}, y = {y}   <- 没换成（重绑定不穿透）")
print("（Python 层面的解法是元组解包: x, y = y, x——语言替你做，不靠传参）")

print("\n== ② 改内容穿透，重绑定不穿透 ==")
nums = [1, 2, 3]
mutate(nums)
print(f"mutate(nums) 之后: {nums}   <- append 穿透")
rebind(nums)
print(f"rebind(nums) 之后: {nums}   <- 重绑定只动了函数内的名字")

print("\n== ③ 陷阱：+= 与 = + 一字之差，两种命运 ==")
a = [1, 2, 3]
augmented(a)
print(f"函数内 lst += [7]:    a = {a}   <- 原地修改，穿透！")
b = [1, 2, 3]
plain_add(b)
print(f"函数内 lst = lst+[8]: b = {b}      <- 新建对象，不穿透")
print("（+= 调 __iadd__ 原地改；= + 调 __add__ 造新的——引用语义的一字之谜）")

print("\n== ④ 不可变对象让引用语义『看起来像』值语义 ==")
s = "hello"
t = s                           # 共享同一个字符串对象
t = t.upper()                   # 但字符串不可变——upper 只能造新的
print(f"t = s 后 t = t.upper()，s = '{s}'，t = '{t}'")
print("（不是拷贝了字符串——是不可变对象根本没有『被改』这条路，第 21 章的设计）")
