//Serial = main console seen on PC
//SerialUSB1 = used for animation sync signal

#define BUS_2 Serial1

void setup() {
  BUS_2.begin(38400); // initialize 
  Serial.println("This may be sent before your PC is able to receive");
  while (!Serial) {}
  Serial.println("Serial Ready");
  Serial.println("");
  Serial.println("Enter only one key at a time: '?' for a list of all commands");
}

bool 

void loop() {
  if (Serial.available()) {
    incomingByte = Serial.read(); 
  }
  if (incomingByte == "?") {
    PrintCommands();
  }
  else if (incomingByte == "A") { // Select Animation 1

  }
  else if (incomingByte == "B") { // Select Animation 2

  }
  else if (incomingByte == "C") { // Select Animation 3

  }
}

void PrintCommands() {
  for (int i = 0; i < 5; i++) {
    Serial.println("");
  }
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
}
