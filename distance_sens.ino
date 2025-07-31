#define Distance_sens A0
#define Buzzer 7



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  
  pinMode(Distance_sens, INPUT);
  pinMode(Buzzer, OUTPUT);
  
  digitalWrite(Buzzer, LOW);


}

void loop() {
  // put your main code here, to run repeatedly:
  int distance = analogRead(Distance_sens);
  Serial.println(distance);
 

}
