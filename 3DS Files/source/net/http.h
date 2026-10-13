// HTTPS for OpenStreetMap: libcurl over mbedTLS, both from
// devkitPro's portlibs, on the 3DS's sockets (soc:U). The game's online maps
// came through Google's Java SDK; the port fetches OpenStreetMap's tiles and
// data itself.
//
// Certificates are checked against the CA bundle prepare_data.py copies to
// the card (data/cacert.pem, Mozilla's, from certifi). Every request names
// the port in its User-Agent, as OpenStreetMap's usage policy asks.
#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace net {

// Brings up the sockets and TLS, once; safe from any thread. False if they
// will not come up (the reason is logged), and every request then fails.
bool init();
// At exit, after every client is gone.
void exit();
// Whether the console is on a Wi-Fi network now (ac's status).
bool wifiConnected();

extern const char* const kUserAgent;

// One connection's worth: a client keeps its connection open between
// requests to the same host. A client is used by one thread at a time.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // GETs url into body. The HTTP status, or 0 when there was no answer
    // (error says why). body holds whatever came, whatever the status.
    int get(const std::string& url, std::vector<u8>& body, std::string& error);
    // POSTs form (application/x-www-form-urlencoded) to url, likewise.
    int post(const std::string& url, const std::string& form, std::vector<u8>& body, std::string& error);

    // A cap on the body, beyond which the request fails (0: none).
    void setMaxBytes(size_t bytes) { _maxBytes = bytes; }
    // Seconds the whole request may take (0: none).
    void setTimeout(long seconds) { _timeout = seconds; }

    // Called during a transfer with the bytes so far and the total (0 if not
    // known); returning false aborts it. From the requesting thread.
    typedef bool (*Progress)(void* user, size_t now, size_t total);
    void setProgress(Progress fn, void* user)
    {
        _progress = fn;
        _progressUser = user;
    }

private:
    int perform(const std::string& url, const std::string* form, std::vector<u8>& body, std::string& error);

    void* _curl = nullptr;
    size_t _maxBytes = 0;
    long _timeout = 30;
    Progress _progress = nullptr;
    void* _progressUser = nullptr;
};

// %-escapes s for a URL's query or a form.
std::string urlEncode(const std::string& s);

} // namespace net
