#include "crawler.h"
#include "database.h"
#include "HTMLparsing.h"
#include "WebsiteGenerator.h"
#include "runtime_paths.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
// 单个种子可持续翻页直到用完新增配额；该上限只用于防止异常分页形成死循环，
// 实际续爬位置由数据库游标保存，下一次运行不会重新下载已经入库的详情页。
constexpr int kListPagesPerSeedPerRun = 100;
// 只有 SQLite 确认插入成功才消耗新增配额，旧链接和无效正文均不计数。
constexpr int kNewArticlesPerSeedPerRun = 20;
// 垃圾链接虽然不消耗新增配额，但此独立上限可阻止异常列表制造无限详情请求。
constexpr int kDetailAttemptsPerSeedPerRun = 80;
constexpr int kEngine2PageSize = 20;
constexpr int kMaxFailuresPerUrl = 3;
constexpr std::size_t kMaxCandidatesPerListPage = 100;
constexpr auto kRequestInterval = std::chrono::milliseconds(250);

enum class DetailResult
{
    known,      // 数据库已有，未重复下载正文
    inserted,   // 本次成功解析并入库
    retry,      // 暂时失败，下次运行重试
    abandoned   // 连续失败达到阈值，跳过以免永远卡住回溯游标
};

struct RunStats
{
    int list_requests = 0;
    int detail_requests = 0;
    int inserted = 0;
    int known = 0;
    int failed = 0;
};

// 校验解析结果是否达到入库标准；它与 database::addNews 的数据库层校验形成双重防线，
// 防止栏目标题、乱码、脚本源码和缺少正文的页面被当作一条成功资讯。
bool valid_news(const News& news)
{
    static const std::set<std::string> navigation_titles = {
        "首页", "党建工作", "通知公告", "新闻中心", "综合新闻", "学院新闻",
        "学校新闻", "更多", "详情", "栏目列表", "四川大学就业指导中心",
        "服务指南", "中心介绍", "下载中心", "学习园地", "毕业生数据", "机构设置",
        "帮助信息", "版权声明", "网站地图", "联系我们"
    };
    auto valid_utf8_text = [](const std::string& text) {
        if (text.empty() || text.find('\0') != std::string::npos) return false;
        std::size_t controls = 0;
        for (std::size_t i = 0; i < text.size();)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80)
            {
                if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') ++controls;
                ++i;
                continue;
            }
            int continuation = 0;
            if ((c & 0xE0) == 0xC0) continuation = 1;
            else if ((c & 0xF0) == 0xE0) continuation = 2;
            else if ((c & 0xF8) == 0xF0) continuation = 3;
            else return false;
            if (i + continuation >= text.size()) return false;
            for (int j = 1; j <= continuation; ++j)
                if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) return false;
            i += static_cast<std::size_t>(continuation + 1);
        }
        return controls * 100 < text.size() &&
               text.find("\xEF\xBF\xBD") == std::string::npos &&
               text.find("锟斤拷") == std::string::npos;
    };
    const bool four_digits = news.title.size() >= 4 &&
        std::all_of(news.title.begin(), news.title.begin() + 4,
                    [](unsigned char c) { return std::isdigit(c) != 0; });
    const bool year_only_title = four_digits &&
        (news.title.size() == 4 || (news.title.size() == 7 && news.title.substr(4) == "年"));
    const bool image_notice = news.image.rfind("http://", 0) == 0 ||
                              news.image.rfind("https://", 0) == 0;
    const bool navigation_with_site_suffix =
        news.title.rfind("通知公告-", 0) == 0 ||
        news.title.rfind("新闻动态-", 0) == 0 ||
        news.title.rfind("学院新闻-", 0) == 0;
    return news.title.size() >= 4 && news.title.size() <= 300 &&
           (news.content.size() >= 80 || image_notice) &&
           valid_utf8_text(news.title) && valid_utf8_text(news.content) &&
           news.content.find("<script") == std::string::npos &&
           news.content.find("<!DOCTYPE") == std::string::npos &&
           !year_only_title &&
           !navigation_with_site_suffix &&
           navigation_titles.find(news.title) == navigation_titles.end();
}

