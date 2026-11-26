#pragma once
#include "views.h"

#include <cstdint>

struct Series {
    const double* x = nullptr;
    const double* y = nullptr;
    size_t n = 0;  // 0 if the curve has no data for the panel
};
Series series_of(const Curve& c, const Panel& panel);
uint64_t version_of(const Curve& c, const Panel& panel);
