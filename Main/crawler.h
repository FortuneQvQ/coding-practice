#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include "curl/curl.h"
#include "json.hpp"
#include "crawler_rules.h"

using json = nlohmann::json;

// 抓取网页到内存字符串，是列表页和详情页共用的 GET 请求入口。
std::string fetch_page(const std::string& url);
// 提交动态列表所需的表单参数，并把 JSON 响应返回给 Engine2 解析流程。
std::string fetch_form(const std::string& url, const std::string& form_data);

// 抓取网页到磁盘文件，仅用于保存原始响应进行调试。
bool fetch_and_save(const std::string& url, const std::string& filename);

// 从 URL 提取纯域名，供日志和来源提示使用。
std::string extract_domain(const std::string& url);

// 从 config.json 加载种子 URL 列表，供 main 逐站调度。
std::vector<std::string> load_config(const std::string& config_path);

// 将一条带级别的信息写入当天日期命名的日志文件。
void log_write(const std::string& level, const std::string& msg);
