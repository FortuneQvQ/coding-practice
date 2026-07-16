#include "crawler_rules.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

namespace
{
struct UrlParts
{
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
    std::string query;
};

// 去除文本首尾空白；URL、锚文本和查询参数进入评分逻辑前都先用它规范输入。
std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// 将 ASCII 字符转为小写，使域名、扩展名和路由关键字比较不受大小写影响。
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// 把 URL 拆分为协议、主机、端口、路径、查询串和片段；
// normalize_url、resolve_url 与同域判断共享该结构，避免各自重复解析。
bool parse_url(const std::string& input, UrlParts& out)
{
    const std::string url = trim(input);
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;

    out.scheme = lower(url.substr(0, scheme_end));
    if (out.scheme != "http" && out.scheme != "https") return false;

    const auto authority_start = scheme_end + 3;
    const auto authority_end = url.find_first_of("/?", authority_start);
    const std::string authority = url.substr(
        authority_start,
        authority_end == std::string::npos ? std::string::npos : authority_end - authority_start);
    if (authority.empty()) return false;

    const auto at = authority.rfind('@');
    const std::string host_port = authority.substr(at == std::string::npos ? 0 : at + 1);
    const auto colon = host_port.rfind(':');
    if (colon != std::string::npos && host_port.find(']') == std::string::npos)
    {
        out.host = lower(host_port.substr(0, colon));
        out.port = host_port.substr(colon + 1);
    }
    else
    {
        out.host = lower(host_port);
    }
    if (out.host.empty()) return false;

    const std::string rest = authority_end == std::string::npos ? "" : url.substr(authority_end);
    const auto fragment_start = rest.find('#');
    const std::string without_fragment = rest.substr(0, fragment_start);
    const auto query_start = without_fragment.find('?');
    out.path = query_start == std::string::npos ? without_fragment : without_fragment.substr(0, query_start);
    out.query = query_start == std::string::npos ? "" : without_fragment.substr(query_start + 1);
    if (out.path.empty()) out.path = "/";
    return true;
}

// 消解路径中的“.”和“..”片段，防止相对链接解析后形成语义相同但字符串不同的 URL。
std::string remove_dot_segments(const std::string& path)
{
    const bool trailing_slash = path.size() > 1 && path.back() == '/';
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const auto end = path.find('/', start);
        const std::string segment = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == ".")
        {
            // 忽略空片段和当前目录片段，保留真正影响路径层级的部分。
        }
        else if (segment == "..")
        {
            if (!segments.empty()) segments.pop_back();
        }
        else
        {
            segments.push_back(segment);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    std::string result = "/";
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        if (i != 0) result += '/';
        result += segments[i];
    }
    if (trailing_slash && result.size() > 1) result += '/';
    return result;
}

// 组合主机与可选端口，供 resolve_url 重建绝对 URL。
std::string authority(const UrlParts& parts)
{
    const bool default_port = (parts.scheme == "http" && parts.port == "80") ||
                              (parts.scheme == "https" && parts.port == "443");
    return parts.host + (parts.port.empty() || default_port ? "" : ":" + parts.port);
}

// 判断字符串是否包含任一关键字，为资源过滤和新闻链接评分提供通用匹配能力。
bool contains_any(const std::string& value, const std::vector<std::string>& words)
{
    for (const auto& word : words)
        if (value.find(word) != std::string::npos) return true;
    return false;
}

// 识别下一页、首页和页码等分页锚文本，避免导航链接进入新闻候选集合。
bool is_pagination_anchor(const std::string& text)
{
    const std::string value = trim(text);
    if (value.empty()) return false;
    if (contains_any(lower(value), {"next", "prev", "first", "last"})) return true;
    if (value == "首页" || value == "尾页" || value == "上页" || value == "下页" ||
        value == "上一页" || value == "下一页" || value == ">" || value == "<" ||
        value == "›" || value == "‹" || value == "»" || value == "«")
        return true;
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// 统计字符串中的数字数量，较长文章编号可作为详情链接评分的一项证据。
int digit_count(const std::string& value)
{
    return static_cast<int>(std::count_if(value.begin(), value.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; }));
}

