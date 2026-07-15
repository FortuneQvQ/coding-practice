#pragma once

#include <string>

struct News;

// 将来源和主题转换为展示系统约定的固定枚举值。
// 规则完全在本地执行，不依赖 AI 或外部接口，结果稳定且便于审计。
std::string classify_news_source(const News& news);
std::string classify_news_topic(const News& news);
void normalize_news_metadata(News& news);

