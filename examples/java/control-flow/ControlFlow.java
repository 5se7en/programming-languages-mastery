// 第 11 章 · 流程控制 — Java 示例
// 运行：javac ControlFlow.java && java ControlFlow
import java.util.List;
import java.util.ArrayList;

public class ControlFlow {
    static String grade(int score) {
        if (score >= 90) return "A";
        else if (score >= 60) return "B";
        else return "C";
    }

    // 演示 switch 穿透：故意不写 break
    static String fallThrough(int x) {
        StringBuilder sb = new StringBuilder();
        switch (x) {
            case 1: sb.append("一 ");
            case 2: sb.append("二 ");
            case 3: sb.append("三 "); break;
            default: sb.append("其他");
        }
        return sb.toString().trim();
    }

    public static void main(String[] args) {
        int[] scores = {92, 75, 50};

        // 1. 分支
        StringBuilder g = new StringBuilder();
        for (int s : scores) g.append(grade(s)).append(" ");
        System.out.println("分支: " + g.toString().trim());

        // 2. switch 穿透（忘写 break 的后果）
        System.out.println("switch(1) 无 break → " + fallThrough(1) + "  ← 一路穿透");
        System.out.println("switch(3)          → " + fallThrough(3));

        // 3. Java 14+ switch 表达式：不穿透，还能返回值
        String msg = switch (grade(92)) {
            case "A" -> "优秀";
            case "B" -> "及格";
            default  -> "不及格";
        };
        System.out.println("switch 表达式: " + msg);

        // 4. 带标签 break：一次跳出多层循环
        outer:
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                if (i * j == 2) { System.out.println("带标签 break 于 i=" + i + " j=" + j); break outer; }

        // 5. 遍历时删除要用 removeIf，不能在循环里删
        List<String> list = new ArrayList<>(List.of("a", "", "b"));
        list.removeIf(String::isEmpty);
        System.out.println("removeIf 后: " + list);
    }
}