// 提取 URL 路径中最长的数字提示，用于在候选过多时优先较新的文章编号。
unsigned long long numeric_path_hint(const std::string& url)
{
    UrlParts parts;
    if (!parse_url(url, parts)) return 0;
    unsigned long long best = 0;
    unsigned long long current = 0;
    for (unsigned char c : parts.path)
    {
        if (std::isdigit(c))
        {
            if (current <= 1000000000000000000ULL)
                current = current * 10 + static_cast<unsigned long long>(c - '0');
        }
        else
        {
            best = (std::max)(best, current);
            current = 0;
        }
    }
    return (std::max)(best, current);
}

// 判断路径是否指向图片、脚本、压缩包等资源，防止其被当作新闻详情请求。
bool is_resource_path(const std::string& path)
{
    static const std::vector<std::string> extensions = {
        ".css", ".js", ".png", ".jpg", ".jpeg", ".gif", ".ico", ".svg",
        ".woff", ".woff2", ".ttf", ".pdf", ".doc", ".docx", ".xls", ".xlsx",
        ".zip", ".rar", ".mp4", ".mp3", ".webp"
    };
    const std::string path_lower = lower(path);
    for (const auto& extension : extensions)
    {
        if (path_lower.size() >= extension.size() &&
            path_lower.compare(path_lower.size() - extension.size(), extension.size(), extension) == 0)
            return true;
    }
    return false;
}
}

// 生成供数据库和集合去重使用的规范 URL，统一协议、主机、路径与片段处理。
std::string normalize_url(const std::string& input)
{
    UrlParts parts;
    if (!parse_url(input, parts)) return "";
    parts.path = remove_dot_segments(parts.path);
    if (parts.path.size() > 1 && parts.path.back() == '/') parts.path.pop_back();
    return parts.scheme + "://" + authority(parts) + parts.path +
           (parts.query.empty() ? "" : "?" + parts.query);
}

// 按基准页面解析相对链接并返回规范绝对 URL，HTML 链接提取器依赖它完成跳转。
std::string resolve_url(const std::string& base_url, const std::string& relative_url)
{
    const std::string raw = trim(relative_url);
    if (raw.empty()) return "";

    UrlParts base;
    if (!parse_url(base_url, base)) return "";

    if (raw.rfind("//", 0) == 0)
        return normalize_url(base.scheme + ":" + raw);
    if (raw.rfind("http://", 0) == 0 || raw.rfind("https://", 0) == 0)
        return normalize_url(raw);
    if (raw.rfind("javascript:", 0) == 0 || raw.rfind("mailto:", 0) == 0 ||
        raw.rfind("tel:", 0) == 0 || raw.rfind("data:", 0) == 0)
        return "";

    const auto fragment = raw.find('#');
    const std::string without_fragment = raw.substr(0, fragment);
    if (without_fragment.empty())
        return normalize_url(base_url);

    const auto query = without_fragment.find('?');
    const std::string path_part = query == std::string::npos ? without_fragment : without_fragment.substr(0, query);
    // 非空相对路径会替换基准查询串；只有空路径引用才允许继承或单独替换当前查询串。
    const std::string query_part = query == std::string::npos
        ? (path_part.empty() ? base.query : "")
        : without_fragment.substr(query + 1);

    std::string path;
    if (path_part.empty())
        path = base.path;
    else if (path_part.front() == '/')
        path = path_part;
    else
    {
        const auto slash = base.path.rfind('/');
        path = (slash == std::string::npos ? "/" : base.path.substr(0, slash + 1)) + path_part;
    }

    return normalize_url(base.scheme + "://" + authority(base) + remove_dot_segments(path) +
                         (query_part.empty() ? "" : "?" + query_part));
}

// 判断候选链接是否与种子同域，阻止爬虫跳向站外广告和无关页面。
bool is_same_domain(const std::string& url, const std::string& seed_url)
{
    UrlParts left;
    UrlParts right;
    return parse_url(url, left) && parse_url(seed_url, right) && left.host == right.host;
}

