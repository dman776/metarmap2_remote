#include "wifi_setup.h"

#include <DNSServer.h>
#include <WiFi.h>

#include "log.h"
#include "settings.h"

namespace {

constexpr uint32_t kRetryMs = 30 * 1000;      // a lost network: tried again this often, while no phone is joined
constexpr uint32_t kIdleMs = 10 * 60 * 1000;  // ...and config mode closes after this long with none
constexpr const char* kAddress = "http://192.168.4.1/";
const IPAddress kApIp(192, 168, 4, 1);

Settings* settings = nullptr;
DNSServer dns;
bool active = false;
bool fresh = false;
std::string apName;
uint32_t lastPhoneAt = 0, lastTryAt = 0;
bool scanning = false;
std::vector<ScannedNetwork> networks;

void scan() {
  if (scanning) return;
  scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;  // in the background; wifi_setup_tick collects it
}

void collectScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanning = false;
  if (n < 0) return;  // failed: keep the last list
  std::vector<ScannedNetwork> found;
  for (int i = 0; i < n; i++) {
    ScannedNetwork net;
    net.ssid = WiFi.SSID(i).c_str();
    net.rssi = WiFi.RSSI(i);
    net.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    found.push_back(net);
  }
  WiFi.scanDelete();
  networks = tidyScan(found);
  LOGF("wifi setup: found %u networks\n", (unsigned)networks.size());
}

}  // namespace

void wifi_setup_begin(Settings& s) { settings = &s; }

void wifi_setup_start(SetupReason why) {
  if (active || !settings) return;
  fresh = why == SetupReason::Fresh;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  apName = setupApName(mac);

  WiFi.setAutoReconnect(false);  // a lost network is retried by hand, only while no phone is joined
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);  // the station side scans (and, for a lost network, keeps trying it)
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str());  // open: no password
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", kApIp);  // every name is the remote: a phone's "sign in to network" opens the setup page
  active = true;
  lastPhoneAt = lastTryAt = millis();
  networks.clear();
  scan();
  LOGF("wifi setup: on (%s), network %s at %s\n", fresh ? "not set up yet" : "can't join the saved Wi-Fi",
       apName.c_str(), kAddress);
}

void wifi_setup_stop() {
  if (!active || fresh) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings->wifiName.c_str(), settings->wifiPassword.c_str());
  active = false;
  LOGF("wifi setup: off; joining %s\n", settings->wifiName.c_str());
}

bool wifi_setup_active() { return active; }
bool wifi_setup_fresh() { return active && fresh; }
const std::string& wifi_setup_ap_name() { return apName; }
const char* wifi_setup_address() { return kAddress; }
int wifi_setup_phones() { return active ? WiFi.softAPgetStationNum() : 0; }

std::vector<ScannedNetwork> wifi_setup_networks(bool& busy) {
  busy = scanning;
  return networks;
}

void wifi_setup_rescan() {
  if (active) scan();
}

void wifi_setup_tick() {
  if (!active) return;
  dns.processNextRequest();
  if (scanning) collectScan();
  uint32_t now = millis();
  bool phone = WiFi.softAPgetStationNum() > 0;
  if (phone) lastPhoneAt = now;
  if (fresh) return;  // nothing to go back to

  if (WiFi.status() == WL_CONNECTED) {  // the saved network is back
    LOGF("wifi setup: joined %s again\n", settings->wifiName.c_str());
    wifi_setup_stop();
    return;
  }
  if (!phone && now - lastPhoneAt >= kIdleMs) {
    LOGF("wifi setup: no phone for 10 minutes\n");
    wifi_setup_stop();
    return;
  }
  if (!phone && !scanning && now - lastTryAt >= kRetryMs) {
    lastTryAt = now;
    WiFi.begin(settings->wifiName.c_str(), settings->wifiPassword.c_str());
  }
}
