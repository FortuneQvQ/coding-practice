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
// A seed may keep paging until it has inserted its quota.  The high page ceiling is only a
// loop guard; the persisted cursor avoids re-downloading known detail pages on later runs.
constexpr int kListPagesPerSeedPerRun = 100;
// Only a successful SQLite insertion consumes this quota.
constexpr int kNewArticlesPerSeedPerRun = 20;
// Bad links do not consume the insertion quota, but this separate guard prevents a hostile
// page containing endless garbage links from causing unbounded network requests.
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

bool parse_detail_page(const std::string& html, const std::string& url, News& news)
{
    // Engine2 details carry the complete article in structured JSON.  Ordinary sites are
    // parsed from their article DOM first so unrelated configuration JSON cannot masquerade
    // as news content.
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

    // Only invoke the structured-script parser for frameworks that actually expose article
    // state there. This avoids sending arbitrary malformed legacy HTML through Tidy.
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
        std::cout << "    [inserted] " << news.title << '\n';
        return DetailResult::inserted;
    }
    return database::newsExists(normalized) ? DetailResult::known : DetailResult::retry;
}

// 处理一页详情候选。只有全部候选已入库、成功入库或已达到失败阈值时，才允许推进页游标。
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
        // Navigation links are often repeated on every list page.  De-duplicate for the
        // whole seed run so a bad shared link can consume at most one detail attempt.
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
            std::cout << "  [engine2] page " << page_num << " returned no detail candidates\n";
            return true;
        }

        int unseen = 0;
        bool has_retry = false;
        const bool can_continue = process_candidates(
            candidates, extract_domain(seed_url), stats, successful_insertions,
            detail_attempts_left, unseen, has_retry, seen_details);
        cursor_blocked = cursor_blocked || has_retry;
        std::cout << "  [engine2] page=" << page_num << "/" << total_pages
                  << " candidates=" << candidates.size() << " unseen=" << unseen << '\n';

        // 增量阶段遇到整页旧数据，说明后续更旧，无需继续翻页。
        if (incremental_mode && unseen == 0) return true;
        // 配额耗尽或有暂时失败的详情，不推进游标，下次仍从本页补齐。
        if (!can_continue) return true;

        // Every run checks page 1 for fresh news, then jumps to the persisted historical cursor.
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
        std::cout << "  [list-scan] raw_links=" << raw.size()
                  << " detail_candidates=" << candidates.size() << '\n';
        if (candidates.empty())
        {
            std::cout << "  [list] no reliable detail links: " << page_url << '\n';
            break;
        }

        int unseen = 0;
        bool has_retry = false;
        const bool can_continue = process_candidates(
            candidates, extract_domain(seed_url), stats, successful_insertions,
            detail_attempts_left, unseen, has_retry, seen_details);
        cursor_blocked = cursor_blocked || has_retry;
        std::cout << "  [list] candidates=" << candidates.size() << " unseen=" << unseen
                  << " next=" << (next_page.empty() ? "none" : next_page) << '\n';

        if (incremental_mode && unseen == 0) break;
        if (!can_continue) break;

        // Always inspect page 1 for newly published items, then continue old-news backfill from
        // the saved cursor instead of walking the same known list pages again.
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

