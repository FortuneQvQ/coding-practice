#define PUGIXML_HEADER_ONLY
#include "HTMLparsing.h"
using json = nlohmann::json;

struct News
{
    std::string id; // 新闻ID
    std::string title; // 标题
    std::string time; // 发布时间
    std::string content; // 正文
    std::string abstract; // 摘要
    std::string url; // 原链接
    std::string source; // 来源
    std::string image; // 图片URL
    std::string topic; // 主题
};

void parser(const std::string& url, std::string& html) 
{
    SetConsoleOutputCP(65001);
    std::vector<News> newsList;
    News oneNews;
    std::set<std::string> links;

    //读取整个html文件到字符串
    // std::ifstream file("page_0.html", std::ios::binary);
    // std::stringstream buffer;
    // buffer << file.rdbuf();
    // std::string html = buffer.str();

    oneNews.url = url;

    //尝试JSON查找
    if (json_search(html, oneNews, links) == -1)
    {
        std::cout << "查找过程中出现错误" << std::endl;
    }

    // if (write_links_to_file(links, "links.txt"))
    // {
    //     std::cout << "链接写入成功" << std::endl;
    // }
    // else
    // {
    //     std::cout << "链接写入失败" << std::endl;
    // }

    return;
}

std::string get_baseUrl(const std::string& url)
{
    size_t start = url.find("https://", 0);
    size_t end = url.find('/', start + 8);
    if (end != std::string::npos)
    {
        return url.substr(start, end);
    }
    else
    {
        return url;
    }
}

std::string resolve_Url(const std::string& baseUrl, const std::string& relativeUrl)
{
    if (relativeUrl.empty())
        return "";

    // 如果 relativeUrl 已经是绝对路径，则直接返回
    if (relativeUrl.find("http://") == 0 || relativeUrl.find("https://") == 0)
        return relativeUrl;

    // 如果 relativeUrl 以 '/' 开头，则表示从根目录开始
    if (relativeUrl[0] == '/')
    {
        // 提取 baseUrl 的协议和域名部分
        size_t pos = baseUrl.find("://");
        if (pos != std::string::npos)
        {
            pos = baseUrl.find('/', pos + 3); // 查找第一个 '/'，跳过协议部分
            if (pos != std::string::npos)
            {
                return baseUrl.substr(0, pos) + relativeUrl; // 拼接绝对路径
            }
            else
            {
                return baseUrl + relativeUrl; // baseUrl 没有路径部分，直接拼接
            }
        }
        else
        {
            return baseUrl + relativeUrl; // baseUrl 没有协议部分，直接拼接
        }
    }
    else
    {
        // 如果 relativeUrl 是相对路径，则直接拼接到 baseUrl 的末尾
        return baseUrl + relativeUrl;
    }
}

std::set<std::string> extract_all_links(const std::string& html, const std::string& baseUrl)
{
    std::set<std::string> links_set;  // 自动去重

    //将 html 转为 xhtml
    std::string xhtml = tidy_html(html);
    //使用load_string 加载 xHTML 文件的所有内容
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xhtml.c_str(), pugi::parse_full);
    if (!result)
    {
        //std::cout << html << std::endl;
        std::cout << "加载失败: " << result.description() << std::endl;
        std::cout << "解析错误偏移量: " << result.offset << std::endl;
        return links_set;
    }

    //解析网页含有的所有URL
    pugi::xpath_node_set link_nodes = doc.select_nodes("//a/@href | //area/@href | //link/@href");
    for (pugi::xpath_node node : link_nodes)
    {
        std::string raw = node.attribute().value();
        if (raw.empty()) continue;
        // 过滤掉 javascript: 等非跳转链接
        if (raw.find("javascript:") == 0) continue;
        // 转换为绝对 URL
        std::string abs_url = resolve_Url(baseUrl, raw);
        links_set.insert(abs_url);
    }

    return links_set;
}

