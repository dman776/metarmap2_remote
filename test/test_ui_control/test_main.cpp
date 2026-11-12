// Remote control's requests: the screen and button names, the parameters, which visualizer and which airport a
// request means, and /api/ui/state's JSON.
#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>

#include "ui_control.h"

using ui_control::Arg;
using ui_control::Button;
using ui_control::CardRequest;
using ui_control::Outcome;
using ui_control::Screen;
using ui_control::ShowRequest;

void setUp() {}
void tearDown() {}

namespace {

Arg given(const char* v) {
  Arg a;
  a.given = true;
  a.value = v;
  return a;
}
const Arg none;

Visualizer vis(const char* id, const char* name, const char* shortName) {
  Visualizer v;
  v.id = id;
  v.name = name;
  v.shortName = shortName;
  return v;
}

CardRequest atPosition(int position, int page = 0) {
  CardRequest r;
  r.position = position;
  r.page = page;
  return r;
}

const std::vector<std::string> kCodes = {"KAUS", "KDFW", "KHOU", "KIAH", "KSAT"};  // 2 to a page: 3 pages

}  // namespace

void test_every_screen_name_round_trips() {
  const char* names[] = {"chart", "airports", "splash", "update", "picker", "brightness", "volume", "close"};
  for (const char* n : names) {
    Screen s = Screen::Close;
    TEST_ASSERT_TRUE_MESSAGE(ui_control::screenFrom(n, s), n);
    TEST_ASSERT_EQUAL_STRING(n, ui_control::screenName(s));
  }
}

void test_screen_names_any_case_but_nothing_else() {
  Screen s = Screen::Close;
  TEST_ASSERT_TRUE(ui_control::screenFrom("Picker", s));
  TEST_ASSERT_TRUE(s == Screen::Picker);
  TEST_ASSERT_FALSE(ui_control::screenFrom("leds", s));
  TEST_ASSERT_FALSE(ui_control::screenFrom("", s));
  TEST_ASSERT_FALSE(ui_control::screenFrom("chart ", s));  // the request's spaces are trimmed before
  TEST_ASSERT_TRUE(s == Screen::Picker);                   // untouched by a name that isn't one
}

void test_every_button_name_round_trips() {
  const char* names[] = {"locate", "awos", "atis", "stop"};
  for (const char* n : names) {
    Button b = Button::None;
    TEST_ASSERT_TRUE_MESSAGE(ui_control::buttonFrom(n, b), n);
    TEST_ASSERT_EQUAL_STRING(n, ui_control::buttonName(b));
  }
  Button b = Button::None;
  TEST_ASSERT_TRUE(ui_control::buttonFrom("AWOS", b));
  TEST_ASSERT_TRUE(b == Button::Awos);
  TEST_ASSERT_FALSE(ui_control::buttonFrom("play", b));
}

void test_numbers_are_plain_digits_from_one() {
  int n = 0;
  TEST_ASSERT_TRUE(ui_control::parseNumber("1", n));
  TEST_ASSERT_EQUAL(1, n);
  TEST_ASSERT_TRUE(ui_control::parseNumber("12", n));
  TEST_ASSERT_EQUAL(12, n);
  TEST_ASSERT_TRUE(ui_control::parseNumber("9999", n));
  const char* bad[] = {"", "0", "-1", "+2", "1.5", "2x", " 3", "10000", "00"};
  for (const char* b : bad) {
    n = 7;
    TEST_ASSERT_FALSE_MESSAGE(ui_control::parseNumber(b, n), b);
    TEST_ASSERT_EQUAL(7, n);
  }
}

void test_show_needs_a_known_screen() {
  ShowRequest r;
  TEST_ASSERT_EQUAL(400, ui_control::parseShow(none, none, r).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseShow(given(""), none, r).status);
  Outcome o = ui_control::parseShow(given("cockpit"), none, r);
  TEST_ASSERT_EQUAL(400, o.status);
  TEST_ASSERT_TRUE(o.text.find("cockpit") != std::string::npos);
  TEST_ASSERT_TRUE(o.text.find("picker") != std::string::npos);  // says which there are
  TEST_ASSERT_TRUE(ui_control::parseShow(given("splash"), none, r).ok());
  TEST_ASSERT_TRUE(r.screen == Screen::Splash);
  TEST_ASSERT_EQUAL(0, r.page);
}

