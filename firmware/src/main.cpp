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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epd2_stream.h"
#include "epd_7in5b_v2.h"
#include "serial_log.h"

constexpr uint32_t WIFI_TIMEOUT_SECONDS = 30;
constexpr uint32_t SETUP_TIMEOUT_SECONDS = 10 * 60;
constexpr uint32_t DEFAULT_UPDATE_INTERVAL_SECONDS = 60 * 60;
constexpr uint32_t MIN_UPDATE_INTERVAL_SECONDS = 60;
constexpr uint32_t MAX_UPDATE_INTERVAL_SECONDS = 24 * 60 * 60;
constexpr uint32_t RETRIEVAL_RETRY_DELAY_SECONDS = 60;
constexpr uint32_t FAILURE_BACKOFF_SECONDS = 60 * 60;
constexpr uint32_t SECONDS_PER_MINUTE = 60;
constexpr size_t SETUP_PASSWORD_DIGITS = 8;
constexpr uint32_t TEST_WIFI_TIMEOUT_SECONDS = 18;
constexpr uint32_t TEST_TIME_SYNC_TIMEOUT_SECONDS = 12;
constexpr uint32_t TEST_HTTP_TIMEOUT_SECONDS = 15;
constexpr uint32_t TEST_TOTAL_BUDGET_SECONDS =
    TEST_WIFI_TIMEOUT_SECONDS + TEST_TIME_SYNC_TIMEOUT_SECONDS +
    TEST_HTTP_TIMEOUT_SECONDS;
constexpr uint8_t BOOT_BUTTON_GPIO = 0;
constexpr uint8_t STATUS_LED_GPIO = 2;
constexpr uint32_t SETUP_BUTTON_HOLD_SECONDS = 5;
constexpr uint32_t STARTUP_SETTINGS_DISPLAY_SECONDS = 60;
constexpr float SETUP_LED_BLINK_INTERVAL_SECONDS = 0.1f;
constexpr uint8_t MAX_RETRIEVAL_ATTEMPTS = 5;
constexpr size_t MAX_API_BASE_URL_LENGTH = 256;
constexpr size_t MAX_API_KEY_LENGTH = 256;
constexpr size_t MAX_METADATA_LENGTH = 2048;

constexpr uint32_t millisecondsFromSeconds(uint32_t seconds)
{
  return seconds * 1000UL;
}

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
uint32_t updateIntervalMinutes =
    DEFAULT_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE;
bool hardwareAvailable = false;
bool statusLedEnabled = true;
String setupSsid;
String setupPassword;
String testedWifiSsid;
String testedWifiPassword;
String testedApiBaseUrl;
String testedApiKey;
uint32_t testedUpdateIntervalMinutes =
    DEFAULT_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE;
bool testedStatusLedEnabled = true;
int64_t testedContentId = 0;
String successfulTestToken;
RTC_DATA_ATTR char lastDisplayedImageHash[65];
RTC_DATA_ATTR uint8_t consecutiveRetrievalFailures = 0;
RTC_DATA_ATTR bool retryErrorDisplayed = false;

struct MetadataResponse
{
  char data[MAX_METADATA_LENGTH + 1];
  size_t length;
  bool overflow;
};

struct DisplaySettings
{
  int64_t contentId;
  uint16_t width;
  uint16_t height;
  uint8_t colorCount;
  size_t planeBytes;
  size_t bodyBytes;
  String colors;
};

struct ImageMetadata
{
  String hash;
  String responseId;
  size_t bodyBytes;
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
    input[type=checkbox]{width:auto}
    button{cursor:pointer;margin-top:1rem}
    #save{background:#176b3a;color:white}
    #reset{background:#9b1c1c;color:white}
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
    <input id="apiKey" name="apiKey" type="password" required
           autocomplete="off" value="" maxlength="256">
    <p id="apiKeyHint" class="hint">API key is not displayed.</p>
    <label for="contentId">Content ID</label>
    <input id="contentId" name="contentId" type="text" required
           pattern="[0-9]+" maxlength="19" value="0">

    <label for="statusLed">Use LED as status indicator</label>
    <input id="statusLed" name="statusLed" type="checkbox" value="1" checked>

    <label for="updateInterval">Time until next update (1 to 1440 minutes)</label>
    <input id="updateInterval" name="updateInterval" type="number" required
           min="1" max="1440" step="1" value="60">
    <p class="hint">Short intervals can significantly increase battery consumption.</p>
    <p id="httpWarning" class="hint" hidden>
      Warning: HTTP sends the API key without encryption. Use HTTPS on
      untrusted networks.
    </p>

