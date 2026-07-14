#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
#include<filesystem>

#include "database.h"
#include "other/category.h"

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


//生成最新新闻卡片HTML(数量由count决定）
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
        newsBlock += ".html?from=index\">";
        newsBlock += u8"查看详情";
        newsBlock += "</a>";
        newsBlock += "</div>";

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


//生成分类详细页
void generateCategoryResultPage(){

    //读取模板
    string html = readTemplate("templates/category_result.html");
    if (html.empty()) {
        cerr << "category_result.html is empty. Cannot generate category page." << endl;
        return;
    }

    //生成HTML文件
    ofstream outFile("output/category_result.html", ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: output/category_result.html" << endl;
    }
    
}


//生成新闻JSON文件
void generateNewJson(const vector<News>& newslist){

    ofstream file("output/json/news.json", ios::binary);
    if(!file.is_open()){
        cout<<"Failed to create news.json"<<endl;
        return;
    }
    file<<"[\n";
    for(int i = 0; i < newslist.size(); i++){
        const News& news = newslist[i];
        file<<"{\n";
        file<<"\"id\":\""<<news.id<<"\",\n";
        file<<"\"title\":\""<<news.title<<"\",\n";
        file<<"\"time\":\""<<news.time<<"\",\n";
        file<<"\"content\":\""<<news.content<<"\",\n";
        file<<"\"abstract\":\""<<news.abstract<<"\",\n";
        file<<"\"url\":\""<<news.url<<"\",\n";
        file<<"\"source\":\""<<news.source<<"\",\n";
        file<<"\"image\":\""<<news.image<<"\",\n";
        file<<"\"topic\":\""<<news.topic<<"\"\n";
        file<<"}";
        if(i < newslist.size() - 1){
            file<<",";
        }
        file<<"\n";
    }
    file<<"]";
    file.close();

}


//生成搜索页
void generateSearchPage(){

    //读取模板
    string html = readTemplate("templates/search.html");
    if (html.empty()) {
        cerr << "search.html is empty. Cannot generate search page." << endl;
        return;
    }

    //生成HTML文件
    ofstream outFile("output/search.html", ios::binary);
    if (outFile.is_open()) {
        outFile << html;
        outFile.close();
    } else {
        cerr << "Failed to open output file: output/search.html" << endl;
    }

}

void generateNewsWebsite(){
	
    //生成新闻详情页
    vector<News> newsList = database::getAllNews();
    for(auto& news : newsList)
    {
        generateNewsDetailPage(news);
    }
    cout << "News detail pages generated successfully!" << endl;

    //生成新闻JSON文件
    generateNewJson(newsList);
    cout << "News JSON file generated successfully!" << endl;

    //生成新闻首页
    generateNewsIndexPage(newsList);
    cout << "News index page generated successfully!" << endl;

    //生成分类首页
    generateCategoryIndexPage();
    cout << "Category index page generated successfully!" << endl;

    //生成分类详情页
    generateCategoryResultPage();
    cout << "Category result page generated successfully!" << endl;

    //生成搜索页
    generateSearchPage();
    cout << "Search page generated successfully!" << endl;

}

void generateNewsWebsite(const string& dbPath)
{
    database::openDb();
    generateNewsWebsite();
    database::closeDb();
}

#ifdef WEBSITE_GENERATOR_STANDALONE
int main()
{
    generateNewsWebsite("../DataBase/data/news.db");
    return 0;
}
#endif
