/*
  Music Sampling Shoes : Minima foot sketch (one per shoe)
  ------------------------------------------------
  Board: Arduino UNO R4 Minima
  Reads:  FSR in the sole (A0) + MPU-6050 gyro/accelerometer on the breadboard (I2C)
  Sends:  the numbers 50 times a second, to
            - Serial (USB)  -> Serial Monitor, or the sampler's "USB test" button
            - the XIAO, over the same two I2C wires (SDA/SCL) the gyro uses.
              The XIAO listens at address 0x08 and sends the numbers over Bluetooth.
              No voltage divider is needed: the gyro board keeps SDA/SCL at 3.3 V.

  Wiring (same on both shoes):
     FSR:  one leg -> 5V, other leg -> A0, 220 ohm (red red brown) from A0 -> GND
     Gyro: VCC -> 5V, GND -> GND, SCL -> SCL, SDA -> SDA   (via A5/A4, the same pins)
     XIAO: 5V -> 5V, GND -> GND, D4 -> SCL, D5 -> SDA

  USB line format:  FOOT,fsr,pitch,roll,shake
     FOOT   L or R
     fsr    0..4095 (higher = more pressure)
     pitch  degrees, toe up / toe down
     roll   degrees, ankle rolling in / out
     shake  how fast the foot is rotating, degrees per second

  BEFORE UPLOADING: set FOOT below to 'L' or 'R'.
  No extra libraries needed.
*/

#include <Wire.h>
#include <math.h>

#define FOOT 'L'            // <<< 'L' for the left shoe, 'R' for the right shoe
#define FSR_PIN A0
#define SEND_HZ 50

const uint8_t MPU_ADDR  = 0x68;  // use 0x69 if the GY-521 AD0 pin is wired to VCC
const uint8_t XIAO_ADDR = 0x08;  // must match xiao_radio.ino

bool mpuOk = false;
float fsrF = 0, pitchF = 0, rollF = 0, shakeF = 0;
uint32_t lastSend = 0, lastRetry = 0;
uint8_t xiaoErr = 0;

// 8 bytes sent to the XIAO, little endian (same layout the XIAO sends to the laptop)
struct __attribute__((packed)) FootData {
  uint16_t fsr;       // 0..4095
  int16_t  pitch100;  // degrees * 100
  int16_t  roll100;   // degrees * 100
  uint16_t shake;     // deg/s
};

// The R4's Wire library turns on the chip's internal pull-ups, which go to 5 V.
// The XIAO only takes 3.3 V, so switch them off and let the gyro board's
// 2.2k pull-ups (to 3.3 V) set the level.
void i2cPullupsOff() {
  R_IOPORT_PinCfg(&g_ioport_ctrl, g_pin_cfg[WIRE_SDA_PIN].pin,
                  (uint32_t)(IOPORT_CFG_DRIVE_MID | IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC));
  R_IOPORT_PinCfg(&g_ioport_ctrl, g_pin_cfg[WIRE_SCL_PIN].pin,
                  (uint32_t)(IOPORT_CFG_DRIVE_MID | IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC));
}

// ---------- MPU-6050 helpers (raw I2C, no library) ----------
void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool mpuInit() {
  mpuWrite(0x6B, 0x00);   // wake up
  delay(50);
  mpuWrite(0x1A, 0x03);   // low pass filter ~44 Hz (smooths footstep jitter)
  mpuWrite(0x1B, 0x08);   // gyro range +/-500 deg/s  (65.5 per deg/s)
  mpuWrite(0x1C, 0x00);   // accel range +/-2 g       (16384 per g)
  Wire.beginTransmission(MPU_ADDR);
  return Wire.endTransmission() == 0;
}

bool mpuRead(int16_t v[7]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)14) != 14) return false;
  for (int i = 0; i < 7; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    v[i] = (int16_t)((hi << 8) | lo);
  }
  return true;
}

// ---------- output ----------
void sendLine(Stream &out) {
  out.print(FOOT);           out.print(',');
  out.print((int)fsrF);      out.print(',');
  out.print(pitchF, 2);      out.print(',');
  out.print(rollF, 2);       out.print(',');
  out.println((int)shakeF);
}

void sendToXiao() {
  FootData d;
  d.fsr      = (uint16_t)constrain((int)fsrF, 0, 65535);
  d.pitch100 = (int16_t)constrain(pitchF * 100.0f, -32000.0f, 32000.0f);
  d.roll100  = (int16_t)constrain(rollF * 100.0f, -32000.0f, 32000.0f);
  d.shake    = (uint16_t)constrain((int)shakeF, 0, 65535);
  Wire.beginTransmission(XIAO_ADDR);
  Wire.write((const uint8_t *)&d, sizeof(d));
  xiaoErr = Wire.endTransmission();   // 0 = the XIAO got it
}

void setup() {
  Serial.begin(115200);      // USB
  analogReadResolution(12);  // 0..4095
  Wire.begin();
  Wire.setClock(100000);     // 100 kHz: plenty for 50 messages a second, and easy on the XIAO
  i2cPullupsOff();
  mpuOk = mpuInit();
  delay(200);
  Serial.print("# Music Sampling Shoes Minima foot ");
  Serial.print(FOOT);
  Serial.println(mpuOk ? " ready, MPU-6050 found" : " ready, MPU-6050 NOT FOUND (check SDA/SCL/VCC)");
}

void loop() {
  uint32_t now = millis();
  if (now - lastSend < 1000 / SEND_HZ) return;
  lastSend = now;

  // FSR: average a few reads, light smoothing so stomps stay sharp
  long raw = 0;
  for (int i = 0; i < 4; i++) raw += analogRead(FSR_PIN);
  raw /= 4;
  fsrF += 0.5f * ((float)raw - fsrF);

  // MPU-6050: tilt from the accelerometer, shake from the gyro
  int16_t v[7];
  if (mpuOk && mpuRead(v)) {
    float ax = v[0] / 16384.0f, ay = v[1] / 16384.0f, az = v[2] / 16384.0f;
    float gx = v[4] / 65.5f,    gy = v[5] / 65.5f,    gz = v[6] / 65.5f;
    // The breadboard sits ACROSS the shoe, so toe up/down is the gyro's Y-axis tilt.
    // (If it ever sits heel-to-toe instead, swap these two lines back.)
    float p = atan2f(ay, az) * 57.2958f;                          // toe up / down
    float r = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.2958f;   // ankle in / out
    pitchF += 0.25f * (p - pitchF);
    rollF  += 0.25f * (r - rollF);
    float g = sqrtf(gx * gx + gy * gy + gz * gz);
    shakeF += 0.3f * (g - shakeF);
  } else if (now - lastRetry > 2000) {
    lastRetry = now;           // sensor unplugged? keep trying
    mpuOk = mpuInit();
  }

  sendToXiao();
  sendLine(Serial);

  // once a second, say if the XIAO is answering
  static uint32_t lastDbg = 0;
  if (now - lastDbg > 1000) {
    lastDbg = now;
    Serial.println(xiaoErr == 0 ? "# xiao: ok" : "# xiao: NOT ANSWERING (check D4/D5 wires and XIAO power)");
  }
}
