

#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>     // sleep 用
#include <chrono>     // 时间单位用
#include <curl/curl.h>
#include <ctime>
#include "json.hpp"
using namespace std;
using json = nlohmann::json;

void log_write(const string& level, const string& msg);
vector<string> load_config(const string& config_path) {
    vector<string> all_urls;
    ifstream f(config_path);
    if (!f) {
        log_write("ERROR", "无法打开配置文件: " + config_path);
        return all_urls;
    }
    json config = json::parse(f);
    for (auto& source : config["sources"]) {
        for (auto& u : source["url_list"]) {
            all_urls.push_back(u);
        }
    }
    return all_urls;
}
//日志文件函数
void log_write(const string& level, const string& msg)
{
    time_t now = time(NULL);
    char buf_now[25];
    strftime(buf_now, sizeof(buf_now), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char buf_day[20];
    strftime(buf_day, sizeof(buf_day), "%Y-%m-%d", localtime(&now));
    string filename =  string(buf_day) + ".txt";
    ofstream log(filename, ios::app | ios::binary);
    log << "[" << buf_now << "]" << "[" << level << "]" << msg << endl;
}




// 回调函数：curl 每收到一段数据就调一次，把数据写进文件。
// 这跟你原来的 write_callback 一模一样，只是不再往控制台打印了。
static size_t write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    ofstream* file = static_cast<ofstream*>(userdata);
    file->write(ptr, realsize);
    return realsize;   // 必须返回收到的字节数，否则 curl 认为出错
}

// 抓一个网址，存成一个文件。成功返回 true，失败返回 false。
bool fetch_and_save(const string& url, const string& filename) {
    CURL* curl = curl_easy_init();
    log_write("INFO", "开始抓取：" + url);
    if (!curl) {
        log_write("ERROR", "curl初始化失败");
        cout << "curl 初始化失败\n";
        return false;
    }

    // 用二进制方式新建/清空文件（trunc 表示每次重写）
    ofstream file(filename, ios::binary | ios::trunc);
    if (!file) {
        cout << "打开文件失败: " << filename << "\n";
        log_write("ERROR", "打开文件：" + filename + "失败");
        curl_easy_cleanup(curl);   // 清理 curl句柄
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);   // 跟随网页跳转
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);         // 总超时 15 秒
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // 连接超时 5 秒

    CURLcode code = curl_easy_perform(curl);   // 真正开始下载
    file.close();                              // 下完关文件

    if (code != CURLE_OK) {       // 网络层面失败（超时、断网等）
        string err = curl_easy_strerror(code);
        log_write("ERROR", "抓取失败，错误" + err);
        cout << "抓取失败 " << url << " : " << curl_easy_strerror(code) << "\n";
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);  // 取 HTTP 状态码
    curl_easy_cleanup(curl);

    if (http_code != 200) { // 服务器返回 404/500 等
        string warn = "HTTP " + to_string(http_code) + url;
        log_write("WARN", warn);
        cout << "抓取失败 " << url << " HTTP " << http_code << "\n";
        return false;
    }
    log_write("INFO", "抓取成功" + url);
    cout << "抓取成功 " << url << " -> " << filename << "\n";
    return true;
}

int main() {
    system("chcp 65001");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    //记录日志
    log_write("INFO", "爬虫启动");


    // 先手写几个网址测试（以后这一步换成从 config.json 读进来）
    vector<string> urls = load_config("config.json");
    if (urls.empty()) {
        log_write("ERROR", "config.json 中没有网址，程序退出");
        return 1;
    }
    //日志记录
    log_write("INFO", "共加载" + to_string(urls.size()) + "个网站");

    int success = 0;
    int fail = 0;

    // 顺便建一个清单文件，告诉解析模块每个文件对应哪个网址
    ofstream manifest("manifest.txt", ios::trunc);

    for (size_t i = 0; i < urls.size(); i++) {
        string filename = "page_" + to_string(i) + ".html";   // page_0.html, page_1.html ...
        bool ok = fetch_and_save(urls[i], filename);

        if (ok) {
            // 每行格式：文件名 | 原始网址
            manifest << filename << " | " << urls[i] << "\n";
            success++;
        }
        else {
            fail++;
        }

        this_thread::sleep_for(chrono::seconds(1));   // 限流：每次间隔 1秒（文档 RULE-002）
    }
    //日志记录
    log_write("INFO", "爬虫结束，成功" + to_string(success) + "条,失败" +to_string(fail)+ "条");
    manifest.close();
    curl_global_cleanup();
    return 0;
}