// BT Speed Dyno Hardware - ESP32-S3
// VIP filter core + runtime-adjustable engine RPM pulse filter
// Sensor core ported from the user's Dyno ESP32 VIP code.
// GPIO18 = wheel Hall, GPIO16 = engine pickup, GPIO4 = AFR analog.
// Serial and BLE both publish: D,timeMs,wheelRPM,engineRPM,afrVoltage

#include <Arduino.h>
#include "driver/pcnt.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ============================================================================
// HARDWARE IO
// ============================================================================
#define HALL_PIN    18
#define ENGINE_PIN  16
#define AFR_PIN     4

// ============================================================================
// SENSOR CONFIG - PRESERVED FROM VIP CODE
// ============================================================================
const float PULSES_PER_REV_ROLLER = 1.0f;
const float PULSES_PER_REV_ENGINE = 1.0f;

const unsigned long SAMPLE_INTERVAL_MS = 25;
const unsigned long ENGINE_TIMEOUT_MS = 400;
const unsigned long ROLLER_TIMEOUT_MS = 1000;

const float MAX_RPM_ENGINE = 20000.0f;
const float MAX_RPM_ROLLER = 10000.0f;

// Engine RPM pulse filter.
// VIP source used ENGINE_MIN_PERIOD_US = 250 us.
// Keep the same behavior, but make it runtime adjustable from the app.
const uint32_t DEFAULT_ENGINE_FILTER_US = 250;
const uint32_t ENGINE_FILTER_MIN_US = 50;
const uint32_t ENGINE_FILTER_MAX_US = 10000;
volatile uint32_t engineFilterUs = DEFAULT_ENGINE_FILTER_US;
Preferences prefs;

// Full app configuration stored in ESP32-S3 NVS.
// Web localStorage remains a fallback/cache, but the S3 becomes the source of truth
// after the first migration from an existing web installation.
const uint8_t FULL_CONFIG_VERSION = 1;
bool fullConfigInitialized = false;
bool configBatchMode = false;

float cfgWheelDiameterMm = 240.0f;
float cfgVehicleInertiaJ = 1.73f;
uint32_t cfgMaxRpmDisplay = 16000;
float cfgMaxSpeedDisplay = 160.0f;
float cfgAfrVMin = 0.0f;
float cfgAfrMin = 10.0f;
float cfgAfrVMax = 3.15f;
float cfgAfrMax = 20.0f;
float cfgAfrDecelEps = 40.0f;
uint16_t cfgIgnitionCycle = 360;
float cfgAutoSpeed = 10.0f;
float cfgAutoHp = 1.0f;
float cfgAutoAlpha = 10.0f;
bool cfgAutoStart = false;
bool cfgAutoEndRun = true;
uint8_t cfgSmoothingLevel = 1;

const uint32_t ROLLER_ABS_MIN_PERIOD_US = 6000;
const uint32_t ROLLER_EARLY_GATE_PERCENT = 45;

const float ROLLER_PERIOD_ALPHA = 0.40f;
const float ROLLER_OUTPUT_ALPHA = 0.18f;

const float ADC_REF_V = 3.3f;
const int ADC_MAX = 4095;

// AFR live filter:
// 1) Median over the latest 5 voltage samples to reject spikes.
// 2) Light EMA after the median so live AFR stays smooth but responsive.
// Higher alpha = faster response / lighter smoothing.
const float AFRV_EMA_ALPHA = 0.35f;

static inline float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static float medianSmallFloat(const float *src, uint8_t count) {
  if (count == 0) return 0.0f;
  if (count > 5) count = 5;

  float v[5];
  for (uint8_t i = 0; i < count; i++) {
    v[i] = src[i];
  }

  for (uint8_t i = 0; i + 1 < count; i++) {
    for (uint8_t j = i + 1; j < count; j++) {
      if (v[j] < v[i]) {
        float t = v[i];
        v[i] = v[j];
        v[j] = t;
      }
    }
  }

  uint8_t mid = count / 2;
  if (count & 1U) return v[mid];
  return (v[mid - 1] + v[mid]) * 0.5f;
}


static bool parseFloatStrict(const String &s, float &out) {
  char *end = nullptr;
  out = strtof(s.c_str(), &end);
  return end != nullptr &&
         end != s.c_str() &&
         *end == '\0' &&
         isfinite(out);
}

static bool parseLongStrict(const String &s, long &out) {
  char *end = nullptr;
  out = strtol(s.c_str(), &end, 10);
  return end != nullptr &&
         end != s.c_str() &&
         *end == '\0';
}

