// SPDX-License-Identifier: TBD
// Minimal fixed-width text table for console output.
#pragma once

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

namespace platter::cli {

class TextTable {
public:
    enum class Align { left, right };

    explicit TextTable(std::vector<std::string> headers, std::vector<Align> align = {})
        : headers_(std::move(headers)), align_(std::move(align)) {
        align_.resize(headers_.size(), Align::left);
    }

    void add(std::vector<std::string> row) {
        row.resize(headers_.size());
        rows_.push_back(std::move(row));
    }

    void print(std::ostream& os) const {
        std::vector<std::size_t> w(headers_.size());
        for (std::size_t c = 0; c < headers_.size(); ++c) {
            w[c] = headers_[c].size();
            for (const auto& r : rows_) w[c] = std::max(w[c], r[c].size());
        }
        auto line = [&](const std::vector<std::string>& cells) {
            for (std::size_t c = 0; c < cells.size(); ++c) {
                const auto pad = std::string(w[c] - cells[c].size(), ' ');
                if (c) os << "  ";
                if (align_[c] == Align::right) os << pad << cells[c];
                else os << cells[c] << (c + 1 < cells.size() ? pad : "");
            }
            os << '\n';
        };
        line(headers_);
        std::vector<std::string> rule;
        for (auto x : w) rule.emplace_back(x, '-');
        line(rule);
        for (const auto& r : rows_) line(r);
    }

private:
    std::vector<std::string> headers_;
    std::vector<Align> align_;
    std::vector<std::vector<std::string>> rows_;
};

}  // namespace platter::cli
