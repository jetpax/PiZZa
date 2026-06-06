/*
 * Blink — toggles an LED wired to D13 (= BCM GPIO 11, header pin 23).
 * The Pi Zero 2 W has no on-board LED on the 40-pin header; wire an LED
 * + 330 ohm resistor between header pin 23 and ground (header pin 25).
 */

const int LED = 13;

void setup() {
  pinMode(LED, OUTPUT);
}

void loop() {
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(500);
}
