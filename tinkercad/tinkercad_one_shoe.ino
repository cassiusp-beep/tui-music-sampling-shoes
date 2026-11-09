/*
  Music Sampling Shoes : Tinkercad model (one shoe per Uno)
  ---------------------------------------------------------
  Make TWO Arduino Uno R3 boards in Tinkercad: one for the Left shoe, one for the Right shoe.
  Paste this code into both. On the second one, change FOOT to 'R'.

  Parts on each Uno (only the real parts, nothing extra):
    Force Sensor  = the FSR in the sole
    Tilt Sensor   = stands in for the gyro (Tinkercad has no gyro)

  Wiring on each Uno:
    Force Sensor: one leg to 5V, other leg to A0, and a 10k resistor from A0 to GND
    Tilt Sensor:  one side to pin 2, other side to GND (no resistor needed)

  Try it: click Start Simulation, open the Serial Monitor,
          drag the Force Sensor slider and click the Tilt Sensor to tip it.
*/

#define FOOT 'L'          // 'L' on the left shoe's Uno, 'R' on the right shoe's Uno

const int FSR_PIN = A0;
const int TILT_PIN = 2;

void setup() {
  Serial.begin(9600);
  pinMode(TILT_PIN, INPUT_PULLUP);   // reads LOW when tipped one way, HIGH the other
}

void loop() {
  int pressure = analogRead(FSR_PIN);            // 0 = no press, up to about 1000 = hard press
  int tilted = digitalRead(TILT_PIN) == LOW ? 1 : 0;

  Serial.print(FOOT);
  Serial.print(" shoe  pressure: ");
  Serial.print(pressure);
  Serial.print("  tilted: ");
  Serial.println(tilted ? "yes" : "no");
  delay(200);
}