void test_show_page_only_with_airports() {
  ShowRequest r;
  TEST_ASSERT_TRUE(ui_control::parseShow(given("airports"), given("2"), r).ok());
  TEST_ASSERT_TRUE(r.screen == Screen::Airports);
  TEST_ASSERT_EQUAL(2, r.page);
  TEST_ASSERT_EQUAL(400, ui_control::parseShow(given("chart"), given("2"), r).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseShow(given("airports"), given("0"), r).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseShow(given("airports"), given("two"), r).status);
}

void test_card_by_code_in_upper_case() {
  CardRequest r;
  TEST_ASSERT_TRUE(ui_control::parseCard(given("khou"), none, none, none, r).ok());
  TEST_ASSERT_EQUAL_STRING("KHOU", r.airport.c_str());
  TEST_ASSERT_TRUE(r.opens());
  TEST_ASSERT_TRUE(r.button == Button::None);
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(given(""), none, none, none, r).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(given("KH/OU"), none, none, none, r).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(given("KHOUSTONX"), none, none, none, r).status);
}

void test_card_by_position_and_page() {
  CardRequest r;
  TEST_ASSERT_TRUE(ui_control::parseCard(none, given("3"), given("2"), none, r).ok());
  TEST_ASSERT_EQUAL(3, r.position);
  TEST_ASSERT_EQUAL(2, r.page);
  TEST_ASSERT_TRUE(r.airport.empty());
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(given("KHOU"), given("1"), none, none, r).status);  // not both
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(given("KHOU"), none, given("1"), none, r).status);  // page: positions
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(none, given("0"), none, none, r).status);
}

void test_card_button_alone_or_with_an_airport() {
  CardRequest r;
  TEST_ASSERT_TRUE(ui_control::parseCard(none, none, none, given("atis"), r).ok());
  TEST_ASSERT_TRUE(r.button == Button::Atis);
  TEST_ASSERT_FALSE(r.opens());
  TEST_ASSERT_TRUE(ui_control::parseCard(given("KHOU"), none, none, given("locate"), r).ok());
  TEST_ASSERT_TRUE(r.button == Button::Locate);
  TEST_ASSERT_TRUE(r.opens());
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(none, none, none, given("play"), r).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(none, none, none, none, r).status);  // nothing to do
}

void test_bad_requests_leave_the_output_alone() {
  CardRequest r;
  r.airport = "KAUS";
  TEST_ASSERT_EQUAL(400, ui_control::parseCard(given("KHOU"), none, none, given("play"), r).status);
  TEST_ASSERT_EQUAL_STRING("KAUS", r.airport.c_str());
}

void test_visualizer_id_needed() {
  std::string id;
  TEST_ASSERT_EQUAL(400, ui_control::parseVisualizer(none, id).status);
  TEST_ASSERT_EQUAL(400, ui_control::parseVisualizer(given(""), id).status);
  TEST_ASSERT_TRUE(ui_control::parseVisualizer(given("wind"), id).ok());
  TEST_ASSERT_EQUAL_STRING("wind", id.c_str());
}

void test_find_visualizer_by_id_name_or_short_name() {
  std::vector<Visualizer> v = {vis("flight_category", "Flight Category", "Category"), vis("wind", "Wind", "Wind"),
                               vis("density_altitude", "Density Altitude", "Density Alt"), vis("Wind2", "Gusts", "")};
  TEST_ASSERT_EQUAL(1, ui_control::findVisualizer(v, "wind"));
  TEST_ASSERT_EQUAL(0, ui_control::findVisualizer(v, "Flight Category"));
  TEST_ASSERT_EQUAL(2, ui_control::findVisualizer(v, "density alt"));
  TEST_ASSERT_EQUAL(2, ui_control::findVisualizer(v, "DENSITY_ALTITUDE"));
  TEST_ASSERT_EQUAL(3, ui_control::findVisualizer(v, "Wind2"));  // an exact id first
  TEST_ASSERT_EQUAL(3, ui_control::findVisualizer(v, "gusts"));
  TEST_ASSERT_EQUAL(-1, ui_control::findVisualizer(v, "rainbow"));
  TEST_ASSERT_EQUAL(-1, ui_control::findVisualizer(v, ""));  // an empty short name matches nothing
  TEST_ASSERT_EQUAL(-1, ui_control::findVisualizer({}, "wind"));
}