// 统一解析详情页：先按网页类型调用结构化或通用解析器，再通过 valid_news 验证。
// 该函数隔离各站模板差异，为 crawl_detail 提供统一的成功或失败结果。
bool parse_detail_page(const std::string& html, const std::string& url, News& news)
{
    // Engine2 详情将完整正文放在结构化 JSON 中；普通站点先解析正文区域，
    // 避免把页面里的无关配置 JSON 错认成新闻。
    if (url.find("/engine2/d/") != std::string::npos)
    {
        news = News{};
        news.url = url;
        if (parse_embedded_news_json(html, news) && valid_news(news)) return true;
    }
    std::set<std::string> ignored_links;
    news = News{};
    news.url = url;
    if (generic_parse(html, url, news, ignored_links) && valid_news(news)) return true;

    // 只有页面明确包含结构化脚本标记时才启用脚本解析，避免畸形旧页面无谓进入 Tidy。
    if (html.find("__NEXT_DATA__") != std::string::npos ||
        html.find("__INITIAL_STATE__") != std::string::npos ||
        html.find("__NUXT__") != std::string::npos ||
        html.find("application/ld+json") != std::string::npos)
    {
        news = News{};
        news.url = url;
        return parse_embedded_news_json(html, news) && valid_news(news);
    }
    return false;
}

// 下载、解析并尝试入库一个详情链接，同时维护失败次数和运行统计。
// 返回值供分页层判断该链接是已知、新增、可重试还是应放弃。
DetailResult crawl_detail(const std::string& url, const std::string& source_domain, RunStats& stats)
{
    const std::string normalized = normalize_url(url);
    if (normalized.empty()) return DetailResult::abandoned;
    if (database::newsExists(normalized))
    {
        ++stats.known;
        return DetailResult::known;
    }
    if (database::getFailureCount(normalized) >= kMaxFailuresPerUrl)
        return DetailResult::abandoned;

    ++stats.detail_requests;
    const std::string html = fetch_page(normalized);
    News news;
    if (html.empty() || !parse_detail_page(html, normalized, news))
    {
        database::recordFailure(normalized, html.empty() ? "empty response" : "no valid article body");
        ++stats.failed;
        return database::getFailureCount(normalized) >= kMaxFailuresPerUrl
            ? DetailResult::abandoned : DetailResult::retry;
    }

    news.url = normalized;
    if (news.source.empty()) news.source = source_domain;
    const bool inserted = database::addNews(news);
    database::clearFailure(normalized);
    if (inserted)
    {
        ++stats.inserted;
        std::cout << "    [新增入库] " << news.title << '\n';
        return DetailResult::inserted;
    }
    return database::newsExists(normalized) ? DetailResult::known : DetailResult::retry;
}

// 处理一页详情候选并扣减独立请求额度；只有候选均已处理或确定放弃时才允许推进分页游标，
// 从而与数据库去重、失败重试和每种子新增配额共同保证不漏爬、不空耗。
bool process_candidates(const std::vector<LinkCandidate>& candidates,
                        const std::string& source_domain,
                        RunStats& stats,
                        int& successful_insertions,
                        int& detail_attempts_left,
                        int& unseen_count,
                        bool& has_retry,
                        std::unordered_set<std::string>& seen_details)
{
    bool can_continue_paging = true;
    unseen_count = 0;
    has_retry = false;
    for (const auto& candidate : candidates)
    {
        const std::string url = normalize_url(candidate.url);
        // 导航链接会在每个列表页重复出现；按整个种子运行去重，使同一垃圾链接最多尝试一次。
        if (url.empty() || !seen_details.insert(url).second) continue;
        if (database::newsExists(url))
        {
            ++stats.known;
            continue;
        }

        ++unseen_count;
        if (successful_insertions >= kNewArticlesPerSeedPerRun || detail_attempts_left <= 0)
        {
            can_continue_paging = false;
            continue;
        }
        --detail_attempts_left;
        const DetailResult result = crawl_detail(url, source_domain, stats);
        if (result == DetailResult::inserted) ++successful_insertions;
        if (result == DetailResult::retry) has_retry = true;
        std::this_thread::sleep_for(kRequestInterval);
    }
    return can_continue_paging;
}

