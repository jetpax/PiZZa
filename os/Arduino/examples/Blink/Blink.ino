/*
 * Blink — toggles the on-board green ACT LED via Arduino's
 * `LED_BUILTIN` macro. On the Pi Zero 2 W variant that resolves to
 * D14 = BCM GPIO 29, wired in the variant overlay's
 * `builtin-led-gpios`. No external LED required.
 */

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
}
