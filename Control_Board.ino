//Serial = main console seen on PC: user input & data display
//SerialUSB1 = used for animation sync signal & Master -> Slave communication
#define s1 SerialUSB1
int Refresh = 100; // default refresh rate in ms for all animations
int Default_Brightness = 2; // default brightness; range is 1-5 with 1 being off, 5 being 80% brightness

void setup() {
  Serial.println("This may be sent before your PC is able to receive");
  while (!Serial) {}
  Serial.println("Serial Ready");
  Serial.println("");
  Serial.println("Enter only one character at a time: '?' for a list of all commands");
  SetBrightness(Default_Brightness + 48);
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
  else if (incomingByte >= 49 && incomingByte <= 53) { // '1', '2', '3', '4', '5' = 0%, 20%, 40%, 60%, 80% brightness
    SetBrightness(incomingByte);
    clear();
  }
  else if (incomingByte == 110) { // 'n' for off
    Off();
    clear();
  }
  else if (incomingByte == 32) { // ' ' to start/stop toggle
    run();
    clear();
  }
  delay(500); // monitor delay 
}

void clear() {incomingByte = 1;} // reset byte char

void PrintCommands() { // print to console list of all possible commands
  for (int i = 0; i < 5; i++) {
    Serial.println("");
  }
  Serial.println("'A' to select animation 1: Candycane");
  Serial.println("'B' to select animation 2: Shooting Stars");
  Serial.println("'C' to select animatino 3: Twinkling Lights");
  Serial.println("'1', '2', '3', '4', or '5' to set the global brightness level");
  Serial.println("'n' to quickly turn off all LEDs. Can be used while animations are playing");
  Serial.println("' ' to start running animation; ' ' again to stop animation");
}

void SelectAnimation(int Animation) { // Parameter options are 1, 2, or 3
  if (Animation == 1) {s1.write("A");}
  else if (Animation == 2) {s1.write("B");}
  else if (Animation == 3) {s1.write("C");}
}

void SetBrightness(char brightness) { // use 251 -> 255 for percent brightness
  s1.write(brightness + 202);
}

void Off() {s1.write("$");} // shuts off all LEDs

void run() { // when animations are running, all other controls are un-usable, until animation is toggled off; off toggle halts the animation and shuts off all LEDs
  char incoming = 1;
  while (incoming != 32) {
    if (Serial.available()) {
      incoming = Serial.read();
    }
    if (incoming == 110) {
      incoming = 32;
      Off();
    }
    s1.write("%");
    delay(Refresh);
  }
}
