#include "database.h"
#include "news_classifier.h"

#include <algorithm>
#include <set>

using namespace std;

sqlite3* database::db = nullptr;

namespace
{
bool safe_database_text(const std::string& text)
{
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find("\xEF\xBF\xBD") != std::string::npos ||
        text.find("锟斤拷") != std::string::npos)
        return false;
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
        int count = 0;
        if ((c & 0xE0) == 0xC0) count = 1;
        else if ((c & 0xF0) == 0xE0) count = 2;
        else if ((c & 0xF8) == 0xF0) count = 3;
        else return false;
        if (i + count >= text.size()) return false;
        for (int j = 1; j <= count; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) return false;
        i += static_cast<std::size_t>(count + 1);
    }
    return controls * 100 < text.size();
}
}

bool database::addNews(const News& news)
{
    News normalized = news;
    normalize_news_metadata(normalized);
    static const std::set<std::string> rejected_titles = {
        "首页", "通知公告", "新闻中心", "综合新闻", "学院新闻", "学校新闻",
        "更多", "详情", "栏目列表", "服务指南", "中心介绍", "下载中心",
        "机构设置", "帮助信息", "版权声明", "网站地图", "联系我们"
    };
    const bool image_notice = normalized.image.rfind("http://", 0) == 0 ||
                              normalized.image.rfind("https://", 0) == 0;
    const bool navigation_with_site_suffix =
        normalized.title.rfind("通知公告-", 0) == 0 ||
        normalized.title.rfind("新闻动态-", 0) == 0 ||
        normalized.title.rfind("学院新闻-", 0) == 0;
    if (db == nullptr || normalized.url.empty() || normalized.title.size() < 4 || normalized.title.size() > 300 ||
        (normalized.content.size() < 80 && !image_notice) || !safe_database_text(normalized.title) ||
        !safe_database_text(normalized.content) || normalized.content.find("<script") != std::string::npos ||
        normalized.content.find("<!DOCTYPE") != std::string::npos ||
        navigation_with_site_suffix ||
        rejected_titles.find(normalized.title) != rejected_titles.end())
    {
        cerr << "[DB] skip invalid news" << endl;
        return false;
    }

    const char* sql = R"(
        INSERT OR IGNORE INTO news(title,time,content,abstract,url,source,image,topic)
        VALUES(?,?,?,?,?,?,?,?);
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, normalized.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, normalized.time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, normalized.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, normalized.abstract.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, normalized.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, normalized.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, normalized.image.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, normalized.topic.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    const bool inserted = rc == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) cerr << "[DB] insert failed: " << sqlite3_errmsg(db) << endl;
    return inserted;
}