    <button id="test" type="button">Test Wi-Fi and image status</button>
    <p id="status" role="status" aria-live="polite"></p>
    <button id="save" type="button" hidden>Save settings</button>
  </form>
  <p id="setupHint" class="hint">Settings are stored only after both tests succeed.
  Password fields are never prefilled when this page is loaded.</p>
  <button id="reset" type="button" hidden>Erase saved settings and reset</button>
  <script>
    const form=document.querySelector('#settings');
    const ssid=document.querySelector('#ssid');
    const status=document.querySelector('#status');
    const save=document.querySelector('#save');
    const setupHint=document.querySelector('#setupHint');
    const apiUrl=document.querySelector('#apiUrl');
    const httpWarning=document.querySelector('#httpWarning');
    const wifiPassword=document.querySelector('#wifiPassword');
    const apiKey=document.querySelector('#apiKey');
    const apiKeyHint=document.querySelector('#apiKeyHint');
    const reset=document.querySelector('#reset');
    let hasSavedSettings=false;
    let savedSsid='';
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
        const selected=ssid.value||savedSsid;
        ssid.replaceChildren(new Option('Select a network',''));
        for(const network of data.networks){
          const option=new Option(
            network.ssid+(network.secure?' (secured)':' (open)')+' · '+network.rssi+' dBm',
            network.ssid
          );
          option.selected=network.ssid===selected;
          ssid.add(option);
        }
        if(savedSsid&&!Array.from(ssid.options).some(option=>option.value===savedSsid)){
          ssid.add(new Option(savedSsid+' (saved; not currently visible)',savedSsid,true,true));
        }
        status.textContent=data.networks.length?'Select your network.':'No networks found; scan again.';
      }catch(error){status.textContent='Could not scan for networks.';}
    }
    document.querySelector('#scan').addEventListener('click',scanNetworks);
    apiUrl.addEventListener('input',updateTransportWarning);
    apiUrl.addEventListener('change',updateTransportWarning);
    window.addEventListener('pageshow',()=>{
      wifiPassword.value='';
      apiKey.value='';
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
          reset.hidden=true;
        }else{
          status.textContent=data.message||'Could not save settings.';
          save.disabled=false;
        }
      }catch(error){status.textContent='Could not save settings.';save.disabled=false;}
    });
    async function loadCurrentSettings(){
      try{
        const response=await fetch('/settings');
        const data=await response.json();
        if(!response.ok)throw new Error(data.message||'Could not load settings');
        hasSavedSettings=data.configured;
        savedSsid=data.ssid||'';
        if(hasSavedSettings){
          apiUrl.value=data.apiUrl;
          document.querySelector('#contentId').value=data.contentId;
          document.querySelector('#updateInterval').value=data.updateInterval;
          document.querySelector('#statusLed').checked=data.statusLed;
          wifiPassword.placeholder='Leave blank to keep the current Wi-Fi password';
          if(data.apiKeyConfigured){
            apiKey.placeholder='Leave blank to keep the current API key';
            apiKeyHint.textContent='API key not changed unless a new key is entered.';
            apiKey.required=false;
          }else{
            apiKey.required=true;
            apiKeyHint.textContent='Enter an API key to configure the image server.';
          }
          reset.hidden=false;
        }else{
          apiKey.required=true;
        }
        await scanNetworks();
      }catch(error){
        status.textContent='Could not load current settings. Please reload this page.';
      }
    }
    reset.addEventListener('click',async()=>{
      if(!confirm('Erase all saved Wi-Fi and image-server settings?'))return;
      reset.disabled=true;
      status.textContent='Erasing saved settings…';
      try{
        const response=await fetch('/reset',{method:'POST'});
        const data=await response.json();
        if(!response.ok||!data.ok)throw new Error(data.message||'Reset failed');
        setupHint.textContent=data.message;
        status.textContent='';
        form.hidden=true;
        reset.hidden=true;
      }catch(error){
        status.textContent='Could not erase saved settings.';
        reset.disabled=false;
      }
    });
    loadCurrentSettings();
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
  {
    SerialLog::error("Could not open persistent settings while checking configuration.");
    return false;
  }
  const bool configured = preferences.getBool("configured", false) &&
                          preferences.getString("ssid", "").length() > 0 &&
                          preferences.getString("api_url", "").length() > 0 &&
                          preferences.getString("api_key", "").length() > 0 &&
                          preferences.getString("content_id", "").length() > 0;
  preferences.end();
  return configured;
}

static void setStatusLed(bool active)
{
  digitalWrite(STATUS_LED_GPIO, statusLedEnabled && active ? HIGH : LOW);
}

static bool parseUpdateIntervalMinutes(const String &value, uint32_t &minutes)
{
  if (value.length() == 0)
    return false;
  for (size_t i = 0; i < value.length(); ++i)
  {
    if (value[i] < '0' || value[i] > '9')
      return false;
  }

  const unsigned long parsed = strtoul(value.c_str(), nullptr, 10);
  if (parsed < MIN_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE ||
      parsed > MAX_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE)
  {
    return false;
  }
  minutes = static_cast<uint32_t>(parsed);
  return true;
}

static bool parseDisplaySettings(const String &contentIdValue,
                                 DisplaySettings &settings)
{
  if (contentIdValue.length() == 0)
    return false;

  uint64_t contentId = 0;
  const uint64_t maximum = static_cast<uint64_t>(INT64_MAX);
  for (size_t i = 0; i < contentIdValue.length(); ++i)
  {
    const char digit = contentIdValue[i];
    if (digit < '0' || digit > '9')
      return false;
    const uint64_t value = static_cast<uint64_t>(digit - '0');
    if (contentId > (maximum - value) / 10)
      return false;
    contentId = contentId * 10 + value;
  }

  Epd2Sizes sizes{};
  if (!calculateEpd2Sizes(EPD2_WIDTH, EPD2_HEIGHT, EPD2_COLOR_COUNT, sizes))
    return false;

  settings.contentId = static_cast<int64_t>(contentId);
  settings.width = EPD2_WIDTH;
  settings.height = EPD2_HEIGHT;
  settings.colorCount = EPD2_COLOR_COUNT;
  settings.planeBytes = sizes.planeBytes;
  settings.bodyBytes = sizes.bodyBytes;
  settings.colors = EPD2_COLORS;
  return true;
}

static String formatContentId(int64_t contentId)
{
  char value[22];
  snprintf(value, sizeof(value), "%lld", static_cast<long long>(contentId));
  return String(value);
}

static String metadataRequestBody(const DisplaySettings &settings)
{
  return "content_id=" + formatContentId(settings.contentId) +
         "&width=" + String(settings.width) +
         "&height=" + String(settings.height) +
         "&colors=" + settings.colors;
}

