#include "ui_control.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace ui_control {

namespace {

struct ScreenName {
  Screen screen;
  const char* name;
};
constexpr ScreenName kScreens[] = {
    {Screen::Chart, "chart"},   {Screen::Airports, "airports"}, {Screen::Splash, "splash"},
    {Screen::Update, "update"}, {Screen::Picker, "picker"},     {Screen::Brightness, "brightness"},
    {Screen::Volume, "volume"}, {Screen::Close, "close"},
};
constexpr const char* kScreenList = "chart, airports, splash, update, picker, brightness, volume or close";

struct ButtonName {
  Button button;
  const char* name;
};
constexpr ButtonName kButtons[] = {
    {Button::Locate, "locate"},
    {Button::Awos, "awos"},
    {Button::Atis, "atis"},
    {Button::Stop, "stop"},
};
constexpr const char* kButtonList = "locate, awos, atis or stop";

constexpr int kMaxNumber = 9999;
constexpr size_t kMaxCode = 8;  // the Chart's codes: KHOU, K8T8, ...
constexpr size_t kMaxId = 48;   // a visualizer's id or name

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
  return s;
}

}  // namespace

bool screenFrom(const std::string& name, Screen& out) {
  std::string n = lower(name);
  const ScreenName* end = std::end(kScreens);
  const ScreenName* it = std::find_if(std::begin(kScreens), end, [&](const ScreenName& s) { return n == s.name; });
  if (it == end) return false;
  out = it->screen;
  return true;
}

const char* screenName(Screen s) {
  const ScreenName* end = std::end(kScreens);
  const ScreenName* it = std::find_if(std::begin(kScreens), end, [&](const ScreenName& e) { return e.screen == s; });
  return it == end ? "" : it->name;
}

bool buttonFrom(const std::string& name, Button& out) {
  std::string n = lower(name);
  const ButtonName* end = std::end(kButtons);
  const ButtonName* it = std::find_if(std::begin(kButtons), end, [&](const ButtonName& b) { return n == b.name; });
  if (it == end) return false;
  out = it->button;
  return true;
}

const char* buttonName(Button b) {
  const ButtonName* end = std::end(kButtons);
  const ButtonName* it = std::find_if(std::begin(kButtons), end, [&](const ButtonName& e) { return e.button == b; });
  return it == end ? "" : it->name;
}

