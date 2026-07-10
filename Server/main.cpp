#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
#include<filesystem>

#include "database.h"
#include "Category.h"

using namespace std;


//获取HTML模板
string readTemplate(const string& templateFile) {

    ifstream file(templateFile, ios::binary);
    if (!file.is_open()) {
        cerr << "Failed to open template file: " << templateFile << endl;
        return "";
    }
    stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    return buffer.str();

}


//字符串替换函数
void replace(string &html, string key, string value){

    size_t pos;
    while((pos = html.find(key)) != string::npos){
        html.replace(pos, key.length(), value);
    }

}


//生成单个新闻详情页
void generateNewsDetailPage(const News& news) {

    //读取HTML模板
    string html = readTemplate("templates/news_detail.html");
    if (html.empty()) {
        cerr << "Template is empty. Cannot generate news detail page." << endl;
        return;
    }

    //进行字符串替换
    replace(html, "{{title}}", news.title);
    replace(html, "{{time}}", news.time);
    replace(html, "{{content}}", news.content);
    replace(html, "{{url}}", news.url);
    replace(html, "{{source}}", news.source);
    string imageBlock;
    if(!news.image.empty() && filesystem::exists("output/" + news.image)){
        imageBlock = "<img class=\"news-image\" src=\"../" + news.image + "\">";
    }
    else{
        imageBlock = "";
    }   
    replace(html, "{{imageBlock}}",imageBlock);
    replace(html, "{{abstract}}", news.abstract);

    //生成HTML文件
    string filename = "output/detail/" + to_string(news.id) + ".html";
    ofstream outFile(filename, ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: " << filename << endl;
    }

}


//生成最新新闻卡片HTML(数量由count决定）（用于生成新闻主页）
string generateLatestNewsCards(const vector<News>& newsList)
{

    string newsBlock;

    int count = 0;

    for(auto& news : newsList)
    {
        if(count >= 3)
            break;

        newsBlock += "<div class=\"news-card\">";
         //来源
        newsBlock += "<div class=\"news-source\">";
        newsBlock += news.source;
        newsBlock += "</div>";
        //标题
        newsBlock += "<div class=\"news-title\">";
        newsBlock += news.title;
        newsBlock += "</div>";
        //时间
        newsBlock += "<div class=\"news-time\">";
        newsBlock += u8"发布时间：";
        newsBlock += news.time;
        newsBlock += "</div>";
        //摘要
        newsBlock += "<div class=\"news-abstract\">";
        newsBlock += news.abstract;
        newsBlock += "</div>";
        //详情按钮
        newsBlock += "<a class=\"detail-btn\" href=\"detail/";
        newsBlock += to_string(news.id);
        newsBlock += ".html\">";
        newsBlock += u8"查看详情";
        newsBlock += "</a>";
        newsBlock += "</div>";

        count++;
    }

    return newsBlock;

}


//生成所有新闻卡片HTML（用于生成分类详情页）
string generateAllNewsCards(const vector<News>& newsList)
{

    string newsBlock;

    for(auto& news : newsList)
    {
        newsBlock += "<div class=\"news-card\">";
         //来源
        newsBlock += "<div class=\"news-source\">";
        newsBlock += news.source;
        newsBlock += "</div>";
        //标题
        newsBlock += "<div class=\"news-title\">";
        newsBlock += news.title;
        newsBlock += "</div>";
        //时间
        newsBlock += "<div class=\"news-time\">";
        newsBlock += u8"发布时间：";
        newsBlock += news.time;
        newsBlock += "</div>";
        //摘要
        newsBlock += "<div class=\"news-abstract\">";
        newsBlock += news.abstract;
        newsBlock += "</div>";
        //详情按钮
        newsBlock += "<a class=\"detail-btn\" href=\"../detail/";
        newsBlock += to_string(news.id);
        newsBlock += ".html\">";
        newsBlock += u8"查看详情";
        newsBlock += "</a>";
        newsBlock += "</div>";
    }

    return newsBlock;

}


