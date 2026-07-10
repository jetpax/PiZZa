/*
 * Fade — ramps an LED up and down with hardware PWM via
 * `analogWrite()`. The BCM283x PWM block reaches exactly two 40-pin
 * header pins on the PiZZa boards:
 *
 *   D15 = BCM GPIO 12 (PWM channel 0), header pin 32
 *   D16 = BCM GPIO 13 (PWM channel 1), header pin 33
 *
 * Wire an LED (+ series resistor, e.g. 330R) from header pin 32 to
 * ground. `analogWrite()` on any other pin has no hardware PWM and
 * falls back to plain digital HIGH/LOW at half scale — the on-board
 * ACT LED (`LED_BUILTIN`) cannot fade.
 */

const int fadePin = 15; // D15 = GPIO 12, header pin 32

void setup() {
}

void loop() {
  for (int b = 0; b <= 255; b += 5) {
    analogWrite(fadePin, b);
    delay(30);
  }
  for (int b = 255; b >= 0; b -= 5) {
    analogWrite(fadePin, b);
    delay(30);
  }
}
