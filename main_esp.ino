#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <TM1637Display.h>
#include <Wire.h>
#include <time.h>

#define SLAVE_ADDRESS 0x08

#define SLAVE_KEY_COUNT 5
#define SLAVE_KEY_OFFSET 0

int buzzerPins[] = {25, 26, 16, 17, 5, 23, 4};  // 7 buzzers

int tpin[] = {15, 13, 12, 14, 27, 33, 32};  // 7 touch sensors

#define KEY_COUNT 7          
#define LOCAL_KEY_OFFSET 5    
#define TOTAL_KEY_COUNT 12

#define MODE_BUTTON_PIN   35 
#define METRO_BUTTON_PIN  34 

#define SLIDE_PIN 39

#define CLK 18
#define DIO 19

TM1637Display display(CLK, DIO);

int localTones[] = {349, 370, 392, 415, 440, 466, 494};

const int touchThreshold[KEY_COUNT] = {500, 450, 475, 450, 530, 710, 650}; //low = less sensitive, high = more sensitive

#define MAX_NOTES 200

struct NoteEvent {
  byte note;          
  unsigned long time;
};

NoteEvent melody[MAX_NOTES];
int melodyLen = 0;

bool isRecording = false;
unsigned long recordStartTime = 0;


bool keyPressed[TOTAL_KEY_COUNT]; 


bool requestPlay = false;


float bpm = 120.0; 


bool metronomeOn = false;
unsigned long lastTickTime = 0;


bool modeBtnPressed = false;
unsigned long modeLastTime = 0;
int buttonMode = 0; 

bool metroBtnPressed = false;
unsigned long metroLastTime = 0;

const unsigned long debounceDelay = 200;

const char* ssid     = "yonugy";
const char* password = "yongwai!";

WebServer server(80);

char* firebaseHost = "https://piano-composer-default-rtdb.asia-southeast1.firebasedatabase.app";
char* firebaseBasePath = "/melodies";


void setup() {

  Serial.begin(115200);

  Wire.begin(21, 22);  // SDA=21, SCL=22
  delay(100);

  for(int i=0; i<TOTAL_KEY_COUNT; i++){
    keyPressed[i] = false;
  }

  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(METRO_BUTTON_PIN, INPUT_PULLUP);

  pinMode(SLIDE_PIN, INPUT);

  display.setBrightness(0x0f);
  analogReadResolution(12);

  Serial.println("Testing I2C to Slave...");
  Wire.beginTransmission(SLAVE_ADDRESS);
  byte error = Wire.endTransmission();
  if(error == 0){
    Serial.println("Slave ESP32 found!");
  } else {
    Serial.println("WARNING: Slave not responding!");
  }

  connectWiFi();
  setupServer();

  Serial.println("ESP32 MASTER Ready - 12-Key Piano");
  Serial.println("Slave (Keys 0-4): C, C#, D, D#, E");
  Serial.println("Local (Keys 5-11): F, F#, G, G#, A, A#, B");
  Serial.println("Mode Button (GPIO 35): Start -> Stop -> Play Latest");
  Serial.println("Metronome Button (GPIO 34): Toggle metronome on/off");
}

/* ------------------------------------------------ */

void loop() {

  // Handle web server only if WiFi is connected
  if(WiFi.status() == WL_CONNECTED){
    server.handleClient();
  }

  // Always scan keys - this is the priority!
  scanKeys();

  handleModeButton();
  handleMetronomeButton();

  updateMetronome();

  if(requestPlay){
    requestPlay = false;
    playMelody();
  }
}

/* ------------------------------------------------ */
/* ---------------- TOUCH KEYS -------------------- */