static void loadFullConfigFromPrefs() {
  fullConfigInitialized =
      prefs.getUChar("cfgVer", 0) == FULL_CONFIG_VERSION;

  if (!fullConfigInitialized) return;

  float f = 0.0f;
  uint32_t u = 0;

  f = prefs.getFloat("rollerD", cfgWheelDiameterMm);
  if (isfinite(f) && f >= 50.0f && f <= 2000.0f) cfgWheelDiameterMm = f;

  f = prefs.getFloat("rollerJ", cfgVehicleInertiaJ);
  if (isfinite(f) && f >= 0.01f && f <= 1000.0f) cfgVehicleInertiaJ = f;

  u = prefs.getUInt("maxRpm", cfgMaxRpmDisplay);
  if (u >= 1000 && u <= 50000) cfgMaxRpmDisplay = u;

  f = prefs.getFloat("maxSpeed", cfgMaxSpeedDisplay);
  if (isfinite(f) && f >= 1.0f && f <= 1000.0f) cfgMaxSpeedDisplay = f;

  f = prefs.getFloat("afrVMin", cfgAfrVMin);
  if (isfinite(f) && f >= 0.0f && f <= 5.0f) cfgAfrVMin = f;

  f = prefs.getFloat("afrMin", cfgAfrMin);
  if (isfinite(f) && f >= 5.0f && f <= 30.0f) cfgAfrMin = f;

  f = prefs.getFloat("afrVMax", cfgAfrVMax);
  if (isfinite(f) && f >= 0.0f && f <= 5.0f) cfgAfrVMax = f;

  f = prefs.getFloat("afrMax", cfgAfrMax);
  if (isfinite(f) && f >= 5.0f && f <= 30.0f) cfgAfrMax = f;

  f = prefs.getFloat("afrDecel", cfgAfrDecelEps);
  if (isfinite(f) && f >= 0.0f && f <= 500.0f) cfgAfrDecelEps = f;

  u = prefs.getUInt("ignCycle", cfgIgnitionCycle);
  if (u == 360 || u == 720) cfgIgnitionCycle = (uint16_t)u;

  u = prefs.getUInt("rpmFiltUs", engineFilterUs);
  if (u >= ENGINE_FILTER_MIN_US && u <= ENGINE_FILTER_MAX_US) {
    engineFilterUs = u;
  }

  f = prefs.getFloat("autoSpeed", cfgAutoSpeed);
  if (isfinite(f) && f >= 0.0f && f <= 500.0f) cfgAutoSpeed = f;

  f = prefs.getFloat("autoHp", cfgAutoHp);
  if (isfinite(f) && f >= 0.0f && f <= 1000.0f) cfgAutoHp = f;

  f = prefs.getFloat("autoAlpha", cfgAutoAlpha);
  if (isfinite(f) && f >= 0.0f && f <= 10000.0f) cfgAutoAlpha = f;

  cfgAutoStart = prefs.getBool("autoStart", cfgAutoStart);
  cfgAutoEndRun = prefs.getBool("autoEnd", cfgAutoEndRun);

  u = prefs.getUInt("smooth", cfgSmoothingLevel);
  if (u <= 2) cfgSmoothingLevel = (uint8_t)u;
}

static bool persistConfigKey(const String &key) {
  if (key == "WD") return prefs.putFloat("rollerD", cfgWheelDiameterMm) > 0;
  if (key == "IJ") return prefs.putFloat("rollerJ", cfgVehicleInertiaJ) > 0;
  if (key == "MR") return prefs.putUInt("maxRpm", cfgMaxRpmDisplay) > 0;
  if (key == "MS") return prefs.putFloat("maxSpeed", cfgMaxSpeedDisplay) > 0;
  if (key == "V0") return prefs.putFloat("afrVMin", cfgAfrVMin) > 0;
  if (key == "A0") return prefs.putFloat("afrMin", cfgAfrMin) > 0;
  if (key == "V1") return prefs.putFloat("afrVMax", cfgAfrVMax) > 0;
  if (key == "A1") return prefs.putFloat("afrMax", cfgAfrMax) > 0;
  if (key == "DE") return prefs.putFloat("afrDecel", cfgAfrDecelEps) > 0;
  if (key == "IC") return prefs.putUInt("ignCycle", cfgIgnitionCycle) > 0;
  if (key == "RF") return prefs.putUInt("rpmFiltUs", engineFilterUs) > 0;
  if (key == "SP") return prefs.putFloat("autoSpeed", cfgAutoSpeed) > 0;
  if (key == "HP") return prefs.putFloat("autoHp", cfgAutoHp) > 0;
  if (key == "AA") return prefs.putFloat("autoAlpha", cfgAutoAlpha) > 0;
  if (key == "AS") return prefs.putBool("autoStart", cfgAutoStart) > 0;
  if (key == "AE") return prefs.putBool("autoEnd", cfgAutoEndRun) > 0;
  if (key == "SM") return prefs.putUInt("smooth", cfgSmoothingLevel) > 0;
  return false;
}

