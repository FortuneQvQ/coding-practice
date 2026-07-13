#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include "curl/curl.h"
#include "json.hpp"

using json = nlohmann::json;

// ===== 爬虫模块 =====

// 抓取网页到内存字符串（主接口）
std::string fetch_page(const std::string& url);

// 抓取网页到磁盘文件（调试用）
bool fetch_and_save(const std::string& url, const std::string& filename);

// 从 URL 提取纯域名
std::string extract_domain(const std::string& url);

// ===== 配置加载 =====

// 从 config.json 加载种子 URL 列表
std::vector<std::string> load_config(const std::string& config_path);

// ===== 日志 =====

// 写日志到 当天日期.txt
void log_write(const std::string& level, const std::string& msg);
