/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TitleDiff.h"

#include <algorithm>
#include <cwchar>

namespace unglom {
namespace {

constexpr const wchar_t* kSeparators[] = {
    L" - ", L" \u2014 ", L" \u2013 ", L" | ", L" \u00B7 ", L" \u2022 ",
};

constexpr const wchar_t* kTidyChars = L" \t,;:-|\u2014\u2013\u00B7\u2022";

struct Split {
  std::vector<std::wstring> parts;
  std::vector<std::wstring> seps;  // seps[i] sits between parts[i] and parts[i + 1].
};

size_t SeparatorLengthAt(const std::wstring& s, size_t i, bool words) {
  if (words) {
    size_t n = 0;
    while (i + n < s.size() && s[i + n] == L' ') ++n;
    return n;
  }
  size_t best = 0;
  for (const wchar_t* sep : kSeparators) {
    size_t n = wcslen(sep);
    if (n > best && s.compare(i, n, sep) == 0) best = n;
  }
  return best;
}

Split SplitOn(const std::wstring& s, bool words) {
  Split out;
  size_t start = 0;
  size_t i = 0;
  while (i < s.size()) {
    size_t len = SeparatorLengthAt(s, i, words);
    if (len == 0) {
      ++i;
      continue;
    }
    out.parts.push_back(s.substr(start, i - start));
    out.seps.push_back(s.substr(i, len));
    i += len;
    start = i;
  }
  out.parts.push_back(s.substr(start));
  return out;
}

std::wstring Join(const Split& s, size_t from, size_t to) {
  std::wstring r;
  for (size_t i = from; i < to; ++i) {
    if (i > from) r += s.seps[i - 1];
    r += s.parts[i];
  }
  return r;
}

// Removes the parts that every split shares at its start and end, while
// leaving at least `keep` parts in each.
std::vector<std::wstring> TrimCommon(const std::vector<Split>& splits, size_t keep) {
  size_t minParts = splits[0].parts.size();
  for (const Split& s : splits) minParts = std::min(minParts, s.parts.size());
  size_t budget = minParts > keep ? minParts - keep : 0;

  auto allEqual = [&](auto partAt) {
    for (const Split& s : splits) {
      if (partAt(s) != partAt(splits[0])) return false;
    }
    return true;
  };

  size_t prefix = 0;
  while (prefix < budget &&
         allEqual([&](const Split& s) -> const std::wstring& { return s.parts[prefix]; })) {
    ++prefix;
  }
  size_t suffix = 0;
  while (prefix + suffix < budget &&
         allEqual([&](const Split& s) -> const std::wstring& {
           return s.parts[s.parts.size() - 1 - suffix];
         })) {
    ++suffix;
  }

  std::vector<std::wstring> out;
  for (const Split& s : splits) out.push_back(Join(s, prefix, s.parts.size() - suffix));
  return out;
}

std::wstring Tidy(const std::wstring& s) {
  size_t b = s.find_first_not_of(kTidyChars);
  if (b == std::wstring::npos) return L"";
  size_t e = s.find_last_not_of(kTidyChars);
  return s.substr(b, e - b + 1);
}

}  // namespace

std::vector<std::wstring> DistinctLabels(const std::vector<std::wstring>& titles) {
  if (titles.size() < 2) return std::vector<std::wstring>(titles.size());

  std::vector<Split> segments;
  for (const std::wstring& t : titles) segments.push_back(SplitOn(t, false));
  std::vector<std::wstring> labels = TrimCommon(segments, 0);

  bool allNonEmpty = std::none_of(labels.begin(), labels.end(),
                                  [](const std::wstring& l) { return l.empty(); });
  if (allNonEmpty) {
    std::vector<Split> words;
    for (const std::wstring& l : labels) words.push_back(SplitOn(l, true));
    labels = TrimCommon(words, 1);
  }

  for (size_t i = 0; i < labels.size(); ++i) {
    labels[i] = Tidy(labels[i]);
    if (labels[i].empty()) labels[i] = Tidy(segments[i].parts[0]);
    if (labels[i].empty()) labels[i] = titles[i];
  }
  return labels;
}

}  // namespace unglom
