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
bool wantScan = false;    // a scan is asked for and hasn't answered yet: start one, again if it fails
uint32_t nextScanAt = 0;  // not before: after a failure
int scanFailures = 0;
std::vector<ScannedNetwork> networks;

constexpr int kScanTries = 6;
constexpr uint32_t kScanRetryMs = 2000;

// Starts a scan in the background (wifi_setup_tick collects it). It can refuse right after the radio changed
// mode, or while the station is busy connecting: then it's asked again a moment later.
void startScan() {
  int rc = WiFi.scanNetworks(true);
  scanning = rc == WIFI_SCAN_RUNNING;
  if (scanning) return;
  LOGF("wifi setup: scan didn't start (%d)\n", rc);
  if (++scanFailures < kScanTries)
    nextScanAt = millis() + kScanRetryMs;
  else
    wantScan = false;
}

void scan() {
  wantScan = true;
  scanFailures = 0;
  nextScanAt = millis();
}

void collectScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanning = false;
  if (n < 0) {  // failed: asked again (the last list stays meanwhile)
    LOGF("wifi setup: scan failed (%d)\n", n);
    WiFi.scanDelete();
    if (++scanFailures < kScanTries)
      nextScanAt = millis() + kScanRetryMs;
    else
      wantScan = false;
    return;
  }
  wantScan = false;
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
  scan();  // started by wifi_setup_tick, and asked again if the radio isn't ready yet
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
  busy = scanning || wantScan;
  return networks;
}

void wifi_setup_rescan() {
  if (active && !scanning) scan();
}

void wifi_setup_tick() {
  if (!active) return;
  dns.processNextRequest();
  if (scanning)
    collectScan();
  else if (wantScan && (int32_t)(millis() - nextScanAt) >= 0)
    startScan();
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