std::string tidy_html(const std::string& html_input)
{
    TidyDoc tdoc = tidyCreate();
    TidyBuffer output = {0};

    // 设置 tidy 配置选项
    tidyOptSetBool(tdoc, TidyXhtmlOut, yes); // 输出 XHTML
    tidyOptSetBool(tdoc, TidyQuiet, yes);    // 静默模式
    tidyOptSetBool(tdoc, TidyShowWarnings, no); // 不显示警告

    // 解析 HTML 输入
    int rc = tidyParseString(tdoc, html_input.c_str());
    if (rc >= 0)
    {
        // 清理和格式化 HTML
        rc = tidyCleanAndRepair(tdoc);
        if (rc >= 0)
        {
            // 将清理后的 HTML 输出到缓冲区
            rc = tidySaveBuffer(tdoc, &output);
            if (rc >= 0)
            {
                std::string cleaned_html((char*)output.bp, output.size);
                tidyBufFree(&output);
                tidyRelease(tdoc);
                //std::cout << "HTML cleaned successfully." << std::endl;
                return cleaned_html;
            }
        }
    }

    // 如果出现错误，返回原始 HTML
    tidyBufFree(&output);
    tidyRelease(tdoc);
    std::cout << "Error occurred while tidying HTML." << std::endl;
    return html_input;
}

int json_search(std::string& html, News& oneNews, std::set<std::string>& links)
{
    //查找 JSON 数据的起始位置（找 "content":）
    size_t pos = html.find("\"content\":\"");
    if (pos == std::string::npos)
    {
        std::cerr << "未找到 content 字段" << std::endl;
        return -1;
    }

    //从该位置往前找最近的 '{' 作为 JSON 开始
    size_t start = html.rfind('{', pos);
    if (start == std::string::npos)
    {
        std::cerr << "找不到 JSON 开始" << std::endl;
        return -1;
    }

    //从 start 开始匹配括号，找到结束位置
    int brace_count = 0;
    size_t end = start;
    for (; end < html.length(); ++end)
    {
        char c = html[end];
        if (c == '{')
            brace_count++;
        else if (c == '}')
        {
            brace_count--;
            if (brace_count == 0)
            {
                //std::cout << "找到结束位置" << std::endl;
                break;
            }
        }
    }
    if (brace_count != 0)
    {
        std::cerr << "JSON 括号不匹配" << std::endl;
        return -1;
    }
    std::string json_str = html.substr(start, end - start + 1);
    //std::cout << "共" << end - start + 1 << "个字符" << std::endl;

    //解析 JSON（自动处理 Unicode 转义）
    try
    {
        json j = json::parse(json_str);
        // 使用 load_string 加载 HTML 文件的 JSON 内容
        pugi::xml_document doc_main;
        pugi::xml_parse_result result = doc_main.load_string(j.value("content", "").c_str(), pugi::parse_full);
        // 检查是否加载成功
        if (!result)
        {
            //std::cout << html << std::endl;
            std::cout << "加载失败: " << result.description() << std::endl;
            std::cout << "解析错误偏移量: " << result.offset << std::endl;
            return -1;
        }
        oneNews.title = j.value("title", "");
        oneNews.time = j.value("publishTime", "");
        //oneNews.content = j.value("content", "");
        pugi::xpath_node_set p_nodes = doc_main.select_nodes("//p");
        for (auto& p_node : p_nodes)
        {
            if (p_nodes.empty())
            {
                std::cout << "未找到正文节点" << std::endl;
                break;
            }
            //查询当前节点(.)的所有文本
            pugi::xpath_query query("string(.)");
            std::string full_text = query.evaluate_string(p_node);
            oneNews.content += full_text + "\n"; // 将每个<p>的文本内容拼接到正文中
        }

        //std::cout << "标题: " << oneNews.title << std::endl;
        //std::cout << "发布时间: " << oneNews.time << std::endl;
        //std::cout << "正文：" << oneNews.content << std::endl;
        //content 可能包含 HTML 标签，可以选择用 pugixml 再解析其中的文本
        //或者直接用字符串替换去除标签，但一般保留 HTML 格式给存储模块

        std::string baseUrl = get_baseUrl(oneNews.url);
        links = extract_all_links(html, baseUrl);
        // for(auto it = links.begin(); it != links.end(); ++it)
        // {
        //     std::cout << "找到链接：" << *it << std::endl;
        // }
    }
    catch (const std::exception& e)
    {
        std::cerr << "JSON 解析失败: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}

bool write_links_to_file(const std::set<std::string>& links, const std::string& filepath)
{
    std::ofstream outfile(filepath);
    if (!outfile.is_open())
    {
        std::cerr << "无法打开文件: " << filepath << std::endl;
        return false;
    }

    for (const auto& link : links)
    {
        outfile << link << "\n";
    }

    outfile.close();
    return true;
}