void crawl_seed(const std::string& raw_seed, RunStats& stats)
{
    const std::string seed_url = normalize_url(raw_seed);
    if (seed_url.empty()) return;
    CrawlProgress progress = database::getCrawlProgress(seed_url);
    // Page 1 is checked on every run for new publications. Historical pages resume separately.
    const std::string start_url = seed_url;

    ++stats.list_requests;
    std::cout << "\n[seed] " << seed_url
              << " mode=" << (progress.backfill_complete ? "incremental" : "backfill") << '\n';
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

// 只读验收模式：检查每个种子的首两页，并实际解析一个详情页，不修改新闻表和游标。
bool scan_seed(const std::string& raw_seed)
{
    const std::string seed_url = normalize_url(raw_seed);
    if (seed_url.empty()) return false;
    std::cout << "\n[scan-seed] " << seed_url << '\n';
    const std::string html = fetch_page(seed_url);
    if (html.empty())
    {
        std::cout << "  [scan-fail] seed fetch failed\n";
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
        std::cout << "  [scan-list] engine2 page=1/" << total_pages
                  << " candidates=" << first_candidates.size() << '\n';
        if (total_pages > 1)
        {
            Engine2ListRequest request2;
            extract_engine2_list_request(html, seed_url, request2, 2, kEngine2PageSize);
            const auto second_candidates = extract_engine2_detail_links(
                fetch_form(request2.endpoint, request2.form_data), seed_url, request2,
                kEngine2PageSize, nullptr);
            std::cout << "  [scan-next] engine2 page=2 candidates=" << second_candidates.size() << '\n';
            if (second_candidates.empty()) return false;
        }
    }
    else
    {
        const auto raw = extract_link_candidates(html, seed_url);
        first_candidates = select_news_links(raw, seed_url, kMaxCandidatesPerListPage);
        const std::string next = extract_next_page_link(html, seed_url);
        std::cout << "  [scan-list] raw=" << raw.size() << " candidates=" << first_candidates.size()
                  << " next=" << (next.empty() ? "none" : next) << '\n';
        if (!next.empty())
        {
            const std::string page2_html = fetch_page(next);
            const auto page2_candidates = select_news_links(
                extract_link_candidates(page2_html, next), seed_url, kMaxCandidatesPerListPage);
            std::cout << "  [scan-next] candidates=" << page2_candidates.size() << '\n';
            if (page2_candidates.empty()) return false;
        }
    }

    if (first_candidates.empty())
    {
        std::cout << "  [scan-fail] no reliable detail candidates\n";
        return false;
    }

    // Try several real details. A legitimate list can contain an image-only notice which is
    // rejected by the text-quality gate; that must not hide valid text articles behind it.
    const std::size_t sample_limit = (std::min<std::size_t>)(5, first_candidates.size());
    for (std::size_t i = 0; i < sample_limit; ++i)
    {
        const std::string detail_html = fetch_page(first_candidates[i].url);
        News sample;
        if (detail_html.empty() || !parse_detail_page(detail_html, first_candidates[i].url, sample))
        {
            std::cout << "  [scan-reject] invalid detail: " << first_candidates[i].url << '\n';
            continue;
        }
        std::cout << "  [scan-ok] sample=" << sample.title
                  << " content_bytes=" << sample.content.size()
                  << " image=" << (sample.image.empty() ? "none" : sample.image)
                  << " url=" << first_candidates[i].url << '\n';
        return true;
    }
    std::cout << "  [scan-fail] first " << sample_limit << " details contained no valid text article\n";
    return false;
}
}

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(65001);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 固定运行根目录，避免从 x64/Debug、x64/Release 或 output 启动时出现
    // 数据库路径漂移和 output/output 嵌套目录。
    const std::filesystem::path root = find_runtime_root();
    std::error_code path_error;
    std::filesystem::current_path(root, path_error);
    if (path_error)
    {
        std::cerr << "[runtime] cannot enter project directory: " << path_error.message() << '\n';
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
            std::cerr << "[config] no seed URL found\n";
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
        std::cout << "[metadata] normalized_rows=" << normalized << '\n';
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
            std::cout << "  [seed-result] inserted=" << seed_inserted
                      << "/" << kNewArticlesPerSeedPerRun
                      << " known=" << (stats.known - before_seed.known)
                      << " failed=" << (stats.failed - before_seed.failed)
                      << (seed_inserted == kNewArticlesPerSeedPerRun
                              ? " quota_filled" : " source_exhausted_or_blocked")
                      << '\n';
        }
    }

    if (scan_only)
    {
        std::cout << "\nscan finished: seeds=" << unique_seeds.size()
                  << " failures=" << scan_failures << '\n';
        database::closeDb();
        curl_global_cleanup();
        return scan_failures == 0 ? 0 : 2;
    }

    std::cout << "\nfinished: inserted=" << stats.inserted
              << " known=" << stats.known
              << " failed=" << stats.failed
              << " list_requests=" << stats.list_requests
              << " detail_requests=" << stats.detail_requests << '\n';
    bool website_ok = true;
    if (!no_website) website_ok = generateNewsWebsite();
    database::closeDb();
    curl_global_cleanup();
    return website_ok ? 0 : 3;
}
