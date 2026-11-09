// Wi-Fi config mode: the remote opens its own Wi-Fi network (open, e.g. "MetarMap2-Remote-A1B2") with a captive
// portal, so a phone that joins it gets the setup page (remote_web.cpp) to pick the Wi-Fi and the map's address.
//
// A fresh remote (its saved Wi-Fi is kSetupSsid: a build without secrets.h) starts in it and stays until it's set
// up. A remote that can't join its network any more starts it from the splash's Wi-Fi setup button; it keeps trying
// the saved network while no phone is connected (the access point has to follow that network's channel while it
// tries), and leaves when it joins, on Cancel, or after 10 minutes with no phone connected.
#pragma once

#include <string>
#include <vector>

#include "wifi_setup_text.h"

struct Settings;

enum class SetupReason { Fresh, Lost };

void wifi_setup_begin(Settings& settings);

void wifi_setup_start(SetupReason why);
void wifi_setup_stop();  // back to the saved Wi-Fi (not for a fresh remote: it has none)

bool wifi_setup_active();
bool wifi_setup_fresh();                  // started because nothing is set up yet
const std::string& wifi_setup_ap_name();  // e.g. MetarMap2-Remote-A1B2
const char* wifi_setup_address();         // http://192.168.4.1/
int wifi_setup_phones();                  // devices joined to the access point

// The networks found (strongest first); `scanning` while a scan runs
std::vector<ScannedNetwork> wifi_setup_networks(bool& scanning);
void wifi_setup_rescan();

// Call from loop(): the captive portal's DNS, scan results, retrying the saved network, the timeout
void wifi_setup_tick();