// 处理 Engine2 动态列表接口：构造分页请求、提取详情链接、调用候选处理器并持久化页码。
// 它与普通 HTML 分页函数并列，由 crawl_seed 根据种子页特征选择。
bool crawl_engine2_seed(const std::string& seed_url,
                        const std::string& seed_html,
                        CrawlProgress progress,
                        RunStats& stats)
{
    const bool incremental_mode = progress.backfill_complete;
    const int resume_page = (std::max)(1, progress.next_page);
    int page_num = 1;
    int successful_insertions = 0;
    int detail_attempts_left = kDetailAttemptsPerSeedPerRun;
    bool cursor_blocked = false;
    std::unordered_set<std::string> seen_details;

    for (int page_count = 0; page_count < kListPagesPerSeedPerRun; ++page_count)
    {
        Engine2ListRequest request;
        if (!extract_engine2_list_request(seed_html, seed_url, request, page_num, kEngine2PageSize))
            return false;

        ++stats.list_requests;
        const std::string response = fetch_form(request.endpoint, request.form_data);
        int total_pages = 0;
        const auto candidates = extract_engine2_detail_links(
            response, seed_url, request, kEngine2PageSize, &total_pages);
        if (candidates.empty())
        {
            std::cout << "  [动态列表] 第 " << page_num << " 页未返回详情候选\n";
            return true;
        }

        int unseen = 0;
        bool has_retry = false;
        const bool can_continue = process_candidates(
            candidates, extract_domain(seed_url), stats, successful_insertions,
            detail_attempts_left, unseen, has_retry, seen_details);
        cursor_blocked = cursor_blocked || has_retry;
        std::cout << "  [动态列表] 页码=" << page_num << "/" << total_pages
                  << "，候选数=" << candidates.size() << "，未处理数=" << unseen << '\n';

        // 增量阶段遇到整页旧数据，说明后续更旧，无需继续翻页。
        if (incremental_mode && unseen == 0) return true;
        // 配额耗尽或有暂时失败的详情，不推进游标，下次仍从本页补齐。
        if (!can_continue) return true;

        // 每次先检查第一页的新资讯，再跳转到数据库保存的历史回溯页码。
        if (!incremental_mode && page_num == 1 && resume_page > 1)
        {
            page_num = resume_page;
            continue;
        }

        if (total_pages > 0 && page_num >= total_pages)
        {
            if (!cursor_blocked)
            {
                progress.next_page = 1;
                progress.backfill_complete = true;
                database::saveCrawlProgress(seed_url, progress);
            }
            return true;
        }

        ++page_num;
        if (!incremental_mode && !cursor_blocked)
        {
            progress.next_page = page_num;
            database::saveCrawlProgress(seed_url, progress);
        }
        if (successful_insertions >= kNewArticlesPerSeedPerRun || detail_attempts_left <= 0) return true;
        std::this_thread::sleep_for(kRequestInterval);
    }
    return true;
}

// 处理普通 HTML 列表：逐页筛选可靠详情链接、入库新正文并保存下一页 URL。
// 第一页负责增量更新，历史游标负责继续回溯，避免每次从头重复翻页。
void crawl_html_seed(const std::string& seed_url,
                     const std::string& first_html,
                     CrawlProgress progress,
                     RunStats& stats)
{
    const bool incremental_mode = progress.backfill_complete;
    const std::string resume_url = progress.next_list_url;
    std::string page_url = seed_url;
    std::string html = first_html;
    int successful_insertions = 0;
    int detail_attempts_left = kDetailAttemptsPerSeedPerRun;
    bool cursor_blocked = false;
    std::unordered_set<std::string> visited_pages;
    std::unordered_set<std::string> seen_details;

    for (int page_count = 0; page_count < kListPagesPerSeedPerRun; ++page_count)
    {
        page_url = normalize_url(page_url);
        if (page_url.empty() || !visited_pages.insert(page_url).second) break;
        if (html.empty())
        {
            ++stats.list_requests;
            html = fetch_page(page_url);
        }
        if (html.empty()) break;

        const auto raw = extract_link_candidates(html, page_url);
        const auto candidates = select_news_links(raw, seed_url, kMaxCandidatesPerListPage);
        const std::string next_page = extract_next_page_link(html, page_url);
        std::cout << "  [列表扫描] 原始链接数=" << raw.size()
                  << "，详情候选数=" << candidates.size() << '\n';
        if (candidates.empty())
        {
            std::cout << "  [列表] 未发现可靠的详情链接：" << page_url << '\n';
            break;
        }

        int unseen = 0;
        bool has_retry = false;
        const bool can_continue = process_candidates(
            candidates, extract_domain(seed_url), stats, successful_insertions,
            detail_attempts_left, unseen, has_retry, seen_details);
        cursor_blocked = cursor_blocked || has_retry;
        std::cout << "  [列表] 候选数=" << candidates.size() << "，未处理数=" << unseen
                  << "，下一页=" << (next_page.empty() ? "无" : next_page) << '\n';

        if (incremental_mode && unseen == 0) break;
        if (!can_continue) break;

        // 始终先检查第一页的新发布内容，再从保存的游标继续历史回溯，避免重复遍历旧列表页。
        if (!incremental_mode && normalize_url(page_url) == seed_url &&
            !resume_url.empty() && normalize_url(resume_url) != seed_url)
        {
            page_url = resume_url;
            html.clear();
            continue;
        }
        if (next_page.empty())
        {
            if (!cursor_blocked)
            {
                progress.next_list_url = seed_url;
                progress.backfill_complete = true;
                database::saveCrawlProgress(seed_url, progress);
            }
            break;
        }

        page_url = next_page;
        html.clear();
        if (!incremental_mode && !cursor_blocked)
        {
            progress.next_list_url = page_url;
            database::saveCrawlProgress(seed_url, progress);
        }
        if (successful_insertions >= kNewArticlesPerSeedPerRun || detail_attempts_left <= 0) break;
        std::this_thread::sleep_for(kRequestInterval);
    }
}

