// Motor - Left
#define MOTOR_LEFT_PWM   2
#define MOTOR_LEFT_LPWM  3
#define MOTOR_LEFT_RPWM  4

// Motor - Right
#define MOTOR_RIGHT_PWM   7
#define MOTOR_RIGHT_LPWM  6
#define MOTOR_RIGHT_RPWM  5

void setup() {
  // Pin modları
  pinMode(MOTOR_LEFT_PWM, OUTPUT);
  pinMode(MOTOR_LEFT_LPWM, OUTPUT);
  pinMode(MOTOR_LEFT_RPWM, OUTPUT);

  pinMode(MOTOR_RIGHT_PWM, OUTPUT);
  pinMode(MOTOR_RIGHT_LPWM, OUTPUT);
  pinMode(MOTOR_RIGHT_RPWM, OUTPUT);

  stop(); // Başlangıçta motorları durdur
}

void loop() {
  forward();
 // Biraz beklet, sürekli tekrar etmesin
}

void forward() {
  analogWrite(MOTOR_LEFT_PWM, 200);      // PWM sinyali
  digitalWrite(MOTOR_LEFT_LPWM, LOW);    // LPWM LOW
  digitalWrite(MOTOR_LEFT_RPWM, HIGH);   // RPWM HIGH (ileri)
/*
  analogWrite(MOTOR_RIGHT_PWM, 200);
  digitalWrite(MOTOR_RIGHT_LPWM, LOW);
  digitalWrite(MOTOR_RIGHT_RPWM, HIGH);*/
}


void backward() {
  analogWrite(MOTOR_LEFT_PWM, 200);
  digitalWrite(MOTOR_LEFT_LPWM, HIGH);
  digitalWrite(MOTOR_LEFT_RPWM, LOW);
/*
  analogWrite(MOTOR_RIGHT_PWM, 200);
  digitalWrite(MOTOR_RIGHT_LPWM, HIGH);
  digitalWrite(MOTOR_RIGHT_RPWM, LOW);*/
}

void stop() {
  analogWrite(MOTOR_LEFT_PWM, 0);
  digitalWrite(MOTOR_LEFT_LPWM, LOW);
  digitalWrite(MOTOR_LEFT_RPWM, LOW);

  analogWrite(MOTOR_RIGHT_PWM, 0);
  digitalWrite(MOTOR_RIGHT_LPWM, LOW);
  digitalWrite(MOTOR_RIGHT_RPWM, LOW);
}
