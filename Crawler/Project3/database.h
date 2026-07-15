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
    static bool openDb()
    {
        const int code = sqlite3_open("../DataBase/data/news.db", &db);
        if (code != SQLITE_OK)
        {
            std::cerr << "[DB] open failed, code=" << code
                      << ", reason=" << (db == nullptr ? "unknown" : sqlite3_errmsg(db)) << '\n';
            if (db != nullptr) sqlite3_close(db);
            db = nullptr;
            return false;
        }

        // WAL lets readers continue while the crawler performs its short single-row writes.
        // The busy timeout waits briefly for another writer instead of immediately failing
        // with "database is locked"; no long transaction is kept during network requests.
        sqlite3_busy_timeout(db, 5000);
        char* pragma_error = nullptr;
        const int pragma_code = sqlite3_exec(
            db,
            "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;",
            nullptr, nullptr, &pragma_error);
        if (pragma_code != SQLITE_OK)
        {
            std::cerr << "[DB] WAL setup failed: "
                      << (pragma_error == nullptr ? "unknown" : pragma_error) << '\n';
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
            std::cerr << "[DB] create table failed: " << (error == nullptr ? "unknown" : error) << '\n';
            sqlite3_free(error);
            sqlite3_close(db);
            db = nullptr;
            return false;
        }
        std::cout << "[DB] opened\n";
        return true;
    }

    static void closeDb()
    {
        if (db != nullptr)
        {
            const int code = sqlite3_close(db);
            if (code != SQLITE_OK)
                std::cerr << "[DB] close failed: " << sqlite3_errstr(code) << '\n';
            db = nullptr;
        }
    }

    static bool addNews(const News& news);
    // 重新计算历史记录的来源和主题，只更新发生变化的行。
    static int normalizeNewsMetadata();
    static bool newsExists(const std::string& url);
    static CrawlProgress getCrawlProgress(const std::string& seed_url);
    static bool saveCrawlProgress(const std::string& seed_url, const CrawlProgress& progress);
    static int getFailureCount(const std::string& url);
    static void recordFailure(const std::string& url, const std::string& error);
    static void clearFailure(const std::string& url);
    static std::vector<News> getTodayNews();
    static std::vector<News> getWeekNews();
    static std::vector<News> getMonthNews();
    static std::vector<News> getAllNews();
    static std::vector<News> getNewsBySource(const std::string& source);
    static std::vector<News> getNewsByTopic(const std::string& topic);
    static std::vector<News> getNewsByTitle(const std::string& title);
};
