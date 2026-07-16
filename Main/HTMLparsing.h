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

// 从页面 URL 提取目录级基准地址；保留该兼容接口，供旧解析调用与相对链接补全共同使用。
std::string get_baseUrl(const std::string& url);
// 将相对链接转换为规范绝对 URL；内部复用 crawler_rules 的统一规则，避免解析模块和去重模块结果不一致。
std::string resolve_Url(const std::string& baseUrl, const std::string& relativeUrl);
// 提取页面中的全部链接并以集合去重；主要为旧版兼容流程提供结果。
std::set<std::string> extract_all_links(const std::string& html_source, const std::string& base_url);
// 提取带锚文本的链接候选；主流程随后根据 crawler_rules 的评分筛选真实新闻详情链接。
std::vector<LinkCandidate> extract_link_candidates(const std::string& html_source,
                                                   const std::string& page_url);
// 在原生异常保护下修复畸形 HTML，供通用详情 DOM 解析使用；列表链接发现不依赖此函数。
std::string tidy_html(const std::string& html_input);
// 兼容旧调用：优先解析结构化新闻 JSON，否则返回页面中筛选后的链接集合。
int json_search(std::string& html, News& oneNews, std::set<std::string>& links);
// 从页面内嵌的结构化脚本中恢复新闻字段，作为动态渲染页面无需浏览器执行脚本时的解析入口。
bool parse_embedded_news_json(const std::string& html, News& out_news);
// 从脚本中发现可能的 JSON 接口地址，为动态页面接口抓取提供后备候选。
std::vector<std::string> extract_json_api_candidates(const std::string& html,
                                                     const std::string& page_url);

// 描述 Engine2 动态列表的一次请求；解析模块负责生成参数，crawler 模块负责实际发送表单。
struct Engine2ListRequest
{
    std::string endpoint;
    std::string form_data;
    std::string app_id;
    std::string engine_instance_id;
    std::string type_id;
};

// 从 Engine2 种子页提取接口和应用参数，并按照指定页码、页容量构造表单数据。
bool extract_engine2_list_request(const std::string& html, const std::string& page_url,
                                  Engine2ListRequest& out_request,
                                  int page_num = 1, int page_size = 20);
// 从 Engine2 接口响应提取详情候选及总页数；主流程据此逐条抓详情并保存下一次续爬页码。
std::vector<LinkCandidate> extract_engine2_detail_links(const std::string& json_response,
                                                        const std::string& page_url,
                                                        const Engine2ListRequest& request,
                                                        std::size_t limit,
                                                        int* total_pages = nullptr);

// 从普通 HTML 列表中只选择“下一页”，避免把首页、尾页或页码导航当成新闻。
std::string extract_next_page_link(const std::string& html, const std::string& page_url);
// 通用详情解析器：结合已知站点模板与受保护的 DOM 兜底提取新闻；返回 true 表示得到有效候选内容。
bool generic_parse(const std::string& html, const std::string& page_url,
                   News& out_news, std::set<std::string>& out_links);

// 判断 URL 是否具备新闻详情特征；外部兼容代码可用它预先排除明显目录链接。
bool is_news_detail_url(const std::string& url);

// 判断 URL 是否具备列表或目录特征；与详情判断配合，避免把栏目标题误存为正文。
bool is_list_page_url(const std::string& url);