static void saveFullConfigToPrefs() {
  prefs.putFloat("rollerD", cfgWheelDiameterMm);
  prefs.putFloat("rollerJ", cfgVehicleInertiaJ);
  prefs.putUInt("maxRpm", cfgMaxRpmDisplay);
  prefs.putFloat("maxSpeed", cfgMaxSpeedDisplay);
  prefs.putFloat("afrVMin", cfgAfrVMin);
  prefs.putFloat("afrMin", cfgAfrMin);
  prefs.putFloat("afrVMax", cfgAfrVMax);
  prefs.putFloat("afrMax", cfgAfrMax);
  prefs.putFloat("afrDecel", cfgAfrDecelEps);
  prefs.putUInt("ignCycle", cfgIgnitionCycle);
  prefs.putUInt("rpmFiltUs", engineFilterUs);
  prefs.putFloat("autoSpeed", cfgAutoSpeed);
  prefs.putFloat("autoHp", cfgAutoHp);
  prefs.putFloat("autoAlpha", cfgAutoAlpha);
  prefs.putBool("autoStart", cfgAutoStart);
  prefs.putBool("autoEnd", cfgAutoEndRun);
  prefs.putUInt("smooth", cfgSmoothingLevel);

  // Write version last so a brand-new migration is only considered complete
  // after every config field has been stored.
  prefs.putUChar("cfgVer", FULL_CONFIG_VERSION);
  fullConfigInitialized = true;
}

static bool setConfigValue(const String &key, const String &value) {
  float f = 0.0f;
  long n = 0;

  if (key == "WD") {
    if (!parseFloatStrict(value, f) || f < 50.0f || f > 2000.0f) return false;
    cfgWheelDiameterMm = f;
  } else if (key == "IJ") {
    if (!parseFloatStrict(value, f) || f < 0.01f || f > 1000.0f) return false;
    cfgVehicleInertiaJ = f;
  } else if (key == "MR") {
    if (!parseLongStrict(value, n) || n < 1000 || n > 50000) return false;
    cfgMaxRpmDisplay = (uint32_t)n;
  } else if (key == "MS") {
    if (!parseFloatStrict(value, f) || f < 1.0f || f > 1000.0f) return false;
    cfgMaxSpeedDisplay = f;
  } else if (key == "V0") {
    if (!parseFloatStrict(value, f) || f < 0.0f || f > 5.0f) return false;
    cfgAfrVMin = f;
  } else if (key == "A0") {
    if (!parseFloatStrict(value, f) || f < 5.0f || f > 30.0f) return false;
    cfgAfrMin = f;
  } else if (key == "V1") {
    if (!parseFloatStrict(value, f) || f < 0.0f || f > 5.0f) return false;
    cfgAfrVMax = f;
  } else if (key == "A1") {
    if (!parseFloatStrict(value, f) || f < 5.0f || f > 30.0f) return false;
    cfgAfrMax = f;
  } else if (key == "DE") {
    if (!parseFloatStrict(value, f) || f < 0.0f || f > 500.0f) return false;
    cfgAfrDecelEps = f;
  } else if (key == "IC") {
    if (!parseLongStrict(value, n) || (n != 360 && n != 720)) return false;
    cfgIgnitionCycle = (uint16_t)n;
  } else if (key == "RF") {
    if (!parseLongStrict(value, n) ||
        n < (long)ENGINE_FILTER_MIN_US ||
        n > (long)ENGINE_FILTER_MAX_US) return false;
    engineFilterUs = (uint32_t)n;
  } else if (key == "SP") {
    if (!parseFloatStrict(value, f) || f < 0.0f || f > 500.0f) return false;
    cfgAutoSpeed = f;
  } else if (key == "HP") {
    if (!parseFloatStrict(value, f) || f < 0.0f || f > 1000.0f) return false;
    cfgAutoHp = f;
  } else if (key == "AA") {
    if (!parseFloatStrict(value, f) || f < 0.0f || f > 10000.0f) return false;
    cfgAutoAlpha = f;
  } else if (key == "AS") {
    if (!parseLongStrict(value, n) || (n != 0 && n != 1)) return false;
    cfgAutoStart = (n == 1);
  } else if (key == "AE") {
    if (!parseLongStrict(value, n) || (n != 0 && n != 1)) return false;
    cfgAutoEndRun = (n == 1);
  } else if (key == "SM") {
    if (!parseLongStrict(value, n) || n < 0 || n > 2) return false;
    cfgSmoothingLevel = (uint8_t)n;
  } else {
    return false;
  }

  return true;
}