void scanKeys(){

  // Scan remote keys first (0-4) from Slave via I2C
  Wire.requestFrom(SLAVE_ADDRESS, SLAVE_KEY_COUNT);
  
  int keyIndex = SLAVE_KEY_OFFSET;  // Start from key 0
  
  while(Wire.available() && keyIndex < SLAVE_KEY_COUNT){
    byte keyState = Wire.read();
    
    bool pressed = (keyState == 1);
    
    if(pressed && !keyPressed[keyIndex]){
      
      keyPressed[keyIndex] = true;
      
      if(isRecording && melodyLen < MAX_NOTES){
        melody[melodyLen].note = keyIndex;
        melody[melodyLen].time = millis() - recordStartTime;
        melodyLen++;
      }
      
      Serial.print("Key ");
      Serial.println(keyIndex);
    }
    
    if(!pressed && keyPressed[keyIndex]){
      keyPressed[keyIndex] = false;
    }
    
    keyIndex++;
  }

  // Scan local keys (5-11)
  for(int t=0; t<KEY_COUNT; t++){

    int touchValue = touchRead(tpin[t]);
    bool pressed = touchValue < touchThreshold[t];
    
    int actualKey = LOCAL_KEY_OFFSET + t;  // Map to keys 5-11

    if(pressed && !keyPressed[actualKey]){

      keyPressed[actualKey] = true;

      // Turn on local buzzer
      ledcAttach(buzzerPins[t], localTones[t], 13);
      ledcWrite(buzzerPins[t], 4096);

      if(isRecording && melodyLen < MAX_NOTES){
        melody[melodyLen].note = actualKey;
        melody[melodyLen].time = millis() - recordStartTime;
        melodyLen++;
      }

      Serial.print("Key ");
      Serial.println(actualKey);
    }

    if(!pressed && keyPressed[actualKey]){
      keyPressed[actualKey] = false;
      ledcDetach(buzzerPins[t]);
    }
  }
}


void handleModeButton(){
  
  bool btnState = (digitalRead(MODE_BUTTON_PIN) == LOW);  // LOW when pressed with pull-up
  
  if(btnState && !modeBtnPressed && (millis() - modeLastTime > debounceDelay)){
    
    modeBtnPressed = true;
    modeLastTime = millis();
    
    // Execute action based on current mode
    if(buttonMode == 0){
      // Mode 0: Start Recording
      if(!isRecording){
        startRecording();
        Serial.println("Mode: START RECORDING");
      }
      buttonMode = 1;  // Next: Stop
      
    } else if(buttonMode == 1){
      // Mode 1: Stop Recording
      if(isRecording){
        stopRecording();
        Serial.println("Mode: STOP RECORDING");
      }
      buttonMode = 2;  // Next: Play
      
    } else if(buttonMode == 2){
      // Mode 2: Play Latest Melody
      if(melodyLen > 0){
        requestPlay = true;
        Serial.println("Mode: PLAY LATEST");
      } else {
        Serial.println("No recording to play");
      }
      buttonMode = 0;  // Next: Start
    }
  }
  
  if(!btnState){
    modeBtnPressed = false;
  }
}


void handleMetronomeButton(){
  
  bool btnState = (digitalRead(METRO_BUTTON_PIN) == LOW);  // LOW when pressed with pull-up
  
  if(btnState && !metroBtnPressed && (millis() - metroLastTime > debounceDelay)){
    
    metroBtnPressed = true;
    metroLastTime = millis();
    
    // Toggle metronome
    metronomeOn = !metronomeOn;
    Serial.println(metronomeOn ? "Metronome ON" : "Metronome OFF");
  }
  
  if(!btnState){
    metroBtnPressed = false;
  }
}


void playStartMelody(){
  // Send command to Slave to play C-E-G ascending melody
  Wire.beginTransmission(SLAVE_ADDRESS);
  Wire.write(0xFE);  // Command for start melody
  Wire.endTransmission();
  delay(500);  // Wait for melody to complete
}

void playStopMelody(){
  // Send command to Slave to play G-E-C descending melody
  Wire.beginTransmission(SLAVE_ADDRESS);
  Wire.write(0xFD);  // Command for stop melody
  Wire.endTransmission();
  delay(500);  // Wait for melody to complete
}

void startRecording(){

  playStartMelody();

  melodyLen = 0;
  recordStartTime = millis();
  isRecording = true;

  Serial.println("Recording started");
}

void stopRecording(){

  isRecording = false;

  playStopMelody();

  Serial.print("Recording stopped, notes = ");
  Serial.println(melodyLen);
}