bool parseNumber(const std::string& text, int& out) {
  if (text.empty() || text.size() > 4) return false;
  if (!std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
  int n = std::stoi(text);
  if (n < 1 || n > kMaxNumber) return false;
  out = n;
  return true;
}

Outcome parseShow(const Arg& screen, const Arg& page, ShowRequest& out) {
  ShowRequest r;
  if (!screen.given || screen.value.empty()) return fail(400, std::string("screen= is needed: ") + kScreenList + ".");
  if (!screenFrom(screen.value, r.screen)) {
    return fail(400, "No screen \"" + screen.value + "\": " + kScreenList + ".");
  }
  if (page.given) {
    if (r.screen != Screen::Airports) return fail(400, "page= goes with screen=airports.");
    if (!parseNumber(page.value, r.page)) return fail(400, "page= is a number from 1.");
  }
  out = r;
  return done();
}

Outcome parseCard(const Arg& airport, const Arg& position, const Arg& page, const Arg& button, CardRequest& out) {
  CardRequest r;
  if (airport.given && position.given) return fail(400, "airport= or position=, not both.");
  if (airport.given) {
    const std::string& v = airport.value;
    bool alnum = std::all_of(v.begin(), v.end(), [](unsigned char c) { return std::isalnum(c); });
    if (v.empty() || v.size() > kMaxCode || !alnum) {
      return fail(400, "airport= is an airport's code, like KHOU.");
    }
    r.airport = upper(v);
  }
  if (position.given && !parseNumber(position.value, r.position)) {
    return fail(400, "position= is a number from 1 (the first airport on the page).");
  }
  if (page.given) {
    if (!position.given) return fail(400, "page= goes with position=.");
    if (!parseNumber(page.value, r.page)) return fail(400, "page= is a number from 1.");
  }
  if (button.given && !buttonFrom(button.value, r.button)) {
    return fail(400, "No button \"" + button.value + "\": " + kButtonList + ".");
  }
  if (!r.opens() && r.button == Button::None) {
    return fail(400, std::string("airport=, position= or button= is needed (button: ") + kButtonList + ").");
  }
  out = r;
  return done();
}

Outcome parseVisualizer(const Arg& id, std::string& out) {
  if (!id.given || id.value.empty() || id.value.size() > kMaxId) {
    return fail(400, "id= is a visualizer's id or name, like wind.");
  }
  out = id.value;
  return done();
}

int findVisualizer(const std::vector<Visualizer>& visualizers, const std::string& want) {
  auto at = [&](std::vector<Visualizer>::const_iterator it) {
    return it == visualizers.end() ? -1 : (int)std::distance(visualizers.begin(), it);
  };
  int exact =
      at(std::find_if(visualizers.begin(), visualizers.end(), [&](const Visualizer& v) { return v.id == want; }));
  if (exact >= 0) return exact;
  std::string w = lower(want);
  return at(std::find_if(visualizers.begin(), visualizers.end(), [&](const Visualizer& v) {
    return lower(v.id) == w || lower(v.name) == w || (!v.shortName.empty() && lower(v.shortName) == w);
  }));
}

Outcome checkPage(int page, int pages) {
  if (page >= 1 && page <= pages) return done();
  return fail(409, "There " + std::string(pages == 1 ? "is 1 page" : "are " + std::to_string(pages) + " pages") +
                       " of airports.");
}

Outcome findCard(const std::vector<std::string>& codes, int perPage, int shownPage, const CardRequest& req,
                 int& index) {
  if (codes.empty()) return fail(409, "The Chart's airports aren't known yet.");
  if (!req.airport.empty()) {
    auto it = std::find(codes.begin(), codes.end(), req.airport);
    if (it == codes.end()) return fail(404, req.airport + " isn't one of the Chart's airports.");
    index = (int)std::distance(codes.begin(), it);
    return done();
  }
  int count = (int)codes.size();
  int pages = (count + perPage - 1) / perPage;
  int page = req.page > 0 ? req.page - 1 : shownPage;
  Outcome inRange = checkPage(page + 1, pages);
  if (!inRange.ok()) return inRange;
  int onPage = std::min(perPage, count - page * perPage);
  if (req.position > onPage) {
    return fail(409, "Page " + std::to_string(page + 1) + " has " + std::to_string(onPage) + " airport" +
                         (onPage == 1 ? "." : "s."));
  }
  index = page * perPage + req.position - 1;
  return done();
}

Outcome checkOpenCard(bool open, int index, int count) {
  if (!open || index < 0) return fail(409, "No airport's card is open: give airport= or position= to open one.");
  if (index >= count) {
    return fail(409, "The card's airport is gone from the Chart's airports: give airport= or position= to open one.");
  }
  return done();
}

std::string stateJson(const State& s) {
  JsonDocument doc;
  doc["remoteControl"] = s.remoteControl;
  doc["screen"] = s.screen;
  doc["panel"] = s.panel;
  doc["overlay"] = s.overlay;
  if (s.card.empty()) {
    doc["card"] = nullptr;
  } else {
    doc["card"] = s.card;
  }
  if (s.playing.empty()) {
    doc["playing"] = nullptr;
  } else {
    doc["playing"] = s.playing;
  }
  doc["page"] = s.page;
  doc["pages"] = s.pages;
  JsonArray on = doc["pageAirports"].to<JsonArray>();
  for (const std::string& c : s.pageAirports) on.add(c);
  doc["airports"] = s.airports;
  if (s.visualizer.empty()) {
    doc["visualizer"] = nullptr;
  } else {
    doc["visualizer"] = s.visualizer;
  }
  JsonArray vis = doc["visualizers"].to<JsonArray>();
  for (const std::string& v : s.visualizers) vis.add(v);
  doc["online"] = s.online;
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace ui_control
