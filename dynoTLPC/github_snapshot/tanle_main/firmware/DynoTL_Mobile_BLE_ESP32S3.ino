// DynoTL Mobile Hardware - ESP32-S3
// Sensor core ported from the user's Dyno ESP32 VIP code.
// GPIO18 = wheel Hall, GPIO16 = engine pickup, GPIO4 = AFR analog.
// Serial and BLE both publish: D,timeMs,wheelRPM,engineRPM,afrVoltage

#include <Arduino.h>
#include "driver/pcnt.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

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

const uint32_t ENGINE_MIN_PERIOD_US = 250;
const uint32_t ROLLER_ABS_MIN_PERIOD_US = 6000;
const uint32_t ROLLER_EARLY_GATE_PERCENT = 45;

const float ROLLER_PERIOD_ALPHA = 0.40f;
const float ROLLER_OUTPUT_ALPHA = 0.18f;

const float ADC_REF_V = 3.3f;
const int ADC_MAX = 4095;
const float AFRV_ALPHA = 0.20f;

static inline float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

// ============================================================================
// DYNOTL MOBILE BLE
// ============================================================================
static const char *DEVICE_NAME  = "DynoTL-Mobile";
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
    Serial.println("[DynoTL] BLE client connected");
  }

  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    delay(120);
    s->getAdvertising()->start();
    Serial.println("[DynoTL] BLE advertising restarted");
  }
};

class DynoBleCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String cmd = characteristic->getValue().c_str();
    cmd.trim();

    if (cmd.equalsIgnoreCase("PING")) {
      bleNotifyChunks(statusChar, "PONG\n");
    } else if (cmd.equalsIgnoreCase("STATUS")) {
      bleNotifyChunks(statusChar, "DYNOTL READY\n");
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
  Serial.println("[DynoTL] ESP32-S3 BLE advertising STARTED");
  Serial.printf("[DynoTL] Name    : %s\n", DEVICE_NAME);
  Serial.printf("[DynoTL] Service : %s\n", SERVICE_UUID);
  Serial.println("[DynoTL] BLE mode: same structure as Blink-Redleo");
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

  if (per >= ENGINE_MIN_PERIOD_US && per > 0) {
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

  Serial.println();
  Serial.println("========================================");
  Serial.println("[BOOT] DynoTL Mobile Hardware");
  Serial.println("[BOOT] ESP32-S3 firmware started");
  Serial.println("[BOOT] GPIO18=Wheel Hall | GPIO16=Engine RPM | GPIO4=AFR");
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
      "DynoTL Mobile Hardware - ESP32-S3 - VIP sensor core + BLE"
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

    // AFR voltage with the same 12-bit ADC and EMA.
    int raw = analogRead(AFR_PIN);

    raw = (raw < 0)
        ? 0
        : (raw > ADC_MAX ? ADC_MAX : raw);

    float v =
        (raw * ADC_REF_V) /
        (float)ADC_MAX;

    v = clampf(v, 0.0f, 3.3f);

    if (afrVoltFiltered <= 0.0001f) {
      afrVoltFiltered = v;
    } else {
      afrVoltFiltered =
          afrVoltFiltered * (1.0f - AFRV_ALPHA) +
          v * AFRV_ALPHA;
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
