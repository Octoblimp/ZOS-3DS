#include "net/http.h"

#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "debuglog.h"
#include "paths.h"

namespace net {

const char* const kUserAgent = "ZombieOutbreakSim-3DS/1.0 (Nintendo 3DS homebrew port of Zombie Outbreak Simulator)";

namespace {

// The CA bundle, in the build's RomFS or on the card (paths.h).
std::string s_caBundle;
const u32 kSocBufferSize = 0x100000;

LightLock s_lock;
bool s_lockReady = false;
bool s_tried = false;
bool s_ok = false;
bool s_ac = false;
bool s_sslc = false;
u32* s_socBuffer = nullptr;
bool s_haveCaBundle = false;

struct Transfer {
    std::vector<u8>* body;
    size_t maxBytes;
    bool overflowed;
    HttpClient::Progress progress;
    void* user;
};

size_t onData(char* data, size_t size, size_t count, void* userdata)
{
    Transfer* t = static_cast<Transfer*>(userdata);
    const size_t n = size * count;
    if (t->maxBytes != 0 && t->body->size() + n > t->maxBytes) {
        t->overflowed = true;
        return 0;
    }
    t->body->insert(t->body->end(), reinterpret_cast<u8*>(data), reinterpret_cast<u8*>(data) + n);
    return n;
}

int onProgress(void* userdata, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
    Transfer* t = static_cast<Transfer*>(userdata);
    if (t->progress && !t->progress(t->user, (size_t)now, (size_t)total))
        return 1;
    return 0;
}

} // namespace

bool init()
{
    // The first call comes from the main thread (GameNode, MapSelectScene)
    // before any loader thread runs, so making the lock here is safe.
    if (!s_lockReady) {
        LightLock_Init(&s_lock);
        s_lockReady = true;
    }
    LightLock_Lock(&s_lock);
    if (s_tried) {
        LightLock_Unlock(&s_lock);
        return s_ok;
    }
    s_tried = true;
    const u64 start = osGetTime();
    s_socBuffer = static_cast<u32*>(memalign(0x1000, kSocBufferSize));
    Result rc = s_socBuffer ? socInit(s_socBuffer, kSocBufferSize) : -1;
    if (R_FAILED(rc)) {
        dbg("net: socInit failed %08lx", (unsigned long)rc);
        free(s_socBuffer);
        s_socBuffer = nullptr;
        LightLock_Unlock(&s_lock);
        return false;
    }
    // mbedTLS's entropy (devkitPro's mbedtls_hardware_poll) is ssl:C's.
    rc = sslcInit(0);
    s_sslc = R_SUCCEEDED(rc);
    if (!s_sslc)
        dbg("net: sslcInit failed %08lx", (unsigned long)rc);
    s_ac = R_SUCCEEDED(acInit());
    CURLcode cc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (cc != CURLE_OK) {
        dbg("net: curl_global_init failed: %s", curl_easy_strerror(cc));
        LightLock_Unlock(&s_lock);
        return false;
    }
    struct stat st;
    s_caBundle = std::string(zosAssetRoot()) + "/data/cacert.pem";
    s_haveCaBundle = stat(s_caBundle.c_str(), &st) == 0;
    if (!s_haveCaBundle)
        dbg("net: no %s, HTTPS will fail (run tools/prepare_data.py)", s_caBundle.c_str());
    s_ok = true;
    dbg("net: up (%s) in %d ms, Wi-Fi %s", curl_version(), (int)(osGetTime() - start),
        wifiConnected() ? "connected" : "not connected");
    LightLock_Unlock(&s_lock);
    return true;
}

void exit()
{
    if (!s_lockReady)
        return;
    LightLock_Lock(&s_lock);
    if (s_ok)
        curl_global_cleanup();
    if (s_socBuffer) {
        socExit();
        free(s_socBuffer);
        s_socBuffer = nullptr;
    }
    if (s_sslc)
        sslcExit();
    if (s_ac)
        acExit();
    s_ok = s_sslc = s_ac = false;
    s_tried = false;
    LightLock_Unlock(&s_lock);
}

bool wifiConnected()
{
    if (!s_ac)
        return true; // cannot tell: let the request find out
    u32 status = 0;
    if (R_FAILED(ACU_GetWifiStatus(&status)))
        return true;
    return status != 0;
}

HttpClient::HttpClient()
{
    if (init())
        _curl = curl_easy_init();
}

HttpClient::~HttpClient()
{
    if (_curl)
        curl_easy_cleanup(static_cast<CURL*>(_curl));
}

int HttpClient::get(const std::string& url, std::vector<u8>& body, std::string& error)
{
    return perform(url, nullptr, body, error);
}

int HttpClient::post(const std::string& url, const std::string& form, std::vector<u8>& body, std::string& error)
{
    return perform(url, &form, body, error);
}

int HttpClient::perform(const std::string& url, const std::string* form, std::vector<u8>& body,
                        std::string& error)
{
    body.clear();
    error.clear();
    CURL* c = static_cast<CURL*>(_curl);
    if (!c) {
        error = "no network";
        return 0;
    }
    if (!wifiConnected()) {
        error = "not connected to the Internet";
        return 0;
    }
    Transfer t = {&body, _maxBytes, false, _progress, _progressUser};
    char curlError[CURL_ERROR_SIZE] = {};
    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, _timeout);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c, CURLOPT_CAINFO, s_caBundle.c_str());
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, curlError);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
    if (_progress) {
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
    }
    if (form) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, form->c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)form->size());
    }
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        if (t.overflowed)
            error = "the answer was too big";
        else
            error = curlError[0] ? curlError : curl_easy_strerror(rc);
        return 0;
    }
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    return (int)status;
}

std::string urlEncode(const std::string& s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~') {
            out += (char)ch;
        } else {
            out += '%';
            out += hex[ch >> 4];
            out += hex[ch & 15];
        }
    }
    return out;
}

} // namespace net
