#include <Wire.h>

#define SLAVE_ADDRESS 0x08


int buzzerPins[] = {25, 26, 16, 17, 5};

#define METRO_BUZZER_PIN 23

int tpin[] = {33, 27, 14, 12, 13};

#define KEY_COUNT 5

int tones[] = {262, 277, 294, 311, 330};


const int touchThreshold[KEY_COUNT] = {610, 550, 550, 450, 595}; //low = less sensitive, high = more sensitive


bool keyPressed[KEY_COUNT];
byte touchStates[KEY_COUNT];


void setup() {

  Serial.begin(115200);

  // Initialize key states
  for(int i=0; i<KEY_COUNT; i++){
    keyPressed[i] = false;
    touchStates[i] = 0;
  }

  // Initialize I2C as Slave
  Wire.setPins(21, 22);                // SDA=21, SCL=22
  Wire.begin(SLAVE_ADDRESS);           // Slave address
  Wire.onRequest(requestEvent);        // Register request handler
  Wire.onReceive(receiveEvent);        // Register receive handler

  Serial.println("ESP32 SLAVE Ready");
  Serial.println("Handling Keys 7-11 (C, C#, D, D#, E)");
}


void loop() {

  // Continuously scan touch sensors
  scanKeys();
  
  delay(10);
}


void scanKeys(){

  for(int t=0; t<KEY_COUNT; t++){

    int touchValue = touchRead(tpin[t]);
    bool pressed = touchValue < touchThreshold[t];

    if(pressed && !keyPressed[t]){
      keyPressed[t] = true;
      touchStates[t] = 1;
      
      // Turn on local buzzer immediately
      ledcAttach(buzzerPins[t], tones[t], 13);
      // Use higher duty cycle for pins 2-4 (D, D#, E)
      int duty = (t >= 2) ? 6000 : 4096;
      ledcWrite(buzzerPins[t], duty);
      
      Serial.print("Key ");
      Serial.print(t);  // Display as key 0-4
      Serial.print(" (");
      Serial.print(tones[t]);
      Serial.println(" Hz)");
    }

    if(!pressed && keyPressed[t]){
      keyPressed[t] = false;
      touchStates[t] = 0;
      
      // Turn off local buzzer
      ledcDetach(buzzerPins[t]);
    }
  }
}

// Called when Master requests data
void requestEvent(){
  // Send all 5 touch states
  Wire.write(touchStates, KEY_COUNT);
}

// Called when Master sends data
void receiveEvent(int numBytes){
  
  if(numBytes == 1){
    // Single byte command - metronome control
    byte command = Wire.read();
    
    if(command == 0xFE){
      // Start melody: C-E-G
      playStartMelody();
    }
    else if(command == 0xFD){
      // Stop melody: G-E-C
      playStopMelody();
    }
    else if(command == 0xFF){
      // Metronome tick
      playMetronomeTick();
    }
  }
  else if(numBytes >= 2){
    // Two byte command - regular buzzer control [key_index, on/off]
    byte keyIndex = Wire.read();
    byte onOff = Wire.read();
    
    if(keyIndex < KEY_COUNT){
      if(onOff == 1){
        // Turn on buzzer
        ledcAttach(buzzerPins[keyIndex], tones[keyIndex], 13);
        // Use higher duty cycle for keys 2-4 (D, D#, E)
        int duty = (keyIndex >= 2) ? 6000 : 4096;
        ledcWrite(buzzerPins[keyIndex], duty);
      } else {
        // Turn off buzzer
        ledcDetach(buzzerPins[keyIndex]);
      }
    }
  }
  
  // Clear any remaining bytes
  while(Wire.available()){
    Wire.read();
  }
}


void playStartMelody(){
  // C-E-G ascending
  ledcAttach(METRO_BUZZER_PIN, 262, 13);  // C
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(150);
  ledcDetach(METRO_BUZZER_PIN);
  
  ledcAttach(METRO_BUZZER_PIN, 330, 13);  // E
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(150);
  ledcDetach(METRO_BUZZER_PIN);
  
  ledcAttach(METRO_BUZZER_PIN, 392, 13);  // G
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(150);
  ledcDetach(METRO_BUZZER_PIN);
}

void playStopMelody(){
  // G-E-C descending
  ledcAttach(METRO_BUZZER_PIN, 392, 13);  // G
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(150);
  ledcDetach(METRO_BUZZER_PIN);
  
  ledcAttach(METRO_BUZZER_PIN, 330, 13);  // E
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(150);
  ledcDetach(METRO_BUZZER_PIN);
  
  ledcAttach(METRO_BUZZER_PIN, 262, 13);  // C
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(150);
  ledcDetach(METRO_BUZZER_PIN);
}

void playMetronomeTick(){
  // 1000 Hz tick
  ledcAttach(METRO_BUZZER_PIN, 1000, 13);
  ledcWrite(METRO_BUZZER_PIN, 4096);
  delay(20);
  ledcDetach(METRO_BUZZER_PIN);
}