// ============================================================================
// DYNOTL MOBILE BLE
// ============================================================================
static const char *DEVICE_NAME  = "BT Speed Dyno";
static const char *SERVICE_UUID = "d7a10001-7c35-4a6d-9f0e-2ea3117f1000";
static const char *LIVE_UUID    = "d7a10002-7c35-4a6d-9f0e-2ea3117f1000";
static const char *COMMAND_UUID = "d7a10003-7c35-4a6d-9f0e-2ea3117f1000";
static const char *STATUS_UUID  = "d7a10005-7c35-4a6d-9f0e-2ea3117f1000";

BLEServer *bleServer = nullptr;
BLECharacteristic *liveChar = nullptr;
BLECharacteristic *commandChar = nullptr;
BLECharacteristic *statusChar = nullptr;
volatile bool deviceConnected = false;

static void bleNotifyChunks(BLECharacteristic *ch, const char *s) {
  if (!deviceConnected || ch == nullptr || s == nullptr) return;

  size_t len = strlen(s);
  for (size_t off = 0; off < len; off += 18) {
    size_t n = len - off;
    if (n > 18) n = 18;

    ch->setValue((uint8_t *)s + off, n);
    ch->notify();
    delay(2);
  }
}

static void notifyFullConfig() {
  if (!fullConfigInitialized) {
    char out[64];
    snprintf(
        out,
        sizeof(out),
        "CFGEMPTY;RF=%lu\n",
        (unsigned long)engineFilterUs
    );
    bleNotifyChunks(statusChar, out);
    return;
  }

  char out[320];
  snprintf(
      out,
      sizeof(out),
      "CFG;WD=%.3f;IJ=%.4f;MR=%lu;MS=%.3f;"
      "V0=%.3f;A0=%.3f;V1=%.3f;A1=%.3f;DE=%.3f;"
      "IC=%u;RF=%lu;AS=%u;AE=%u;SP=%.3f;HP=%.3f;AA=%.3f;SM=%u\n",
      cfgWheelDiameterMm,
      cfgVehicleInertiaJ,
      (unsigned long)cfgMaxRpmDisplay,
      cfgMaxSpeedDisplay,
      cfgAfrVMin,
      cfgAfrMin,
      cfgAfrVMax,
      cfgAfrMax,
      cfgAfrDecelEps,
      (unsigned int)cfgIgnitionCycle,
      (unsigned long)engineFilterUs,
      cfgAutoStart ? 1U : 0U,
      cfgAutoEndRun ? 1U : 0U,
      cfgAutoSpeed,
      cfgAutoHp,
      cfgAutoAlpha,
      (unsigned int)cfgSmoothingLevel
  );
  bleNotifyChunks(statusChar, out);
}

void dynoPublishSample(
    uint32_t timeMs,
    float wheelRPM,
    float engineRPM,
    float afrVoltage
) {
  if (!deviceConnected || liveChar == nullptr) return;

  char line[112];
  snprintf(
      line,
      sizeof(line),
      "D,%lu,%.2f,%.2f,%.3f\n",
      (unsigned long)timeMs,
      wheelRPM,
      engineRPM,
      afrVoltage
  );
  bleNotifyChunks(liveChar, line);
}

class DynoBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    deviceConnected = true;
    Serial.println("[BT Speed Dyno] BLE client connected");
  }

  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    delay(120);
    s->getAdvertising()->start();
    Serial.println("[BT Speed Dyno] BLE advertising restarted");
  }
};

class DynoBleCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String cmd = characteristic->getValue().c_str();
    cmd.trim();

    if (cmd.equalsIgnoreCase("CONFIG?")) {
      notifyFullConfig();
      return;
    }

    if (cmd.equalsIgnoreCase("CFGINIT")) {
      configBatchMode = true;
      bleNotifyChunks(statusChar, "CFGINIT OK\n");
      return;
    }

    if (cmd.equalsIgnoreCase("CFGSAVE")) {
      saveFullConfigToPrefs();
      configBatchMode = false;
      bleNotifyChunks(statusChar, "CFGSAVED\n");
      return;
    }

    if (cmd.startsWith("CFG ")) {
      int split = cmd.indexOf(' ', 4);
      if (split <= 4) {
        bleNotifyChunks(statusChar, "ERR CFG FORMAT\n");
        return;
      }

      String key = cmd.substring(4, split);
      String value = cmd.substring(split + 1);
      key.trim();
      value.trim();
      key.toUpperCase();

      if (!setConfigValue(key, value)) {
        bleNotifyChunks(statusChar, "ERR CFG VALUE\n");
        return;
      }

      if (fullConfigInitialized && !configBatchMode) {
        if (!persistConfigKey(key)) {
          bleNotifyChunks(statusChar, "ERR CFG SAVE\n");
          return;
        }
      }

      char ack[40];
      snprintf(ack, sizeof(ack), "CFGACK %s\n", key.c_str());
      bleNotifyChunks(statusChar, ack);
      return;
    }

    if (cmd.equalsIgnoreCase("PING")) {
      bleNotifyChunks(statusChar, "PONG\n");
      return;
    }

    if (cmd.equalsIgnoreCase("STATUS")) {
      char out[96];
      snprintf(
          out,
          sizeof(out),
          "BT SPEED DYNO READY;RPMFILTER=%lu\n",
          (unsigned long)engineFilterUs
      );
      bleNotifyChunks(statusChar, out);
      return;
    }

    if (
        cmd.equalsIgnoreCase("RPMFILTER?") ||
        cmd.equalsIgnoreCase("RPMLOCK?")
    ) {
      char out[48];
      snprintf(
          out,
          sizeof(out),
          "RPMFILTER=%lu\n",
          (unsigned long)engineFilterUs
      );
      bleNotifyChunks(statusChar, out);
      return;
    }

    bool isFilterCmd = cmd.startsWith("RPMFILTER ");
    bool isLegacyLockCmd = cmd.startsWith("RPMLOCK ");

    if (isFilterCmd || isLegacyLockCmd) {
      long requested = isFilterCmd
          ? cmd.substring(10).toInt()
          : cmd.substring(8).toInt();

      if (
          requested >= (long)ENGINE_FILTER_MIN_US &&
          requested <= (long)ENGINE_FILTER_MAX_US
      ) {
        engineFilterUs = (uint32_t)requested;
        prefs.putUInt("rpmFiltUs", engineFilterUs);

        char out[56];
        snprintf(
            out,
            sizeof(out),
            "RPMFILTER=%lu;SAVED\n",
            (unsigned long)engineFilterUs
        );
        bleNotifyChunks(statusChar, out);
      } else {
        bleNotifyChunks(statusChar, "ERR RPMFILTER RANGE 50..10000\n");
      }
      return;
    }
  }
};

void dynoBleBegin() {
  Serial.println("[BLE] 1/6 init device...");
  Serial.flush();
  BLEDevice::init(DEVICE_NAME);
  Serial.println("[BLE] 2/6 device init OK");

  bleServer = BLEDevice::createServer();
  Serial.println("[BLE] 3/6 server OK");
  bleServer->setCallbacks(new DynoBleServerCallbacks());

  BLEService *svc = bleServer->createService(SERVICE_UUID);
  Serial.println("[BLE] 4/6 service OK");

  liveChar = svc->createCharacteristic(
      LIVE_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_NOTIFY
  );
  liveChar->addDescriptor(new BLE2902());

  commandChar = svc->createCharacteristic(
      COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR
  );
  commandChar->setCallbacks(new DynoBleCommandCallbacks());

  statusChar = svc->createCharacteristic(
      STATUS_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_NOTIFY
  );
  statusChar->addDescriptor(new BLE2902());

  svc->start();
  Serial.println("[BLE] 5/6 characteristics + service started");

  // Same advertising structure as the working Blink-Redleo ESP32-S3 project.
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("[BLE] 6/6 advertising started");
  Serial.flush();

  Serial.println("========================================");
  Serial.println("[BT Speed Dyno] ESP32-S3 BLE advertising STARTED");
  Serial.printf("[BT Speed Dyno] Name    : %s\n", DEVICE_NAME);
  Serial.printf("[BT Speed Dyno] Service : %s\n", SERVICE_UUID);
  Serial.println("[BT Speed Dyno] BLE transport ready");
  Serial.println("========================================");
}

// ============================================================================
// ENGINE RPM - PCNT, SAME LOGIC AS VIP
// ============================================================================
static const pcnt_unit_t ENG_PCNT_UNIT = PCNT_UNIT_1;
static const pcnt_channel_t ENG_PCNT_CH = PCNT_CHANNEL_0;

void setupEnginePCNTCount() {
  pcnt_config_t c = {};

  c.pulse_gpio_num = ENGINE_PIN;
  c.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  c.unit = ENG_PCNT_UNIT;
  c.channel = ENG_PCNT_CH;

  c.pos_mode = PCNT_COUNT_INC;
  c.neg_mode = PCNT_COUNT_DIS;

  c.lctrl_mode = PCNT_MODE_KEEP;
  c.hctrl_mode = PCNT_MODE_KEEP;

  c.counter_l_lim = 0;
  c.counter_h_lim = 32767;

  pcnt_unit_config(&c);

  // About 12.8 us maximum legacy PCNT filter at APB 80 MHz.
  pcnt_set_filter_value(ENG_PCNT_UNIT, 1023);
  pcnt_filter_enable(ENG_PCNT_UNIT);

  pcnt_counter_pause(ENG_PCNT_UNIT);
  pcnt_counter_clear(ENG_PCNT_UNIT);
  pcnt_counter_resume(ENG_PCNT_UNIT);
}