void playMelody(){

  if(melodyLen == 0) return;

  bool prevMetro = metronomeOn;
  metronomeOn = false;

  unsigned long prevTime = 0;

  Serial.println("Playback");

  for(int i=0; i<melodyLen; i++){

    unsigned long waitTime = melody[i].time - prevTime;
    unsigned long startWait = millis();
    
    // Non-blocking wait - still scan keys during playback
    while(millis() - startWait < waitTime){
      scanKeys();
      delay(1);  // Minimal delay to prevent watchdog issues
    }

    int n = melody[i].note;

    // Play note on slave (0-4) or local (5-11) buzzer
    if(n < SLAVE_KEY_COUNT){
      // Slave buzzer (keys 0-4: C-E)
      Wire.beginTransmission(SLAVE_ADDRESS);
      Wire.write(n);  // 0-4
      Wire.write(1);
      Wire.endTransmission();
    } else {
      // Local buzzer (keys 5-11: F-B)
      int localIndex = n - LOCAL_KEY_OFFSET;  // Convert to 0-6
      ledcAttach(buzzerPins[localIndex], localTones[localIndex], 13);
      ledcWrite(buzzerPins[localIndex], 4096);
    }
    
    // Brief wait while allowing key scanning
    startWait = millis();
    while(millis() - startWait < 120){
      scanKeys();
      delay(1);
    }
    
    // Turn off buzzer
    if(n < SLAVE_KEY_COUNT){
      Wire.beginTransmission(SLAVE_ADDRESS);
      Wire.write(n);
      Wire.write(0);
      Wire.endTransmission();
    } else {
      int localIndex = n - LOCAL_KEY_OFFSET;
      ledcDetach(buzzerPins[localIndex]);
    }

    prevTime = melody[i].time;
  }

  metronomeOn = prevMetro;
}


void updateMetronome(){

  // Always read slider and update display BPM
  int raw = analogRead(SLIDE_PIN);
  bpm = map(raw, 0, 4095, 40, 200);
  display.showNumberDec(bpm, false, 4, 0);

  // Only tick if metronome is on
  if(!metronomeOn) return;

  unsigned long interval = 60000UL / bpm;
  unsigned long now = millis();

  if(now - lastTickTime >= interval){

    lastTickTime = now;

    // Send metronome tick command to Slave
    Wire.beginTransmission(SLAVE_ADDRESS);
    Wire.write(0xFF);  // Command for metronome tick
    Wire.endTransmission();
  }
}


void connectWiFi(){

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  // Non-blocking WiFi connection with timeout
  unsigned long startAttempt = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000){
    scanKeys();  // Keep keys responsive during WiFi connection
    delay(100);
    Serial.print(".");
  }

  Serial.println();
  if(WiFi.status() == WL_CONNECTED){
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
    
    // Configure NTP time
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // GMT+8 (Malaysia/Singapore)
    Serial.println("NTP time configured");
  } else {
    Serial.println("WiFi connection timeout - Piano still works!");
  }
}


void setupServer(){

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/play", handlePlay);
  server.on("/upload", handleUpload);
  server.on("/list", handleList);
  server.on("/delete", handleDelete);
  server.on("/get", handleGet);
  server.enableCORS(true);

  server.begin();
}


