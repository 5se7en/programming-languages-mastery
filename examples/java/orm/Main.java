import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

/**
 * ORM：手写一个反射 ORM——第 30 章的反射在这里有了最实际的用途。
 * （不引第三方 JDBC 驱动，用内存行存储模拟表；对象↔行的映射是真的。）
 */
public class Main {

    @Retention(RetentionPolicy.RUNTIME) @Target(ElementType.TYPE)
    @interface Table { String value(); }

    @Retention(RetentionPolicy.RUNTIME) @Target(ElementType.FIELD)
    @interface Column { String value() default ""; }

    @Retention(RetentionPolicy.RUNTIME) @Target(ElementType.FIELD)
    @interface Id { }

    // ---------- 实体：只有注解，没有一行映射代码 ----------
    @Table("users")
    public static class User {
        @Id @Column public Integer id;
        @Column public String name;
        @Column("city_name") public String city;      // 字段名与列名不同
        public String notPersisted = "不带 @Column 的字段不入库";

        @Override public String toString() {
            return "User{id=" + id + ", name='" + name + "', city='" + city + "'}";
        }
    }

    // ---------- 微型 ORM ----------
    static class MiniOrm {
        /** 内存里的「表」: 表名 → (主键 → 一行的列值) */
        private final Map<String, Map<Object, Map<String, Object>>> db = new LinkedHashMap<>();

        static String tableOf(Class<?> c) {
            Table t = c.getAnnotation(Table.class);
            if (t == null) throw new IllegalArgumentException(c.getSimpleName() + " 没有 @Table");
            return t.value();
        }

        /** 反射：把「字段」映射成「列名」 */
        static Map<String, Field> columnsOf(Class<?> c) {
            Map<String, Field> cols = new LinkedHashMap<>();
            for (Field f : c.getFields()) {
                Column col = f.getAnnotation(Column.class);
                if (col == null) continue;                       // 没标注的字段不入库
                cols.put(col.value().isEmpty() ? f.getName() : col.value(), f);
            }
            return cols;
        }

        static Field idOf(Class<?> c) {
            for (Field f : c.getFields()) if (f.getAnnotation(Id.class) != null) return f;
            throw new IllegalArgumentException("没有 @Id");
        }

        /** 生成 INSERT —— 这正是 ORM 在你调用 save() 时做的事 */
        String insertSql(Object entity) throws Exception {
            Class<?> c = entity.getClass();
            Map<String, Field> cols = columnsOf(c);
            List<String> names = new ArrayList<>(cols.keySet());
            List<String> vals = new ArrayList<>();
            for (Field f : cols.values()) {
                Object v = f.get(entity);
                vals.add(v instanceof String ? "'" + v + "'" : String.valueOf(v));
            }
            return "INSERT INTO " + tableOf(c) + " (" + String.join(", ", names)
                    + ") VALUES (" + String.join(", ", vals) + ")";
        }

        void save(Object entity) throws Exception {
            Class<?> c = entity.getClass();
            Map<String, Object> row = new LinkedHashMap<>();
            for (Map.Entry<String, Field> e : columnsOf(c).entrySet())
                row.put(e.getKey(), e.getValue().get(entity));
            Object pk = idOf(c).get(entity);
            db.computeIfAbsent(tableOf(c), k -> new LinkedHashMap<>()).put(pk, row);
        }

        /** 反射：把「行」还原成「对象」 */
        <T> T find(Class<T> c, Object pk) throws Exception {
            Map<String, Object> row = db.getOrDefault(tableOf(c), Map.of()).get(pk);
            if (row == null) return null;
            T obj = c.getDeclaredConstructor().newInstance();
            for (Map.Entry<String, Field> e : columnsOf(c).entrySet())
                e.getValue().set(obj, row.get(e.getKey()));
            return obj;
        }

        String selectSql(Class<?> c, Object pk) {
            return "SELECT " + String.join(", ", columnsOf(c).keySet())
                    + " FROM " + tableOf(c) + " WHERE " + idOf(c).getName() + " = " + pk;
        }
    }

