#pragma once

#include <string>
#include <vector>

struct LinkCandidate
{
    std::string url;
    std::string anchor_text;
    int score = 0;
};

// 将相对链接转换为绝对 URL，供 HTML 锚点解析使用。
std::string resolve_url(const std::string& base_url, const std::string& relative_url);
// 规范化 URL，供数据库唯一键、失败记录和运行期集合共同去重。
std::string normalize_url(const std::string& url);
// 判断链接和种子是否同域，防止跨站跳转。
bool is_same_domain(const std::string& url, const std::string& seed_url);
// 根据路由、锚文本和编号特征计算新闻详情可信分。
int score_news_link(const std::string& url, const std::string& anchor_text);
// 判断单个链接是否达到详情候选门槛。
bool is_candidate_news_link(const std::string& url, const std::string& anchor_text);
// 对一页链接进行筛选、排序与限量，供分页爬取流程调用。
std::vector<LinkCandidate> select_news_links(
    const std::vector<LinkCandidate>& candidates,
    const std::string& seed_url,
    std::size_t limit);
