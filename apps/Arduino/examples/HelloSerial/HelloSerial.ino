/*
 * HelloSerial — prints once at boot then echoes any received bytes back.
 * Console is the mini-UART on header pins 8/10 (BCM GPIO 14/15) at 115200.
 */

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("HelloSerial: PiZZA Arduino sketch, type something:");
}

void loop() {
  while (Serial.available()) {
    int c = Serial.read();
    Serial.write(c);
  }
  delay(1);
}