//生成新闻首页
void generateNewsIndexPage(const vector<News>& newsList) {

    //读取HTML模板
    string html = readTemplate("templates/index.html");
    if (html.empty()) {
        cerr << "Template is empty. Cannot generate news index page." << endl;
        return;
    }

    //生成新闻列表HTML
    string newsListHtml = generateLatestNewsCards(newsList);

    //将新闻列表HTML插入到首页模板中
    replace(html, "{{news_list}}", newsListHtml);

    //生成HTML文件
    ofstream outFile("output/index.html", ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: output/index.html" << endl;
    }

}


//生成分类首页
void generateCategoryIndexPage(){

    //读取模板
    string html = readTemplate("templates/category_index.html");
    if (html.empty()) {
        cerr << "category_index is empty. Cannot generate category index page." << endl;
        return;
    }

    //生成HTML文件
    ofstream outFile("output/category_index.html", ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: output/category_index.html" << endl;
    }

}


//获得新闻筛选结果
vector<News> getCategoryNews(const Category& category){

    vector<News> result;

    //时间分类
    if(category.type == "time"){
        if(category.value == "today"){
            result = database::getTodayNews();
        }
        else if(category.value == "week_ago"){
            result = database::getWeekNews();
        }
        else if(category.value == "month_ago"){
            result = database::getMonthNews();
        }
        else if(category.value == "history"){
            result = database::getAllNews();
        }
    }

    //来源分类
    else if(category.type == "source"){
        result = database::getNewsBySource(category.value);
    }

    //主题分类
    else if(category.type == "topic"){
        result = database::getNewsByTopic(category.value);
    }

    return result;

}


//生成分类详细页
void generateCategoryResultPage(const vector<News>& result, const Category& category){

    //读取模板
    string html = readTemplate("templates/category_result.html");
    if (html.empty()) {
        cerr << "category_result is empty. Cannot generate category page." << endl;
        return;
    }

    //替换标题
    replace(html, "{{category_title}}", category.title);

    //替换新闻列表
    replace(html, "{{news_list}}", generateAllNewsCards(result));

    //生成HTML文件
    ofstream outFile("output/categories/" + category.filename + ".html", ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: " << category.filename + ".html" << endl;
    }
    
}


int main()
{
    database::openDb();

    //生成新闻详情页
    vector<News> newsList = database::getAllNews();
    for(auto& news : newsList)
    {
        generateNewsDetailPage(news);
    }
    cout << "News detail pages generated successfully!" << endl;

    //生成新闻首页
    generateNewsIndexPage(newsList);
    cout << "News index page generated successfully!" << endl;

    //生成分类首页
    generateCategoryIndexPage();
    cout << "Category index page generated successfully!" << endl;

    //生成分类详情页
    vector<Category> categories =
    {
        {
            u8"按时间分类：今日新闻",
            "time_today",
            "time",
            "today"
        },
        {
            u8"按时间分类：最近一周",
            "time_weekago",
            "time",
            "week_ago"
        },
        {
            u8"按时间分类：最近一个月",
            "time_monthago",
            "time",
            "month_ago"
        },
        {
            u8"按时间分类：历史咨询",
            "time_history",
            "time",
            "history"
        },
        {
            u8"按来源分类：教务处",
            "source_jwc",
            "source",
            "jwc"
        },
        {
            u8"按来源分类：学生会",
            "source_xsh",
            "source",
            "xsh"
        },
        {
            u8"按来源分类：学院通知",
            "source_xy",
            "source",
            "xy"
        },
        {
            u8"按来源分类：学生社团",
            "source_club",
            "source",
            "club"
        },
        {
            u8"按主题分类：教学通知",
            "topic_teach",
            "topic",
            "teach"
        },
        {
            u8"按主题分类：校园活动",
            "topic_activity",
            "topic",
            "activity"
        },
        {
            u8"按主题分类：竞赛比赛",
            "topic_competition",
            "topic",
            "competition"
        },
        {
            u8"按主题分类：考试安排",
            "topic_exam",
            "topic",
            "exam"
        },
        {
            u8"按主题分类：科研创新",
            "topic_research",
            "topic",
            "research"
        }
    };
    for(auto& category : categories)
    {
        vector<News> result = getCategoryNews(category);
        generateCategoryResultPage(result, category);
    }
    cout << "Category pages generated successfully!" << endl;

    database::closeDb();
    
    return 0;
}
