#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "epd2_stream.h"
#include "epd_7in5b_v2.h"

constexpr uint32_t WIFI_TIMEOUT_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t SETUP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr uint64_t UPDATE_INTERVAL_US = 60ULL * 60ULL * 1000000ULL;
constexpr size_t SETUP_PASSWORD_DIGITS = 8;
constexpr uint32_t TEST_WIFI_TIMEOUT_MS = 18000;
constexpr uint32_t TEST_TIME_SYNC_TIMEOUT_MS = 12000;
constexpr uint32_t TEST_HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t TEST_TOTAL_BUDGET_MS =
    TEST_WIFI_TIMEOUT_MS + TEST_TIME_SYNC_TIMEOUT_MS + TEST_HTTP_TIMEOUT_MS;
constexpr uint8_t BOOT_BUTTON_GPIO = 0;
constexpr uint8_t STATUS_LED_GPIO = 2;
constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;
constexpr uint32_t STARTUP_SETTINGS_DISPLAY_MS = 60UL * 1000UL;
constexpr uint32_t RESET_LED_BLINK_INTERVAL_MS = 100;
constexpr uint32_t SERVER_RETRY_DELAY_MS = 60UL * 1000UL;
constexpr uint8_t MAX_SERVER_ATTEMPTS = 10;
constexpr size_t MAX_API_BASE_URL_LENGTH = 256;
constexpr size_t MAX_API_KEY_LENGTH = 256;
constexpr size_t MAX_METADATA_LENGTH = 2048;

enum class State : uint8_t
{
  WifiConnecting,
  WifiSetup,
  NormalOperation,
  Shutdown,
};

DNSServer dnsServer;
WebServer webServer(80);
Preferences preferences;
Epd7in5bV2 display;

State state = State::WifiConnecting;
uint32_t stateStartedMs = 0;
bool hardwareAvailable = false;
String setupSsid;
String setupPassword;
String testedWifiSsid;
String testedWifiPassword;
String testedApiBaseUrl;
String testedApiKey;
String successfulTestToken;
RTC_DATA_ATTR char lastDisplayedImageHash[65];

struct MetadataResponse
{
  char data[MAX_METADATA_LENGTH + 1];
  size_t length;
  bool overflow;
};

enum class ServerFailure : uint8_t
{
  None,
  Unreachable,
  NotFound,
  ApiKeyRejected,
  Other,
};

