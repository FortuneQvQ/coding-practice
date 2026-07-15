#pragma execution_character_set("utf-8")
#define _HAS_STD_BYTE 0
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
#include "json.hpp"
#include <set>
#include "database.h"
#include "crawler_rules.h"

// 辅助函数：根据网页源URL获取基准URL
std::string get_baseUrl(const std::string& url);
// 辅助函数：将相对路径转换为绝对路径
std::string resolve_Url(const std::string& baseUrl, const std::string& relativeUrl);
// 辅助函数，存储得到的URL
std::set<std::string> extract_all_links(const std::string& html_source, const std::string& base_url);
std::vector<LinkCandidate> extract_link_candidates(const std::string& html_source,
                                                   const std::string& page_url);
// 辅助函数：使用 tidy 清理 HTML
std::string tidy_html(const std::string& html_input);
// 辅助函数：通过 JSON 查找
int json_search(std::string& html, News& oneNews, std::set<std::string>& links);
bool parse_embedded_news_json(const std::string& html, News& out_news);
std::vector<std::string> extract_json_api_candidates(const std::string& html,
                                                     const std::string& page_url);

struct Engine2ListRequest
{
    std::string endpoint;
    std::string form_data;
    std::string app_id;
    std::string engine_instance_id;
    std::string type_id;
};

bool extract_engine2_list_request(const std::string& html, const std::string& page_url,
                                  Engine2ListRequest& out_request,
                                  int page_num = 1, int page_size = 20);
std::vector<LinkCandidate> extract_engine2_detail_links(const std::string& json_response,
                                                        const std::string& page_url,
                                                        const Engine2ListRequest& request,
                                                        std::size_t limit,
                                                        int* total_pages = nullptr);

// 从普通 HTML 列表中只选择“下一页”，避免把首页、尾页或页码导航当成新闻。
std::string extract_next_page_link(const std::string& html, const std::string& page_url);
// 通用 HTML 解析器：不依赖 engine2 的 JSON，直接从 HTML 标签提取新闻
// @return true 表示提取到有效内容
bool generic_parse(const std::string& html, const std::string& page_url,
                   News& out_news, std::set<std::string>& out_links);

// 判断 URL 是否像新闻详情页（有具体文章内容）
bool is_news_detail_url(const std::string& url);

// 判断 URL 是否像列表页/目录页（只有标题链接，没有正文）
bool is_list_page_url(const std::string& url);
