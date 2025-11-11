/*
 * Toy Gun Turret Control - Step 1
 * Control 2 servos and set them to 90 degrees
 *
 * Hardware:
 * - Horizontal servo on GPIO 12
 * - Vertical servo on GPIO 13
 * - Both servos powered by 6V buck converter (not ESP32!)
 */

#include <ESP32Servo.h>

// GPIO pin definitions
#define SERVO_PIN_HORIZONTAL 12
#define SERVO_PIN_VERTICAL   13

// Servo objects
Servo horizontalServo;
Servo verticalServo;

// Current angle variables (tracking servo positions)
int horizontalAngle = 80;  // Center position (0-180)
int verticalAngle = 80;    // Center position (0-180)

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  Serial.println("Toy Gun Turret - Step 1: Servo Control");

  // Attach servos to GPIO pins
  horizontalServo.attach(SERVO_PIN_HORIZONTAL);
  verticalServo.attach(SERVO_PIN_VERTICAL);

  // Set both servos to 90 degrees (center position)
  horizontalServo.write(horizontalAngle);
  verticalServo.write(verticalAngle);

  Serial.println("Servos initialized to 90 degrees");
  Serial.print("Horizontal angle: ");
  Serial.println(horizontalAngle);
  Serial.print("Vertical angle: ");
  Serial.println(verticalAngle);
}

void loop() {
  // Step 1: Just hold position at 90 degrees
  // (No movement in this step - servos stay at center)
  delay(1000);
}
