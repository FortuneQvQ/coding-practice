#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "sqlite3.h"

struct News
{
    int id = 0;
    std::string title;
    std::string time;
    std::string content;
    std::string abstract;
    std::string url;
    std::string source;
    std::string image;
    std::string topic;
};

// 每个种子独立保存回溯进度。普通列表使用 next_list_url，动态接口使用 next_page。
struct CrawlProgress
{
    std::string next_list_url;
    int next_page = 1;
    bool backfill_complete = false;
};

class database
{
private:
    static sqlite3* db;

public:
    // 打开统一新闻数据库、启用 WAL 并创建所需表；main 必须先调用它，
    // 后续去重、进度、失败记录和展示查询才有可用连接。
    static bool openDb()
    {
        const int code = sqlite3_open("data/news.db", &db);
        if (code != SQLITE_OK)
        {
            std::cerr << "[数据库] 打开失败，错误码=" << code
                      << "，原因=" << (db == nullptr ? "未知" : sqlite3_errmsg(db)) << '\n';
            if (db != nullptr) sqlite3_close(db);
            db = nullptr;
            return false;
        }

        // WAL 允许展示端在爬虫短事务写入时继续读取；忙等待让并发写入先短暂等待，
        // 网络请求期间不持有长事务，因此不会长期锁住数据库。
        sqlite3_busy_timeout(db, 5000);
        char* pragma_error = nullptr;
        const int pragma_code = sqlite3_exec(
            db,
            "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;",
            nullptr, nullptr, &pragma_error);
        if (pragma_code != SQLITE_OK)
        {
            std::cerr << "[数据库] WAL 模式设置失败："
                      << (pragma_error == nullptr ? "未知" : pragma_error) << '\n';
            sqlite3_free(pragma_error);
            sqlite3_close(db);
            db = nullptr;
            return false;
        }

        const char* create_sql = R"(
            CREATE TABLE IF NOT EXISTS news (
                id       INTEGER PRIMARY KEY AUTOINCREMENT,
                title    TEXT NOT NULL,
                time     TEXT,
                content  TEXT NOT NULL,
                abstract TEXT,
                url      TEXT UNIQUE NOT NULL,
                source   TEXT,
                image    TEXT,
                topic    TEXT
            );

            CREATE TABLE IF NOT EXISTS crawl_progress (
                seed_url          TEXT PRIMARY KEY,
                next_list_url     TEXT,
                next_page         INTEGER NOT NULL DEFAULT 1,
                backfill_complete INTEGER NOT NULL DEFAULT 0,
                updated_at        TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS crawl_failures (
                url           TEXT PRIMARY KEY,
                failure_count INTEGER NOT NULL DEFAULT 0,
                last_error    TEXT,
                updated_at    TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )";
        char* error = nullptr;
        const int create_code = sqlite3_exec(db, create_sql, nullptr, nullptr, &error);
        if (create_code != SQLITE_OK)
        {
            std::cerr << "[数据库] 创建数据表失败：" << (error == nullptr ? "未知" : error) << '\n';
            sqlite3_free(error);
            sqlite3_close(db);
            db = nullptr;
            return false;
        }
        std::cout << "[数据库] 已打开\n";
        return true;
    }

    // 关闭共享数据库连接；main 在所有爬取和网站生成结束后统一调用，确保 WAL 正常收尾。
    static void closeDb()
    {
        if (db != nullptr)
        {
            const int code = sqlite3_close(db);
            if (code != SQLITE_OK)
                std::cerr << "[数据库] 关闭失败：" << sqlite3_errstr(code) << '\n';
            db = nullptr;
        }
    }

    // 校验并插入一条新闻，URL 唯一约束保证重复内容不消耗新增配额。
    static bool addNews(const News& news);
    // 重新计算历史记录的来源和主题，只更新发生变化的行。
    static int normalizeNewsMetadata();
    // 判断规范 URL 是否已入库，供详情下载前去重。
    static bool newsExists(const std::string& url);
    // 读取指定种子的分页游标，使下一次运行能从上次位置续爬。
    static CrawlProgress getCrawlProgress(const std::string& seed_url);
    // 保存指定种子的下一页位置和回溯完成状态。
    static bool saveCrawlProgress(const std::string& seed_url, const CrawlProgress& progress);
    // 查询详情链接连续失败次数，达到阈值后避免反复浪费请求。
    static int getFailureCount(const std::string& url);
    // 累加详情失败次数并保存原因，供后续运行决定是否重试。
    static void recordFailure(const std::string& url, const std::string& error);
    // 成功入库后清除旧失败记录，恢复该 URL 的正常状态。
    static void clearFailure(const std::string& url);
    // 查询最近一天资讯，供按时间展示使用。
    static std::vector<News> getTodayNews();
    // 查询最近一周资讯，供按时间展示使用。
    static std::vector<News> getWeekNews();
    // 查询最近一个月资讯，供按时间展示使用。
    static std::vector<News> getMonthNews();
    // 查询全部资讯，展示生成器据此生成详情页和 JSON。
    static std::vector<News> getAllNews();
    // 按来源筛选资讯，保留给服务端分类查询使用。
    static std::vector<News> getNewsBySource(const std::string& source);
    // 按主题筛选资讯，保留给服务端分类查询使用。
    static std::vector<News> getNewsByTopic(const std::string& topic);
    // 按标题关键字筛选资讯，保留给服务端搜索使用。
    static std::vector<News> getNewsByTitle(const std::string& title);
};
