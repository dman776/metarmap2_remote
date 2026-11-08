// The Airports panel's pages: the airports in alphabetical order, a page of tiles at a time, and the strip of page
// buttons under them. Plain C++, so it's unit tested on the host (test/test_pages).
#pragma once

#include <string>
#include <vector>

namespace pages {

// How many pages `count` items take, `per` to a page (at least 1, so an empty map still has a page)
int count(int items, int per);

// The page item `index` is on
int of(int index, int per);

// The order to show `codes` in: alphabetical (indexes into codes; equal codes keep their order)
std::vector<int> alphabetical(const std::vector<std::string>& codes);

// One button of the strip under the tiles: a page (its first airport's code), or a step to the previous or next
struct Button {
  enum Kind { Page, Previous, Next, Position } kind;
  int page;           // Page: the page it opens; Previous/Next: the page they open (-1: none, disabled)
  std::string label;  // Page: the first code on it; Position: "3 / 9"
};

// The strip for `firsts` (each page's first code), page `current` open: a button per page while they fit
// (at most `fit`), else previous, "3 / 9" and next
std::vector<Button> strip(const std::vector<std::string>& firsts, int current, int fit);

}  // namespace pages
