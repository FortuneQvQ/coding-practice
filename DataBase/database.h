#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "sqlite3.h"
using namespace std;

struct news
{
	int id;
	string title;
	string time;
	string content;
	string abstract;
	string url;
	string source;
	string image;//图片路径
	string topic;
};

static class database
{
private:
	static sqlite3* db;
public:
	static void openDb()
	{
		int code = sqlite3_open("news.db", &db);
		if (code != SQLITE_OK)
		{
			cout << "数据库打开失败！错误码" << to_string(code) << 
				"原因:" << sqlite3_errmsg(db) << ".";
			db = nullptr;
		}
		else
		{
			cout << "数据库打开成功!";
		}
	}
	static void closeDb()
	{
		if (db != nullptr)
		{
			sqlite3_close(db);
			db = nullptr;
		}
	}

	static void addNews(news news);//返回值

	//所有查询结果按照时间降序排列
	static vector<news> getTodayNews();
	static vector<news> getWeekNews();
	static vector<news> getMonthNews();
	static vector<news> getAllNews();

	//来源和主题有些难以用代码归类，暂搁置
	//static vector<news> getNewsBySource(string source);//按来源分类
	//static vector<news> getNewsByTopic(string topic);//按主题分类

	static vector<news> getNewsByTitle(string title);
};

