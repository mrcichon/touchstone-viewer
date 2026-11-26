#include "names.h"

#include "number.h"

#include <algorithm>
#include <set>

namespace core {
namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::vector<std::string> words_of(const std::string& name) {
    std::vector<std::string> words;
    std::string word;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        bool decimal_point = c == '.' && i > 0 && i + 1 < name.size() && is_digit(name[i - 1]) && is_digit(name[i + 1]);
        if ((c == '_' || c == '-' || c == ' ' || c == '.') && !decimal_point) {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

std::string last_part(const std::string& folder) {
    size_t slash = folder.find_last_of("/\\");
    return slash == std::string::npos ? folder : folder.substr(slash + 1);
}

size_t distinct_count(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return static_cast<size_t>(std::unique(v.begin(), v.end()) - v.begin());
}

}  // namespace

std::vector<NumberInName> numbers_in(const std::string& name) {
    std::vector<NumberInName> numbers;
    for (size_t i = 0; i < name.size();) {
        if (!is_digit(name[i])) {
            ++i;
            continue;
        }
        size_t begin = i;
        std::string digits;
        bool has_decimal_mark = false;
        for (; i < name.size(); ++i) {
            bool decimal_mark = !has_decimal_mark && (name[i] == '.' || name[i] == ',') && i + 1 < name.size() && is_digit(name[i + 1]);
            if (!is_digit(name[i]) && !decimal_mark) break;
            has_decimal_mark = has_decimal_mark || decimal_mark;
            digits += decimal_mark ? '.' : name[i];
        }
        const char* p = digits.data();
        double value = 0.0;
        if (parse_number(p, digits.data() + digits.size(), value)) numbers.push_back({value, begin, i});
    }
    return numbers;
}

bool natural_less(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (is_digit(a[i]) && is_digit(b[j])) {
            size_t i_end = i, j_end = j;
            while (i_end < a.size() && is_digit(a[i_end])) ++i_end;
            while (j_end < b.size() && is_digit(b[j_end])) ++j_end;
            while (i < i_end - 1 && a[i] == '0') ++i;  // leading zeros are ignored
            while (j < j_end - 1 && b[j] == '0') ++j;
            if (i_end - i != j_end - j) return i_end - i < j_end - j;  // the number with more digits is larger
            int order = a.compare(i, i_end - i, b, j, j_end - j);
            if (order != 0) return order < 0;
            i = i_end;
            j = j_end;
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

std::vector<std::string> short_labels(const std::vector<std::string>& names, const std::vector<std::string>& folders) {
    if (names.size() < 2) return names;
    std::vector<std::string> full = names;
    if (distinct_count(names) < names.size())
        for (size_t i = 0; i < full.size(); ++i) full[i] = last_part(folders[i]) + " " + names[i];

    std::vector<std::vector<std::string>> words;
    for (const std::string& name : full) words.push_back(words_of(name));
    bool same_word_count = std::all_of(words.begin(), words.end(), [&](const auto& w) { return w.size() == words[0].size(); });

    std::vector<std::string> labels(full.size());
    bool dropped_a_word = false;
    auto append = [](std::string& label, const std::string& word) {
        if (!label.empty()) label += ' ';
        label += word;
    };
    if (same_word_count) {
        for (size_t position = 0; position < words[0].size(); ++position) {
            bool shared = std::all_of(words.begin(), words.end(), [&](const auto& w) { return w[position] == words[0][position]; });
            if (shared) dropped_a_word = true;
            else
                for (size_t i = 0; i < words.size(); ++i) append(labels[i], words[i][position]);
        }
    } else {
        std::set<std::string> shared(words[0].begin(), words[0].end());
        for (const auto& w : words)
            std::erase_if(shared, [&](const std::string& s) { return std::find(w.begin(), w.end(), s) == w.end(); });
        for (size_t i = 0; i < words.size(); ++i)
            for (const std::string& word : words[i])
                if (!shared.count(word)) append(labels[i], word);
        dropped_a_word = !shared.empty();
    }

    bool any_empty = std::any_of(labels.begin(), labels.end(), [](const std::string& s) { return s.empty(); });
    if (!dropped_a_word || any_empty || distinct_count(labels) < distinct_count(full)) return full;
    return labels;
}

}  // namespace core
