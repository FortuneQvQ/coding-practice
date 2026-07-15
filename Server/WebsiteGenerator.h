#pragma once

#include <string>
#include <vector>

#include "database.h"
#include "other/category.h"

void generateNewsWebsite();
void generateNewsWebsite(const std::string& dbPath);

void generateNewsIndexPage(const std::vector<News>& newsList);
void generateNewsDetailPage(const News& news);
void generateSearchPage();
void generateCategoryIndexPage();
void generateCategoryResultPage(const std::vector<News>& result, const Category& category);