void handleRoot(){

  String html = "<!DOCTYPE html>\n"
  "<html>\n"
  "<head>\n"
  "  <meta charset='UTF-8'>\n"
  "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
  "  <title>Piano Melody Manager</title>\n"
  "  <style>\n"
  "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
  "    body {\n"
  "      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
  "      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
  "      min-height: 100vh;\n"
  "      padding: 20px;\n"
  "    }\n"
  "    .container {\n"
  "      max-width: 1200px;\n"
  "      margin: 0 auto;\n"
  "      background: white;\n"
  "      border-radius: 15px;\n"
  "      padding: 30px;\n"
  "      box-shadow: 0 10px 30px rgba(0,0,0,0.3);\n"
  "    }\n"
  "    h1 {\n"
  "      text-align: center;\n"
  "      color: #667eea;\n"
  "      margin-bottom: 10px;\n"
  "      font-size: 2em;\n"
  "    }\n"
  "    .subtitle {\n"
  "      text-align: center;\n"
  "      color: #666;\n"
  "      margin-bottom: 30px;\n"
  "    }\n"
  "    .controls {\n"
  "      display: flex;\n"
  "      gap: 15px;\n"
  "      margin-bottom: 30px;\n"
  "      flex-wrap: wrap;\n"
  "      justify-content: center;\n"
  "    }\n"
  "    button {\n"
  "      padding: 12px 25px;\n"
  "      font-size: 16px;\n"
  "      border: none;\n"
  "      border-radius: 8px;\n"
  "      cursor: pointer;\n"
  "      transition: all 0.3s ease;\n"
  "      font-weight: 600;\n"
  "    }\n"
  "    .btn-primary { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }\n"
  "    .btn-success { background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%); color: white; }\n"
  "    .btn-danger { background: linear-gradient(135deg, #eb3349 0%, #f45c43 100%); color: white; }\n"
  "    .btn-info { background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%); color: white; }\n"
  "    .btn-warning { background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%); color: white; }\n"
  "    button:hover { transform: translateY(-2px); }\n"
  "    button:disabled { opacity: 0.5; cursor: not-allowed; }\n"
  "    .melody-list {\n"
  "      display: grid;\n"
  "      grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));\n"
  "      gap: 20px;\n"
  "      margin-top: 30px;\n"
  "    }\n"
  "    .melody-card {\n"
  "      background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);\n"
  "      border-radius: 12px;\n"
  "      padding: 20px;\n"
  "      box-shadow: 0 5px 15px rgba(0,0,0,0.1);\n"
  "      transition: transform 0.3s ease;\n"
  "    }\n"
  "    .melody-card:hover { transform: translateY(-5px); }\n"
  "    .melody-card h3 { color: #333; margin-bottom: 10px; font-size: 1.3em; }\n"
  "    .melody-card .info { color: #666; font-size: 0.9em; margin-bottom: 15px; }\n"
  "    .melody-card .actions { display: flex; gap: 10px; flex-wrap: wrap; }\n"
  "    .melody-card button { flex: 1; min-width: 80px; padding: 8px 15px; font-size: 14px; }\n"
  "    .modal {\n"
  "      display: none;\n"
  "      position: fixed;\n"
  "      top: 0;\n"
  "      left: 0;\n"
  "      width: 100%;\n"
  "      height: 100%;\n"
  "      background: rgba(0,0,0,0.5);\n"
  "      z-index: 1000;\n"
  "      justify-content: center;\n"
  "      align-items: center;\n"
  "    }\n"
  "    .modal.active { display: flex; }\n"
  "    .modal-content {\n"
  "      background: white;\n"
  "      padding: 30px;\n"
  "      border-radius: 12px;\n"
  "      max-width: 500px;\n"
  "      width: 90%;\n"
  "    }\n"
  "    .modal-content h2 { margin-bottom: 20px; color: #667eea; }\n"
  "    .form-group { margin-bottom: 20px; }\n"
  "    .form-group label { display: block; margin-bottom: 8px; font-weight: 600; }\n"
  "    .form-group input {\n"
  "      width: 100%;\n"
  "      padding: 12px;\n"
  "      border: 2px solid #ddd;\n"
  "      border-radius: 8px;\n"
  "      font-size: 16px;\n"
  "    }\n"
  "    .modal-actions { display: flex; gap: 10px; justify-content: flex-end; }\n"
  "    .status {\n"
  "      padding: 15px;\n"
  "      border-radius: 8px;\n"
  "      margin-bottom: 20px;\n"
  "      text-align: center;\n"
  "      font-weight: 600;\n"
  "      display: none;\n"
  "    }\n"
  "    .status.success { background: #d4edda; color: #155724; }\n"
  "    .status.error { background: #f8d7da; color: #721c24; }\n"
  "    .status.info { background: #d1ecf1; color: #0c5460; }\n"
  "    .empty-state { text-align: center; padding: 60px 20px; color: #999; }\n"
  "    .loading { text-align: center; padding: 40px; color: #667eea; }\n"
  "    .spinner {\n"
  "      border: 4px solid #f3f3f3;\n"
  "      border-top: 4px solid #667eea;\n"
  "      border-radius: 50%;\n"
  "      width: 40px;\n"
  "      height: 40px;\n"
  "      animation: spin 1s linear infinite;\n"
  "      margin: 20px auto;\n"
  "    }\n"
  "    @keyframes spin {\n"
  "      0% { transform: rotate(0deg); }\n"
  "      100% { transform: rotate(360deg); }\n"
  "    }\n"
  "    .piano-keyboard {\n"
  "      display: flex;\n"
  "      gap: 5px;\n"
  "      margin-top: 15px;\n"
  "      justify-content: center;\n"
  "      flex-wrap: wrap;\n"
  "    }\n"
  "    .key {\n"
  "      width: 40px;\n"
  "      height: 120px;\n"
  "      background: white;\n"
  "      border: 2px solid #333;\n"
  "      border-radius: 0 0 5px 5px;\n"
  "      cursor: pointer;\n"
  "      transition: all 0.1s ease;\n"
  "      position: relative;\n"
  "    }\n"
  "    .key.active { background: #667eea; transform: translateY(2px); }\n"
  "    .key-label {\n"
  "      position: absolute;\n"
  "      bottom: 10px;\n"
  "      width: 100%;\n"
  "      text-align: center;\n"
  "      font-size: 12px;\n"
  "      font-weight: bold;\n"
  "      color: #666;\n"
  "    }\n"
  "  </style>\n"
  "</head>\n"
  "<body>\n"
  "  <div class='container'>\n"
  "    <h1>&#127929; Piano Melody Manager</h1>\n"
  "    <p class='subtitle'>12-Key Piano (C to B)</p>\n"
  "    <div id='status' class='status'></div>\n"
  "    \n"
  "    <div class='controls'>\n"
  "      <button class='btn-success' onclick='startRecording()'>&#9654; Start Recording</button>\n"
  "      <button class='btn-danger' onclick='stopRecording()'>&#9632; Stop Recording</button>\n"
  "      <button class='btn-info' onclick='playMelody()'>&#9835; Play Current</button>\n"
  "      <button class='btn-warning' onclick='showUploadModal()'>&#11014; Upload</button>\n"
  "      <button class='btn-primary' onclick='loadMelodies()'>&#128257; Refresh</button>\n"
  "    </div>\n"
  "    \n"
  "    <div class='piano-keyboard' id='pianoKeyboard'>\n"
  "      <div class='key' data-note='0'><span class='key-label'>C</span></div>\n"
  "      <div class='key' data-note='1'><span class='key-label'>C#</span></div>\n"
  "      <div class='key' data-note='2'><span class='key-label'>D</span></div>\n"
  "      <div class='key' data-note='3'><span class='key-label'>D#</span></div>\n"
  "      <div class='key' data-note='4'><span class='key-label'>E</span></div>\n"
  "      <div class='key' data-note='5'><span class='key-label'>F</span></div>\n"
  "      <div class='key' data-note='6'><span class='key-label'>F#</span></div>\n"
  "      <div class='key' data-note='7'><span class='key-label'>G</span></div>\n"
  "      <div class='key' data-note='8'><span class='key-label'>G#</span></div>\n"
  "      <div class='key' data-note='9'><span class='key-label'>A</span></div>\n"
  "      <div class='key' data-note='10'><span class='key-label'>A#</span></div>\n"
  "      <div class='key' data-note='11'><span class='key-label'>B</span></div>\n"
  "    </div>\n"
  "    \n"
  "    <div id='loading' class='loading' style='display:none'>\n"
  "      <div class='spinner'></div>\n"
  "      Loading...\n"
  "    </div>\n"
  "    \n"
  "    <div id='melodyList' class='melody-list'></div>\n"
  "  </div>\n"
  "  \n"
  "  <div id='uploadModal' class='modal'>\n"
  "    <div class='modal-content'>\n"
  "      <h2>Upload Melody</h2>\n"
  "      <div class='form-group'>\n"
  "        <label for='melodyName'>Melody Name:</label>\n"
  "        <input type='text' id='melodyName' placeholder='Enter melody name...'/>\n"
  "      </div>\n"
  "      <div class='modal-actions'>\n"
  "        <button class='btn-primary' onclick='uploadMelody()'>Upload</button>\n"
  "        <button class='btn-danger' onclick='closeUploadModal()'>Cancel</button>\n"
  "      </div>\n"
  "    </div>\n"
  "  </div>\n"
  "  \n"
  "  <script>\n"
  "    const FIREBASE_URL = 'https://piano-composer-default-rtdb.asia-southeast1.firebasedatabase.app/melodies.json';\n"
  "    let audioContext;\n"
  "    const tones = [262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494];\n"
  "    \n"
  "    function initAudio() {\n"
  "      if (!audioContext) {\n"
  "        audioContext = new (window.AudioContext || window.webkitAudioContext)();\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    function playTone(frequency, duration) {\n"
  "      initAudio();\n"
  "      const oscillator = audioContext.createOscillator();\n"
  "      const gainNode = audioContext.createGain();\n"
  "      oscillator.connect(gainNode);\n"
  "      gainNode.connect(audioContext.destination);\n"
  "      oscillator.frequency.value = frequency;\n"
  "      oscillator.type = 'sine';\n"
  "      gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);\n"
  "      gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + duration/1000);\n"
  "      oscillator.start(audioContext.currentTime);\n"
  "      oscillator.stop(audioContext.currentTime + duration/1000);\n"
  "    }\n"
  "    \n"
  "    async function startRecording() {\n"
  "      try {\n"
  "        const response = await fetch('/start');\n"
  "        const data = await response.text();\n"
  "        showStatus(data, 'success');\n"
  "      } catch (error) {\n"
  "        showStatus('Failed: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    async function stopRecording() {\n"
  "      try {\n"
  "        const response = await fetch('/stop');\n"
  "        const data = await response.text();\n"
  "        showStatus(data, 'success');\n"
  "      } catch (error) {\n"
  "        showStatus('Failed: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    async function playMelody() {\n"
  "      try {\n"
  "        const response = await fetch('/play');\n"
  "        const data = await response.text();\n"
  "        showStatus(data, 'info');\n"
  "      } catch (error) {\n"
  "        showStatus('Failed: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    function showUploadModal() {\n"
  "      document.getElementById('uploadModal').classList.add('active');\n"
  "      document.getElementById('melodyName').value = 'Melody ' + new Date().toLocaleString();\n"
  "    }\n"
  "    \n"
  "    function closeUploadModal() {\n"
  "      document.getElementById('uploadModal').classList.remove('active');\n"
  "    }\n"
  "    \n"
  "    async function uploadMelody() {\n"
  "      const name = document.getElementById('melodyName').value || 'Untitled';\n"
  "      try {\n"
  "        const response = await fetch('/upload?name=' + encodeURIComponent(name));\n"
  "        const data = await response.text();\n"
  "        showStatus(data, 'success');\n"
  "        closeUploadModal();\n"
  "        setTimeout(loadMelodies, 1000);\n"
  "      } catch (error) {\n"
  "        showStatus('Failed: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    async function loadMelodies() {\n"
  "      const listDiv = document.getElementById('melodyList');\n"
  "      const loadingDiv = document.getElementById('loading');\n"
  "      listDiv.innerHTML = '';\n"
  "      loadingDiv.style.display = 'block';\n"
  "      \n"
  "      try {\n"
  "        const response = await fetch(FIREBASE_URL);\n"
  "        const data = await response.json();\n"
  "        loadingDiv.style.display = 'none';\n"
  "        \n"
  "        if (!data || Object.keys(data).length === 0) {\n"
  "          listDiv.innerHTML = \"<div class='empty-state'><h3>No melodies yet</h3><p>Start recording and upload your first melody!</p></div>\";\n"
  "          return;\n"
  "        }\n"
  "        \n"
  "        const melodies = Object.entries(data)\n"
  "          .map(([id, melody]) => ({ id, ...melody }))\n"
  "          .sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0));\n"
  "        \n"
  "        melodies.forEach(melody => {\n"
  "          const card = createMelodyCard(melody);\n"
  "          listDiv.appendChild(card);\n"
  "        });\n"
  "      } catch (error) {\n"
  "        loadingDiv.style.display = 'none';\n"
  "        showStatus('Failed to load: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    function createMelodyCard(melody) {\n"
  "      const card = document.createElement('div');\n"
  "      card.className = 'melody-card';\n"
  "      const date = melody.timestamp ? new Date(melody.timestamp).toLocaleString() : 'Unknown';\n"
  "      const noteCount = melody.notes ? melody.notes.split(',').length : 0;\n"
  "      \n"
  "      card.innerHTML = \"<h3>&#9835; \" + (melody.name || 'Untitled') + \"</h3>\" +\n"
  "        \"<div class='info'>&#128197; \" + date + \"<br>&#127929; \" + noteCount + \" notes</div>\" +\n"
  "        \"<div class='actions'>\" +\n"
  "        \"<button class='btn-info' onclick='playMelodyInBrowser(\\\"\" + melody.id + \"\\\")'>&#9654; Play</button>\" +\n"
  "        \"<button class='btn-warning' onclick='editMelody(\\\"\" + melody.id + \"\\\",\\\"\" + melody.name + \"\\\")'>&#9998; Edit</button>\" +\n"
  "        \"<button class='btn-danger' onclick='deleteMelody(\\\"\" + melody.id + \"\\\")'>&#128465; Delete</button>\" +\n"
  "        \"</div>\";\n"
  "      \n"
  "      return card;\n"
  "    }\n"
  "    \n"
  "    async function playMelodyInBrowser(id) {\n"
  "      try {\n"
  "        const response = await fetch('https://piano-composer-default-rtdb.asia-southeast1.firebasedatabase.app/melodies/' + id + '.json');\n"
  "        const melody = await response.json();\n"
  "        \n"
  "        if (!melody || !melody.notes || !melody.times) {\n"
  "          showStatus('Invalid melody data', 'error');\n"
  "          return;\n"
  "        }\n"
  "        \n"
  "        const notes = melody.notes.split(',').map(n => parseInt(n));\n"
  "        const times = melody.times.split(',').map(t => parseInt(t));\n"
  "        \n"
  "        showStatus('Playing...', 'info');\n"
  "        const keys = document.querySelectorAll('.key');\n"
  "        \n"
  "        for (let i = 0; i < notes.length; i++) {\n"
  "          const noteIndex = notes[i];\n"
  "          const delay = i === 0 ? 0 : times[i] - times[i-1];\n"
  "          \n"
  "          await new Promise(resolve => setTimeout(resolve, delay));\n"
  "          \n"
  "          if (keys[noteIndex]) {\n"
  "            keys[noteIndex].classList.add('active');\n"
  "            setTimeout(() => keys[noteIndex].classList.remove('active'), 150);\n"
  "          }\n"
  "          \n"
  "          playTone(tones[noteIndex], 120);\n"
  "        }\n"
  "        \n"
  "        showStatus('Complete!', 'success');\n"
  "      } catch (error) {\n"
  "        showStatus('Failed to play: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    async function editMelody(id, currentName) {\n"
  "      const newName = prompt('Enter new name:', currentName);\n"
  "      if (!newName || newName === currentName) return;\n"
  "      \n"
  "      try {\n"
  "        const response = await fetch('https://piano-composer-default-rtdb.asia-southeast1.firebasedatabase.app/melodies/' + id + '.json');\n"
  "        const melody = await response.json();\n"
  "        melody.name = newName;\n"
  "        \n"
  "        const updateResponse = await fetch('https://piano-composer-default-rtdb.asia-southeast1.firebasedatabase.app/melodies/' + id + '.json', {\n"
  "          method: 'PUT',\n"
  "          headers: { 'Content-Type': 'application/json' },\n"
  "          body: JSON.stringify(melody)\n"
  "        });\n"
  "        \n"
  "        if (updateResponse.ok) {\n"
  "          showStatus('Renamed!', 'success');\n"
  "          loadMelodies();\n"
  "        } else {\n"
  "          showStatus('Failed to rename', 'error');\n"
  "        }\n"
  "      } catch (error) {\n"
  "        showStatus('Error: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    async function deleteMelody(id) {\n"
  "      if (!confirm('Delete this melody?')) return;\n"
  "      \n"
  "      try {\n"
  "        const response = await fetch('https://piano-composer-default-rtdb.asia-southeast1.firebasedatabase.app/melodies/' + id + '.json', {\n"
  "          method: 'DELETE'\n"
  "        });\n"
  "        \n"
  "        if (response.ok) {\n"
  "          showStatus('Deleted!', 'success');\n"
  "          loadMelodies();\n"
  "        } else {\n"
  "          showStatus('Failed to delete', 'error');\n"
  "        }\n"
  "      } catch (error) {\n"
  "        showStatus('Error: ' + error.message, 'error');\n"
  "      }\n"
  "    }\n"
  "    \n"
  "    function showStatus(message, type) {\n"
  "      const statusDiv = document.getElementById('status');\n"
  "      statusDiv.textContent = message;\n"
  "      statusDiv.className = 'status ' + type;\n"
  "      statusDiv.style.display = 'block';\n"
  "      setTimeout(() => {\n"
  "        statusDiv.style.display = 'none';\n"
  "      }, 5000);\n"
  "    }\n"
  "    \n"
  "    window.addEventListener('DOMContentLoaded', () => {\n"
  "      loadMelodies();\n"
  "    });\n"
  "    \n"
  "    document.getElementById('uploadModal').addEventListener('click', (e) => {\n"
  "      if (e.target.id === 'uploadModal') {\n"
  "        closeUploadModal();\n"
  "      }\n"
  "    });\n"
  "  </script>\n"
  "</body>\n"
  "</html>";

  server.send(200, "text/html", html);
}

