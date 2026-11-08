#include "pages.h"

#include <algorithm>
#include <numeric>

namespace pages {

int count(int items, int per) {
  if (per <= 0 || items <= 0) return 1;
  return (items + per - 1) / per;
}

int of(int index, int per) { return per > 0 && index > 0 ? index / per : 0; }

std::vector<int> alphabetical(const std::vector<std::string>& codes) {
  std::vector<int> order(codes.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return codes[a] < codes[b]; });
  return order;
}

std::vector<Button> strip(const std::vector<std::string>& firsts, int current, int fit) {
  int n = (int)firsts.size();
  std::vector<Button> buttons;
  if (n <= 1) return buttons;  // one page: no strip
  if (n <= fit) {
    for (int i = 0; i < n; i++) buttons.push_back({Button::Page, i, firsts[i]});
    return buttons;
  }
  current = std::max(0, std::min(current, n - 1));
  buttons.push_back({Button::Previous, current > 0 ? current - 1 : -1, ""});
  buttons.push_back({Button::Position, current, std::to_string(current + 1) + " / " + std::to_string(n)});
  buttons.push_back({Button::Next, current < n - 1 ? current + 1 : -1, ""});
  return buttons;
}

}  // namespace pages
