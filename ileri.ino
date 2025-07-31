// Motor - Left
#define MOTOR_LEFT_PWM   7
#define MOTOR_LEFT_LPWM  6
#define MOTOR_LEFT_RPWM  5

// Motor - Right
#define MOTOR_RIGHT_PWM   2
#define MOTOR_RIGHT_LPWM  3
#define MOTOR_RIGHT_RPWM  4

void setup() {
  pinMode(MOTOR_LEFT_PWM, OUTPUT);
  pinMode(MOTOR_LEFT_LPWM, OUTPUT);
  pinMode(MOTOR_LEFT_RPWM, OUTPUT);

  pinMode(MOTOR_RIGHT_PWM, OUTPUT);
  pinMode(MOTOR_RIGHT_LPWM, OUTPUT);
  pinMode(MOTOR_RIGHT_RPWM, OUTPUT);

}

void loop() {
  // Hızlan
  accelerateForward();
  // Yavaşla
}

void accelerateForward() {
  analogWrite(MOTOR_LEFT_LPWM, 0);
  analogWrite(MOTOR_LEFT_RPWM, 255);

  analogWrite(MOTOR_RIGHT_LPWM, 0);
  analogWrite(MOTOR_RIGHT_RPWM, 250);

  digitalWrite(MOTOR_LEFT_PWM, HIGH);
  digitalWrite(MOTOR_RIGHT_PWM, HIGH);
}