static bool waitForSetupButton(uint32_t windowSeconds)
{
  pinMode(BOOT_BUTTON_GPIO, INPUT_PULLUP);
  digitalWrite(STATUS_LED_GPIO, LOW);
  bool buttonPressed = false;
  bool ledOn = false;
  uint32_t buttonPressedAt = 0;
  uint32_t nextLedToggleAt = millis();
  const uint32_t started = millis();
  while (millis() - started < millisecondsFromSeconds(windowSeconds))
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
      else if (now - buttonPressedAt >=
               millisecondsFromSeconds(SETUP_BUTTON_HOLD_SECONDS))
      {
        digitalWrite(STATUS_LED_GPIO, LOW);
        return true;
      }
      if (now - nextLedToggleAt >= static_cast<uint32_t>(
                                       SETUP_LED_BLINK_INTERVAL_SECONDS * 1000))
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

static bool clearSavedSettings()
{
  if (!preferences.begin("wifi", false))
  {
    SerialLog::error("Settings reset failed: could not open settings storage.");
    return false;
  }
  const bool cleared = preferences.clear();
  preferences.end();
  if (!cleared)
  {
    SerialLog::error("Settings reset failed: could not erase settings.");
    return false;
  }
  statusLedEnabled = true;
  setStatusLed(true);
  memset(lastDisplayedImageHash, 0, sizeof(lastDisplayedImageHash));
  SerialLog::info("Wi-Fi and API settings erased.");
  return true;
}

static bool readSavedSecrets(String &wifiPassword, String &apiKey,
                             bool &configured)
{
  if (!preferences.begin("wifi", true))
  {
    return false;
  }
  configured = preferences.getBool("configured", false);
  if (configured)
  {
    wifiPassword = preferences.getString("password", "");
    apiKey = preferences.getString("api_key", "");
  }
  preferences.end();
  return true;
}

static void clearLegacyImageHash()
{
  if (!preferences.begin("wifi", false))
  {
    SerialLog::error("Could not check for a legacy image hash in NVS.");
    return;
  }
  if (preferences.isKey("image_hash") && !preferences.remove("image_hash"))
  {
    SerialLog::error("Could not remove the legacy image hash from NVS.");
  }
  preferences.end();
}

static void prepareImageHashCache()
{
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP)
  {
    memset(lastDisplayedImageHash, 0, sizeof(lastDisplayedImageHash));
    consecutiveRetrievalFailures = 0;
    retryErrorDisplayed = false;
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
  if (ok)
    SerialLog::infof("Setup portal response %d: %s", statusCode, message);
  else
    SerialLog::errorf("Setup portal response %d: %s", statusCode, message);
  JsonDocument response;
  response["ok"] = ok;
  response["message"] = message;
  if (token.length() > 0)
    response["token"] = token;
  String body;
  serializeJson(response, body);
  webServer.send(statusCode, "application/json", body);
}

static void handlePortalSettings()
{
  if (!preferences.begin("wifi", true))
  {
    sendPortalResult(500, false, "Could not read saved settings.");
    return;
  }

  const bool configured = preferences.getBool("configured", false);
  const String ssid = configured ? preferences.getString("ssid", "") : "";
  const String apiUrl = configured ? preferences.getString("api_url", "") : "";
  const String contentId = configured
                               ? preferences.getString("content_id", "0")
                               : "0";
  const uint32_t updateMinutes = configured
                                     ? preferences.getUInt(
                                           "update_minutes",
                                           DEFAULT_UPDATE_INTERVAL_SECONDS /
                                               SECONDS_PER_MINUTE)
                                     : DEFAULT_UPDATE_INTERVAL_SECONDS /
                                           SECONDS_PER_MINUTE;
  const bool useStatusLed = configured
                                ? preferences.getBool("status_led", true)
                                : true;
  const bool apiKeyConfigured =
      configured && preferences.getString("api_key", "").length() > 0;
  preferences.end();

  JsonDocument response;
  response["configured"] = configured;
  response["ssid"] = ssid;
  response["apiUrl"] = apiUrl;
  response["contentId"] = contentId;
  response["updateInterval"] = updateMinutes;
  response["statusLed"] = useStatusLed;
  response["apiKeyConfigured"] = apiKeyConfigured;
  String body;
  serializeJson(response, body);
  webServer.send(200, "application/json", body);
}

static void handleNetworks()
{
  const int count = WiFi.scanNetworks();
  if (count < 0)
  {
    SerialLog::error("Wi-Fi network scan failed.");
    sendPortalResult(503, false, "Wi-Fi scan failed. Please try again.");
    return;
  }
  SerialLog::infof("Wi-Fi network scan completed: %d network(s) found.", count);

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

static bool isSha256Hex(const char *value)
{
  if (strlen(value) != 64)
    return false;
  for (size_t i = 0; i < 64; ++i)
  {
    if (!isxdigit(static_cast<unsigned char>(value[i])))
      return false;
  }
  return true;
}

static bool validMetadata(const MetadataResponse &response,
                          const DisplaySettings &settings,
                          ImageMetadata &imageMetadata)
{
  if (response.overflow || response.length == 0)
  {
    SerialLog::error("Image metadata is empty or exceeds the supported response size.");
    return false;
  }

  JsonDocument metadata;
  const DeserializationError decodeError =
      deserializeJson(metadata, response.data, response.length);
  if (decodeError)
  {
    SerialLog::errorf("Could not decode image metadata JSON: %s.",
                      decodeError.c_str());
    return false;
  }
  if (metadata["width"].as<uint16_t>() != settings.width ||
      metadata["height"].as<uint16_t>() != settings.height ||
      metadata["size"].as<uint32_t>() != settings.bodyBytes ||
      strcmp(metadata["format"] | "", "EPD2") != 0)
  {
    SerialLog::error("Image metadata dimensions, format, or declared size are invalid.");
    return false;
  }

  const char *hash = metadata["hash"] | "";
  const char *responseId = metadata["response_id"] | "";
  if (!isSha256Hex(hash) || !isSha256Hex(responseId))
  {
    SerialLog::error("Image metadata hash or response ID is not a valid SHA-256 value.");
    return false;
  }
  imageMetadata.hash = hash;
  imageMetadata.hash.toLowerCase();
  imageMetadata.responseId = responseId;
  imageMetadata.responseId.toLowerCase();
  imageMetadata.bodyBytes = settings.bodyBytes;
  return true;
}

static bool waitForSystemTime()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t started = millis();
  while (millis() - started <
         millisecondsFromSeconds(TEST_TIME_SYNC_TIMEOUT_SECONDS))
  {
    if (time(nullptr) >= 1735689600)
      return true;
    delay(250);
  }
  return false;
}

static bool formatServerRetryTime(char *buffer, size_t bufferSize)
{
  const time_t currentTime = time(nullptr);
  if (currentTime < 1735689600)
    return false;

  const time_t retryTime =
      currentTime + static_cast<time_t>(FAILURE_BACKOFF_SECONDS);
  struct tm retryUtc{};
  if (gmtime_r(&retryTime, &retryUtc) == nullptr)
    return false;

  const int written = snprintf(
      buffer, bufferSize, "Retry: %04d-%02d-%02d %02d:%02d UTC",
      retryUtc.tm_year + 1900, retryUtc.tm_mon + 1, retryUtc.tm_mday,
      retryUtc.tm_hour, retryUtc.tm_min);
  return written > 0 && static_cast<size_t>(written) < bufferSize;
}

static void showRetryErrorMessage()
{
  if (!retryErrorDisplayed)
  {
    char retryTime[40];
    const char *retryMessage =
        formatServerRetryTime(retryTime, sizeof(retryTime))
            ? retryTime
            : "Time not loaded.";
    if (hardwareAvailable)
      display.showServerUnavailableMessage(retryMessage);
    retryErrorDisplayed = true;
  }
}

static bool fetchCurrentImageMetadata(const String &apiBaseUrl,
                                      const String &apiKey,
                                      const DisplaySettings &settings,
                                      ImageMetadata &imageMetadata,
                                      String &failureReason,
                                      ServerFailure *failureClass = nullptr)
{
  if (failureClass != nullptr)
    *failureClass = ServerFailure::Other;
  SerialLog::info("Requesting current image metadata.");
  const String endpoint = apiBaseUrl + "/api/display/current";
  const String requestBody = metadataRequestBody(settings);
  MetadataResponse response{};
  esp_http_client_config_t config{};
  config.url = endpoint.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = millisecondsFromSeconds(TEST_HTTP_TIMEOUT_SECONDS);
  config.disable_auto_redirect = true;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not initialize the image-server request.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  esp_err_t result = esp_http_client_set_header(client, "Content-Type",
                                                "application/x-www-form-urlencoded");
  if (result == ESP_OK)
    result = esp_http_client_set_header(client, "X-API-Key", apiKey.c_str());
  if (result == ESP_OK)
    result = esp_http_client_open(client, requestBody.length());
  if (result == ESP_OK)
  {
    const int written = esp_http_client_write(
        client, requestBody.c_str(), static_cast<int>(requestBody.length()));
    if (written != static_cast<int>(requestBody.length()))
      result = ESP_FAIL;
  }
  if (result != ESP_OK)
  {
    esp_http_client_cleanup(client);
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not send the image metadata request.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  const int64_t contentLength = esp_http_client_fetch_headers(client);
  const int statusCode = esp_http_client_get_status_code(client);
  SerialLog::infof("Image metadata HTTP status: %d.", statusCode);

  if (statusCode == 403)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (failureClass != nullptr)
      *failureClass = ServerFailure::ApiKeyRejected;
    failureReason = "The image server rejected the API key (403).";
    SerialLog::error(failureReason.c_str());
    return false;
  }
  if (statusCode == 404)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (failureClass != nullptr)
      *failureClass = ServerFailure::NotFound;
    failureReason = "The image server endpoint returned HTTP 404.";
    SerialLog::error(failureReason.c_str());
    return false;
  }
  if (statusCode <= 0 || statusCode >= 500)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "The image server could not be reached or returned a server error.";
    SerialLog::error(failureReason.c_str());
    return false;
  }
  if (statusCode != 200)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    failureReason = "The image server returned HTTP " + String(statusCode) + ".";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  if (contentLength > static_cast<int64_t>(MAX_METADATA_LENGTH))
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    failureReason = "The image metadata response is too large.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  while (response.length < MAX_METADATA_LENGTH)
  {
    const size_t remaining = MAX_METADATA_LENGTH - response.length;
    const int count = esp_http_client_read(
        client, response.data + response.length,
        static_cast<int>(remaining));
    if (count == 0)
      break;
    if (count < 0)
    {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      if (failureClass != nullptr)
        *failureClass = ServerFailure::Unreachable;
      failureReason = "Could not read the image metadata response.";
      SerialLog::error(failureReason.c_str());
      return false;
    }
    response.length += static_cast<size_t>(count);
  }
  response.data[response.length] = '\0';
  if (response.length == MAX_METADATA_LENGTH)
    response.overflow = true;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!validMetadata(response, settings, imageMetadata))
  {
    failureReason = "The server responded, but its image metadata is not valid EPD2.";
    return false;
  }
  SerialLog::infof("Image metadata validated; declared EPD2 body size is %u bytes.",
                   static_cast<unsigned>(imageMetadata.bodyBytes));
  if (failureClass != nullptr)
    *failureClass = ServerFailure::None;
  return true;
}

