#!/usr/bin/env bash
# 运行 examples/ 下的示例代码，逐个执行并汇总结果。
#
# 用法：
#   ./run-all.sh                只跑一遍全部概念
#   ./run-all.sh variables      只跑 variables 这个概念
#   ./run-all.sh -v             显示每个示例的完整输出
#
# 缺少某语言的运行时会「跳过」而不是「失败」。

set -uo pipefail
cd "$(dirname "$0")"

VERBOSE=0
TOPIC=""
for arg in "$@"; do
  case "$arg" in
    -v|--verbose) VERBOSE=1 ;;
    *)            TOPIC="$arg" ;;
  esac
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0; SKIP=0
FAILED_LIST=()

c_green=$'\033[32m'; c_red=$'\033[31m'; c_yellow=$'\033[33m'; c_dim=$'\033[2m'; c_off=$'\033[0m'

report() { # report <status> <label> <detail>
  case "$1" in
    PASS) printf "  ${c_green}✓ PASS${c_off}  %-28s %s\n" "$2" "${3:-}"; PASS=$((PASS+1)) ;;
    FAIL) printf "  ${c_red}✗ FAIL${c_off}  %-28s %s\n" "$2" "${3:-}"; FAIL=$((FAIL+1)); FAILED_LIST+=("$2") ;;
    SKIP) printf "  ${c_yellow}- SKIP${c_off}  %-28s %s\n" "$2" "${3:-}"; SKIP=$((SKIP+1)) ;;
  esac
}

# 选择入口文件：优先 main.* / Main.*，其次与概念同名，最后取第一个
pick_entry() { # pick_entry <dir> <ext>
  local dir="$1" ext="$2"
  local f
  f=$(find "$dir" -maxdepth 1 \( -name "main.$ext" -o -name "Main.$ext" \) 2>/dev/null | head -1)
  [ -n "$f" ] && { echo "$f"; return; }
  find "$dir" -maxdepth 1 -name "*.$ext" 2>/dev/null | sort | head -1
}

run_one() { # run_one <label> <output-file> <command...>
  local label="$1" out="$2"; shift 2
  if "$@" > "$out" 2>&1; then
    report PASS "$label"
    [ "$VERBOSE" -eq 1 ] && sed "s/^/      ${c_dim}|${c_off} /" "$out"
  else
    report FAIL "$label" "退出码 $?"
    sed "s/^/      ${c_dim}|${c_off} /" "$out" | head -15
  fi
}

# 收集要跑的概念目录名（各语言子目录的并集）
topics=$(find . -mindepth 2 -maxdepth 2 -type d -not -path './.*' -exec basename {} \; | sort -u)
[ -n "$TOPIC" ] && topics="$TOPIC"

for topic in $topics; do
  echo
  echo "═══ 概念：$topic ═══"

  # ---- JavaScript ----
  f=$(pick_entry "javascript/$topic" js)
  if [ -n "${f:-}" ]; then
    if command -v node >/dev/null 2>&1; then
      run_one "JavaScript" "$TMP/js.out" node "$f"
    else report SKIP "JavaScript" "未找到 node"; fi
  fi

  # ---- Python ----
  f=$(pick_entry "python/$topic" py)
  if [ -n "${f:-}" ]; then
    if command -v python3 >/dev/null 2>&1; then
      run_one "Python" "$TMP/py.out" python3 "$f"
    else report SKIP "Python" "未找到 python3"; fi
  fi

  # ---- Java ----
  f=$(pick_entry "java/$topic" java)
  if [ -n "${f:-}" ]; then
    if command -v javac >/dev/null 2>&1 && command -v java >/dev/null 2>&1; then
      cls=$(basename "$f" .java)
      if javac -d "$TMP/java" "java/$topic"/*.java > "$TMP/java.out" 2>&1; then
        run_one "Java" "$TMP/java.out" java -cp "$TMP/java" "$cls"
      else
        report FAIL "Java" "编译失败"; sed 's/^/      | /' "$TMP/java.out" | head -15
      fi
    else report SKIP "Java" "未找到 javac/java"; fi
  fi

  # ---- C++ ----
  f=$(pick_entry "cpp/$topic" cpp)
  if [ -n "${f:-}" ]; then
    if command -v g++ >/dev/null 2>&1; then
      if g++ -std=c++20 -I"cpp/$topic" -o "$TMP/cpp.out.bin" "cpp/$topic"/*.cpp > "$TMP/cpp.out" 2>&1; then
        run_one "C++" "$TMP/cpp.out" "$TMP/cpp.out.bin"
      else
        report FAIL "C++" "编译失败"; sed 's/^/      | /' "$TMP/cpp.out" | head -15
      fi
    else report SKIP "C++" "未找到 g++"; fi
  fi

  # ---- C# ----
  f=$(pick_entry "csharp/$topic" cs)
  if [ -n "${f:-}" ]; then
    if command -v dotnet >/dev/null 2>&1; then
      export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
      app="$TMP/cs-$topic"
      if dotnet new console -o "$app" > "$TMP/cs.out" 2>&1; then
        cp "$f" "$app/Program.cs"
        sed -i '' 's|<PropertyGroup>|<PropertyGroup><AllowUnsafeBlocks>true</AllowUnsafeBlocks>|' "$app"/*.csproj
        run_one "C#" "$TMP/cs.out" dotnet run --project "$app"
      else
        report FAIL "C#" "创建项目失败"; sed 's/^/      | /' "$TMP/cs.out" | head -10
      fi
    else report SKIP "C#" "未找到 dotnet"; fi
  fi

  # ---- SQL ----
  f=$(pick_entry "sql/$topic" sql)
  if [ -n "${f:-}" ]; then
    if command -v sqlite3 >/dev/null 2>&1; then
      if sqlite3 :memory: < "$f" > "$TMP/sql.out" 2>&1; then
        report PASS "SQL"
        [ "$VERBOSE" -eq 1 ] && sed "s/^/      ${c_dim}|${c_off} /" "$TMP/sql.out"
      else
        report FAIL "SQL"; sed 's/^/      | /' "$TMP/sql.out" | head -10
      fi
    else report SKIP "SQL" "未找到 sqlite3"; fi
  fi
done

echo
echo "───────────────────────────────"
printf "汇总：${c_green}%d 通过${c_off}  ${c_red}%d 失败${c_off}  ${c_yellow}%d 跳过${c_off}\n" "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -gt 0 ]; then
  echo "失败项：${FAILED_LIST[*]}"
  exit 1
fi
exit 0