int database::normalizeNewsMetadata()
{
    if (db == nullptr) return 0;
    const std::vector<News> rows = getAllNews();
    sqlite3_stmt* update = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE news SET source=?,topic=? WHERE id=?", -1,
                           &update, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    int changed = 0;
    for (News row : rows)
    {
        const std::string old_source = row.source;
        const std::string old_topic = row.topic;
        normalize_news_metadata(row);
        if (row.source == old_source && row.topic == old_topic) continue;

        sqlite3_reset(update);
        sqlite3_clear_bindings(update);
        sqlite3_bind_text(update, 1, row.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(update, 2, row.topic.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(update, 3, row.id);
        if (sqlite3_step(update) == SQLITE_DONE) ++changed;
    }
    sqlite3_finalize(update);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    return changed;
}

bool database::newsExists(const std::string& url)
{
    if (db == nullptr || url.empty()) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM news WHERE url=? LIMIT 1", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

CrawlProgress database::getCrawlProgress(const std::string& seed_url)
{
    CrawlProgress result;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT next_list_url,next_page,backfill_complete FROM crawl_progress WHERE seed_url=?";
    if (db == nullptr || sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, seed_url.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text != nullptr) result.next_list_url = reinterpret_cast<const char*>(text);
        result.next_page = std::max(1, sqlite3_column_int(stmt, 1));
        result.backfill_complete = sqlite3_column_int(stmt, 2) != 0;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool database::saveCrawlProgress(const std::string& seed_url, const CrawlProgress& progress)
{
    const char* sql = R"(
        INSERT INTO crawl_progress(seed_url,next_list_url,next_page,backfill_complete,updated_at)
        VALUES(?,?,?,?,CURRENT_TIMESTAMP)
        ON CONFLICT(seed_url) DO UPDATE SET next_list_url=excluded.next_list_url,
            next_page=excluded.next_page,backfill_complete=excluded.backfill_complete,
            updated_at=CURRENT_TIMESTAMP;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (db == nullptr || sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, seed_url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, progress.next_list_url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, std::max(1, progress.next_page));
    sqlite3_bind_int(stmt, 4, progress.backfill_complete ? 1 : 0);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

int database::getFailureCount(const std::string& url)
{
    sqlite3_stmt* stmt = nullptr;
    if (db == nullptr || sqlite3_prepare_v2(db, "SELECT failure_count FROM crawl_failures WHERE url=?", -1, &stmt, nullptr) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    const int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    return count;
}

void database::recordFailure(const std::string& url, const std::string& error)
{
    const char* sql = R"(
        INSERT INTO crawl_failures(url,failure_count,last_error,updated_at)
        VALUES(?,1,?,CURRENT_TIMESTAMP)
        ON CONFLICT(url) DO UPDATE SET failure_count=failure_count+1,
            last_error=excluded.last_error,updated_at=CURRENT_TIMESTAMP;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (db == nullptr || sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void database::clearFailure(const std::string& url)
{
    sqlite3_stmt* stmt = nullptr;
    if (db == nullptr || sqlite3_prepare_v2(db, "DELETE FROM crawl_failures WHERE url=?", -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

namespace
{
int callback(void* data, int argc, char** argv, char** col_name)
{
    auto* news_list = static_cast<vector<News>*>(data);
    News news;
    for (int i = 0; i < argc; ++i)
    {
        if (argv[i] == nullptr) continue;
        const string column = col_name[i];
        const string value = argv[i];
        if (column == "id") news.id = stoi(value);
        else if (column == "title") news.title = value;
        else if (column == "time") news.time = value;
        else if (column == "content") news.content = value;
        else if (column == "abstract") news.abstract = value;
        else if (column == "url") news.url = value;
        else if (column == "source") news.source = value;
        else if (column == "image") news.image = value;
        else if (column == "topic") news.topic = value;
    }
    news_list->push_back(std::move(news));
    return 0;
}

vector<News> query_news(sqlite3* db, const string& sql)
{
    vector<News> result;
    if (db != nullptr) sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}
}

vector<News> database::getTodayNews()
{
    return query_news(db, "SELECT * FROM news WHERE time>=datetime('now','-1 day','localtime') ORDER BY time DESC;");
}

vector<News> database::getWeekNews()
{
    return query_news(db, "SELECT * FROM news WHERE time>=datetime('now','-7 day','localtime') ORDER BY time DESC;");
}

vector<News> database::getMonthNews()
{
    return query_news(db, "SELECT * FROM news WHERE time>=datetime('now','-1 month','localtime') ORDER BY time DESC;");
}

vector<News> database::getAllNews()
{
    return query_news(db, "SELECT * FROM news ORDER BY time DESC;");
}

vector<News> database::getNewsByTitle(const string& title)
{
    return query_news(db, "SELECT * FROM news WHERE title LIKE '%" + title + "%' ORDER BY time DESC;");
}

vector<News> database::getNewsBySource(const string& source)
{
    return query_news(db, "SELECT * FROM news WHERE source LIKE '%" + source + "%' ORDER BY time DESC;");
}

vector<News> database::getNewsByTopic(const string& topic)
{
    return query_news(db, "SELECT * FROM news WHERE topic LIKE '%" + topic + "%' ORDER BY time DESC;");
}
