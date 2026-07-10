#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
#include<filesystem>

#include "../DataBase/database.h"
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


//生成最新新闻卡片HTML(数量由count决定)
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


//生成所有新闻卡片HTML
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
        newsBlock += "<a class=\"detail-btn\" href=\"detail/";
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
    filesystem::create_directories("output/detail");

    vector<News> newsList =
    {

        {
            1,
            u8"关于暑假安排的通知",
            "2026-07-07",
            u8"为进一步激发学生创新热情，促进校园科技交流，四川大学近日举办2026年校园科技创新成果展示活动。本次活动汇聚了来自多个学院的优秀科研项目，涵盖人工智能、智能制造、生物技术、绿色能源等多个领域。活动现场，学生团队通过实物展示、项目演示和现场讲解等方式，向师生展示了近年来在课程实践、创新创业以及科研探索中的成果。多个项目结合实际校园需求，展现了青年学生将专业知识应用于实际问题解决的能力。学校相关负责人表示，未来将继续完善创新实践平台建设，为学生提供更多参与科研探索和技术创新的机会，鼓励更多同学在实践中提升综合能力，为科技发展贡献青春力量。",
            u8"学校发布暑假安排通知，请同学查看。",
            "https://www.baidu.com",
            u8"教务处",
            "images/1.jpg"
        },


        {
            2,
            u8"社团招新公告",
            "2026-07-05",
            u8"正文2",
            u8"校园社团招新活动开始。",
            "https://xxx.edu.cn/news2",
            u8"学生会",
            "images/2.jpg"
        },


        {
            3,
            u8"竞赛报名通知",
            "2026-07-01",
            u8"正文3",
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
    
    return 0;
}
