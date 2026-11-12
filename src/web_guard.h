// The web server's guards against other web pages (remote_web.cpp uses them): plain C++, so they're unit tested on
// the host (test/test_web_guard).
//
//   - Every POST carries the header X-Copilot: 1. A page from another site can't add a custom header to a request
//     to the Copilot without a CORS preflight, which the Copilot never answers, so it can't make the browser POST
//     with the login it has cached (cross-site request forgery). The Copilot's own pages and tools/shoot.py send it.
//   - Every request behind the login names the Copilot in its Host: an IPv4 address (any: a page can't move an IP
//     address to itself), or its mDNS name metarmap-copilot.local. A page whose own name an attacker points at the
//     Copilot (DNS rebinding) has its name in the Host, so it's refused before the login is tried.
//   - Remote control (the /api/ui/ controls) can't be turned on or used while the web password is still the
//     default (map), which any page could try.
#pragma once

#include <string>

namespace web_guard {

constexpr const char* kHeader = "X-Copilot";       // and its value: 1
constexpr const char* kName = "metarmap-copilot";  // the mDNS name (.local) and the DHCP host name

// The X-Copilot header's value (as received, "" when missing): 1 is the only one
bool headerOk(const std::string& value);

// A Host header: an IPv4 address, a [bracketed] IPv6 address, or metarmap-copilot(.local), in any case and with a
// trailing dot, each with a :port or not. Anything else (another name, or none: browsers always send one) is refused.
bool hostOk(const std::string& host);

// Four dotted numbers 0-255, digits only ("192.168.4.1"; not "1.2.3", "1.2.3.256" or "1.2.3.4.nip.io")
bool isIpv4(const std::string& text);

// The web password is still the default, kDefaultWebPassword (settings.h)
bool isDefaultPassword(const std::string& password);

// The answers, as text: a POST without the header, a Host that isn't the Copilot's, remote control with the default
// password
extern const char* const kNoHeader;
extern const char* const kBadHost;
extern const char* const kDefaultPasswordRemote;

}  // namespace web_guard
