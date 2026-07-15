#include "news_classifier.h"

#include "database.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace
{
bool contains_any(const std::string& text, const std::vector<std::string>& words)
{
    return std::any_of(words.begin(), words.end(), [&](const std::string& word) {
        return !word.empty() && text.find(word) != std::string::npos;
    });
}

std::string ascii_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int keyword_score(const std::string& text,
                  const std::vector<std::pair<std::string, int>>& keywords)
{
    int score = 0;
    for (const auto& keyword : keywords)
        if (text.find(keyword.first) != std::string::npos) score += keyword.second;
    return score;
}
}

std::string classify_news_source(const News& news)
{
    const std::string url = ascii_lower(news.url);
    const std::string evidence = news.title + "\n" + news.content + "\n" + news.source;

    // 教务处域名是最可靠的来源证据，优先于正文中的泛化关键词。
    if (url.find("jwc.scu.edu.cn") != std::string::npos ||
        contains_any(evidence, {"教务处", "本科生院"}))
        return "教务处";

    // 学生会和研究生会均归入用户约定的“学生会”。
    if (contains_any(evidence, {"学生会", "研究生会", "学代会", "学生代表大会"}))
        return "学生会";

    // 校团委及明确的社团内容归入“学生社团”。
    if (url.find("tuanwei.scu.edu.cn") != std::string::npos ||
        contains_any(evidence, {"学生社团", "社团联合会", "社团招新", "社联"}))
        return "学生社团";

    // 当前展示端只接受四种来源；其余校级部门和各学院通知统一进入此兜底类。
    return "学院通知";
}

std::string classify_news_topic(const News& news)
{
    // 标题通常比正文更能代表通知主题，因此标题重复一次以提高权重。
    const std::string evidence = news.title + "\n" + news.title + "\n" + news.content;

    static const std::array<std::pair<const char*, std::vector<std::pair<std::string, int>>>, 5>
        rules = {{
            {"考试安排", {{"考试", 5}, {"补考", 6}, {"缓考", 6}, {"考场", 5},
                         {"准考证", 5}, {"四六级", 5}, {"成绩查询", 4}, {"监考", 4}}},
            {"竞赛比赛", {{"竞赛", 5}, {"比赛", 5}, {"大赛", 5}, {"挑战杯", 6},
                         {"参赛", 4}, {"赛题", 4}, {"决赛", 4}, {"获奖", 3}}},
            {"科研创新", {{"科研", 5}, {"科学研究", 5}, {"学术", 3}, {"实验室", 4},
                         {"基金", 4}, {"项目申报", 5}, {"论文", 3}, {"专利", 4},
                         {"创新创业", 5}, {"科技成果", 5}}},
            {"校园活动", {{"校园活动", 6}, {"活动", 3}, {"社团", 5}, {"学生会", 4},
                         {"志愿", 4}, {"招新", 5}, {"文化节", 5}, {"运动会", 5},
                         {"文体", 4}, {"讲座", 2}, {"宣讲会", 3}}},
            {"教学通知", {{"教学", 5}, {"课程", 4}, {"选课", 5}, {"调课", 5},
                         {"停课", 5}, {"培养方案", 5}, {"教材", 4}, {"实习", 3},
                         {"答辩", 3}, {"教务", 4}, {"课堂", 3}}}
        }};

    const char* best_topic = "教学通知";
    int best_score = 0;
    for (const auto& rule : rules)
    {
        const int score = keyword_score(evidence, rule.second);
        if (score > best_score)
        {
            best_score = score;
            best_topic = rule.first;
        }
    }
    return best_topic;
}

void normalize_news_metadata(News& news)
{
    news.source = classify_news_source(news);
    news.topic = classify_news_topic(news);
}