const char PORTAL_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Display setup</title>
  <style>
    body{font:16px system-ui,sans-serif;max-width:38rem;margin:2rem auto;padding:0 1rem;color:#17212b}
    label{display:block;font-weight:600;margin-top:1rem}
    input,select,button{box-sizing:border-box;font:inherit;padding:.65rem;width:100%;margin-top:.3rem}
    button{cursor:pointer;margin-top:1rem}
    #save{background:#176b3a;color:white}
    #status{min-height:1.5em;margin-top:1rem}
    .hint{color:#465766;font-size:.92rem}
  </style>
</head>
<body>
  <h1>Display setup</h1>
  <form id="settings">
    <label for="ssid">Wi-Fi network</label>
    <select id="ssid" name="ssid" required>
      <option value="">Scan for networks</option>
    </select>
    <button id="scan" type="button">Scan again</button>

    <label for="wifiPassword">Wi-Fi password</label>
    <input id="wifiPassword" name="wifiPassword" type="password"
           autocomplete="new-password" value="" maxlength="64">

    <label for="apiUrl">Image server base URL (HTTP or HTTPS)</label>
    <input id="apiUrl" name="apiUrl" type="url" required autocomplete="url">

    <label for="apiKey">API key</label>
    <input id="apiKey" name="apiKey" type="text" required
           autocomplete="off" value="" maxlength="256">
    <p id="httpWarning" class="hint" hidden>
      Warning: HTTP sends the API key without encryption. Use HTTPS on
      untrusted networks.
    </p>

    <button id="test" type="button">Test Wi-Fi and image status</button>
    <p id="status" role="status" aria-live="polite"></p>
    <button id="save" type="button" hidden>Save settings</button>
  </form>
  <p id="setupHint" class="hint">The settings are stored only after both tests succeed.
  Password fields are never prefilled when this page is loaded.</p>
  <script>
    const form=document.querySelector('#settings');
    const ssid=document.querySelector('#ssid');
    const status=document.querySelector('#status');
    const save=document.querySelector('#save');
    const setupHint=document.querySelector('#setupHint');
    const apiUrl=document.querySelector('#apiUrl');
    const httpWarning=document.querySelector('#httpWarning');
    let testToken='';
    function invalidateTest(){testToken='';save.hidden=true;}
    function updateTransportWarning(){
      httpWarning.hidden=!apiUrl.value.trim().toLowerCase().startsWith('http://');
    }
    async function scanNetworks(){
      status.textContent='Scanning for Wi-Fi networks…';
      try{
        const response=await fetch('/networks');
        const data=await response.json();
        if(!response.ok) throw new Error(data.message||'Scan failed');
        const selected=ssid.value;
        ssid.replaceChildren(new Option('Select a network',''));
        for(const network of data.networks){
          const option=new Option(
            network.ssid+(network.secure?' (secured)':' (open)')+' · '+network.rssi+' dBm',
            network.ssid
          );
          option.selected=network.ssid===selected;
          ssid.add(option);
        }
        status.textContent=data.networks.length?'Select your network.':'No networks found; scan again.';
      }catch(error){status.textContent='Could not scan for networks.';}
    }
    document.querySelector('#scan').addEventListener('click',scanNetworks);
    apiUrl.addEventListener('input',updateTransportWarning);
    apiUrl.addEventListener('change',updateTransportWarning);
    window.addEventListener('pageshow',()=>{
      document.querySelector('#wifiPassword').value='';
      document.querySelector('#apiKey').value='';
      updateTransportWarning();
    });
    for(const field of form.querySelectorAll('input,select')){
      field.addEventListener('input',invalidateTest);
      field.addEventListener('change',invalidateTest);
    }
    document.querySelector('#test').addEventListener('click',async()=>{
      if(!form.reportValidity())return;
      invalidateTest();
      status.textContent='Testing Wi-Fi and image server…';
      try{
        const response=await fetch('/test',{method:'POST',
          headers:{'Content-Type':'application/x-www-form-urlencoded'},
          body:new URLSearchParams(new FormData(form))});
        const data=await response.json();
        status.textContent=data.message;
        if(response.ok&&data.ok){testToken=data.token;save.hidden=false;}
      }catch(error){status.textContent='Test request failed. Check the connection and try again.';}
    });
    save.addEventListener('click',async()=>{
      if(!testToken)return;
      const body=new URLSearchParams(new FormData(form));
      body.append('token',testToken);
      save.disabled=true;
      status.textContent='Saving settings…';
      try{
        const response=await fetch('/save',{method:'POST',
          headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
        const data=await response.json();
        if(response.ok&&data.ok){
          setupHint.textContent=data.message;
          status.textContent='';
          form.hidden=true;
        }else{
          status.textContent=data.message||'Could not save settings.';
          save.disabled=false;
        }
      }catch(error){status.textContent='Could not save settings.';save.disabled=false;}
    });
    scanNetworks();
  </script>
</body>
</html>
)HTML";

static String makeSetupSsid()
{
  uint32_t n = esp_random() % 10000;
  char digits[5];
  snprintf(digits, sizeof(digits), "%04u", static_cast<unsigned>(n));
  return String("DISPLAY_") + digits;
}

static String makeSetupPassword()
{
  char digits[SETUP_PASSWORD_DIGITS + 1];
  for (size_t i = 0; i < SETUP_PASSWORD_DIGITS; ++i)
  {
    digits[i] = '0' + static_cast<char>(esp_random() % 10);
  }
  digits[SETUP_PASSWORD_DIGITS] = '\0';
  return String(digits);
}

static bool hasSavedConfiguration()
{
  if (!preferences.begin("wifi", true))
    return false;
  const bool configured = preferences.getBool("configured", false) &&
                          preferences.getString("ssid", "").length() > 0 &&
                          preferences.getString("api_url", "").length() > 0 &&
                          preferences.getString("api_key", "").length() > 0;
  preferences.end();
  return configured;
}

static bool waitForFactoryResetButton(uint32_t windowMs)
{
  pinMode(BOOT_BUTTON_GPIO, INPUT_PULLUP);
  digitalWrite(STATUS_LED_GPIO, LOW);
  bool buttonPressed = false;
  bool ledOn = false;
  uint32_t buttonPressedAt = 0;
  uint32_t nextLedToggleAt = millis();
  const uint32_t started = millis();
  while (millis() - started < windowMs)
  {
    const uint32_t now = millis();
    if (digitalRead(BOOT_BUTTON_GPIO) == LOW)
    {
      if (!buttonPressed)
      {
        buttonPressed = true;
        buttonPressedAt = now;
        nextLedToggleAt = now;
      }
      else if (now - buttonPressedAt >= FACTORY_RESET_HOLD_MS)
      {
        digitalWrite(STATUS_LED_GPIO, LOW);
        return true;
      }
      if (now - nextLedToggleAt >= RESET_LED_BLINK_INTERVAL_MS)
      {
        ledOn = !ledOn;
        digitalWrite(STATUS_LED_GPIO, ledOn ? HIGH : LOW);
        nextLedToggleAt = now;
      }
    }
    else
    {
      buttonPressed = false;
      ledOn = false;
      digitalWrite(STATUS_LED_GPIO, LOW);
    }
    delay(20);
  }
  digitalWrite(STATUS_LED_GPIO, LOW);
  return false;
}

static bool resetSavedSettings()
{
  if (!preferences.begin("wifi", false))
  {
    Serial.println("Factory reset failed: could not open settings storage.");
    return false;
  }
  const bool cleared = preferences.clear();
  preferences.end();
  if (!cleared)
  {
    Serial.println("Factory reset failed: could not erase settings.");
    return false;
  }
  memset(lastDisplayedImageHash, 0, sizeof(lastDisplayedImageHash));
  Serial.println("Wi-Fi and API settings erased; entering setup.");
  return true;
}

static String maskApiKey(const String &apiKey)
{
  String masked;
  masked.reserve(apiKey.length());
  for (size_t i = 0; i < apiKey.length(); ++i)
  {
    masked += i < 4 ? apiKey[i] : '*';
  }
  return masked;
}

static void clearLegacyImageHash()
{
  if (!preferences.begin("wifi", false))
  {
    Serial.println("Could not check for a legacy image hash in NVS.");
    return;
  }
  if (preferences.isKey("image_hash") && !preferences.remove("image_hash"))
  {
    Serial.println("Could not remove the legacy image hash from NVS.");
  }
  preferences.end();
}

static void prepareImageHashCache()
{
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP)
  {
    memset(lastDisplayedImageHash, 0, sizeof(lastDisplayedImageHash));
    return;
  }

  lastDisplayedImageHash[64] = '\0';
  for (size_t i = 0; i < 64; ++i)
  {
    if (!isxdigit(static_cast<unsigned char>(lastDisplayedImageHash[i])))
    {
      memset(lastDisplayedImageHash, 0, sizeof(lastDisplayedImageHash));
      return;
    }
  }
}

static void sendPortalResult(int statusCode, bool ok, const char *message,
                             const String &token = String())
{
  JsonDocument response;
  response["ok"] = ok;
  response["message"] = message;
  if (token.length() > 0)
    response["token"] = token;
  String body;
  serializeJson(response, body);
  webServer.send(statusCode, "application/json", body);
}

static void handleNetworks()
{
  const int count = WiFi.scanNetworks();
  if (count < 0)
  {
    sendPortalResult(503, false, "Wi-Fi scan failed. Please try again.");
    return;
  }

  JsonDocument response;
  JsonArray networks = response["networks"].to<JsonArray>();
  for (int i = 0; i < count; ++i)
  {
    const String name = WiFi.SSID(i);
    if (name.length() == 0)
      continue;
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = name;
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();

  String body;
  serializeJson(response, body);
  webServer.send(200, "application/json", body);
}

static bool normalizeApiBaseUrl(String &url)
{
  url.trim();
  if (url.length() < 7 || url.length() > MAX_API_BASE_URL_LENGTH)
  {
    return false;
  }

  String scheme = url.substring(0, url.length() < 8 ? url.length() : 8);
  scheme.toLowerCase();
  const bool https = scheme.startsWith("https://");
  const bool http = scheme.startsWith("http://");
  const size_t authorityStart = https ? 8 : 7;
  if ((!https && !http) || url.length() <= authorityStart)
    return false;

  url = String(https ? "https://" : "http://") + url.substring(authorityStart);
  for (size_t i = 0; i < url.length(); ++i)
  {
    const char c = url[i];
    if (c <= 0x20 || c == 0x7F || c == '@' || c == '?' || c == '#' || c == '\\')
    {
      return false;
    }
  }
  if (url[authorityStart] == '/' || url[authorityStart] == ':')
    return false;
  while (url.endsWith("/"))
    url.remove(url.length() - 1);
  return url.length() > 8;
}

static esp_err_t metadataEventHandler(esp_http_client_event_t *event)
{
  if (event->event_id != HTTP_EVENT_ON_DATA)
    return ESP_OK;

  auto *body = static_cast<MetadataResponse *>(event->user_data);
  if (event->data_len < 0 ||
      static_cast<size_t>(event->data_len) > MAX_METADATA_LENGTH - body->length)
  {
    body->overflow = true;
    return ESP_FAIL;
  }
  memcpy(body->data + body->length, event->data, event->data_len);
  body->length += event->data_len;
  body->data[body->length] = '\0';
  return ESP_OK;
}

static bool validMetadata(const MetadataResponse &response, String *imageHash = nullptr)
{
  if (response.overflow || response.length == 0)
    return false;

  JsonDocument metadata;
  if (deserializeJson(metadata, response.data, response.length))
    return false;
  if (metadata["width"].as<uint16_t>() != EPD2_WIDTH ||
      metadata["height"].as<uint16_t>() != EPD2_HEIGHT ||
      metadata["size"].as<uint32_t>() != EPD2_IMAGE_BYTES ||
      strcmp(metadata["format"] | "", "EPD2") != 0)
  {
    return false;
  }

  const char *hash = metadata["hash"] | "";
  if (strlen(hash) != 64)
    return false;
  for (size_t i = 0; i < 64; ++i)
  {
    if (!isxdigit(static_cast<unsigned char>(hash[i])))
      return false;
  }
  if (imageHash != nullptr)
  {
    *imageHash = hash;
    imageHash->toLowerCase();
  }
  return true;
}

static bool waitForSystemTime()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t started = millis();
  while (millis() - started < TEST_TIME_SYNC_TIMEOUT_MS)
  {
    if (time(nullptr) >= 1735689600)
      return true;
    delay(250);
  }
  return false;
}

static bool fetchCurrentImageHash(const String &apiBaseUrl, const String &apiKey,
                                  String &imageHash, String &failureReason,
                                  ServerFailure *failureClass = nullptr)
{
  if (failureClass != nullptr)
    *failureClass = ServerFailure::Other;
  const String endpoint = apiBaseUrl + "/api/display/current";
  MetadataResponse response{};
  esp_http_client_config_t config{};
  config.url = endpoint.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = TEST_HTTP_TIMEOUT_MS;
  config.disable_auto_redirect = true;
  config.event_handler = metadataEventHandler;
  config.user_data = &response;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not initialize the image-server request.";
    return false;
  }

  esp_err_t result = esp_http_client_set_header(client, "X-API-Key", apiKey.c_str());
  if (result == ESP_OK)
    result = esp_http_client_perform(client);
  const int statusCode = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (statusCode == 403)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::ApiKeyRejected;
    failureReason = "The image server rejected the API key (403).";
    return false;
  }
  if (statusCode == 404)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::NotFound;
    failureReason = "The image server endpoint returned HTTP 404.";
    return false;
  }
  if (statusCode <= 0 || statusCode >= 500)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "The image server could not be reached or returned a server error.";
    return false;
  }
  if (result != ESP_OK)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = apiBaseUrl.startsWith("https://")
                        ? "HTTPS request failed. Check the URL, certificate, and network."
                        : "HTTP request failed. Check the URL and network.";
    return false;
  }
  if (statusCode != 200)
  {
    failureReason = "The image server returned HTTP " + String(statusCode) + ".";
    return false;
  }
  if (!validMetadata(response, &imageHash))
  {
    failureReason = "The server responded, but its image metadata is not valid EPD2.";
    return false;
  }
  if (failureClass != nullptr)
    *failureClass = ServerFailure::None;
  return true;
}

