// 指针：地址 + 类型。类型决定步长、决定解引用方式——这是指针的全部秘密。
#include <cstdio>
#include <cstdlib>

int compare_int(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

// 下面这个警告本身就是教学内容：编译器知道 arr 已经退化成指针
#pragma GCC diagnostic ignored "-Wsizeof-array-argument"
void takes_array(int arr[]) {           // 参数写成数组，其实是指针
    printf("  函数内 sizeof(arr) = %zu 字节   <- 退化成指针了！\n", sizeof(arr));
}

int main() {
    printf("== ① 指针 = 地址 + 类型：类型决定步长 ==\n");
    int    ints[3]    = {10, 20, 30};
    double doubles[3] = {1.5, 2.5, 3.5};
    char   chars[3]   = {'a', 'b', 'c'};
    int* pi = ints; double* pd = doubles; char* pc = chars;
    printf("int*    +1: %p -> %p   步进 %td 字节\n", (void*)pi, (void*)(pi + 1), (char*)(pi + 1) - (char*)pi);
    printf("double* +1: %p -> %p   步进 %td 字节\n", (void*)pd, (void*)(pd + 1), (char*)(pd + 1) - (char*)pd);
    printf("char*   +1: %p -> %p   步进 %td 字节\n", (void*)pc, (void*)(pc + 1), (char*)(pc + 1) - (char*)pc);
    printf("（p + 1 从来不是加 1 字节——是加一个「所指类型」的宽度）\n");

    printf("\n== ② arr[i] 就是 *(arr + i)：数组与指针的血缘 ==\n");
    printf("ints[2] = %d，*(ints + 2) = %d，2[ints] = %d   <- 三种写法同一件事\n",
           ints[2], *(ints + 2), 2 [ints]);
    printf("sizeof(ints) 在 main 里 = %zu 字节（真数组）\n", sizeof(ints));
    takes_array(ints);

    printf("\n== ③ 解引用与取地址：一对互逆操作 ==\n");
    int x = 42;
    int* p = &x;                        // & 取地址
    *p = 99;                            // * 解引用——通过地址改写 x
    printf("int x = 42; *(&x) = 99 之后，x = %d   <- 指针是「改变量的另一扇门」\n", x);
    int** pp = &p;                      // 二级指针：指向指针的指针
    printf("**pp = %d   <- 二级指针：门后还有一扇门\n", **pp);

    printf("\n== ④ 函数指针：代码也有地址 ==\n");
    int nums[5] = {31, 4, 15, 9, 26};
    qsort(nums, 5, sizeof(int), compare_int);      // 把「比较函数的地址」交给 qsort
    printf("qsort + 函数指针排序: %d %d %d %d %d\n", nums[0], nums[1], nums[2], nums[3], nums[4]);
    int (*fp)(const void*, const void*) = compare_int;
    printf("compare_int 的地址: %p，经函数指针调用: fp(&3, &7) = %d\n",
           (void*)fp, fp(&ints[0], &ints[1]));

    printf("\n== ⑤ 三大事故（只解剖，不引爆） ==\n");
    printf("空指针:  int* p = nullptr; *p        -> 解引用 0 号地址，立刻段错误（最幸运的事故）\n");
    printf("野指针:  int* p; *p                  -> 未初始化，指向随机地址——读到垃圾或崩溃\n");
    printf("悬垂指针: 指向已释放的内存           -> 第 32/33 章实测过：最阴险，「暂时还能用」\n");
    return 0;
}
