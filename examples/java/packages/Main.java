// 第 15 章 · 包 — Java 示例
// 运行：javac *.java && java Main
import java.util.*;
import java.util.stream.Collectors;

public class Main {
    // 语义化版本的比较：必须按数字，不能按字符串
    record SemVer(int major, int minor, int patch, String pre) implements Comparable<SemVer> {
        static SemVer parse(String v) {
            String[] parts = v.split("-", 2);
            String[] nums = parts[0].split("\\.");
            return new SemVer(Integer.parseInt(nums[0]), Integer.parseInt(nums[1]),
                              Integer.parseInt(nums[2]), parts.length > 1 ? parts[1] : null);
        }
        public int compareTo(SemVer o) {
            if (major != o.major) return Integer.compare(major, o.major);
            if (minor != o.minor) return Integer.compare(minor, o.minor);
            if (patch != o.patch) return Integer.compare(patch, o.patch);
            if (pre != null && o.pre == null) return -1;    // 预发布版在前
            if (pre == null && o.pre != null) return 1;
            return 0;
        }
        public String toString() { return major + "." + minor + "." + patch + (pre != null ? "-" + pre : ""); }
    }

    static boolean satisfies(String version, String spec) {
        char op = (spec.charAt(0) == '^' || spec.charAt(0) == '~') ? spec.charAt(0) : '=';
        SemVer base = SemVer.parse(spec.replaceAll("^[\\^~]", ""));
        SemVer v = SemVer.parse(version);
        if (v.pre() != null) return false;
        if (v.compareTo(base) < 0) return false;
        if (op == '^') return v.major() == base.major();
        if (op == '~') return v.major() == base.major() && v.minor() == base.minor();
        return v.compareTo(base) == 0;
    }

    public static void main(String[] args) {
        List<String> versions = List.of("1.2.3", "1.2.9", "1.3.0", "1.9.9", "2.0.0");
        for (String spec : List.of("^1.2.3", "~1.2.3", "1.2.3")) {
            String ok = versions.stream().filter(v -> satisfies(v, spec)).collect(Collectors.joining(", "));
            System.out.printf("%-8s 匹配 → %s%n", spec, ok);
        }

        System.out.println();
        System.out.println("字符串比较 \"1.10.0\".compareTo(\"1.9.0\") > 0 → "
            + ("1.10.0".compareTo("1.9.0") > 0) + " ← 错误！");
        System.out.println("数字比较   SemVer 1.10.0 > 1.9.0 → "
            + (SemVer.parse("1.10.0").compareTo(SemVer.parse("1.9.0")) > 0) + " ← 正确");

        // Maven 坐标：groupId:artifactId:version
        System.out.println("\nMaven 坐标三元组: com.google.guava:guava:32.1.3-jre");
        System.out.println("冲突解决策略: 最近者优先（依赖树中路径最短的胜出）");
        System.out.println("本地仓库: ~/.m2/repository —— 所有项目共享，与 npm 每项目一份形成对比");
    }
}
