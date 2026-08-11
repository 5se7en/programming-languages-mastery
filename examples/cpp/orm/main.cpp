// ORM：C++ 为什么没有主流 ORM——答案是第 30 章的那个缺口：没有运行时反射。
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

// ---------- 唯一的出路：用宏 + 模板在【编译期】把映射写出来 ----------
#define ORM_FIELD(type, name, column) \
    type name{};                       \
    static constexpr const char* name##_column = column;

/// 实体：字段和列名的对应关系必须【手工声明】，编译器不会替你记住
struct User {
    ORM_FIELD(int, id, "id")
    ORM_FIELD(std::string, name, "name")
    ORM_FIELD(std::string, city, "city_name")

    // 这一段是 C++ ORM 必须要求你写的「元数据」——Java 靠反射自动获得
    static auto meta() {
        return std::make_tuple(
            std::make_tuple("id",        &User::id),
            std::make_tuple("name",      &User::name),
            std::make_tuple("city_name", &User::city));
    }
    static constexpr const char* table = "users";
};

// ---------- 编译期遍历元数据生成 SQL ----------
template <typename Tuple, std::size_t... I>
void collect_columns(const Tuple& t, std::vector<std::string>& out, std::index_sequence<I...>) {
    ((out.push_back(std::get<0>(std::get<I>(t)))), ...);      // C++17 折叠表达式
}

template <typename T>
std::string select_sql() {
    std::vector<std::string> cols;
    auto m = T::meta();
    collect_columns(m, cols, std::make_index_sequence<std::tuple_size_v<decltype(m)>>{});
    std::string s = "SELECT ";
    for (size_t i = 0; i < cols.size(); ++i) s += (i ? ", " : "") + cols[i];
    return s + " FROM " + T::table;
}

/// 取值也要靠成员指针逐个写出来——没有「遍历所有字段」这种能力
std::string insert_sql(const User& u) {
    char buf[512];
    snprintf(buf, sizeof buf,
             "INSERT INTO %s (%s, %s, %s) VALUES (%d, '%s', '%s')",
             User::table, User::id_column, User::name_column, User::city_column,
             u.id, u.name.c_str(), u.city.c_str());
    return buf;
}

int main() {
    printf("== ① C++ 能做到的: 编译期映射 ==\n");
    User u;
    u.id = 1; u.name = "张三"; u.city = "北京";
    printf("  select_sql<User>(): %s\n", select_sql<User>().c_str());
    printf("  insert_sql(u):      %s\n", insert_sql(u).c_str());
    printf("  → 生成的 SQL 与 Java 版一模一样，但【获取映射的方式】完全不同\n");

    printf("\n== ② 差别在哪：Java 用反射，C++ 只能用宏 ==\n");
    printf("  Java  : for (Field f : User.class.getFields())  ← 运行时【问类要字段】\n");
    printf("          @Column 注解也是运行时读出来的 —— 类自己知道自己长什么样\n");
    printf("  C++   : 上面那个 meta() 是【你手写的】—— 类【不知道】自己有哪些字段\n");
    printf("          加一个字段，你必须记得同步改 meta()，编译器不会提醒你漏了\n");
    printf("  → 这就是第 30 章那个缺口的实际后果: C++ 没有运行时反射\n");

    printf("\n== ③ 于是 C++ 的「ORM」分成了三条路 ==\n");
    printf("  ① 宏 / 模板元编程（本例）\n");
    printf("     sqlpp11、sqlite_orm —— 编译期类型安全，SQL 错误在编译期就报\n");
    printf("     代价: 报错信息动辄几百行模板展开，且映射要手写\n");
    printf("  ② 外部代码生成\n");
    printf("     ODB —— 用一个【预处理器】扫描你的头文件，生成映射代码\n");
    printf("     代价: 构建流程里多一个工具，等于自己造了一套反射\n");
    printf("  ③ 干脆不用 ORM\n");
    printf("     直接写 SQL + 手工映射 —— C++ 项目最常见的选择\n");
    printf("  → 三条路都在绕过同一个缺口: 没有反射，映射信息就得【从别处来】\n");

    printf("\n== ④ 编译期映射反而有一个优势 ==\n");
    printf("  Java/Python 的 ORM: 列名写错 → 【运行时】才报错（甚至上线后才发现）\n");
    printf("  C++ 的模板 ORM:     字段类型对不上 → 【编译期】就报错\n");
    printf("  → sqlpp11 甚至能在编译期检查「你 SELECT 的列是否存在于这张表」\n");
    printf("  → 这是零开销哲学的又一次体现: 能在编译期做的，绝不留到运行时\n");
    printf("  → 但代价是灵活性: 运行时才知道表结构的场景（如通用管理后台）它完全做不了\n");

    printf("\n== ⑤ C++26 的反射会改变这一切吗 ==\n");
    printf("  P2996 静态反射已进入 C++26 —— 能在【编译期】遍历类的成员\n");
    printf("  → 上面的 meta() 将可以自动生成，宏可以退休\n");
    printf("  → 但它仍是【静态】反射: 编译期可见，运行时依然没有类型信息\n");
    printf("  → 所以 C++ 会得到更好的编译期 ORM，但不会有 Hibernate 那样的运行时 ORM\n");

    printf("\n== ⑥ 五语言 ORM 的技术路线（同一个问题的五个答案）==\n");
    printf("  ┌────────┬──────────────────┬────────────────────────┐\n");
    printf("  │ 语言    │ 靠什么获得映射     │ 代表                    │\n");
    printf("  ├────────┼──────────────────┼────────────────────────┤\n");
    printf("  │ Java   │ 注解 + 运行时反射  │ Hibernate / JPA         │\n");
    printf("  │ C#     │ 表达式树 + 反射    │ EF Core                 │\n");
    printf("  │ Python │ 描述符 + 元类      │ SQLAlchemy / Django ORM │\n");
    printf("  │ JS     │ 装饰器 + 代码生成  │ Prisma / TypeORM        │\n");
    printf("  │ C++    │ 宏 / 外部代码生成  │ sqlpp11 / ODB（小众）    │\n");
    printf("  └────────┴──────────────────┴────────────────────────┘\n");
    printf("  → 一个规律: 【运行时越能自省的语言，ORM 越强大也越流行】\n");
    printf("  → 反过来也成立: C++ 的 ORM 最弱，恰恰因为它把一切都交给了编译期\n");
    return 0;
}
