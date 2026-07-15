# 新闻爬虫运行说明

## 最终逻辑架构

```text
config.json
    ↓
按种子建立 SourceState（同域、visited、深度、页面配额）
    ↓
libcurl 获取 HTTP 响应
    ↓
内嵌 JSON / JSON-LD / 可发现同域 API
    ↓失败
静态 HTML → 标题 + 高置信度正文容器 + 质量校验
    ↓成功                         ↓失败
SQLite 参数化入库                 新闻链接评分、同域过滤、去重、限量入队
                                      ↓
                                仅抓取高分子链接
```

## 关键停止条件

- 每个规范化种子最多抓取 `kMaxPagesPerSeed = 30` 页。
- 最大扩散深度为 `kMaxDepth = 2`。
- 每个未识别为新闻的页面最多加入 `kMaxChildrenPerPage = 12` 个子链接。
- 每个页面最多请求 2 个同域 JSON/API 线索。
- 新闻标题和正文达到质量阈值后，立即清空该页面的后续链接，不再继续跳转。
- 资源、登录、搜索、分页、外域和重复 URL 不入队。

## 动态页面限制

libcurl 不执行 JavaScript。当前实现只处理以下情况：

1. HTML 中直接存在 JSON-LD、`__NEXT_DATA__`、`__INITIAL_STATE__` 或 `__NUXT__`。
2. HTML/脚本中明确出现同域 `.json`、`/api/` 或 `/ajax/` 地址，且该地址可以直接用 GET 请求返回 JSON。
3. API 返回的字段包含 `title/headline` 与 `content/articleBody` 等新闻字段。

如果接口地址依赖运行时签名、点击事件或 JS 计算参数，当前程序会跳过该动态分支并记录原因。要完整支持这类站点，需要另行接入 WebView2/Chromium/Playwright，不能靠继续扩大 curl 链接队列解决。

## 依赖

编译主程序需要原工程已使用的 libcurl、SQLite3、HTML Tidy、pugixml 和 Windows SDK；`crawler_rules.cpp` 与 `tests/test_crawler_rules.cpp` 只依赖 C++17 标准库，可独立验证 URL 规范化和链接筛选规则。

示例测试命令：

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic tests/test_crawler_rules.cpp crawler_rules.cpp -o tests/test_crawler_rules.exe
tests/test_crawler_rules.exe
```