static bool testImageServer(const String &apiBaseUrl, const String &apiKey,
                            const DisplaySettings &settings,
                            String &failureReason)
{
  ImageMetadata imageMetadata;
  return fetchCurrentImageMetadata(apiBaseUrl, apiKey, settings,
                                   imageMetadata, failureReason);
}

static int readHttpBytes(esp_http_client_handle_t client, uint8_t *buffer,
                         size_t length)
{
  size_t received = 0;
  while (received < length)
  {
    const int count = esp_http_client_read(
        client, reinterpret_cast<char *>(buffer + received),
        static_cast<int>(length - received));
    if (count <= 0)
      return count;
    received += static_cast<size_t>(count);
  }
  return static_cast<int>(received);
}

static bool writeImagePlane(uint8_t plane, const uint8_t *data, size_t length,
                            size_t offset)
{
  return plane == 0 ? display.writeBlackChunk(data, length, offset)
                    : display.writeRedChunk(data, length, offset);
}

static bool downloadAndDisplayImage(const String &apiBaseUrl,
                                    const String &apiKey,
                                    const DisplaySettings &settings,
                                    const ImageMetadata &imageMetadata,
                                    String &failureReason,
                                    ServerFailure *failureClass = nullptr)
{
  if (failureClass != nullptr)
    *failureClass = ServerFailure::Other;
  SerialLog::infof("Image download started; expected EPD2 body size is %u bytes.",
                   static_cast<unsigned>(imageMetadata.bodyBytes));
  const String endpoint = apiBaseUrl + "/api/display/image";
  const String requestBody = "response_id=" + imageMetadata.responseId;
  esp_http_client_config_t config{};
  config.url = endpoint.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = millisecondsFromSeconds(TEST_HTTP_TIMEOUT_SECONDS);
  config.disable_auto_redirect = true;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
  {
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not initialize the EPD2 image download.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  esp_err_t result = esp_http_client_set_header(client, "X-API-Key", apiKey.c_str());
  if (result == ESP_OK)
    result = esp_http_client_set_header(client, "Content-Type",
                                        "application/x-www-form-urlencoded");
  if (result == ESP_OK)
    result = esp_http_client_open(client, requestBody.length());
  if (result == ESP_OK)
  {
    const int written = esp_http_client_write(
        client, requestBody.c_str(), requestBody.length());
    if (written != static_cast<int>(requestBody.length()))
      result = ESP_FAIL;
  }
  if (result != ESP_OK)
  {
    esp_http_client_cleanup(client);
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "Could not open the EPD2 image download.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  const int64_t contentLength = esp_http_client_fetch_headers(client);
  const int statusCode = esp_http_client_get_status_code(client);
  SerialLog::infof("EPD2 image HTTP status: %d.", statusCode);
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
    SerialLog::error(failureReason.c_str());
    return false;
  }
  if (contentLength >= 0 &&
      static_cast<uint64_t>(contentLength) != imageMetadata.bodyBytes)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    failureReason = "The EPD2 image has an unexpected Content-Length.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool transferOk = mbedtls_sha256_starts(&sha, 0) == 0;
  uint8_t buffer[EPD2_TRANSFER_BUFFER_SIZE];
  uint8_t header[5];
  if (transferOk && readHttpBytes(client, header, sizeof(header)) != sizeof(header))
  {
    transferOk = false;
    if (failureClass != nullptr)
      *failureClass = ServerFailure::Unreachable;
    failureReason = "The EPD2 image ended before its five-byte header arrived.";
    SerialLog::error(failureReason.c_str());
  }

  Epd2Sizes declaredSizes{};
  if (transferOk)
  {
    const uint16_t width = static_cast<uint16_t>(header[0] << 8 | header[1]);
    const uint16_t height = static_cast<uint16_t>(header[2] << 8 | header[3]);
    const uint8_t colorCount = header[4];
    transferOk = width == settings.width && height == settings.height &&
                 colorCount == settings.colorCount &&
                 calculateEpd2Sizes(width, height, colorCount, declaredSizes) &&
                 declaredSizes.planeBytes == settings.planeBytes &&
                 declaredSizes.bodyBytes == imageMetadata.bodyBytes;
    if (!transferOk)
    {
      failureReason = "The EPD2 header does not match the requested display specification.";
      SerialLog::error(failureReason.c_str());
    }
    else
    {
      SerialLog::info("EPD2 header validated; streaming image planes to the display.");
    }
  }
  if (transferOk &&
      mbedtls_sha256_update(&sha, header, sizeof(header)) != 0)
  {
    transferOk = false;
    failureReason = "Could not verify the EPD2 image hash.";
    SerialLog::error(failureReason.c_str());
  }
  else if (!transferOk && failureReason.length() == 0)
  {
    failureReason = "Could not initialize EPD2 image hash verification.";
    SerialLog::error(failureReason.c_str());
  }

  size_t received = 0;
  while (transferOk && received < imageMetadata.bodyBytes - sizeof(header))
  {
    const size_t remaining = imageMetadata.bodyBytes - sizeof(header) - received;
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
      SerialLog::error(failureReason.c_str());
      break;
    }

    if (mbedtls_sha256_update(&sha, buffer, static_cast<size_t>(count)) != 0)
    {
      transferOk = false;
      failureReason = "Could not verify the EPD2 image hash.";
      SerialLog::error(failureReason.c_str());
      break;
    }
    for (int i = 0; i < count; ++i)
    {
      buffer[i] = reverseByteBits(buffer[i]);
    }

    const PlaneChunkSplit split =
        splitPlaneChunk(received, static_cast<size_t>(count),
                        settings.planeBytes, settings.colorCount);
    if (!split.valid ||
        (split.firstLength > 0 &&
         !writeImagePlane(0, buffer, split.firstLength, split.firstOffset)) ||
        (split.secondLength > 0 &&
         !writeImagePlane(1, buffer + split.firstLength,
                          split.secondLength, split.secondOffset)))
    {
      transferOk = false;
      failureReason = "Could not stream image data to the e-paper controller.";
      SerialLog::error(failureReason.c_str());
      break;
    }
    received += static_cast<size_t>(count);
  }

  if (transferOk)
  {
    const int extraByte = esp_http_client_read(
        client, reinterpret_cast<char *>(buffer), 1);
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
      SerialLog::error(failureReason.c_str());
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  uint8_t digest[32];
  if (transferOk && mbedtls_sha256_finish(&sha, digest) != 0)
  {
    transferOk = false;
    failureReason = "Could not verify the EPD2 image hash.";
    SerialLog::error(failureReason.c_str());
  }
  mbedtls_sha256_free(&sha);
  if (!transferOk)
    return false;
  if (received != imageMetadata.bodyBytes - sizeof(header))
  {
    failureReason = "The EPD2 image has an unexpected size.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  char digestHex[65];
  for (size_t i = 0; i < sizeof(digest); ++i)
  {
    snprintf(digestHex + i * 2, 3, "%02x", digest[i]);
  }
  digestHex[64] = '\0';
  if (imageMetadata.hash != digestHex)
  {
    failureReason = "The EPD2 image does not match the advertised SHA-256 hash.";
    SerialLog::error(failureReason.c_str());
    return false;
  }
  SerialLog::infof("Image download completed and SHA-256 verified (%u payload bytes).",
                   static_cast<unsigned>(received));
  SerialLog::info("Refreshing the e-paper display.");
  if (!display.refreshAndWait())
  {
    failureReason = "The e-paper controller did not complete its refresh.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  imageMetadata.hash.toCharArray(lastDisplayedImageHash,
                                 sizeof(lastDisplayedImageHash));
  SerialLog::info("Display refresh completed; image hash cache updated.");
  if (failureClass != nullptr)
    *failureClass = ServerFailure::None;
  return true;
}

static void sleepForSeconds(uint32_t seconds)
{
  SerialLog::info("Preparing display and Wi-Fi for deep sleep.");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (hardwareAvailable && !display.sleep())
  {
    SerialLog::error("E-paper sleep failed before scheduled deep sleep.");
  }
  setStatusLed(false);
  state = State::Shutdown;
  const uint64_t updateIntervalUs =
      static_cast<uint64_t>(seconds) * 1000000ULL;
  SerialLog::infof("Entering deep sleep for %lu second(s).",
                   static_cast<unsigned long>(seconds));
  esp_sleep_enable_timer_wakeup(updateIntervalUs);
  esp_deep_sleep_start();
}

static void sleepUntilNextUpdate()
{
  sleepForSeconds(updateIntervalMinutes * SECONDS_PER_MINUTE);
}

static void scheduleRetrievalRetry(const String &failureReason)
{
  const char *message = failureReason.length() == 0
                            ? "Image retrieval failed."
                            : failureReason.c_str();
  SerialLog::error(message);
  if (consecutiveRetrievalFailures < MAX_RETRIEVAL_ATTEMPTS)
    ++consecutiveRetrievalFailures;

  if (consecutiveRetrievalFailures < MAX_RETRIEVAL_ATTEMPTS)
  {
    SerialLog::warnf("Image retrieval attempt %u/%u failed; retrying in one minute.",
                     static_cast<unsigned>(consecutiveRetrievalFailures),
                     static_cast<unsigned>(MAX_RETRIEVAL_ATTEMPTS));
    sleepForSeconds(RETRIEVAL_RETRY_DELAY_SECONDS);
    return;
  }

  showRetryErrorMessage();
  consecutiveRetrievalFailures = 0;
  sleepForSeconds(FAILURE_BACKOFF_SECONDS);
}

static void runNormalOperation()
{
  String failureReason;
  String apiBaseUrl;
  String apiKey;
  DisplaySettings settings{};
  if (!preferences.begin("wifi", true))
  {
    failureReason = "Could not read persistent settings.";
  }
  else
  {
    apiBaseUrl = preferences.getString("api_url", "");
    apiKey = preferences.getString("api_key", "");
    const String contentId = preferences.getString("content_id", "");
    preferences.end();
    if (apiKey.length() == 0 || !normalizeApiBaseUrl(apiBaseUrl) ||
        !parseDisplaySettings(contentId, settings))
    {
      failureReason = "Saved image-server or display specification settings are invalid.";
    }
  }

  if (failureReason.length() > 0)
  {
    scheduleRetrievalRetry(failureReason);
    return;
  }

  if (apiBaseUrl.startsWith("https://") && !waitForSystemTime())
  {
    failureReason = "Could not verify the server certificate because time sync failed.";
    scheduleRetrievalRetry(failureReason);
    return;
  }

  ServerFailure failure = ServerFailure::None;
  ImageMetadata imageMetadata;
  if (!fetchCurrentImageMetadata(apiBaseUrl, apiKey, settings,
                                 imageMetadata, failureReason, &failure))
  {
    scheduleRetrievalRetry(failureReason);
    return;
  }

  if (imageMetadata.hash == String(lastDisplayedImageHash) &&
      !retryErrorDisplayed)
  {
    SerialLog::info("EPD2 image is unchanged; display refresh skipped.");
    consecutiveRetrievalFailures = 0;
    sleepUntilNextUpdate();
    return;
  }

  if (!hardwareAvailable)
  {
    failureReason = "E-paper initialization failed; image cannot be displayed.";
    scheduleRetrievalRetry(failureReason);
    return;
  }

  if (!downloadAndDisplayImage(apiBaseUrl, apiKey, settings,
                               imageMetadata, failureReason, &failure))
  {
    scheduleRetrievalRetry(failureReason);
    return;
  }

  retryErrorDisplayed = false;
  consecutiveRetrievalFailures = 0;
  SerialLog::info("New EPD2 image downloaded and displayed successfully.");
  sleepUntilNextUpdate();
}

static bool connectAndTest(String wifiSsid, String wifiPassword, String apiBaseUrl,
                           String apiKey, uint32_t updateMinutes,
                           bool useStatusLed,
                           const DisplaySettings &settings,
                           String &failureReason)
{
  if (wifiSsid.length() == 0 || wifiSsid.length() > 32 ||
      wifiPassword.length() > 64 || apiKey.length() == 0 ||
      apiKey.length() > MAX_API_KEY_LENGTH || apiKey.indexOf('\r') >= 0 ||
      apiKey.indexOf('\n') >= 0 || !normalizeApiBaseUrl(apiBaseUrl) ||
      updateMinutes < MIN_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE ||
      updateMinutes > MAX_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE)
  {
    failureReason = "Please check the network, server, API key, and update interval fields.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  const uint32_t setupElapsed = millis() - stateStartedMs;
  const uint32_t setupTimeoutMs =
      millisecondsFromSeconds(SETUP_TIMEOUT_SECONDS);
  if (setupElapsed >= setupTimeoutMs ||
      setupTimeoutMs - setupElapsed <
          millisecondsFromSeconds(TEST_TOTAL_BUDGET_SECONDS))
  {
    failureReason = "The setup window is almost over. Restart the controller to try again.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  SerialLog::info("Testing connection to the selected Wi-Fi network.");
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  const uint32_t wifiStarted = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStarted <
             millisecondsFromSeconds(TEST_WIFI_TIMEOUT_SECONDS))
  {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    const wl_status_t wifiStatus = WiFi.status();
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    failureReason = "Could not connect to that Wi-Fi network.";
    SerialLog::errorf("%s (Wi-Fi status %d).", failureReason.c_str(),
                      static_cast<int>(wifiStatus));
    return false;
  }
  SerialLog::info("Wi-Fi test connection established.");

  if (apiBaseUrl.startsWith("https://") && !waitForSystemTime())
  {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    failureReason = "Could not verify the server certificate because time sync failed.";
    SerialLog::error(failureReason.c_str());
    return false;
  }

  if (!testImageServer(apiBaseUrl, apiKey, settings, failureReason))
  {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    return false;
  }
  SerialLog::info("Wi-Fi and EPD2 metadata tests succeeded.");

  testedWifiSsid = wifiSsid;
  testedWifiPassword = wifiPassword;
  testedApiBaseUrl = apiBaseUrl;
  testedApiKey = apiKey;
  testedUpdateIntervalMinutes = updateMinutes;
  testedStatusLedEnabled = useStatusLed;
  testedContentId = settings.contentId;
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
      !webServer.hasArg("apiUrl") || !webServer.hasArg("apiKey") ||
      !webServer.hasArg("updateInterval") || !webServer.hasArg("contentId"))
  {
    sendPortalResult(400, false, "Please fill in all required fields.");
    return;
  }

  DisplaySettings settings{};
  if (!parseDisplaySettings(webServer.arg("contentId"), settings))
  {
    successfulTestToken = "";
    sendPortalResult(400, false,
                     "Content ID must be a non-negative integer in the supported 64-bit range.");
    return;
  }

  uint32_t updateMinutes = 0;
  if (!parseUpdateIntervalMinutes(webServer.arg("updateInterval"), updateMinutes))
  {
    successfulTestToken = "";
    sendPortalResult(400, false, "Update interval must be between 1 and 1440 minutes.");
    return;
  }

  String wifiPassword = webServer.arg("wifiPassword");
  String apiKey = webServer.arg("apiKey");
  String savedWifiPassword;
  String savedApiKey;
  bool savedConfigured = false;
  if (!readSavedSecrets(savedWifiPassword, savedApiKey, savedConfigured))
  {
    sendPortalResult(500, false, "Could not read saved credentials.");
    return;
  }
  if (savedConfigured)
  {
    if (wifiPassword.length() == 0)
      wifiPassword = savedWifiPassword;
    if (apiKey.length() == 0)
      apiKey = savedApiKey;
  }

  String failureReason;
  if (!connectAndTest(webServer.arg("ssid"), wifiPassword,
                      webServer.arg("apiUrl"), apiKey,
                      updateMinutes, webServer.hasArg("statusLed"), settings,
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
      !webServer.hasArg("updateInterval") || !webServer.hasArg("contentId") ||
      !webServer.hasArg("token"))
  {
    sendPortalResult(400, false, "Test the settings before saving.");
    return;
  }

  String wifiPassword = webServer.arg("wifiPassword");
  String apiKey = webServer.arg("apiKey");
  String savedWifiPassword;
  String savedApiKey;
  bool savedConfigured = false;
  if (!readSavedSecrets(savedWifiPassword, savedApiKey, savedConfigured))
  {
    sendPortalResult(500, false, "Could not read saved credentials.");
    return;
  }
  if (savedConfigured)
  {
    if (wifiPassword.length() == 0)
      wifiPassword = savedWifiPassword;
    if (apiKey.length() == 0)
      apiKey = savedApiKey;
  }

  String apiBaseUrl = webServer.arg("apiUrl");
  uint32_t updateMinutes = 0;
  DisplaySettings settings{};
  const bool sameTestedSettings =
      parseUpdateIntervalMinutes(webServer.arg("updateInterval"), updateMinutes) &&
      parseDisplaySettings(webServer.arg("contentId"), settings) &&
      normalizeApiBaseUrl(apiBaseUrl) &&
      webServer.arg("ssid") == testedWifiSsid &&
      wifiPassword == testedWifiPassword &&
      apiBaseUrl == testedApiBaseUrl &&
      apiKey == testedApiKey &&
      updateMinutes == testedUpdateIntervalMinutes &&
      webServer.hasArg("statusLed") == testedStatusLedEnabled &&
      settings.contentId == testedContentId &&
      webServer.arg("token") == successfulTestToken &&
      successfulTestToken.length() > 0;
  if (!sameTestedSettings || WiFi.status() != WL_CONNECTED ||
      millis() - stateStartedMs >=
          millisecondsFromSeconds(SETUP_TIMEOUT_SECONDS))
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
    const bool contentIdStored =
        preferences.putString("content_id", formatContentId(testedContentId)) > 0;
    const bool updateIntervalStored =
        preferences.putUInt("update_minutes", testedUpdateIntervalMinutes) > 0;
    const bool statusLedStored =
        preferences.putBool("status_led", testedStatusLedEnabled) > 0;
    stored = ssidStored && passwordStored && apiUrlStored && apiKeyStored &&
             contentIdStored && updateIntervalStored && statusLedStored &&
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

static void handleReset()
{
  if (!clearSavedSettings())
  {
    sendPortalResult(500, false, "Could not erase saved settings.");
    return;
  }

  sendPortalResult(200, true,
                   "Saved settings erased. The display is restarting in setup mode.");
  delay(1000);
  ESP.restart();
}

static void enterSetup()
{
  if (state == State::WifiSetup)
    return;
  state = State::WifiSetup;
  stateStartedMs = millis();
  setStatusLed(true);

  WiFi.disconnect(true);
  delay(100);
  setupSsid = makeSetupSsid();
  setupPassword = makeSetupPassword();
  WiFi.mode(WIFI_AP);
  const bool accessPointStarted =
      WiFi.softAP(setupSsid.c_str(), setupPassword.c_str(), 1, false, 4);
  const bool dnsStarted =
      accessPointStarted && dnsServer.start(53, "*", WiFi.softAPIP());

  webServer.on("/", HTTP_GET, []()
               {
    webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    webServer.sendHeader("Pragma", "no-cache");
    webServer.send_P(200, "text/html; charset=utf-8", PORTAL_PAGE); });
  webServer.on("/settings", HTTP_GET, handlePortalSettings);
  webServer.on("/networks", HTTP_GET, handleNetworks);
  webServer.on("/test", HTTP_POST, handleTest);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/reset", HTTP_POST, handleReset);
  webServer.begin();
  if (accessPointStarted)
  {
    SerialLog::infof("Setup access point started at %s; credentials are not logged.",
                     WiFi.softAPIP().toString().c_str());
    if (dnsStarted)
      SerialLog::info("Setup DNS redirection started.");
    else
      SerialLog::error("Setup DNS redirection failed to start.");
    SerialLog::info("Setup HTTP portal started.");
  }
  else
  {
    SerialLog::error("Setup access point failed to start.");
  }
  if (hardwareAvailable)
  {
    SerialLog::info("Displaying setup menu.");
    display.showSetupMessage(setupSsid.c_str(), setupPassword.c_str());
  }
}

void setup()
{
  SerialLog::begin(115200);
  SerialLog::info("Firmware startup.");
  SerialLog::infof("Reset reason: %d.", static_cast<int>(esp_reset_reason()));
  pinMode(STATUS_LED_GPIO, OUTPUT);
  if (preferences.begin("wifi", true))
  {
    statusLedEnabled = preferences.getBool("status_led", true);
    preferences.end();
  }
  setStatusLed(true);
  stateStartedMs = millis();
  prepareImageHashCache();
  clearLegacyImageHash();

  SerialLog::info("Initializing e-paper display.");
  hardwareAvailable = display.begin();
  if (!hardwareAvailable)
  {
    SerialLog::error("E-paper initialization failed; setup continues without display output.");
  }
  else
  {
    SerialLog::info("E-paper initialization completed.");
  }

  if (!hasSavedConfiguration())
  {
    SerialLog::info("No saved configuration found; entering setup mode.");
    enterSetup();
  }
  else
  {
    if (!preferences.begin("wifi", true))
    {
      SerialLog::error("Could not read saved settings; entering setup.");
      enterSetup();
      return;
    }
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    String apiUrl = preferences.getString("api_url", "");
    String apiKey = preferences.getString("api_key", "");
    updateIntervalMinutes =
        preferences.getUInt(
            "update_minutes",
            DEFAULT_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE);
    statusLedEnabled = preferences.getBool("status_led", true);
    preferences.end();
    setStatusLed(true);

    if (updateIntervalMinutes < MIN_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE ||
        updateIntervalMinutes > MAX_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE)
    {
      SerialLog::error("Saved update interval is invalid; using the 60-minute default.");
      updateIntervalMinutes =
          DEFAULT_UPDATE_INTERVAL_SECONDS / SECONDS_PER_MINUTE;
    }

    if (esp_reset_reason() != ESP_RST_DEEPSLEEP)
    {
      memset(lastDisplayedImageHash, 0, sizeof(lastDisplayedImageHash));
      if (hardwareAvailable)
      {
        SerialLog::info("Displaying saved startup settings (API key is not logged).");
        display.showStartupSettings(ssid.c_str(), apiUrl.c_str(), apiKey.c_str());
      }
      SerialLog::info("Waiting up to one minute for the BOOT setup shortcut.");
      if (waitForSetupButton(STARTUP_SETTINGS_DISPLAY_SECONDS))
      {
        SerialLog::info("BOOT held for five seconds; entering setup without erasing settings.");
        enterSetup();
        return;
      }
      SerialLog::info("Startup settings window ended.");
      setStatusLed(true);
    }

    stateStartedMs = millis();
    SerialLog::info("Connecting to the saved Wi-Fi network.");
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
    if (elapsed >= millisecondsFromSeconds(SETUP_TIMEOUT_SECONDS))
    {
      SerialLog::error("Setup timed out; closing the portal and entering deep sleep.");
      dnsServer.stop();
      webServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      if (hardwareAvailable)
      {
        SerialLog::info("Displaying setup timeout message.");
        display.showTimeoutMessage();
        if (!display.sleep())
          SerialLog::error("E-paper sleep failed after setup timeout.");
      }
      setStatusLed(false);
      state = State::Shutdown;
      esp_deep_sleep_start();
      return;
    }
  }
  else if (state == State::WifiConnecting)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      SerialLog::info("Wi-Fi connected; entering normal operation.");
      state = State::NormalOperation;
    }
    else if (millis() - stateStartedMs >=
             millisecondsFromSeconds(WIFI_TIMEOUT_SECONDS))
    {
      String failureReason =
          "Saved Wi-Fi connection failed after 30 seconds (status " +
          String(static_cast<int>(WiFi.status())) + ").";
      scheduleRetrievalRetry(failureReason);
      return;
    }
  }
  else if (state == State::NormalOperation)
  {
    runNormalOperation();
  }

  delay(10);
}
