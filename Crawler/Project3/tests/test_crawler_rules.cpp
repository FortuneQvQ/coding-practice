#include "../crawler_rules.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    assert(resolve_url("https://example.com/news/index.html", "detail/1.html") ==
           "https://example.com/news/detail/1.html");
    assert(resolve_url("https://example.com/news/index.html", "/info/1.html") ==
           "https://example.com/info/1.html");
    assert(normalize_url("https://example.com/news/1.html#top") ==
           "https://example.com/news/1.html");
    assert(normalize_url("https://example.com/news/1.html/") ==
           "https://example.com/news/1.html");

    assert(is_same_domain("https://example.com/info/1.html", "https://example.com"));
    assert(!is_same_domain("https://other.example.com/info/1.html", "https://example.com"));

    assert(is_candidate_news_link("https://example.com/info/123.html", "通知公告"));
    assert(!is_candidate_news_link("https://example.com/search?q=news", "搜索"));
    assert(!is_candidate_news_link("https://example.com/assets/app.js", "新闻"));
    assert(!is_candidate_news_link("https://example.com/engine2/general/more?appId=1", "综合新闻"));

    std::vector<LinkCandidate> candidates = {
        {"https://example.com/search?q=news", "搜索"},
        {"https://example.com/info/123.html", "关于暑期安排的通知"},
        {"https://example.com/article/2026/07/14.html", "校内新闻"}
    };
    auto selected = select_news_links(candidates, "https://example.com", 2);
    assert(selected.size() == 2);
    assert(selected[0].url == "https://example.com/article/2026/07/14.html" ||
           selected[0].url == "https://example.com/info/123.html");

    std::cout << "crawler rules tests passed\n";
    return 0;
}
