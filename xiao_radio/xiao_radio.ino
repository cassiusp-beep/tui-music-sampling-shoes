/*
  Music Sampling Shoes : XIAO radio sketch (one per shoe)
  -----------------------------------------------
  Board: Seeed XIAO ESP32-C6 (IDE board "XIAO_ESP32C6"), esp32 board package 3.x.
  Job:   listen to the Minima on the I2C wires (SDA/SCL, shared with the gyro),
         send the values to the laptop over Bluetooth (BLE).
         The browser sampler connects to both shoes at once, and that is where the
         left and right foot are combined.

  Wiring:
     Minima 5V  -> XIAO 5V
     Minima GND -> XIAO GND
     XIAO D4    -> SCL  (same wire as the gyro's SCL)
     XIAO D5    -> SDA  (same wire as the gyro's SDA)
     No resistors: the gyro board already pulls SDA/SCL up to 3.3 V.
     Note: on this build D4 is SCL and D5 is SDA (swapped from the XIAO's printed
     defaults) so the wires on the breadboard don't cross. The code below matches.

  BEFORE UPLOADING: set FOOT below to match the Minima on the same shoe.
  Upload this to the XIAO BEFORE plugging it into the breadboard.
  Uses the BLE and Wire libraries that come with the "esp32 by Espressif" board package.
*/

#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "soc/pcr_struct.h"   // for the I2C clock fix in setup()

#define FOOT "L"   // <<< "L" or "R", must match the Minima on this shoe

#define XIAO_ADDR 0x08     // must match minima_foot.ino
#define PIN_SDA   D5
#define PIN_SCL   D4

// Must match the sampler (index.html)
#define SERVICE_UUID "7b1e0001-5d2c-4c3b-9a41-50a1c5717e00"
#define DATA_UUID    "7b1e0002-5d2c-4c3b-9a41-50a1c5717e00"

// 8 bytes, little endian. Same layout comes in from the Minima and goes out to the laptop.
struct __attribute__((packed)) FootData {
  uint16_t fsr;       // 0..4095
  int16_t  pitch100;  // degrees * 100
  int16_t  roll100;   // degrees * 100
  uint16_t shake;     // deg/s
};

BLECharacteristic *dataChar = nullptr;
volatile bool connected = false;
FootData data = {0, 0, 0, 0};
volatile FootData incoming = {0, 0, 0, 0};
volatile bool fresh = false;
volatile uint32_t lastMsg = 0;
uint32_t lastNotify = 0;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { connected = true; }
  void onDisconnect(BLEServer *s) override {
    connected = false;
    BLEDevice::startAdvertising();   // let the laptop reconnect
  }
};

// Called when the Minima writes to us
void onI2CReceive(int n) {
  if (n == (int)sizeof(FootData)) {
    uint8_t *p = (uint8_t *)&incoming;
    for (int i = 0; i < n; i++) p[i] = Wire.read();
    fresh = true;
    lastMsg = millis();
  } else {
    while (Wire.available()) Wire.read();   // wrong size, throw it away
  }
}

void setup() {
  Serial.begin(115200);   // USB debug
  Serial.setTxTimeoutMs(0);   // never wait on USB: if no Serial Monitor is open, printing would stall Bluetooth

  Wire.onReceive(onI2CReceive);
  Wire.begin((uint8_t)XIAO_ADDR, PIN_SDA, PIN_SCL, 100000);   // listen, don't drive the clock
  // Fix for the esp32 3.3.x library on the C6: it leaves the I2C clock switched off
  // in listen mode, so the XIAO never answers. This line switches it on.
  PCR.i2c_sclk_conf.i2c_sclk_en = 1;

  String name = String("MusicShoe-") + FOOT;
  BLEDevice::init(name.c_str());
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService *svc = server->createService(SERVICE_UUID);
  dataChar = svc->createCharacteristic(DATA_UUID,
               BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  dataChar->addDescriptor(new BLE2902());
  svc->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("# " + name + " advertising, listening on I2C 0x08");
}

void loop() {
  // copy the latest message from the Minima
  if (fresh) {
    noInterrupts();
    memcpy(&data, (const void *)&incoming, sizeof(data));
    fresh = false;
    interrupts();
  }

  // send to the laptop about 40 times a second
  uint32_t now = millis();
  if (connected && now - lastNotify >= 25) {
    lastNotify = now;
    dataChar->setValue((uint8_t *)&data, sizeof(data));
    dataChar->notify();
  }

  // once a second, a status line on USB for debugging
  static uint32_t lastDbg = 0;
  if (now - lastDbg > 1000 && Serial.availableForWrite() >= 128) {   // skip if nobody is reading USB
    lastDbg = now;
    Serial.printf("# %s  ble:%s  minima:%s  fsr=%u pitch=%.1f shake=%u\n",
                  FOOT, connected ? "connected" : "waiting",
                  (now - lastMsg < 500) ? "ok" : "NO DATA (check D4/D5 wires)",
                  data.fsr, data.pitch100 / 100.0f, data.shake);
  }
}
