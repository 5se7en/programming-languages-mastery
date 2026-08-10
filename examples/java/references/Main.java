public class Main {

    static class Student {
        String name;
        int score;
        Student(String name, int score) { this.name = name; this.score = score; }
    }

    static void swapInt(int a, int b) { int t = a; a = b; b = t; }

    static void swapStudent(Student a, Student b) { Student t = a; a = b; b = t; }

    static void mutate(Student s) { s.score = 100; }        // 修改对象内容

    static void rebind(Student s) { s = new Student("换人", 0); }  // 重绑定参数名

    public static void main(String[] args) {
        System.out.println("== ① swap 测试：Java 全线失败 ==");
        int x = 1, y = 2;
        swapInt(x, y);
        System.out.println("swapInt(x, y) 之后: x = " + x + ", y = " + y + "   <- 没换成（int 按值拷贝）");
        Student s1 = new Student("小明", 90);
        Student s2 = new Student("小红", 85);
        swapStudent(s1, s2);
        System.out.println("swapStudent(s1, s2) 之后: s1 = " + s1.name + ", s2 = " + s2.name
                + "   <- 也没换成！");
        System.out.println("（引用本身是按值传递的——函数里交换的是引用的副本）");

        System.out.println("\n== ② 但修改对象内容能穿透 ==");
        mutate(s1);
        System.out.println("mutate(s1) 之后 s1.score = " + s1.score + "   <- 副本引用指向同一对象");
        rebind(s1);
        System.out.println("rebind(s1) 之后 s1.name = " + s1.name + "   <- 重绑定只影响函数内的副本");
        System.out.println("（一句话：改内容穿透，换指向不穿透——「按值传递引用」的全部行为）");

        System.out.println("\n== ③ 二元世界：原始类型是值，对象是引用 ==");
        int p = 42; int q = p; q = 99;
        System.out.println("int:     q = p 后改 q，p = " + p + "（各是各的）");
        Student u = s2; u.score = 0;
        System.out.println("Student: u = s2 后改 u.score，s2.score = " + s2.score + "（同一对象）");

        System.out.println("\n== ④ Java 的 swap 出路：返回新值 / 包一层 ==");
        int[] pair = { x, y };
        int t = pair[0]; pair[0] = pair[1]; pair[1] = t;    // 数组是对象——内容修改穿透
        System.out.println("装进数组交换: pair = [" + pair[0] + ", " + pair[1] + "]（内容修改，合法穿透）");
    }
}
