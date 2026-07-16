#include "crawler.h"

// 将运行信息追加到按日期命名的日志文件中；网络请求函数统一调用它，
// 便于在控制台关闭后追查具体 URL、HTTP 状态和失败原因。
void log_write(const std::string& level, const std::string& msg)
{
    time_t now = time(nullptr);
    char buf_now[25];
    strftime(buf_now, sizeof(buf_now), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char buf_day[20];
    strftime(buf_day, sizeof(buf_day), "%Y-%m-%d", localtime(&now));
    std::string filename = std::string(buf_day) + ".txt";
    std::ofstream log(filename, std::ios::app | std::ios::binary);
    log << "[" << buf_now << "][" << level << "] " << msg << std::endl;
}

// 接收 libcurl 返回的数据块并拼接到字符串；fetch_page 和 fetch_form
// 通过该回调把网络响应交给后续 HTML 或 JSON 解析器。
static size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    std::string* str = static_cast<std::string*>(userdata);
    str->append(ptr, realsize);
    return realsize;
}

// 创建可共享 Cookie 的句柄，保留给需要连续会话的站点使用；
// 当前通用请求为稳定性保持隔离，因此不会默认挂载该句柄。
static CURLSH* shared_cookies()
{
    static CURLSH* share = []() {
        CURLSH* value = curl_share_init();
        if (value) curl_share_setopt(value, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
        return value;
    }();
    return share;
}

// 设置 GET 与 POST 请求共同需要的浏览器标识、压缩、重定向和超时参数，
// 让 fetch_page 与 fetch_form 采用一致的网络行为和安全边界。
static void configure_common_request(CURL* curl)
{
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    // 不在此处挂载共享 Cookie；Windows 调试版 libcurl 跨多个站点长期共享时曾不稳定，
    // 每个请求保持隔离可避免一个站点的会话状态影响其他种子。
}

// 以 GET 方式抓取一个页面并返回内存字符串；它负责 HTTP 状态校验和日志记录，
// 主流程只在返回非空时把响应交给列表或详情解析器。
std::string fetch_page(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        log_write("错误", "curl 初始化失败");
        return "";
    }

    std::string html;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    configure_common_request(curl);

    CURLcode code = curl_easy_perform(curl);

    if (code != CURLE_OK)
    {
        std::string err = curl_easy_strerror(code);
        log_write("错误", "抓取失败 [" + url + "]: " + err);
        std::cerr << "抓取失败 " << url << " : " << err << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200)
    {
        log_write("警告", "HTTP 状态码=" + std::to_string(http_code) + "，地址=" + url);
        std::cerr << "HTTP 请求失败，状态码=" << http_code << "，地址=" << url << std::endl;
        return "";
    }

    log_write("信息", "抓取成功 [" + url + "] " + std::to_string(html.size()) + " 字节");
    return html;
}

// 以表单 POST 方式调用动态列表接口；主要与 Engine2 参数提取函数配合，
// 将分页接口的 JSON 响应返回给详情链接解析器。
std::string fetch_form(const std::string& url, const std::string& form_data)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form_data.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    configure_common_request(curl);

    const CURLcode code = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK || http_code != 200)
    {
        const std::string reason = code != CURLE_OK ? curl_easy_strerror(code) : "HTTP " + std::to_string(http_code);
        log_write("警告", "POST 抓取失败 [" + url + "]: " + reason);
        return "";
    }
    log_write("信息", "POST 抓取成功 [" + url + "] " + std::to_string(response.size()) + " 字节");
    return response;
}

// 将 libcurl 数据块直接写入已打开的文件流；仅供 fetch_and_save 调试接口使用，
// 避免排查原始响应时占用额外内存。
static size_t write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    std::ofstream* file = static_cast<std::ofstream*>(userdata);
    file->write(ptr, realsize);
    return realsize;
}

// 把指定网页保存为本地文件，供开发阶段检查服务端原始响应；
// 正式爬取使用 fetch_page，不依赖该调试函数。
bool fetch_and_save(const std::string& url, const std::string& filename)
{
    CURL* curl = curl_easy_init();
    log_write("信息", "开始抓取：" + url);
    if (!curl)
    {
        log_write("错误", "curl初始化失败");
        return false;
    }

    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        log_write("错误", "打开文件失败：" + filename);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode code = curl_easy_perform(curl);
    file.close();

    if (code != CURLE_OK)
    {
        std::string err = curl_easy_strerror(code);
        log_write("错误", "抓取失败：" + err);
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200)
    {
        log_write("警告", "HTTP 状态码=" + std::to_string(http_code) + "，地址=" + url);
        return false;
    }

    log_write("信息", "抓取成功 " + url);
    return true;
}

// 从规范 URL 中提取域名；主流程将其作为解析器缺少来源时的临时提示，
// 最终展示来源仍由 news_classifier 统一归类。
std::string extract_domain(const std::string& url)
{
    const std::string normalized = normalize_url(url);
    size_t start = normalized.find("://");
    if (start == std::string::npos) return "";
    start += 3;
    size_t end = normalized.find_first_of("/?", start);
    if (end == std::string::npos)
        return normalized.substr(start);
    return normalized.substr(start, end - start);
}

// 读取 config.json 中所有来源的种子 URL，并返回给 main 的种子循环；
// 配置解析失败时返回空列表，让入口函数停止运行而不是使用错误地址。
std::vector<std::string> load_config(const std::string& config_path)
{
    std::vector<std::string> all_urls;
    std::ifstream f(config_path);
    if (!f) {
        std::cerr << "[配置] 无法打开 " << config_path << std::endl;
        return all_urls;
    }

    try {
        json config = json::parse(f);
        for (auto& source : config["sources"]) {
            for (auto& u : source["url_list"]) {
                all_urls.push_back(u.get<std::string>());
            }
        }
        log_write("信息", "从配置加载了 " + std::to_string(all_urls.size()) + " 个种子 URL");
    }
    catch (const std::exception& e) {
        std::cerr << "[配置] JSON 解析失败：" << e.what() << std::endl;
    }

    return all_urls;
}
