// Motor - Left
#define MOTOR_LEFT_PWM   2
#define MOTOR_LEFT_LPWM  3
#define MOTOR_LEFT_RPWM  4

// Motor - Right
#define MOTOR_RIGHT_PWM   7
#define MOTOR_RIGHT_LPWM  6
#define MOTOR_RIGHT_RPWM  5

void setup() {
  pinMode(MOTOR_LEFT_PWM, OUTPUT);
  pinMode(MOTOR_LEFT_LPWM, OUTPUT);
  pinMode(MOTOR_LEFT_RPWM, OUTPUT);

  pinMode(MOTOR_RIGHT_PWM, OUTPUT);
  pinMode(MOTOR_RIGHT_LPWM, OUTPUT);
  pinMode(MOTOR_RIGHT_RPWM, OUTPUT);

  stop(); // Başlangıçta durdur
}

void loop() {
  // Hızlan
  accelerateForward();
  delay(2000);  // Sabit hızda ilerleme süresi
  // Yavaşla
  decelerateStop();
  delay(3000); // Bekle, sonra tekrar et
}

void accelerateForward() {
  digitalWrite(MOTOR_LEFT_LPWM, LOW);
  digitalWrite(MOTOR_LEFT_RPWM, HIGH);

  digitalWrite(MOTOR_RIGHT_LPWM, LOW);
  digitalWrite(MOTOR_RIGHT_RPWM, HIGH);

  for (int speed = 0; speed <= 200; speed += 1) {
    analogWrite(MOTOR_LEFT_PWM, speed);
    analogWrite(MOTOR_RIGHT_PWM, speed+4);
    delay(50); // Hızlanma süresi (ivmelenme)
  }
}

void decelerateStop() {
  for (int speed = 200; speed >= 0; speed -= 1) {
    analogWrite(MOTOR_LEFT_PWM, speed);
    analogWrite(MOTOR_RIGHT_PWM, speed);
    delay(50); // Yavaşlama süresi (deceleration)
  }
  stop();
}

void stop() {
  analogWrite(MOTOR_LEFT_PWM, 0);
  digitalWrite(MOTOR_LEFT_LPWM, LOW);
  digitalWrite(MOTOR_LEFT_RPWM, LOW);

  analogWrite(MOTOR_RIGHT_PWM, 0);
  digitalWrite(MOTOR_RIGHT_LPWM, LOW);
  digitalWrite(MOTOR_RIGHT_RPWM, LOW);
}
