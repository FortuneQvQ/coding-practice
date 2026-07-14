#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "pugixml.hpp"
#include <iomanip>
#include <windows.h>
#include <tidy.h>
#include <tidybuffio.h>
#include "nlohmann/json.hpp"
#include <set>
#include "database.h"

// 辅助函数：根据网页源URL获取基准URL
std::string get_baseUrl(const std::string& url);
// 辅助函数：将相对路径转换为绝对路径
std::string resolve_Url(const std::string& baseUrl, const std::string& relativeUrl);
// 辅助函数，存储得到的URL
std::set<std::string> extract_all_links(const std::string& html_source, const std::string& base_url);
// 辅助函数：使用 tidy 清理 HTML
std::string tidy_html(const std::string& html_input);
// 辅助函数：通过 JSON 查找
int json_search(std::string& html, News& oneNews, std::set<std::string>& links);
// 辅助函数：将解析到的链接写入文件
bool write_links_to_file(const std::set<std::string>& links, const std::string& filepath);