static bool testImageServer(const String &apiBaseUrl, const String &apiKey,
                            String &failureReason)
{
  String imageHash;
  return fetchCurrentImageHash(apiBaseUrl, apiKey, imageHash, failureReason);
}

static bool downloadAndDisplayImage(const String &apiBaseUrl, const String &apiKey,
                                    const String &expectedHash, String &failureReason,
                                    ServerFailure *failureClass = nullptr)
{
  if (failureClass != nullptr)
    *failureClass = ServerFailure::Other;
  const String endpoint = apiBaseUrl + "/api/display/image";
  esp_http_client_config_t config{};
  config.url = endpoint.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = TEST_HTTP_TIMEOUT_MS;
  config.disable_auto_redirect = true;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not initialize the EPD2 image download.";
    return false;
  }

  esp_err_t result = esp_http_client_set_header(client, "X-API-Key", apiKey.c_str());
  if (result == ESP_OK)
    result = esp_http_client_open(client, 0);
  if (result != ESP_OK)
  {
    esp_http_client_cleanup(client);
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not open the EPD2 image download.";
    return false;
  }

  const int64_t contentLength = esp_http_client_fetch_headers(client);
  const int statusCode = esp_http_client_get_status_code(client);
  if (statusCode != 200)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (statusCode == 403 && failureClass != nullptr)
    {
      *failureClass = ServerFailure::ApiKeyRejected;
    }
    else if (statusCode == 404 && failureClass != nullptr)
    {
      *failureClass = ServerFailure::NotFound;
    }
    else if ((statusCode <= 0 || statusCode >= 500) &&
             failureClass != nullptr)
    {
      *failureClass = ServerFailure::Unreachable;
    }
    failureReason = statusCode == 403
                        ? "The image server rejected the API key (403)."
                        : "The image server returned HTTP " + String(statusCode) + ".";
    return false;
  }
  if (contentLength >= 0 &&
      static_cast<uint64_t>(contentLength) != EPD2_IMAGE_BYTES)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    failureReason = "The EPD2 image has an unexpected Content-Length.";
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool transferOk = mbedtls_sha256_starts(&sha, 0) == 0;
  uint8_t buffer[EPD2_TRANSFER_BUFFER_SIZE];
  size_t received = 0;
  while (transferOk && received < EPD2_IMAGE_BYTES)
  {
    const size_t remaining = EPD2_IMAGE_BYTES - received;
    const int requested = static_cast<int>(
        remaining < sizeof(buffer) ? remaining : sizeof(buffer));
    const int count = esp_http_client_read(
        client, reinterpret_cast<char *>(buffer), requested);
    if (count <= 0)
    {
      transferOk = false;
      if (failureClass != nullptr)
        *failureClass = ServerFailure::Unreachable;
      failureReason = "The EPD2 image download ended before all bytes arrived.";
      break;
    }

    if (mbedtls_sha256_update(&sha, buffer, static_cast<size_t>(count)) != 0)
    {
      transferOk = false;
      failureReason = "Could not verify the EPD2 image hash.";
      break;
    }
    for (int i = 0; i < count; ++i)
    {
      buffer[i] = reverseByteBits(buffer[i]);
    }

    const PlaneChunkSplit split =
        splitPlaneChunk(received, buffer, static_cast<size_t>(count));
    if ((split.blackLength > 0 &&
         !display.writeBlackChunk(buffer, split.blackLength, split.blackOffset)) ||
        (split.redLength > 0 &&
         !display.writeRedChunk(buffer + split.blackLength, split.redLength,
                                split.redOffset)))
    {
      transferOk = false;
      failureReason = "Could not stream image data to the e-paper controller.";
      break;
    }
    received += static_cast<size_t>(count);
  }

  if (transferOk)
  {
    const int extraByte = esp_http_client_read(client, reinterpret_cast<char *>(buffer), 1);
    if (extraByte != 0)
    {
      transferOk = false;
      if (extraByte < 0 && failureClass != nullptr)
      {
        *failureClass = ServerFailure::Unreachable;
      }
      failureReason = extraByte > 0
                          ? "The EPD2 image contains more than the expected number of bytes."
                          : "The EPD2 image transfer did not end cleanly.";
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  uint8_t digest[32];
  if (transferOk && mbedtls_sha256_finish(&sha, digest) != 0)
  {
    transferOk = false;
    failureReason = "Could not verify the EPD2 image hash.";
  }
  mbedtls_sha256_free(&sha);
  if (!transferOk)
    return false;
  if (received != EPD2_IMAGE_BYTES)
  {
    failureReason = "The EPD2 image has an unexpected size.";
    return false;
  }

  char digestHex[65];
  for (size_t i = 0; i < sizeof(digest); ++i)
  {
    snprintf(digestHex + i * 2, 3, "%02x", digest[i]);
  }
  digestHex[64] = '\0';
  if (expectedHash != digestHex)
  {
    failureReason = "The EPD2 image does not match the advertised SHA-256 hash.";
    return false;
  }
  if (!display.refreshAndWait())
  {
    failureReason = "The e-paper controller did not complete its refresh.";
    return false;
  }

  expectedHash.toCharArray(lastDisplayedImageHash, sizeof(lastDisplayedImageHash));
  if (failureClass != nullptr)
    *failureClass = ServerFailure::None;
  return true;
}

static void sleepUntilNextUpdate()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (hardwareAvailable && !display.sleep())
  {
    Serial.println("E-paper sleep failed before scheduled deep sleep.");
  }
  state = State::Shutdown;
  esp_sleep_enable_timer_wakeup(UPDATE_INTERVAL_US);
  esp_deep_sleep_start();
}

