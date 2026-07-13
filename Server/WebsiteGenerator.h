#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
#include<filesystem>

#include "database.h"
#include "other/Category.h"

using namespace std;

void generateNewsWebsite();  //生成网站

void generateNewsIndexPage(const vector<News>& newsList);  //生成新闻首页

void generateNewsDetailPage(const News& news);  //生成单个新闻详情页

void generateSearchPage();  //生成搜索页

void generateCategoryIndexPage();  //生成分类首页

void generateCategoryResultPage(const vector<News>& result, const Category& category);  //生成分类详细页