void handleStart(){
  startRecording();
  server.send(200, "text/plain", "Recording started");
}

void handleStop(){
  stopRecording();
  server.send(200, "text/plain", "Recording stopped");
}

void handlePlay(){
  requestPlay = true;
  server.send(200, "text/plain", "Play");
}


void handleUpload(){

  String name = "Melody";
  if(server.hasArg("name")){
    name = server.arg("name");
  }

  bool ok = uploadToFirebase(name);

  if(ok) server.send(200, "text/plain", "Successfully uploaded to Firebase!");
  else   server.send(500, "text/plain", "Upload failed. Check Serial Monitor.");
}

/* ------------------------------------------------ */

bool uploadToFirebase(String name){

  if(melodyLen == 0) return false;

  HTTPClient http;

  // Get current time in milliseconds
  time_t now;
  time(&now);
  unsigned long long timestamp = (unsigned long long)now * 1000ULL;  // Convert to milliseconds
  String melodyId = String((unsigned long)now);  // Use seconds for ID

  String url = String(firebaseHost) + firebaseBasePath + "/" + melodyId + ".json";

  String json = "{";

  json += "\"name\":\"" + name + "\",";
  json += "\"id\":\"" + melodyId + "\",";
  json += "\"timestamp\":" + String(timestamp) + ",";

  json += "\"notes\":\"";
  for(int i=0;i<melodyLen;i++){
    json += String(melody[i].note);
    if(i < melodyLen-1) json += ",";
  }
  json += "\",";

  json += "\"times\":\"";
  for(int i=0;i<melodyLen;i++){
    json += String(melody[i].time);
    if(i < melodyLen-1) json += ",";
  }
  json += "\"";

  json += "}";

  http.begin(url);
  http.addHeader("Content-Type","application/json");

  int code = http.PUT(json);

  http.end();

  Serial.print("Firebase HTTP code = ");
  Serial.println(code);

  return (code > 0 && code < 400);
}