static void runNormalOperation()
{
  String failureReason;
  String apiBaseUrl;
  String apiKey;
  if (!preferences.begin("wifi", true))
  {
    failureReason = "Could not read persistent settings.";
  }
  else
  {
    apiBaseUrl = preferences.getString("api_url", "");
    apiKey = preferences.getString("api_key", "");
    preferences.end();
    if (apiKey.length() == 0 || !normalizeApiBaseUrl(apiBaseUrl))
    {
      failureReason = "Saved image-server settings are invalid.";
    }
  }

  if (failureReason.length() > 0)
  {
    Serial.printf("Normal operation failed: %s\n", failureReason.c_str());
    sleepUntilNextUpdate();
    return;
  }

  bool reinitializeDisplay = false;
  for (uint8_t attempt = 1; attempt <= MAX_SERVER_ATTEMPTS; ++attempt)
  {
    if (reinitializeDisplay)
    {
      if (!hardwareAvailable || !display.begin())
      {
        failureReason = "Could not reinitialize the display before retrying.";
        Serial.printf("Normal operation failed: %s\n", failureReason.c_str());
        break;
      }
      reinitializeDisplay = false;
    }

    ServerFailure failure = ServerFailure::None;
    String currentHash;
    if (apiBaseUrl.startsWith("https://") && !waitForSystemTime())
    {
      failure = ServerFailure::Unreachable;
      failureReason = "Could not verify the server certificate because time sync failed.";
    }
    else if (!fetchCurrentImageHash(apiBaseUrl, apiKey, currentHash, failureReason,
                                    &failure) &&
             failure == ServerFailure::None)
    {
      failure = ServerFailure::Other;
    }

    if (failure == ServerFailure::None &&
        currentHash == String(lastDisplayedImageHash))
    {
      Serial.println("EPD2 image is unchanged; display refresh skipped.");
      break;
    }

    if (failure == ServerFailure::None)
    {
      if (!hardwareAvailable)
      {
        failure = ServerFailure::Other;
        failureReason = "E-paper initialization failed; image cannot be displayed.";
      }
      else
      {
        reinitializeDisplay = true;
        if (downloadAndDisplayImage(apiBaseUrl, apiKey, currentHash, failureReason,
                                    &failure))
        {
          reinitializeDisplay = false;
          Serial.println("New EPD2 image downloaded and displayed.");
          break;
        }
      }
    }

    if (failure == ServerFailure::ApiKeyRejected)
    {
      if (hardwareAvailable)
        display.showApiKeyErrorMessage();
      Serial.printf("Normal operation failed: %s\n", failureReason.c_str());
      break;
    }

    if (failure == ServerFailure::Unreachable || failure == ServerFailure::NotFound)
    {
      if (attempt < MAX_SERVER_ATTEMPTS)
      {
        Serial.printf("Image server attempt %u/%u failed; retrying in one minute.\n",
                      static_cast<unsigned>(attempt),
                      static_cast<unsigned>(MAX_SERVER_ATTEMPTS));
        delay(SERVER_RETRY_DELAY_MS);
        continue;
      }
      failureReason = "Image server unavailable or endpoint returned 404 after ten attempts.";
      if (hardwareAvailable)
        display.showServerUnavailableMessage();
      Serial.printf("Normal operation failed: %s\n", failureReason.c_str());
      break;
    }

    if (failureReason.length() == 0)
    {
      failureReason = "The image server returned invalid image data.";
    }
    Serial.printf("Normal operation failed: %s\n", failureReason.c_str());
    break;
  }
  sleepUntilNextUpdate();
}

