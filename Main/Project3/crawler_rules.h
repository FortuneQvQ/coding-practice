#pragma once

#include <string>
#include <vector>

struct LinkCandidate
{
    std::string url;
    std::string anchor_text;
    int score = 0;
};

std::string resolve_url(const std::string& base_url, const std::string& relative_url);
std::string normalize_url(const std::string& url);
bool is_same_domain(const std::string& url, const std::string& seed_url);
int score_news_link(const std::string& url, const std::string& anchor_text);
bool is_candidate_news_link(const std::string& url, const std::string& anchor_text);
std::vector<LinkCandidate> select_news_links(
    const std::vector<LinkCandidate>& candidates,
    const std::string& seed_url,
    std::size_t limit);
