#pragma once

#include <string>

struct News;

// 根据新闻证据返回展示系统约定的来源枚举，供新增入库和历史迁移共同使用。
std::string classify_news_source(const News& news);
// 根据关键词权重返回展示系统约定的主题枚举，避免展示端出现空分类。
std::string classify_news_topic(const News& news);
// 同时规范 News 的来源和主题；规则在本地执行，不依赖外部服务。
void normalize_news_metadata(News& news);
