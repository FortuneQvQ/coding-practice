#include <chrono>
#include "database.h"

sqlite3* database::db = nullptr;

void database::addNews(News news)
{
	string sql = "INSERT INTO news (title,time,content,abstract,url,source,image,topic) "
		"VALUES ('"+ news.title + "','" + news.time + "','" + news.content + "','" + news.abstract + "','" + news.url + "','" + news.source + "','" + news.image + "','" + news.topic + "');";
	sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
}

int callback(void* data,int argc, char** argv,char** colName)
{
    vector<News>* newsList = static_cast<vector<News>*>(data);

    News news;

    for (int i = 0; i < argc; i++)
    {
        string column = colName[i];

        if (argv[i] == nullptr)
            continue;

        string value = argv[i];

        if (column == "id")
        {
            news.id = stoi(value);
        }
        else if (column == "title")
        {
            news.title = value;
        }
        else if (column == "time")
        {
            news.time = value;
        }
        else if (column == "content")
        {
            news.content = value;
        }
        else if (column == "abstract")
        {
            news.abstract = value;
        }
        else if (column == "url")
        {
            news.url = value;
        }
        else if (column == "source")
        {
            news.source = value;
        }
        else if (column == "image")
        {
            news.image = value;
        }
        else if (column == "topic")
        {
            news.topic = value;
        }
    }

    newsList->push_back(news);

    return 0;
}

vector<News> database::getTodayNews()
{
    vector<News> result;
	string sql = "SELECT * FROM news "
		"WHERE time>= datetime('now','-1 day','localtime') "
		"ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}

vector<News> database::getWeekNews()
{
    vector<News> result;
    string sql = "SELECT * FROM news "
        "WHERE time>= datetime('now','-7 day','localtime') "
        "ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}

vector<News> database::getMonthNews()
{
    vector<News> result;
    string sql = "SELECT * FROM news "
        "WHERE time>= datetime('now','-1 month','localtime') "
        "ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}

vector<News> database::getAllNews()
{
    vector<News> result;
    string sql = "SELECT * FROM news "
        "ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}

vector<News> database::getNewsByTitle(string title)
{
    vector<News> result;
    string sql = "SELECT * FROM news "
        "WHERE title LIKE'%"+title+"%'"
        "ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}

vector<News> database::getNewsBySource(string source)
{
    vector<News> result;
    string sql = "SELECT * FROM news "
        "WHERE source LIKE'%" + source + "%'"
        "ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}

vector<News> database::getNewsByTopic(string topic)
{
    vector<News> result;
    string sql = "SELECT * FROM news "
        "WHERE topic LIKE'%" + topic + "%'"
        "ORDER BY time DESC;";
    sqlite3_exec(db, sql.c_str(), callback, &result, nullptr);
    return result;
}
