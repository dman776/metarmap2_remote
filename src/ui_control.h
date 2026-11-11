// Remote control of the screens over HTTP, for screenshots (the web page's "Remote control (for screenshots)"): the
// requests' parameters, the screen and button names, which airport a card request means, and /api/ui/state's JSON.
// Plain C++, so it's unit tested on the host (test/test_ui_control); remote_web.cpp parses, ui.cpp does it.
//
// Named screens and items only, no raw taps: what a request can do is what a tap on that name does.
#pragma once

#include <string>
#include <vector>

#include "map_state.h"

namespace ui_control {

// POST /api/ui/show's screen=
enum class Screen {
  Chart,       // the Pixels panel (the rail's first button)
  Airports,    // the Airports panel; page=N for its page N
  Splash,      // the logo's splash
  Update,      // the firmware update screen (the splash's Firmware)
  Picker,      // the visualizer picker
  Brightness,  // the brightness sheet
  Volume,      // the logo's splash with its volume slider open
  Close,       // everything above closed: back to the panel that was open
};

// POST /api/ui/card's button=: the open card's, as tapped
enum class Button {
  None,
  Locate,  // pulses the airport's pixel on the Chart
  Awos,    // reads its METAR aloud (a second press stops it, as the button says Stop then)
  Atis,    // plays its ATIS (likewise)
  Stop,    // stops what the card's airport is saying
};

// A request's parameter: given or not, and its value (spaces at either end trimmed)
struct Arg {
  bool given = false;
  std::string value;
};

// What a request did, as HTTP: 200 done, 400 a bad request, 403 remote control is off, 404 no such airport or
// visualizer, 409 it doesn't apply now (no card open, the update installing), 500 the wrong task
struct Outcome {
  int status = 200;
  std::string text;  // why, for anything but 200
  bool ok() const { return status == 200; }
};

inline Outcome done() { return Outcome(); }
inline Outcome fail(int status, const std::string& text) {
  Outcome r;
  r.status = status;
  r.text = text;
  return r;
}

// The names, as requests and /api/ui/state say them. False (and out untouched) for one that isn't.
bool screenFrom(const std::string& name, Screen& out);
const char* screenName(Screen s);
bool buttonFrom(const std::string& name, Button& out);
const char* buttonName(Button b);

// A position or page number: 1 to 9999, digits only ("1", "12"; not "0", "-1", "+2", "1.5" or "2x")
bool parseNumber(const std::string& text, int& out);

// POST /api/ui/show: screen=, and page= (with airports only; 1 is the first)
struct ShowRequest {
  Screen screen = Screen::Close;
  int page = 0;  // 1-based; 0 for none given
};
Outcome parseShow(const Arg& screen, const Arg& page, ShowRequest& out);

// POST /api/ui/card: airport=KHOU (its code), or position=N (the Nth airport on the Airports page, 1 the first; with
// page=P, on page P, else the page shown), and/or button= (pressed on the card, after opening it if one was given)
struct CardRequest {
  std::string airport;  // upper case; "" for none
  int position = 0;     // 1-based; 0 for none
  int page = 0;         // 1-based; 0 for the page shown
  Button button = Button::None;
  bool opens() const { return !airport.empty() || position > 0; }
};
Outcome parseCard(const Arg& airport, const Arg& position, const Arg& page, const Arg& button, CardRequest& out);

// POST /api/ui/visualizer: id= (an id, or a name or short name in any case)
Outcome parseVisualizer(const Arg& id, std::string& out);

// The visualizer `want` means: an id as it is, else an id, name or short name in any case. -1 for none.
int findVisualizer(const std::vector<Visualizer>& visualizers, const std::string& want);

// Page `page` (1-based) of `pages`: 409 past the end ("There are 3 pages of airports.")
Outcome checkPage(int page, int pages);

// The tile a card request opens: `codes` are the Airports panel's airports in its order (alphabetical), `perPage` to
// a page, `shownPage` (0-based) the page on screen. 404 for a code that isn't there, 409 for a page or position past
// the end (or no airports yet).
Outcome findCard(const std::vector<std::string>& codes, int perPage, int shownPage, const CardRequest& req, int& index);

// A button-only card request's card: the open one, tile `index` of `count`. 409 with none open (or the splash or the
// update screen over it), and 409 when its airport is gone: the Airports panel's tiles are rebuilt when the Chart's
// airports change, with the card left open, so its index can be past the end (or another airport's). Don't read
// the tile unless this is done().
Outcome checkOpenCard(bool open, int index, int count);

// GET /api/ui/state
struct State {
  bool remoteControl = false;
  std::string screen = "main";            // main, splash, update, startup (connecting, or reconnecting), wifiSetup
  std::string panel = "chart";            // chart, airports
  std::string overlay = "none";           // none, picker, brightness, card, volume (the splash's slider)
  std::string card;                       // the open card's airport; "" for none
  std::string playing;                    // the card's airport is saying: awos, atis; "" for nothing
  int page = 1, pages = 1;                // the Airports page shown (1-based), of how many
  std::vector<std::string> pageAirports;  // the airports on it, in order (position 1 first)
  int airports = 0;
  std::string visualizer;  // the id on screen; "" before the Chart says
  std::vector<std::string> visualizers;
  bool online = false;  // the Chart is answering
};
std::string stateJson(const State& s);

}  // namespace ui_control
