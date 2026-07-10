#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "sqlite3.h"
using namespace std;

struct News
{
	int id;
	string title;
	string time;
	string content;
	string abstract;
	string url;
	string source;
	string image;//ͼƬ·��
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
			cout << "���ݿ��ʧ�ܣ�������" << to_string(code) << 
				"ԭ��:" << sqlite3_errmsg(db) << ".";
			db = nullptr;
		}
		else
		{
			cout << "���ݿ�򿪳ɹ�!";
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

	static void addNews(News news);//����ֵ

	//���в�ѯ�������ʱ�併������
	static vector<News> getTodayNews();
	static vector<News> getWeekNews();
	static vector<News> getMonthNews();
	static vector<News> getAllNews();

	//��Դ��������Щ�����ô�����࣬�ݸ���
	static vector<News> getNewsBySource(string source);//����Դ����
	static vector<News> getNewsByTopic(string topic);//���������

	static vector<News> getNewsByTitle(string title);
};