static bool connectAndTest(String wifiSsid, String wifiPassword, String apiBaseUrl,
                           String apiKey, String &failureReason)
{
  if (wifiSsid.length() == 0 || wifiSsid.length() > 32 ||
      wifiPassword.length() > 64 || apiKey.length() == 0 ||
      apiKey.length() > MAX_API_KEY_LENGTH || apiKey.indexOf('\r') >= 0 ||
      apiKey.indexOf('\n') >= 0 || !normalizeApiBaseUrl(apiBaseUrl))
  {
    failureReason = "Please check the network, HTTP/HTTPS server URL, and API key fields.";
    return false;
  }

  const uint32_t setupElapsed = millis() - stateStartedMs;
  if (setupElapsed >= SETUP_TIMEOUT_MS ||
      SETUP_TIMEOUT_MS - setupElapsed < TEST_TOTAL_BUDGET_MS)
  {
    failureReason = "The setup window is almost over. Restart the controller to try again.";
    return false;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  const uint32_t wifiStarted = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStarted < TEST_WIFI_TIMEOUT_MS)
  {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    failureReason = "Could not connect to that Wi-Fi network.";
    return false;
  }

  if (apiBaseUrl.startsWith("https://") && !waitForSystemTime())
  {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    failureReason = "Could not verify the server certificate because time sync failed.";
    return false;
  }

  if (!testImageServer(apiBaseUrl, apiKey, failureReason))
  {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    return false;
  }

  testedWifiSsid = wifiSsid;
  testedWifiPassword = wifiPassword;
  testedApiBaseUrl = apiBaseUrl;
  testedApiKey = apiKey;
  char token[17];
  snprintf(token, sizeof(token), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  successfulTestToken = token;
  return true;
}

static void handleTest()
{
  if (!webServer.hasArg("ssid") || !webServer.hasArg("wifiPassword") ||
      !webServer.hasArg("apiUrl") || !webServer.hasArg("apiKey"))
  {
    sendPortalResult(400, false, "Please fill in all required fields.");
    return;
  }

  String failureReason;
  if (!connectAndTest(webServer.arg("ssid"), webServer.arg("wifiPassword"),
                      webServer.arg("apiUrl"), webServer.arg("apiKey"),
                      failureReason))
  {
    successfulTestToken = "";
    sendPortalResult(400, false, failureReason.c_str());
    return;
  }
  sendPortalResult(200, true, "Wi-Fi and EPD2 image status verified. You can save now.",
                   successfulTestToken);
}

static void handleSave()
{
  if (!webServer.hasArg("ssid") || !webServer.hasArg("wifiPassword") ||
      !webServer.hasArg("apiUrl") || !webServer.hasArg("apiKey") ||
      !webServer.hasArg("token"))
  {
    sendPortalResult(400, false, "Test the settings before saving.");
    return;
  }

  String apiBaseUrl = webServer.arg("apiUrl");
  const bool sameTestedSettings =
      normalizeApiBaseUrl(apiBaseUrl) &&
      webServer.arg("ssid") == testedWifiSsid &&
      webServer.arg("wifiPassword") == testedWifiPassword &&
      apiBaseUrl == testedApiBaseUrl &&
      webServer.arg("apiKey") == testedApiKey &&
      webServer.arg("token") == successfulTestToken &&
      successfulTestToken.length() > 0;
  if (!sameTestedSettings || WiFi.status() != WL_CONNECTED ||
      millis() - stateStartedMs >= SETUP_TIMEOUT_MS)
  {
    successfulTestToken = "";
    sendPortalResult(400, false, "Settings changed or the test expired. Test again.");
    return;
  }

  if (!preferences.begin("wifi", false))
  {
    sendPortalResult(500, false, "Could not open persistent settings storage.");
    return;
  }
  bool stored = !preferences.isKey("configured") || preferences.remove("configured");
  if (stored)
  {
    const bool ssidStored = preferences.putString("ssid", testedWifiSsid) > 0;
    const size_t passwordBytes = preferences.putString("password", testedWifiPassword);
    const bool passwordStored =
        passwordBytes > 0 ||
        (testedWifiPassword.length() == 0 && preferences.isKey("password") &&
         preferences.getString("password", "\x01").length() == 0);
    const bool apiUrlStored = preferences.putString("api_url", testedApiBaseUrl) > 0;
    const bool apiKeyStored = preferences.putString("api_key", testedApiKey) > 0;
    stored = ssidStored && passwordStored && apiUrlStored && apiKeyStored &&
             preferences.putBool("configured", true) > 0;
  }
  preferences.end();

  if (!stored)
  {
    successfulTestToken = "";
    sendPortalResult(500, false, "Could not save all settings. Please test and try again.");
    return;
  }

  successfulTestToken = "";
  sendPortalResult(200, true,
                   "Settings saved. The display is restarting and will fetch the image.");
  delay(1000);
  ESP.restart();
}

static void enterSetup()
{
  if (state == State::WifiSetup)
    return;
  state = State::WifiSetup;
  stateStartedMs = millis();

  WiFi.disconnect(true);
  delay(100);
  setupSsid = makeSetupSsid();
  setupPassword = makeSetupPassword();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(setupSsid.c_str(), setupPassword.c_str(), 1, false, 4);
  dnsServer.start(53, "*", WiFi.softAPIP());

  webServer.on("/", HTTP_GET, []()
               {
    webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    webServer.sendHeader("Pragma", "no-cache");
    webServer.send_P(200, "text/html; charset=utf-8", PORTAL_PAGE); });
  webServer.on("/networks", HTTP_GET, handleNetworks);
  webServer.on("/test", HTTP_POST, handleTest);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.begin();
  Serial.printf("Setup AP: %s\n", setupSsid.c_str());
  Serial.println("Setup password is intentionally not logged.");
  if (hardwareAvailable)
  {
    display.showSetupMessage(setupSsid.c_str(), setupPassword.c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(STATUS_LED_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);
  stateStartedMs = millis();
  prepareImageHashCache();
  clearLegacyImageHash();

  hardwareAvailable = display.begin();
  if (!hardwareAvailable)
  {
    Serial.println("E-paper initialization failed; setup continues without display output.");
  }

  if (!hasSavedConfiguration())
  {
    enterSetup();
  }
  else
  {
    if (!preferences.begin("wifi", true))
    {
      Serial.println("Could not read saved settings; entering setup.");
      enterSetup();
      return;
    }
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    String apiUrl = preferences.getString("api_url", "");
    String apiKey = preferences.getString("api_key", "");
    preferences.end();

    if (esp_reset_reason() != ESP_RST_DEEPSLEEP)
    {
      const String maskedApiKey = maskApiKey(apiKey);
      if (hardwareAvailable)
      {
        display.showStartupSettings(ssid.c_str(), apiUrl.c_str(),
                                    maskedApiKey.c_str());
      }
      if (waitForFactoryResetButton(STARTUP_SETTINGS_DISPLAY_MS))
      {
        resetSavedSettings();
        enterSetup();
        return;
      }
    }

    stateStartedMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
  }
}

void loop()
{
  if (state == State::WifiSetup)
  {
    dnsServer.processNextRequest();
    webServer.handleClient();

    uint32_t elapsed = millis() - stateStartedMs;
    if (elapsed >= SETUP_TIMEOUT_MS)
    {
      dnsServer.stop();
      webServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      if (hardwareAvailable)
      {
        display.showTimeoutMessage();
        if (!display.sleep())
          Serial.println("E-paper sleep failed after setup timeout.");
      }
      state = State::Shutdown;
      esp_deep_sleep_start();
      return;
    }
  }
  else if (state == State::WifiConnecting)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      state = State::NormalOperation;
    }
    else if (millis() - stateStartedMs >= WIFI_TIMEOUT_MS)
    {
      enterSetup();
    }
  }
  else if (state == State::NormalOperation)
  {
    runNormalOperation();
  }

  delay(10);
}
