#ifndef NEWS_H
#define NEWS_H

#include <string>

struct News
{
    std::string title;      // 标题

    std::string time;       // 发布时间

    std::string content;    // 正文

    std::string url;        // 原链接（唯一ID）

    std::string source;     // 来源

    std::string image;      // 图片
};

#endif