static int16_t engLastCount = 0;
static uint32_t engLastUs = 0;
static bool engPeriodReady = false;
static uint32_t engPeriodUs = 0;

void pollEnginePeriodFromPCNT() {
  int16_t c = 0;
  pcnt_get_counter_value(ENG_PCNT_UNIT, &c);

  int16_t delta = c - engLastCount;
  if (delta <= 0) return;

  uint32_t now = micros();
  uint32_t dt = now - engLastUs;
  uint32_t per = dt / (uint32_t)delta;

  uint32_t filterUs = engineFilterUs;
  if (filterUs < ENGINE_FILTER_MIN_US) filterUs = ENGINE_FILTER_MIN_US;
  if (filterUs > ENGINE_FILTER_MAX_US) filterUs = ENGINE_FILTER_MAX_US;

  // Same role as ENGINE_MIN_PERIOD_US in the VIP source:
  // reject engine pickup periods that are too short.
  if (per >= filterUs && per > 0) {
    engPeriodUs = per;
    engPeriodReady = true;
    engLastUs = now;
  } else {
    // Preserve VIP behavior: reject short pulse but refresh time reference.
    engLastUs = now;
  }

  engLastCount = c;

  if (c > 20000) {
    pcnt_counter_clear(ENG_PCNT_UNIT);
    engLastCount = 0;
  }
}

// ============================================================================
// WHEEL HALL - DIRECT INTERRUPT + FALSE-PULSE REJECTION
// ============================================================================
volatile uint32_t hallLastValidEdgeUsISR = 0;
volatile uint32_t hallLastValidPeriodUsISR = 0;
volatile uint32_t hallNewPeriodUsISR = 0;
volatile bool hallNewPeriodReadyISR = false;
volatile uint32_t hallRejectedPulseCountISR = 0;

void IRAM_ATTR hallISR() {
  uint32_t now = micros();

  if (hallLastValidEdgeUsISR == 0) {
    hallLastValidEdgeUsISR = now;
    return;
  }

  uint32_t dt = now - hallLastValidEdgeUsISR;

  if (dt < ROLLER_ABS_MIN_PERIOD_US) {
    hallRejectedPulseCountISR++;
    return;
  }

  if (hallLastValidPeriodUsISR > 0) {
    uint32_t dynamicMinUs =
        (hallLastValidPeriodUsISR * ROLLER_EARLY_GATE_PERCENT) / 100UL;

    if (dynamicMinUs < ROLLER_ABS_MIN_PERIOD_US) {
      dynamicMinUs = ROLLER_ABS_MIN_PERIOD_US;
    }

    if (dt < dynamicMinUs) {
      hallRejectedPulseCountISR++;
      return;
    }
  }

  hallNewPeriodUsISR = dt;
  hallNewPeriodReadyISR = true;
  hallLastValidPeriodUsISR = dt;
  hallLastValidEdgeUsISR = now;
}

// ============================================================================
// WHEEL MEDIAN-5 + PERIOD FILTER
// ============================================================================
static uint32_t rollerPeriodHistory[5] = {0, 0, 0, 0, 0};
static uint8_t rollerPeriodHistoryCount = 0;
static uint8_t rollerPeriodHistoryIndex = 0;

static float rollerPeriodFilteredUs = 0.0f;
static float rollerRPMFiltered = 0.0f;
static float rollerRPMOutput = 0.0f;
static bool rollerStartupLocked = false;
static unsigned long lastHallPulseMs = 0;

static inline uint32_t median5u32(
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d,
    uint32_t e
) {
  uint32_t v[5] = {a, b, c, d, e};

  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t j = i + 1; j < 5; j++) {
      if (v[j] < v[i]) {
        uint32_t t = v[i];
        v[i] = v[j];
        v[j] = t;
      }
    }
  }
  return v[2];
}

static bool startupWindowLooksReasonable(uint32_t medianUs) {
  if (medianUs < ROLLER_ABS_MIN_PERIOD_US) return false;

  uint32_t minOk = (medianUs * 40UL) / 100UL;
  uint32_t maxOk = (medianUs * 250UL) / 100UL;

  if (minOk < ROLLER_ABS_MIN_PERIOD_US) {
    minOk = ROLLER_ABS_MIN_PERIOD_US;
  }

  uint8_t good = 0;
  for (uint8_t i = 0; i < 5; i++) {
    uint32_t p = rollerPeriodHistory[i];
    if (p >= minOk && p <= maxOk) good++;
  }

  return good >= 4;
}

