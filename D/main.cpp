#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
#include<filesystem>

#include "News.h"

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
    replace(html, "{{image}}", news.image);
    replace(html, "{{abstract}}", news.abstract);

    //生成HTML文件
    string filename = "output/detail/" + news.id + ".html";
    ofstream outFile(filename, ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: " << filename << endl;
    }
}

//生成新闻卡片HTML
string generateNewsCards(const vector<News>& newsList)
{

    string newsBlock;
	
    int count = 0;

    for(auto& news : newsList)
    {
        if(count >= 3)
            break;
		
		
        newsBlock += "<div class=\"news-card\">";

		
         //来源

        newsBlock +=
        "<div class=\"news-source\">";

        newsBlock +=
        news.source;

        newsBlock +=
        "</div>";


        //标题

        newsBlock +=
        "<div class=\"news-title\">";

        newsBlock +=
        news.title;

        newsBlock +=
        "</div>";



        //时间

        newsBlock +=
        "<div class=\"news-time\">";

        newsBlock +=
        u8"发布时间：";

        newsBlock +=
        news.time;

        newsBlock +=
        "</div>";



        //摘要

        newsBlock +=
        "<div class=\"news-abstract\">";

        newsBlock +=
        news.abstract;

        newsBlock +=
        "</div>";



        //详情按钮

        newsBlock +=
        "<a class=\"detail-btn\" href=\"detail/";

        newsBlock += 
		news.id;

        newsBlock +=
        ".html\">";


        newsBlock +=
        u8"查看详情";


        newsBlock +=
        "</a>";


        newsBlock +=
        "</div>";

		
        count++;

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
    string newsListHtml = generateNewsCards(newsList);

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


int main()
{
    filesystem::create_directories("output/detail");

    vector<News> newsList =
    {

        {
            "1",
            u8"关于暑假安排的通知",
            "2026-07-07",
            "正文1",
            u8"学校发布暑假安排通知，请同学查看。",
            "https://xxx.edu.cn/news1",
            u8"教务处",
            "images/1.jpg"
        },


        {
            "2",
            u8"社团招新公告",
            "2026-07-05",
            "正文2",
            u8"校园社团招新活动开始。",
            "https://xxx.edu.cn/news2",
            u8"学生会",
            "images/2.jpg"
        },


        {
            "3",
            u8"竞赛报名通知",
            "2026-07-01",
            "正文3",
            u8"欢迎同学报名参加比赛。",
            "https://xxx.edu.cn/news3",
            u8"学院",
            "images/3.jpg"
        }
        
    };
    
    //生成新闻详情页
    for(auto& news : newsList)
    {
        generateNewsDetailPage(news);
    }

    //生成新闻首页
    generateNewsIndexPage(newsList);

    cout << "News pages generated successfully!" << endl;
    
    return 0;
}