void test_find_card_by_code() {
  CardRequest r;
  r.airport = "KIAH";
  int index = -1;
  TEST_ASSERT_TRUE(ui_control::findCard(kCodes, 2, 0, r, index).ok());
  TEST_ASSERT_EQUAL(3, index);
  r.airport = "KXYZ";
  Outcome o = ui_control::findCard(kCodes, 2, 0, r, index);
  TEST_ASSERT_EQUAL(404, o.status);
  TEST_ASSERT_TRUE(o.text.find("KXYZ") != std::string::npos);
  TEST_ASSERT_EQUAL(3, index);  // untouched
}

void test_find_card_by_position_on_the_page_shown_or_asked() {
  int index = -1;
  TEST_ASSERT_TRUE(ui_control::findCard(kCodes, 2, 1, atPosition(1), index).ok());  // page 2 shown: KHOU
  TEST_ASSERT_EQUAL(2, index);
  TEST_ASSERT_TRUE(ui_control::findCard(kCodes, 2, 1, atPosition(2, 1), index).ok());  // page 1 asked: KDFW
  TEST_ASSERT_EQUAL(1, index);
  TEST_ASSERT_TRUE(ui_control::findCard(kCodes, 2, 0, atPosition(1, 3), index).ok());  // the last page: KSAT
  TEST_ASSERT_EQUAL(4, index);
}

void test_find_card_past_the_end() {
  int index = -1;
  Outcome o = ui_control::findCard(kCodes, 2, 0, atPosition(2, 3), index);  // page 3 has only KSAT
  TEST_ASSERT_EQUAL(409, o.status);
  TEST_ASSERT_EQUAL_STRING("Page 3 has 1 airport.", o.text.c_str());
  o = ui_control::findCard(kCodes, 2, 0, atPosition(3, 1), index);
  TEST_ASSERT_EQUAL_STRING("Page 1 has 2 airports.", o.text.c_str());
  o = ui_control::findCard(kCodes, 2, 0, atPosition(1, 4), index);
  TEST_ASSERT_EQUAL(409, o.status);
  TEST_ASSERT_EQUAL_STRING("There are 3 pages of airports.", o.text.c_str());
  TEST_ASSERT_EQUAL(-1, index);
  TEST_ASSERT_EQUAL(409, ui_control::findCard({}, 2, 0, atPosition(1), index).status);  // none known yet
}

void test_check_page() {
  TEST_ASSERT_TRUE(ui_control::checkPage(1, 1).ok());
  TEST_ASSERT_TRUE(ui_control::checkPage(3, 3).ok());
  TEST_ASSERT_EQUAL_STRING("There is 1 page of airports.", ui_control::checkPage(2, 1).text.c_str());
  TEST_ASSERT_EQUAL(409, ui_control::checkPage(0, 3).status);
}

// A button alone, to the open card: the card's tile can have gone (the tiles rebuilt, fewer airports) with the card
// still open, and the request mustn't read past the end
void test_check_open_card() {
  TEST_ASSERT_TRUE(ui_control::checkOpenCard(true, 0, 1).ok());
  TEST_ASSERT_TRUE(ui_control::checkOpenCard(true, 11, 12).ok());
  Outcome closed = ui_control::checkOpenCard(false, 3, 12);
  TEST_ASSERT_EQUAL(409, closed.status);
  TEST_ASSERT_EQUAL_STRING("No airport's card is open: give airport= or position= to open one.", closed.text.c_str());
  TEST_ASSERT_EQUAL(409, ui_control::checkOpenCard(true, -1, 12).status);
  Outcome gone = ui_control::checkOpenCard(true, 12, 12);  // 12 airports were 13 when it opened
  TEST_ASSERT_EQUAL(409, gone.status);
  TEST_ASSERT_NOT_NULL(strstr(gone.text.c_str(), "gone"));
  TEST_ASSERT_EQUAL(409, ui_control::checkOpenCard(true, 5, 0).status);  // no airports at all now
  TEST_ASSERT_EQUAL(409, ui_control::checkOpenCard(false, -1, 0).status);
}