void processRollerPeriod(uint32_t periodUs, unsigned long nowMs) {
  if (periodUs == 0 || periodUs < ROLLER_ABS_MIN_PERIOD_US) return;

  rollerPeriodHistory[rollerPeriodHistoryIndex] = periodUs;

  rollerPeriodHistoryIndex++;
  if (rollerPeriodHistoryIndex >= 5) rollerPeriodHistoryIndex = 0;

  if (rollerPeriodHistoryCount < 5) rollerPeriodHistoryCount++;

  if (!rollerStartupLocked) {
    rollerRPMFiltered = 0.0f;
    rollerRPMOutput = 0.0f;

    if (rollerPeriodHistoryCount < 5) return;

    uint32_t startupMedianUs = median5u32(
        rollerPeriodHistory[0],
        rollerPeriodHistory[1],
        rollerPeriodHistory[2],
        rollerPeriodHistory[3],
        rollerPeriodHistory[4]
    );

    if (!startupWindowLooksReasonable(startupMedianUs)) return;

    float startupRPM =
        60000000.0f /
        ((float)startupMedianUs * PULSES_PER_REV_ROLLER);

    if (startupRPM <= 0.0f || startupRPM > MAX_RPM_ROLLER) return;

    rollerPeriodFilteredUs = (float)startupMedianUs;
    rollerRPMFiltered = startupRPM;
    rollerRPMOutput = 0.0f;

    rollerStartupLocked = true;
    lastHallPulseMs = nowMs;
    return;
  }

  uint32_t periodForFilter = median5u32(
      rollerPeriodHistory[0],
      rollerPeriodHistory[1],
      rollerPeriodHistory[2],
      rollerPeriodHistory[3],
      rollerPeriodHistory[4]
  );

  if (rollerPeriodFilteredUs <= 0.0f) {
    rollerPeriodFilteredUs = (float)periodForFilter;
  } else {
    rollerPeriodFilteredUs =
        rollerPeriodFilteredUs * (1.0f - ROLLER_PERIOD_ALPHA) +
        (float)periodForFilter * ROLLER_PERIOD_ALPHA;
  }

  float rpm =
      60000000.0f /
      (rollerPeriodFilteredUs * PULSES_PER_REV_ROLLER);

  if (rpm >= 0.0f && rpm <= MAX_RPM_ROLLER) {
    rollerRPMFiltered = rpm;
    lastHallPulseMs = nowMs;
  }
}

void updateRollerFromHall(unsigned long nowMs) {
  bool ready = false;
  uint32_t periodUs = 0;

  noInterrupts();

  if (hallNewPeriodReadyISR) {
    periodUs = hallNewPeriodUsISR;
    hallNewPeriodReadyISR = false;
    ready = true;
  }

  interrupts();

  if (ready) processRollerPeriod(periodUs, nowMs);
}