void handleList(){
  HTTPClient http;
  String url = String(firebaseHost) + firebaseBasePath + ".json";
  
  http.begin(url);
  int code = http.GET();
  
  if(code > 0){
    String payload = http.getString();
    server.send(200, "application/json", payload);
  } else {
    server.send(500, "text/plain", "Failed to fetch melodies");
  }
  
  http.end();
}

void handleDelete(){
  if(!server.hasArg("id")){
    server.send(400, "text/plain", "Missing id parameter");
    return;
  }
  
  String id = server.arg("id");
  HTTPClient http;
  String url = String(firebaseHost) + firebaseBasePath + "/" + id + ".json";
  
  http.begin(url);
  int code = http.sendRequest("DELETE");
  
  if(code > 0 && code < 400){
    server.send(200, "text/plain", "Melody deleted");
  } else {
    server.send(500, "text/plain", "Failed to delete melody");
  }
  
  http.end();
}

void handleGet(){
  if(!server.hasArg("id")){
    server.send(400, "text/plain", "Missing id parameter");
    return;
  }
  
  String id = server.arg("id");
  HTTPClient http;
  String url = String(firebaseHost) + firebaseBasePath + "/" + id + ".json";
  
  http.begin(url);
  int code = http.GET();
  
  if(code > 0){
    String payload = http.getString();
    server.send(200, "application/json", payload);
  } else {
    server.send(500, "text/plain", "Failed to fetch melody");
  }
  
  http.end();
}