    public static void main(String[] args) throws Exception {
        MiniOrm orm = new MiniOrm();

        System.out.println("== ① 反射读出「类 → 表」「字段 → 列」的映射 ==");
        System.out.println("  实体类 User 上只有注解，没有一行映射代码:");
        System.out.println("    表名: " + MiniOrm.tableOf(User.class));
        System.out.println("    主键字段: " + MiniOrm.idOf(User.class).getName());
        MiniOrm.columnsOf(User.class).forEach((col, f) ->
                System.out.printf("    列 %-10s ← 字段 %-6s (%s)%n",
                        col, f.getName(), f.getType().getSimpleName()));
        System.out.println("    ⚠️ notPersisted 字段没有 @Column，所以【不在映射里】");
        System.out.println("  → 这就是第 30 章反射最实际的用途: 运行时读出类型结构，据此生成 SQL");

        System.out.println("\n== ② save(obj) 生成的 SQL ==");
        User u = new User();
        u.id = 1; u.name = "张三"; u.city = "北京";
        System.out.println("  你写的代码: orm.save(user)");
        System.out.println("  ORM 生成的: " + orm.insertSql(u));
        orm.save(u);
        User v = new User(); v.id = 2; v.name = "李四"; v.city = "上海";
        orm.save(v);
        System.out.println("  注意 city 字段映射到了 city_name 列（@Column(\"city_name\")）");

        System.out.println("\n== ③ find(cls, id) 生成的 SQL 与还原出的对象 ==");
        System.out.println("  ORM 生成的: " + orm.selectSql(User.class, 1));
        System.out.println("  还原出的对象: " + orm.find(User.class, 1));
        System.out.println("  → 从「一行列值」到「一个对象」的转换，全靠 Field.set()");

        System.out.println("\n== ④ 没有 ORM 时你要写多少行 ==");
        String manual = String.join("\n",
                "  PreparedStatement ps = conn.prepareStatement(",
                "      \"INSERT INTO users (id, name, city_name) VALUES (?, ?, ?)\");",
                "  ps.setInt(1, user.id);",
                "  ps.setString(2, user.name);",
                "  ps.setString(3, user.city);",
                "  ps.executeUpdate();",
                "  // 查询侧还要再写一遍反向映射:",
                "  ResultSet rs = ...;",
                "  User u = new User();",
                "  u.id = rs.getInt(\"id\");",
                "  u.name = rs.getString(\"name\");",
                "  u.city = rs.getString(\"city_name\");");
        System.out.println(manual);
        long manualLines = manual.lines().count();
        System.out.printf("  手写: %d 行【每个实体、每个方向都要重写一遍】%n", manualLines);
        System.out.println("  ORM : orm.save(user) / orm.find(User.class, 1) —— 2 行，且对所有实体通用");
        System.out.printf("  → 10 个实体 × 增删改查 4 个方向 ≈ 手写 %d 行样板 vs ORM 0 行%n",
                manualLines * 10 * 4 / 2);
        System.out.println("  → 这才是 ORM 被广泛采用的真正原因: 消灭【与业务无关的重复代码】");

        System.out.println("\n== ⑤ 但 ORM 也带来了三个新问题 ==");
        System.out.println("  ① N+1 查询: 一次属性访问 = 一条 SQL（Python 版实测 201 条）");
        System.out.println("  ② 延迟加载失效: session 关了之后再访问关联 → LazyInitializationException");
        System.out.println("  ③ 生成的 SQL 不可见: 你写的是对象操作，执行的是你没看过的 SQL");
        System.out.println("  → 三个问题的根源是同一个: ORM 把【数据访问】伪装成了【内存访问】");
        System.out.println("     而内存访问是纳秒级的，数据库访问是毫秒级的——伪装掉的正是这六个数量级");

        System.out.println("\n== ⑥ Java ORM 生态 ==");
        System.out.println("  JPA        : 规范（javax/jakarta.persistence 注解，本例模仿的就是它）");
        System.out.println("  Hibernate  : JPA 最主流的实现；@Entity/@Table/@Column/@OneToMany");
        System.out.println("  MyBatis    : 「半 ORM」——SQL 你自己写，只帮你做结果映射");
        System.out.println("  jOOQ       : 反过来——用 Java 代码写类型安全的 SQL（不隐藏 SQL）");
        System.out.println("  → 光谱的两端: Hibernate 隐藏 SQL 最多，jOOQ 完全不隐藏");
        System.out.println("  → 选型的实质是: 你愿意让多少 SQL 细节【不出现在代码里】");
    }
}
