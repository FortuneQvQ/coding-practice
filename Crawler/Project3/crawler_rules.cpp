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

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

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
            // Skip empty and current-directory segments.
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

std::string authority(const UrlParts& parts)
{
    const bool default_port = (parts.scheme == "http" && parts.port == "80") ||
                              (parts.scheme == "https" && parts.port == "443");
    return parts.host + (parts.port.empty() || default_port ? "" : ":" + parts.port);
}

bool contains_any(const std::string& value, const std::vector<std::string>& words)
{
    for (const auto& word : words)
        if (value.find(word) != std::string::npos) return true;
    return false;
}

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

int digit_count(const std::string& value)
{
    return static_cast<int>(std::count_if(value.begin(), value.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; }));
}

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

std::string normalize_url(const std::string& input)
{
    UrlParts parts;
    if (!parse_url(input, parts)) return "";
    parts.path = remove_dot_segments(parts.path);
    if (parts.path.size() > 1 && parts.path.back() == '/') parts.path.pop_back();
    return parts.scheme + "://" + authority(parts) + parts.path +
           (parts.query.empty() ? "" : "?" + parts.query);
}

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
    // A non-empty relative path replaces the base query. Only an empty path ("?x=1" or an
    // empty reference) may inherit/replace the query of the current document.
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

bool is_same_domain(const std::string& url, const std::string& seed_url)
{
    UrlParts left;
    UrlParts right;
    return parse_url(url, left) && parse_url(seed_url, right) && left.host == right.host;
}

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
    // These patterns are used by several configured sites whose article id is in the query string.
    if (contains_any(route, {"/articles/", "newsdetail", "?notice/_/", "dispatch=showpronews", "news_uuid="})) score += 7;
    if (contains_any(route, {"contentdetail", "list", "search", "login", "logout", "register", "tag", "category", "download", "general/more", "/engine2/m/"})) score -= 8;
    // Explicit list/navigation routes must never win merely because they also contain /news/ and digits.
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

bool is_candidate_news_link(const std::string& url, const std::string& anchor_text)
{
    UrlParts parts;
    if (!parse_url(url, parts) || is_resource_path(parts.path)) return false;
    if (is_pagination_anchor(anchor_text)) return false;
    const std::string normalized_lower = lower(url);
    // The Philosophy notice list shares global navigation links with staff and social-media
    // pages.  Actual notices for this column are all published under the 1016 info tree.
    if (normalized_lower.find("zxx.scu.edu.cn/") != std::string::npos &&
        normalized_lower.find("/info/1016/") == std::string::npos)
        return false;
    if (contains_any(lower(parts.path), {"contentdetail", "/login", "/logout", "/register", "/search", "/download", "/general/more", "/engine2/m/", "/columns/", "/news/main/", "/hospitals/main/"})) return false;
    return score_news_link(url, anchor_text) >= 5;
}

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

        // Some headers mix articles from several categories.  If the seed selects a category,
        // keep candidates from that category so unrelated cards do not consume its quota.
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