void test_state_json() {
  ui_control::State s;
  s.remoteControl = true;
  s.panel = "airports";
  s.overlay = "card";
  s.card = "KHOU";
  s.playing = "atis";
  s.page = 2;
  s.pages = 3;
  s.pageAirports = {"KHOU", "KIAH"};
  s.airports = 5;
  s.visualizer = "wind";
  s.visualizers = {"flight_category", "wind"};
  s.online = true;
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, ui_control::stateJson(s)));
  TEST_ASSERT_TRUE(doc["remoteControl"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("main", doc["screen"]);
  TEST_ASSERT_EQUAL_STRING("airports", doc["panel"]);
  TEST_ASSERT_EQUAL_STRING("card", doc["overlay"]);
  TEST_ASSERT_EQUAL_STRING("KHOU", doc["card"]);
  TEST_ASSERT_EQUAL_STRING("atis", doc["playing"]);
  TEST_ASSERT_EQUAL(2, doc["page"].as<int>());
  TEST_ASSERT_EQUAL(3, doc["pages"].as<int>());
  TEST_ASSERT_EQUAL(2, doc["pageAirports"].size());
  TEST_ASSERT_EQUAL_STRING("KIAH", doc["pageAirports"][1]);
  TEST_ASSERT_EQUAL(5, doc["airports"].as<int>());
  TEST_ASSERT_EQUAL_STRING("wind", doc["visualizer"]);
  TEST_ASSERT_EQUAL(2, doc["visualizers"].size());
  TEST_ASSERT_TRUE(doc["online"].as<bool>());
}

void test_state_json_nothing_open() {
  ui_control::State s;  // remote control off, nothing known yet
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, ui_control::stateJson(s)));
  TEST_ASSERT_FALSE(doc["remoteControl"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("chart", doc["panel"]);
  TEST_ASSERT_EQUAL_STRING("none", doc["overlay"]);
  TEST_ASSERT_TRUE(doc["card"].isNull());
  TEST_ASSERT_TRUE(doc["playing"].isNull());
  TEST_ASSERT_TRUE(doc["visualizer"].isNull());
  TEST_ASSERT_EQUAL(0, doc["pageAirports"].size());
  TEST_ASSERT_TRUE(doc["pageAirports"].is<JsonArray>());  // an empty list, not missing
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_screen_name_round_trips);
  RUN_TEST(test_screen_names_any_case_but_nothing_else);
  RUN_TEST(test_every_button_name_round_trips);
  RUN_TEST(test_numbers_are_plain_digits_from_one);
  RUN_TEST(test_show_needs_a_known_screen);
  RUN_TEST(test_show_page_only_with_airports);
  RUN_TEST(test_card_by_code_in_upper_case);
  RUN_TEST(test_card_by_position_and_page);
  RUN_TEST(test_card_button_alone_or_with_an_airport);
  RUN_TEST(test_bad_requests_leave_the_output_alone);
  RUN_TEST(test_visualizer_id_needed);
  RUN_TEST(test_find_visualizer_by_id_name_or_short_name);
  RUN_TEST(test_find_card_by_code);
  RUN_TEST(test_find_card_by_position_on_the_page_shown_or_asked);
  RUN_TEST(test_find_card_past_the_end);
  RUN_TEST(test_check_page);
  RUN_TEST(test_check_open_card);
  RUN_TEST(test_state_json);
  RUN_TEST(test_state_json_nothing_open);
  return UNITY_END();
}
