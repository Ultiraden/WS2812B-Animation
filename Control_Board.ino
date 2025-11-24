//Serial = main console seen on PC: user input & data display
//SerialUSB1 = used for animation sync signal & Master -> Slave communication
#define s1 SerialUSB1

void setup() {
  Serial.println("This may be sent before your PC is able to receive");
  while (!Serial) {}
  Serial.println("Serial Ready");
  Serial.println("");
  Serial.println("Enter only one character at a time: '?' for a list of all commands");
}
 
char incomingByte = 1;
void loop() {
  if (Serial.available()) {
    incomingByte = Serial.read(); 
  }
  
  if (incomingByte == 63) { // '?'
    PrintCommands();
    clear();
  }
  else if (incomingByte == 65) { // Select Animation 1 if input is 'A'
    SelectAnimation(1);
    clear();
  }
  else if (incomingByte == 66) { // Select Animation 2 if input is 'B'
    SelectAnimation(2);
    clear();
  }
  else if (incomingByte == 67) { // Select Animation 3 if input is 'C'
    SelectAnimation(3);
    clear();
  }
  delay(100);
}

void clear() {incomingByte = -1;} // reset byte char

void PrintCommands() {
  for (int i = 0; i < 5; i++) {
    Serial.println("");
  }
  Serial.println("'A' to select animation 1: Candycane");
  Serial.println("'B' to select animation 2: Shooting Stars");
  Serial.println("'C' to select animatino 3: Twinkling Lights");
  Serial.println("");
  Serial.println("");
  Serial.println("");
}

void SelectAnimation(int Animation) { // Parameter options are 1, 2, or 3
  if (Animation == 1) {s1.write("A");}
  else if (Animation == 2) {s1.write("B");}
  else if (Animation == 3) {s1.write("C");}
}