// 调度单个种子：读取续爬进度、抓取入口页，并按页面特征选择 Engine2 或普通 HTML 流程。
// 主循环只负责逐种子调用本函数，站点级状态由本函数及其下游函数共同维护。
void crawl_seed(const std::string& raw_seed, RunStats& stats)
{
    const std::string seed_url = normalize_url(raw_seed);
    if (seed_url.empty()) return;
    CrawlProgress progress = database::getCrawlProgress(seed_url);
    // 每次运行都检查第一页的新发布内容，历史页面则从独立游标继续。
    const std::string start_url = seed_url;

    ++stats.list_requests;
    std::cout << "\n[种子] " << seed_url
              << "，模式=" << (progress.backfill_complete ? "增量更新" : "历史回溯") << '\n';
    const std::string first_html = fetch_page(start_url);
    if (first_html.empty()) return;

    // engine2 的列表参数都在种子页源码中；若续爬 URL 不是种子，仍以种子页提取接口参数。
    std::string engine_html = first_html;
    if (normalize_url(start_url) != seed_url && seed_url.find("/engine2/") != std::string::npos)
    {
        ++stats.list_requests;
        engine_html = fetch_page(seed_url);
    }
    Engine2ListRequest probe;
    if (extract_engine2_list_request(engine_html, seed_url, probe, 1, kEngine2PageSize))
        crawl_engine2_seed(seed_url, engine_html, progress, stats);
    else
        crawl_html_seed(seed_url, first_html, progress, stats);
}

// 只读验收单个种子：检查前两页并解析若干真实详情样本，但不修改新闻表和续爬游标。
// main 在 --scan-only 模式下调用它，用于上线前确认能翻页且能得到正文。
bool scan_seed(const std::string& raw_seed)
{
    const std::string seed_url = normalize_url(raw_seed);
    if (seed_url.empty()) return false;
    std::cout << "\n[扫描种子] " << seed_url << '\n';
    const std::string html = fetch_page(seed_url);
    if (html.empty())
    {
        std::cout << "  [扫描失败] 种子页面抓取失败\n";
        return false;
    }

    std::vector<LinkCandidate> first_candidates;
    Engine2ListRequest request;
    if (extract_engine2_list_request(html, seed_url, request, 1, kEngine2PageSize))
    {
        const std::string response1 = fetch_form(request.endpoint, request.form_data);
        int total_pages = 0;
        first_candidates = extract_engine2_detail_links(
            response1, seed_url, request, kEngine2PageSize, &total_pages);
        std::cout << "  [扫描列表] 动态列表页码=1/" << total_pages
                  << "，候选数=" << first_candidates.size() << '\n';
        if (total_pages > 1)
        {
            Engine2ListRequest request2;
            extract_engine2_list_request(html, seed_url, request2, 2, kEngine2PageSize);
            const auto second_candidates = extract_engine2_detail_links(
                fetch_form(request2.endpoint, request2.form_data), seed_url, request2,
                kEngine2PageSize, nullptr);
            std::cout << "  [扫描下一页] 动态列表页码=2，候选数=" << second_candidates.size() << '\n';
            if (second_candidates.empty()) return false;
        }
    }
    else
    {
        const auto raw = extract_link_candidates(html, seed_url);
        first_candidates = select_news_links(raw, seed_url, kMaxCandidatesPerListPage);
        const std::string next = extract_next_page_link(html, seed_url);
        std::cout << "  [扫描列表] 原始链接数=" << raw.size() << "，候选数=" << first_candidates.size()
                  << "，下一页=" << (next.empty() ? "无" : next) << '\n';
        if (!next.empty())
        {
            const std::string page2_html = fetch_page(next);
            const auto page2_candidates = select_news_links(
                extract_link_candidates(page2_html, next), seed_url, kMaxCandidatesPerListPage);
            std::cout << "  [扫描下一页] 候选数=" << page2_candidates.size() << '\n';
            if (page2_candidates.empty()) return false;
        }
    }

    if (first_candidates.empty())
    {
        std::cout << "  [扫描失败] 未发现可靠的详情候选\n";
        return false;
    }

    // 连续尝试多个真实详情，避免首条恰好是被文本质量门槛拒绝的纯图片通知，
    // 从而让后面的有效正文仍有机会证明该种子可用。
    const std::size_t sample_limit = (std::min<std::size_t>)(5, first_candidates.size());
    for (std::size_t i = 0; i < sample_limit; ++i)
    {
        const std::string detail_html = fetch_page(first_candidates[i].url);
        News sample;
        if (detail_html.empty() || !parse_detail_page(detail_html, first_candidates[i].url, sample))
        {
            std::cout << "  [扫描拒绝] 无效详情：" << first_candidates[i].url << '\n';
            continue;
        }
        std::cout << "  [扫描成功] 样本标题=" << sample.title
                  << "，正文字节数=" << sample.content.size()
                  << "，图片=" << (sample.image.empty() ? "无" : sample.image)
                  << "，地址=" << first_candidates[i].url << '\n';
        return true;
    }
    std::cout << "  [扫描失败] 前 " << sample_limit << " 个详情均未包含有效的文字资讯\n";
    return false;
}
}

