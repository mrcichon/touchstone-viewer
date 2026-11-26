#include "order.h"

#include <algorithm>
#include <numeric>

namespace core {
namespace {

struct RankRange {
    size_t lo, hi;
};

std::vector<RankRange> expected_ranks(const std::vector<double>& number) {
    size_t m = number.size();
    std::vector<size_t> by_number(m);
    std::iota(by_number.begin(), by_number.end(), size_t{0});
    std::sort(by_number.begin(), by_number.end(), [&](size_t a, size_t b) { return number[a] < number[b]; });
    std::vector<RankRange> ranks(m);
    for (size_t first = 0; first < m;) {
        size_t last = first;
        while (last + 1 < m && !(number[by_number[first]] < number[by_number[last + 1]])) ++last;  // equal numbers
        for (size_t k = first; k <= last; ++k) ranks[by_number[k]] = {first, last};
        first = last + 1;
    }
    return ranks;
}

size_t ordered_count(const std::vector<int8_t>& direction) {
    return static_cast<size_t>(std::count_if(direction.begin(), direction.end(), [](int8_t d) { return d != 0; }));
}

}  // namespace

std::vector<int8_t> ordered_samples(const std::vector<const double*>& y, const std::vector<double>& number, size_t n, size_t allowed) {
    size_t m = y.size();
    std::vector<int8_t> direction(n, 0);
    if (m < 2) return direction;
    std::vector<RankRange> expected = expected_ranks(number);
    if (expected[0].lo == 0 && expected[0].hi == m - 1) return direction;  // all numbers are equal

    std::vector<size_t> by_value(m);
    std::iota(by_value.begin(), by_value.end(), size_t{0});
    for (size_t i = 0; i < n; ++i) {
        // Insertion sort, started from the order of the sample before. That order is almost correct for this sample.
        for (size_t a = 1; a < m; ++a) {
            size_t curve = by_value[a];
            size_t b = a;
            for (; b > 0 && y[by_value[b - 1]][i] > y[curve][i]; --b) by_value[b] = by_value[b - 1];
            by_value[b] = curve;
        }
        // Curves with equal values share the ranks first..last, as curves with equal numbers share their expected ranks.
        bool rises = true, falls = true;
        for (size_t first = 0; first < m && (rises || falls);) {
            size_t last = first;
            while (last + 1 < m && !(y[by_value[first]][i] < y[by_value[last + 1]][i])) ++last;
            for (size_t rank = first; rank <= last; ++rank) {
                RankRange r = expected[by_value[rank]];
                auto difference = [&](size_t lo, size_t hi) { return last < lo ? lo - last : first > hi ? first - hi : 0; };
                rises = rises && difference(r.lo, r.hi) <= allowed;
                falls = falls && difference(m - 1 - r.hi, m - 1 - r.lo) <= allowed;
            }
            first = last + 1;
        }
        direction[i] = rises ? 1 : falls ? -1 : 0;
    }
    return direction;
}

Ordered ordered_samples_without_some(const std::vector<const double*>& y, const std::vector<double>& number, size_t n, size_t allowed, size_t leave_out) {
    std::vector<size_t> kept(y.size());
    std::iota(kept.begin(), kept.end(), size_t{0});
    auto without = [&](size_t skipped) {  // skipped: position in kept, or kept.size() to skip none
        std::vector<const double*> some_y;
        std::vector<double> some_number;
        for (size_t k = 0; k < kept.size(); ++k) {
            if (k == skipped) continue;
            some_y.push_back(y[kept[k]]);
            some_number.push_back(number[kept[k]]);
        }
        return ordered_samples(some_y, some_number, n, allowed);
    };
    Ordered result{without(kept.size()), {}};
    for (size_t step = 0; step < leave_out && kept.size() > 2; ++step) {
        size_t best = kept.size();
        for (size_t k = 0; k < kept.size(); ++k) {
            std::vector<int8_t> direction = without(k);
            if (ordered_count(direction) <= ordered_count(result.direction)) continue;
            result.direction = std::move(direction);
            best = k;
        }
        if (best == kept.size()) break;
        result.left_out.push_back(kept[best]);
        kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(best));
    }
    return result;
}

}  // namespace core
