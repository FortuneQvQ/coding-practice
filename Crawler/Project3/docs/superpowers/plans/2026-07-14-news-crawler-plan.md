# 新闻爬虫静态提取与动态 JSON 线索 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复静态新闻页被误判、垃圾链接反复扩散和正文误提取问题，并在不增加浏览器依赖的前提下支持可直接发现的内嵌 JSON/API 数据。

**Architecture:** 保留现有 `main.cpp` 调度、`crawler.cpp` curl、`HTMLparsing (1).cpp` HTML 解析、`database.cpp` SQLite 分层。把 URL 规范化、新闻链接评分、正文质量校验和 JSON 递归提取集中到 HTML 解析模块；主循环只负责来源配额、队列和停止条件。curl 仍只消费 HTTP 响应，不尝试执行 JavaScript。

**Tech Stack:** C++17、libcurl、pugixml、HTML Tidy、nlohmann/json、SQLite3、现有 Windows 头文件。

---

### Task 1: 建立可复现的 URL 与页面识别失败测试

**Files:**
- Create: `tests/test_crawler_rules.cpp`
- Modify: `HTMLparsing.h` only to declare the test-targeted pure functions if the declarations are not already present

- [ ] **Step 1: 写失败测试**

测试以下行为：

```cpp
assert(resolve_Url("https://example.com/news/index.html", "detail/1.html") == "https://example.com/news/detail/1.html");
assert(normalize_url("https://example.com/news/1.html#top") == "https://example.com/news/1.html");
assert(is_candidate_news_link("https://example.com/info/123.html", "通知公告"));
assert(!is_candidate_news_link("https://example.com/search?q=news", "搜索"));
```

测试还应断言：详情 HTML 提取成功后 `out_links` 为空；只含导航和页脚的 HTML 提取失败。

- [ ] **Step 2: 运行测试确认失败**

如果本机有 `cl`，用现有依赖编译测试目标；否则先运行 `where.exe cl`, `where.exe g++`, `where.exe cmake`，记录缺失项。预期是新函数未定义或断言失败，而不是测试代码语法错误。

- [ ] **Step 3: 保留失败测试作为行为契约**

不在此任务中写生产实现；测试只依赖公开函数和内存 HTML，避免网络、数据库和时间因素。

### Task 2: 实现 URL 规范化、同域校验和新闻链接评分

**Files:**
- Modify: `HTMLparsing.h`
- Modify: `HTMLparsing (1).cpp`
- Modify: `crawler.h`
- Modify: `crawler.cpp`
- Test: `tests/test_crawler_rules.cpp`

- [ ] **Step 1: 实现规范化函数**

新增以下接口：

```cpp
std::string normalize_url(const std::string& url);
bool is_same_domain(const std::string& url, const std::string& seed_url);
bool is_candidate_news_link(const std::string& url, const std::string& anchor_text);
int score_news_link(const std::string& url, const std::string& anchor_text);
```

规范化要求：支持 `http`/`https`、去掉 fragment、去掉末尾多余 `/`（根路径除外）、拒绝危险 scheme；解析相对 URL 时使用当前页面目录而不是只使用站点根目录。

- [ ] **Step 2: 运行 URL 测试确认通过**

运行测试目标，预期 URL 断言全部通过，外域、资源、登录、搜索、脚本链接均被排除。

- [ ] **Step 3: 只保留必要的兼容逻辑**

让 `extract_domain()` 复用同一套 authority 解析，避免 `example.com:443` 与 `example.com` 被拆成两个来源；删除由新实现替代的重复拼接代码。

### Task 3: 重写静态 HTML 新闻提取的质量边界

**Files:**
- Modify: `HTMLparsing.h`
- Modify: `HTMLparsing (1).cpp`
- Test: `tests/test_crawler_rules.cpp`

- [ ] **Step 1: 增加失败测试**

使用内存样例：

```html
<html><head><title>站点首页</title></head><body>
<nav>首页 | 搜索 | 登录</nav>
<article><h1>关于暑期安排的通知</h1><p>这是正文第一段，包含足够的有效新闻信息。</p><p>这是正文第二段，用于验证正文容器和段落提取。</p></article>
<footer>版权信息和友情链接</footer>
</body></html>
```

断言标题为通知标题，正文不含“友情链接”，并且正文质量达标；对只有 `<nav>`、`<footer>` 的页面断言 `generic_parse()` 返回 `false`。

- [ ] **Step 2: 实现最小解析逻辑**

保留 Tidy + pugixml；标题按 `h1`、`article` 标题、`og:title`、`title` 取值。正文仅从 `article` 或高置信度 content/detail/article/main-text 容器中收集段落，跳过 nav/header/footer/aside/script/style 和垃圾 class/id。要求至少两个有效段落或超过明确文本长度阈值，并拒绝把 URL 作为标题。

- [ ] **Step 3: 详情成功时清空后续链接**

`generic_parse()` 和 JSON 解析成功都将 `out_links.clear()`；只有页面没有形成有效 News 时，才返回筛选后的候选链接。运行测试确认详情页不会继续扩散。

- [ ] **Step 4: 运行页面提取测试**

预期静态详情通过、导航页失败、正文中不出现页脚垃圾文本。

### Task 4: 实现可靠的内嵌 JSON 与可发现 API 线索处理

