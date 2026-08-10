public class Main {

    static int depth = 0;

    static void recurse() {
        depth++;
        recurse();                       // 每层调用占一个栈帧，直到栈满
    }

    static long used(Runtime rt) {
        return rt.totalMemory() - rt.freeMemory();
    }

    public static void main(String[] args) throws Exception {
        Runtime rt = Runtime.getRuntime();

        System.out.println("== ① JVM 的堆：三个刻度 ==");
        System.out.printf("maxMemory   (堆上限, -Xmx 可调): %5d MB%n", rt.maxMemory() / 1024 / 1024);
        System.out.printf("totalMemory (当前已向 OS 申请):  %5d MB%n", rt.totalMemory() / 1024 / 1024);
        System.out.printf("freeMemory  (已申请中的空闲):    %5d MB%n", rt.freeMemory() / 1024 / 1024);

        System.out.println("\n== ② 同样一百万个整数，住在堆上的两种方式 ==");
        System.gc();
        Thread.sleep(200);
        long before = used(rt);
        int[] prim = new int[1_000_000];
        long afterPrim = used(rt);
        Integer[] boxed = new Integer[1_000_000];
        for (int i = 0; i < boxed.length; i++) boxed[i] = i + 128;   // 避开小整数缓存
        long afterBoxed = used(rt);
        System.out.printf("int[]     一整块:            %5.1f MB%n", (afterPrim - before) / 1024.0 / 1024);
        System.out.printf("Integer[] 引用数组 + 百万对象: %5.1f MB%n", (afterBoxed - afterPrim) / 1024.0 / 1024);
        if (prim[0] + boxed[0] < 0) System.out.println();            // 防止死代码消除

        System.out.println("\n== ③ 栈溢出：可以捕获的 Error ==");
        try {
            recurse();
        } catch (StackOverflowError e) {
            System.out.println("StackOverflowError，递归深度 = " + depth
                    + "（栈大小由 -Xss 决定）");
        }

        System.out.println("\n== ④ 堆溢出：同样可以捕获 ==");
        try {
            long[] huge = new long[Integer.MAX_VALUE - 2];           // 请求约 16 GB
            System.out.println(huge.length);
        } catch (OutOfMemoryError e) {
            System.out.println("OutOfMemoryError: " + e.getMessage());
        }
    }
}