// 程序总入口：解析运行模式、定位资源、打开数据库、调度爬取或扫描，
// 最后生成展示网站并统一释放数据库与网络库资源。
int main(int argc, char* argv[])
{
    SetConsoleOutputCP(65001);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 固定运行根目录，避免从 bin/x64/Debug、bin/x64/Release 或 output 启动时出现
    // 数据库路径漂移和 output/output 嵌套目录。
    const std::filesystem::path root = find_runtime_root();
    std::error_code path_error;
    std::filesystem::current_path(root, path_error);
    if (path_error)
    {
        std::cerr << "[运行环境] 无法进入项目目录：" << path_error.message() << '\n';
        curl_global_cleanup();
        return 1;
    }

    std::string config_path = "config.json";
    bool scan_only = false;
    bool generate_only = false;
    bool no_website = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--scan-only") scan_only = true;
        else if (argument == "--generate-only") generate_only = true;
        else if (argument == "--no-website") no_website = true;
        else config_path = argument;
    }

    std::vector<std::string> seed_urls;
    if (!generate_only)
    {
        seed_urls = load_config(config_path);
        if (seed_urls.empty())
        {
            std::cerr << "[配置] 未找到种子 URL\n";
            curl_global_cleanup();
            return 1;
        }
    }
    if (!database::openDb())
    {
        curl_global_cleanup();
        return 1;
    }

    // 扫描模式承诺只读；正常运行和只生成网站模式会统一修正历史分类。
    if (!scan_only)
    {
        const int normalized = database::normalizeNewsMetadata();
        std::cout << "[元数据] 已规范记录数=" << normalized << '\n';
    }

    if (generate_only)
    {
        const bool generated = generateNewsWebsite();
        database::closeDb();
        curl_global_cleanup();
        return generated ? 0 : 3;
    }

    RunStats stats;
    int scan_failures = 0;
    std::unordered_set<std::string> unique_seeds;
    for (const auto& raw_seed : seed_urls)
    {
        const std::string seed = normalize_url(raw_seed);
        if (seed.empty() || !unique_seeds.insert(seed).second) continue;
        if (scan_only)
        {
            if (!scan_seed(seed)) ++scan_failures;
        }
        else
        {
            const RunStats before_seed = stats;
            crawl_seed(seed, stats);
            const int seed_inserted = stats.inserted - before_seed.inserted;
            std::cout << "  [种子结果] 新增入库=" << seed_inserted
                      << "/" << kNewArticlesPerSeedPerRun
                      << "，已存在=" << (stats.known - before_seed.known)
                      << "，失败=" << (stats.failed - before_seed.failed)
                      << (seed_inserted == kNewArticlesPerSeedPerRun
                              ? "，配额已完成" : "，来源已爬完或暂时受阻")
                      << '\n';
        }
    }

    if (scan_only)
    {
        std::cout << "\n扫描完成：种子数=" << unique_seeds.size()
                  << "，失败数=" << scan_failures << '\n';
        database::closeDb();
        curl_global_cleanup();
        return scan_failures == 0 ? 0 : 2;
    }

    std::cout << "\n运行完成：新增入库=" << stats.inserted
              << "，已存在=" << stats.known
              << "，失败=" << stats.failed
              << "，列表页请求=" << stats.list_requests
              << "，详情页请求=" << stats.detail_requests << std::endl;
    bool website_ok = true;
    if (!no_website) website_ok = generateNewsWebsite();
    database::closeDb();
    curl_global_cleanup();
    return website_ok ? 0 : 3;
}