**Files:**
- Modify: `HTMLparsing.h`
- Modify: `HTMLparsing (1).cpp`
- Modify: `crawler.h`
- Modify: `crawler.cpp`
- Test: `tests/test_crawler_rules.cpp`

- [ ] **Step 1: 增加 JSON 失败测试**

覆盖：

```html
<script type="application/ld+json">
{"headline":"JSON 新闻","datePublished":"2026-07-14","articleBody":"正文中包含 } 和 { 字符，不应被错误截断。"}
</script>
```

断言能得到完整正文；非法 JSON、只有脚本代码、没有 title/content 的响应均不能形成 News。

- [ ] **Step 2: 用 JSON 解析器替代固定字符串截取**

新增 `parse_embedded_news_json()`，优先解析 JSON-LD、`__NEXT_DATA__`、`__INITIAL_STATE__`、`__NUXT__`。使用 nlohmann::json 递归遍历对象和数组，识别 `headline/title/name`、`datePublished/publishTime/date`、`articleBody/content/description`，对 HTML 内容使用现有纯文本清理。所有异常只返回失败并记录简短原因。

- [ ] **Step 3: 增加同域 API 线索提取**

新增 `extract_json_api_candidates()`，只提取脚本/HTML 中明确的同域 URL，限制 `.json`、`/api/`、`/ajax/` 关键词和最多少量候选；主循环通过 `fetch_page()` 请求后再调用 JSON 解析。失败、跨域、资源 URL 或候选超限均停止该分支。

- [ ] **Step 4: 运行 JSON 测试**

预期带大括号的字符串不被截断，非法 JSON 不抛出到主循环，动态 HTML 无 API 时不会输出大量脚本链接。

### Task 5: 收紧主循环的扩散、配额和停止条件

**Files:**
- Modify: `main.cpp`
- Modify: `crawler.h`
- Test: `tests/test_crawler_rules.cpp` or a small deterministic queue test in `tests/test_crawler_loop.cpp`

- [ ] **Step 1: 增加队列行为失败测试**

用固定链接集合验证：同域候选按分数降序入队；详情成功后不入队；每个列表页最多入队固定数量；跨域链接不入队；visited 使用规范 URL。

- [ ] **Step 2: 引入来源状态**

将当前全局 `pages_per_domain` 改为按种子来源记录 `SourceState`，至少包含 `seed_url`、`source_name`、`allowed_domain`、`pages_fetched`、`visited` 和 `max_children_per_page`。保留总页数和最大深度硬上限。

- [ ] **Step 3: 调整主循环顺序**

先规范化并检查来源配额，再抓取；抓取成功后先尝试静态/JSON 新闻提取；只有未形成有效 News 才筛选候选子链接。每次只加入最高分的有限候选，等待间隔保留但不在空队列时等待。

- [ ] **Step 4: 运行队列测试和日志检查**

预期不会再出现所有未知 URL 都被当作列表页、详情页成功后仍继续扩散、同一 URL 多次访问或跨域失控。

### Task 6: 加固 curl 和 SQLite 边界

**Files:**
- Modify: `crawler.h`
- Modify: `crawler.cpp`
- Modify: `database.h`
- Modify: `database.cpp`
- Modify: `main.cpp`

- [ ] **Step 1: 先加入失败测试/检查**

验证 HTTP 非 2xx、空响应、非 HTML JSON 响应不会被当成成功页面；验证数据库打开失败后主程序不调用 `addNews`。

- [ ] **Step 2: 扩展抓取响应信息**

在不破坏 `fetch_page()` 调用方的前提下增加可选响应元数据或辅助函数：检查最终 URL、Content-Type、HTTP 状态；只对 HTML/JSON 响应进入解析。保留超时、重定向和 User-Agent 设置。

- [ ] **Step 3: 加入 News 入库校验**

`openDb()` 返回成功状态；`addNews()` 在 `db == nullptr`、URL 为空、标题为空或正文低于阈值时直接拒绝；保留参数化 SQL 和 URL UNIQUE 去重。

- [ ] **Step 4: 运行数据库边界测试**

预期数据库未打开不会崩溃，重复规范 URL 只保留一条，正常 News 仍可插入。

### Task 7: 完整验证与交付说明

**Files:**
- Modify: `README.md` only if it exists; otherwise create `docs/superpowers/README-crawler.md`

- [ ] **Step 1: 运行完整测试**

优先运行工程已有测试；没有构建文件时，使用本机可用的 C++ 编译器按实际库路径编译测试目标和主程序。记录每条命令与结果。

- [ ] **Step 2: 做静态检查**

用 `rg` 检查旧的 `is_list_page_url(url)` 全量兜底逻辑、固定 `html.find("\"content\":\"")`、`<body>` 成功兜底和未检查 `db` 的调用是否仍存在；检查新增函数声明和定义一致。

- [ ] **Step 3: 更新运行说明**

写明纯 curl 动态页限制：只能处理响应内已有 JSON 或可直接请求的同域 API；需要执行 JS 的页面必须使用浏览器渲染方案，当前程序会跳过并记录，而不是无限扩散。

- [ ] **Step 4: 交付结果**

汇总修改文件、测试命令、实际通过/阻塞情况、可调整的配额常量和最终逻辑架构。