// 综合路由、文章编号、锚文本和资源类型为链接打分，供候选筛选函数取舍。
int score_news_link(const std::string& url, const std::string& anchor_text)
{
    UrlParts parts;
    if (!parse_url(url, parts)) return -100;
    const std::string path = lower(parts.path);
    const std::string query = lower(parts.query);
    const std::string route = path + (query.empty() ? "" : "?" + query);
    const std::string anchor = trim(anchor_text);
    int score = 0;

    if (contains_any(route, {"/detail", "/article", "/content", "/info/", "/show", "/news/"})) score += 5;
    // 部分已配置站点把文章编号放在查询串中，这些稳定模式可提高详情可信度。
    if (contains_any(route, {"/articles/", "newsdetail", "?notice/_/", "dispatch=showpronews", "news_uuid="})) score += 7;
    if (contains_any(route, {"contentdetail", "list", "search", "login", "logout", "register", "tag", "category", "download", "general/more", "/engine2/m/"})) score -= 8;
    // 明确的列表或导航路由即使含有 news 和数字，也不能仅凭这些特征胜出。
    if (contains_any(path, {"/columns/", "/news/main/", "/hospitals/main/"})) score -= 20;
    if (contains_any(query, {"page=", "pagenum=", "pageindex=", "offset=", "dispatch=list"})) score -= 4;
    if (path.find(".html") != std::string::npos || path.find(".htm") != std::string::npos) score += 2;
    // 大多数正文 URL 含较长文章编号；单个数字常见于栏目名（如 style1），不能作为正文依据。
    if (digit_count(path) >= 4) score += 2;
    if (anchor.size() >= 6 && anchor.size() <= 300) score += 2;
    if (contains_any(anchor, {"概况", "简介", "大事记", "组织机构", "领导分工", "历史沿革"})) score -= 8;
    if (contains_any(anchor, {"新闻", "通知", "公告", "公示", "动态", "简报", "活动", "讲座", "会议"})) score += 2;
    if (is_resource_path(parts.path)) score -= 20;
    return score;
}

// 将评分门槛封装为布尔判断，供外部代码快速检查单个链接是否像新闻详情。
bool is_candidate_news_link(const std::string& url, const std::string& anchor_text)
{
    UrlParts parts;
    if (!parse_url(url, parts) || is_resource_path(parts.path)) return false;
    if (is_pagination_anchor(anchor_text)) return false;
    const std::string normalized_lower = lower(url);
    // 哲学系列表混有教师和社交媒体导航，真实通知都位于 1016 信息目录，因此单独约束。
    if (normalized_lower.find("zxx.scu.edu.cn/") != std::string::npos &&
        normalized_lower.find("/info/1016/") == std::string::npos)
        return false;
    if (contains_any(lower(parts.path), {"contentdetail", "/login", "/logout", "/register", "/search", "/download", "/general/more", "/engine2/m/", "/columns/", "/news/main/", "/hospitals/main/"})) return false;
    return score_news_link(url, anchor_text) >= 5;
}

// 对一页原始链接执行同域过滤、栏目约束、评分、排序和去重；
// 分页流程只访问这里返回的高可信详情，避免垃圾链接消耗种子请求额度。
std::vector<LinkCandidate> select_news_links(
    const std::vector<LinkCandidate>& candidates,
    const std::string& seed_url,
    std::size_t limit)
{
    std::map<std::string, LinkCandidate> unique;
    std::smatch seed_type_match;
    const std::regex type_pattern(R"((?:^|[?&])ntype_id=([^&]+))", std::regex::icase);
    const bool seed_has_type = std::regex_search(seed_url, seed_type_match, type_pattern);
    for (const auto& candidate : candidates)
    {
        const std::string url = normalize_url(candidate.url);
        if (url.empty() || !is_same_domain(url, seed_url) ||
            !is_candidate_news_link(url, candidate.anchor_text))
            continue;

        // 某些首页混合多个栏目；种子已指定栏目时只保留对应目录，避免无关卡片消耗配额。
        if (seed_has_type)
        {
            std::smatch candidate_type_match;
            if (std::regex_search(url, candidate_type_match, type_pattern) &&
                lower(candidate_type_match[1].str()) != lower(seed_type_match[1].str()))
                continue;
        }

        LinkCandidate selected = candidate;
        selected.url = url;
        selected.score = score_news_link(url, selected.anchor_text);
        auto it = unique.find(url);
        if (it == unique.end() || selected.score > it->second.score)
            unique[url] = selected;
    }

    std::vector<LinkCandidate> result;
    for (const auto& item : unique) result.push_back(item.second);
    std::sort(result.begin(), result.end(), [](const LinkCandidate& left, const LinkCandidate& right) {
        if (left.score != right.score) return left.score > right.score;
        const auto left_hint = numeric_path_hint(left.url);
        const auto right_hint = numeric_path_hint(right.url);
        if (left_hint != right_hint) return left_hint > right_hint;
        return left.url < right.url;
    });
    if (result.size() > limit) result.resize(limit);
    return result;
}