void resetRollerState() {
  rollerRPMFiltered = 0.0f;
  rollerRPMOutput = 0.0f;
  rollerPeriodFilteredUs = 0.0f;
  rollerStartupLocked = false;

  for (uint8_t i = 0; i < 5; i++) {
    rollerPeriodHistory[i] = 0;
  }

  rollerPeriodHistoryCount = 0;
  rollerPeriodHistoryIndex = 0;

  noInterrupts();

  hallLastValidEdgeUsISR = 0;
  hallLastValidPeriodUsISR = 0;
  hallNewPeriodUsISR = 0;
  hallNewPeriodReadyISR = false;

  interrupts();
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  // Hall idle LOW, magnet passing HIGH.
  pinMode(HALL_PIN, INPUT_PULLDOWN);

  // Engine pickup / opto / open collector.
  pinMode(ENGINE_PIN, INPUT_PULLUP);

  // AFR ADC.
  analogReadResolution(12);
#if defined(ARDUINO_ARCH_ESP32)
  analogSetPinAttenuation(AFR_PIN, ADC_11db);
#endif
  pinMode(AFR_PIN, INPUT);

  Serial.begin(115200);
  delay(1200);

  prefs.begin("btspeed", false);
  uint32_t savedFilter = prefs.getUInt(
      "rpmFiltUs",
      DEFAULT_ENGINE_FILTER_US
  );
  if (
      savedFilter < ENGINE_FILTER_MIN_US ||
      savedFilter > ENGINE_FILTER_MAX_US
  ) {
    savedFilter = DEFAULT_ENGINE_FILTER_US;
  }
  engineFilterUs = savedFilter;
  loadFullConfigFromPrefs();

  Serial.println();
  Serial.println("========================================");
  Serial.println("[BOOT] BT Speed Dyno Hardware");
  Serial.println("[BOOT] ESP32-S3 firmware started");
  Serial.println("[BOOT] GPIO18=Wheel Hall | GPIO16=Engine RPM | GPIO4=AFR");
  Serial.printf(
      "[BOOT] Engine RPM pulse filter = %lu us\n",
      (unsigned long)engineFilterUs
  );
  Serial.printf(
      "[BOOT] Settings source = %s\n",
      fullConfigInitialized ? "ESP32-S3 NVS" : "legacy/local migration pending"
  );
  Serial.println("========================================");
  Serial.flush();
  delay(200);

  dynoBleBegin();

  setupEnginePCNTCount();

  attachInterrupt(
      digitalPinToInterrupt(HALL_PIN),
      hallISR,
      RISING
  );

  uint32_t nowUs = micros();
  engLastUs = nowUs;
  engLastCount = 0;

  Serial.println(
      "BT Speed Dyno Hardware - ESP32-S3 - VIP sensor core + BLE"
  );
  Serial.println(
      "HEADER,D,timeMs,wheelRPM,engineRPM,afrVoltage"
  );
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  static unsigned long lastSampleMs = 0;
  static float engineRPMFiltered = 0.0f;
  static unsigned long lastEngPulseMs = 0;

  static float afrVoltHistory[5] = {0, 0, 0, 0, 0};
  static uint8_t afrVoltHistoryCount = 0;
  static uint8_t afrVoltHistoryIndex = 0;
  static float afrVoltFiltered = 0.0f;

  unsigned long nowMs = millis();

  // Engine RPM.
  pollEnginePeriodFromPCNT();

  // Wheel RPM.
  updateRollerFromHall(nowMs);

  if (engPeriodReady && PULSES_PER_REV_ENGINE > 0.0f) {
    engPeriodReady = false;

    float rpmInstant =
        (60.0f * 1000000.0f) /
        ((float)engPeriodUs * PULSES_PER_REV_ENGINE);

    if (rpmInstant <= MAX_RPM_ENGINE) {
      const float alphaE = 0.25f;

      engineRPMFiltered =
          (engineRPMFiltered <= 0.0f)
          ? rpmInstant
          : (
              engineRPMFiltered * (1.0f - alphaE) +
              rpmInstant * alphaE
            );

      lastEngPulseMs = nowMs;
    }
  }

  if (nowMs - lastEngPulseMs > ENGINE_TIMEOUT_MS) {
    engineRPMFiltered = 0.0f;
  }

  // Wheel timeout also resets incomplete Startup-5 state.
  uint32_t hallLastEdgeSnapshotUs = 0;

  noInterrupts();
  hallLastEdgeSnapshotUs = hallLastValidEdgeUsISR;
  interrupts();

  if (
      hallLastEdgeSnapshotUs != 0 &&
      (uint32_t)(micros() - hallLastEdgeSnapshotUs)
          > (ROLLER_TIMEOUT_MS * 1000UL)
  ) {
    resetRollerState();
  }

  if (nowMs - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    // Output smoothing from VIP code.
    if (rollerRPMFiltered <= 0.0f) {
      rollerRPMOutput = 0.0f;
    } else if (rollerRPMOutput <= 0.0f) {
      rollerRPMOutput = rollerRPMFiltered;
    } else {
      rollerRPMOutput =
          rollerRPMOutput * (1.0f - ROLLER_OUTPUT_ALPHA) +
          rollerRPMFiltered * ROLLER_OUTPUT_ALPHA;
    }

    // AFR live voltage filter:
    // raw ADC -> voltage -> Median 5 -> light EMA.
    int raw = analogRead(AFR_PIN);

    raw = (raw < 0)
        ? 0
        : (raw > ADC_MAX ? ADC_MAX : raw);

    float v =
        (raw * ADC_REF_V) /
        (float)ADC_MAX;

    v = clampf(v, 0.0f, 3.3f);

    afrVoltHistory[afrVoltHistoryIndex] = v;
    afrVoltHistoryIndex++;
    if (afrVoltHistoryIndex >= 5) afrVoltHistoryIndex = 0;
    if (afrVoltHistoryCount < 5) afrVoltHistoryCount++;

    float afrVoltMedian =
        medianSmallFloat(afrVoltHistory, afrVoltHistoryCount);

    if (afrVoltFiltered <= 0.0001f) {
      afrVoltFiltered = afrVoltMedian;
    } else {
      afrVoltFiltered =
          afrVoltFiltered * (1.0f - AFRV_EMA_ALPHA) +
          afrVoltMedian * AFRV_EMA_ALPHA;
    }

    // Keep old PC serial output.
    Serial.print("D,");
    Serial.print(nowMs);
    Serial.print(",");
    Serial.print(rollerRPMOutput, 2);
    Serial.print(",");
    Serial.print(engineRPMFiltered, 2);
    Serial.print(",");
    Serial.println(afrVoltFiltered, 3);

    // Publish the same sample to DynoTL Mobile over BLE.
    dynoPublishSample(
        nowMs,
        rollerRPMOutput,
        engineRPMFiltered,
        afrVoltFiltered
    );

    lastSampleMs = nowMs;
  }
}
