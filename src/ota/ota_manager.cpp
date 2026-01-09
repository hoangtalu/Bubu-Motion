#include "ota_manager.h"
#include "semver.h"
#include "menu_system.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#define BUBU_FW_VERSION "1.5.4"
static const char* MANIFEST_URL =
  "https://raw.githubusercontent.com/hoangtalu/Bubu-OTA/refs/heads/main/latest.json";

static bool ran = false;
static String m_version, m_fwUrl, m_sha256;

// ---- tiny JSON string getter (same style as your old code) ----
static String jsonGetString(const String& json, const char* key) {
  String pat = String("\"") + key + "\"";
  int k = json.indexOf(pat); if (k < 0) return "";
  int c = json.indexOf(':', k); if (c < 0) return "";
  int q1 = json.indexOf('"', c + 1); if (q1 < 0) return "";
  int q2 = json.indexOf('"', q1 + 1); if (q2 < 0) return "";
  return json.substring(q1 + 1, q2);
}

static bool httpsGet(const String& url, String& body) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  Serial.printf("[OTA] GET %s\n", url.c_str());
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  MenuSystem::otaPulse(millis());
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP %d\n", code);
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  return true;
}

static bool fetchManifest() {
  String body;
  if (!httpsGet(MANIFEST_URL, body)) return false;

  m_version = jsonGetString(body, "version");
  m_fwUrl   = jsonGetString(body, "url");
  m_sha256  = jsonGetString(body, "sha256");

  if (m_version.isEmpty() || m_fwUrl.isEmpty() || m_sha256.isEmpty()) {
    Serial.println("[OTA] Manifest missing fields");
    return false;
  }

  Serial.printf("[OTA] Manifest version=%s\n", m_version.c_str());
  return true;
}

// ---- download + write + compute sha256 at the same time ----
static bool installFirmware() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  Serial.printf("[OTA] Download %s\n", m_fwUrl.c_str());
  if (!http.begin(client, m_fwUrl)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] Firmware HTTP %d\n", code);
    http.end();
    return false;
  }

  int contentLen = http.getSize();
  Serial.printf("[OTA] Content-Length=%d\n", contentLen);

  if (!Update.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN)) {
    Serial.printf("[OTA] Update.begin error: %s\n", Update.errorString());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();

  // SHA256 init
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  uint8_t buf[1024];
  int total = 0;
  uint32_t lastData = millis();

  while (http.connected()) {
    MenuSystem::otaPulse(millis());
    size_t avail = stream->available();

    if (avail) {
      int r = stream->readBytes(buf, static_cast<size_t>(min((size_t)avail, sizeof(buf))));
      if (r > 0) {
        // write to OTA partition
        if (Update.write(buf, r) != static_cast<size_t>(r)) {
          Serial.println("[OTA] Update.write failed");
          mbedtls_sha256_free(&sha);
          Update.abort();
          http.end();
          return false;
        }
        // hash the same bytes
        mbedtls_sha256_update_ret(&sha, buf, r);
        total += r;
        lastData = millis();
      }
    } else {
      // No data yet, wait a bit with timeout
      if (millis() - lastData > 10000) {
        Serial.println("[OTA] Timeout waiting for data");
        break;
      }
      MenuSystem::otaPulse(millis());
      delay(10);
    }
  }

  Serial.printf("[OTA] Total bytes written: %d\n", total);

  http.end();

  // SHA256 finish
  uint8_t hash[32];
  mbedtls_sha256_finish_ret(&sha, hash);
  mbedtls_sha256_free(&sha);

  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i*2, "%02x", hash[i]);
  hex[64] = 0;

  Serial.printf("[OTA] SHA256 computed: %s\n", hex);
  Serial.printf("[OTA] SHA256 expected: %s\n", m_sha256.c_str());

  if (!m_sha256.equalsIgnoreCase(hex)) {
    Serial.println("[OTA] SHA256 MISMATCH -> abort");
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    Serial.printf("[OTA] Update.end error: %s\n", Update.errorString());
    return false;
  }

  Serial.println("[OTA] Update OK -> reboot");
  delay(200);
  ESP.restart();
  return true;
}

namespace BubuOTA {

void begin() { ran = false; }

bool wasRollback() {
  esp_ota_img_states_t st;
  const esp_partition_t* p = esp_ota_get_running_partition();
  if (esp_ota_get_state_partition(p, &st) == ESP_OK) {
    return st == ESP_OTA_IMG_INVALID;
  }
  return false;
}

void runOnce() {
  if (ran) return;
  ran = true;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] WiFi not connected -> skip");
    return;
  }

  if (!fetchManifest()) {
    Serial.println("[OTA] Manifest fetch failed -> skip");
    return;
  }

  // Only update if remote > local
  if (semverCompare(m_version, String(BUBU_FW_VERSION)) <= 0) {
    Serial.println("[OTA] No update needed");
    return;
  }

  Serial.println("[OTA] Update available -> installing...");
  if (!installFirmware()) {
    Serial.println("[OTA] Install failed");
  }
}

void runManual() {
  // Manual trigger ignores previous runs
  ran = false;
  MenuSystem::otaSetActive(true);
  runOnce();
  MenuSystem::otaSetActive(false);
}

} // namespace
