

// Verwendete Bibliotheken
#include <WiFi.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TonUINO_html.h>
#include <Arduino.h>
#include "DFRobotDFPlayerMini.h"
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <math.h>
#include <FastLED.h>
#include <Preferences.h>

//EEPROM Speicher *NVS*
Preferences preferences;
int timeout = 20;


//========================================================================
//Hier nur VAriablen zum Testen

int l = 0;
bool headphoneIn =0 ;
int success = 0;
int success2 = 0;

bool debug = false; // Auf true setzen um debug Informationen über die Serielle Schnittstelle zu erhalten.

//Variablen zum Speichern von EInstellungen
//========================================================================

unsigned long last_color = 0xFFFFFF;
unsigned int last_Volume;
unsigned int last_max_Volume;


//========================================================================
//WS2812b Einstellungen

#define DATA_PIN 2 //signal pin 
#define NUM_LEDS 7 //number of LEDs in your strip
#define BRIGHTNESS 32  //brightness  (max 254) 
#define LED_TYPE WS2811  // I know we are using ws2812, but it's ok!
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

//============Sunrise Variablen===========================================
DEFINE_GRADIENT_PALETTE( sunrise_gp ) {
  0,     0,  0,  0,   //schwarz
128,   240,  0,  0,   //rot
224,   240,240,  0,   //gelb
255,   128,128,240 }; //sehr helles Blau

static uint16_t heatIndex = 0; // start out at 0

//========================================================================
//NTP Variablen
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

//Variable zum Auslesen der HTML Antwort
String TimerOFF = "00:00";
String TimerON = "00:00";
uint8_t TMR_OFF_HH, TMR_OFF_MM,TMR_ON_HH,TMR_ON_MM;
int TMR_OFF_REP = 0;
int TMR_ON_REP = 0;
unsigned int max_Volume = 20;
unsigned int akt_Volume = 10;
bool TMP_OFFTIME = true;
bool TMP_ONTIME = true;
bool WakeUpLight = false;
bool SleepLight = false;
bool startSR = false;

//Set Pins for RC522 Module
const int resetPin = 22; // Reset pin
const int ssPin = 21;    // Slave select pin

//Objekt zur kommunikation mit dem Modul anlegen
MFRC522 mfrc522 = MFRC522(ssPin, resetPin); // Create instance

// this object stores nfc tag data
struct nfcTagObject {
  uint32_t cookie;
  uint8_t version;
  uint8_t folder;
  uint8_t mode;
  uint8_t special;
  uint32_t color;
};

//=======================Funktionen Deklarieren==============================

nfcTagObject myCard;
void resetCard(void);
bool readCard(nfcTagObject *nfcTag);
void setupCard(void);
static void nextTrack();
void startTimer(void);
void stoppTimer(void);
int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0);

bool knownCard = false;
uint16_t numTracksInFolder;
uint16_t track;

//====================Timer Deklaration=====================================

hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t isrCounter = 0;
volatile uint32_t lastIsrAt = 0;

void IRAM_ATTR onTimer(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  isrCounter++;
  lastIsrAt = millis();
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

// implement a notification class,
// its member methods will get called
//
class Mp3Notify {
public:
  static void OnError(uint16_t errorCode) {
    // see DfMp3_Error for code meaning
    Serial.println();
    Serial.print("Com Error ");
    Serial.println(errorCode);
  }
  static void OnPlayFinished(uint16_t track) {
    Serial.print("Track beendet");
    Serial.println(track);
    delay(100);
    nextTrack();
  }
  static void OnCardOnline(uint16_t code) {
    Serial.println(F("SD Karte online "));
  }
  static void OnCardInserted(uint16_t code) {
    Serial.println(F("SD Karte bereit "));
  }
  static void OnCardRemoved(uint16_t code) {
    Serial.println(F("SD Karte entfernt "));
  }
};

HardwareSerial mySoftwareSerial(2);
DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);

// Leider kann das Modul keine Queue abspielen.
static void nextTrack() {
  if (knownCard == false)
    // Wenn eine neue Karte angelernt wird soll das Ende eines Tracks nicht
    // verarbeitet werden
    return;

  if (myCard.mode == 1) {
    Serial.println(F("Hörspielmodus ist aktiv -> Strom sparen"));
    myDFPlayer.sleep();
  }
  if (myCard.mode == 2) {
    if (track != numTracksInFolder) {
      track = track + 1;
      myDFPlayer.playFolder(myCard.folder, track);
      Serial.print(F("Albummodus ist aktiv -> nächster Track: "));
      Serial.print(track);
    } else
      myDFPlayer.sleep();
  }
  if (myCard.mode == 3) {
    track = random(1, numTracksInFolder + 1);
    Serial.print(F("Party Modus ist aktiv -> zufälligen Track spielen: "));
    Serial.println(track);
    myDFPlayer.playFolder(myCard.folder, track);
  }
  if (myCard.mode == 4) {
    Serial.println(F("Einzel Modus aktiv -> Strom sparen"));
    myDFPlayer.sleep();
  }
  if (myCard.mode == 5) {
    if (track != numTracksInFolder) {
      track = track + 1;
      Serial.print(F("Hörbuch Modus ist aktiv -> nächster Track und "
                     "Fortschritt speichern"));
      Serial.println(track);
      myDFPlayer.playFolder(myCard.folder, track);
      // Fortschritt im EEPROM abspeichern
      EEPROM.write(myCard.folder, track);
    } else
      myDFPlayer.sleep();
      // Fortschritt zurück setzen
      EEPROM.write(myCard.folder, 1);
  }
  delay(500);
}

static void previousTrack() {
  if (myCard.mode == 1) {
    Serial.println(F("Hörspielmodus ist aktiv -> Track von vorne spielen"));
    myDFPlayer.playFolder(myCard.folder, track);
  }
  if (myCard.mode == 2) {
    Serial.println(F("Albummodus ist aktiv -> vorheriger Track"));
    if (track != 1) {
      track = track - 1;
    }
    myDFPlayer.playFolder(myCard.folder, track);
  }
  if (myCard.mode == 3) {
    Serial.println(F("Party Modus ist aktiv -> Track von vorne spielen"));
    myDFPlayer.playFolder(myCard.folder, track);
  }
  if (myCard.mode == 4) {
    Serial.println(F("Einzel Modus aktiv -> Track von vorne spielen"));
    myDFPlayer.playFolder(myCard.folder, track);
  }
  if (myCard.mode == 5) {
    Serial.println(F("Hörbuch Modus ist aktiv -> vorheriger Track und "
                     "Fortschritt speichern"));
    if (track != 1) {
      track = track - 1;
    }
    myDFPlayer.playFolder(myCard.folder, track);
    // Fortschritt im EEPROM abspeichern
    EEPROM.write(myCard.folder, track);
  }
}


MFRC522::MIFARE_Key key;
bool successRead;
byte sector = 1;
byte blockAddr = 4;
byte trailerBlock = 7;
MFRC522::StatusCode status;

#define buttonPause 25
#define buttonUp 26
#define buttonDown 27
#define busyPin 4
#define headphonePin 32
#define dfpMute 33

#define LONG_PRESS 1000

Button pauseButton(buttonPause);
Button upButton(buttonUp,100);
Button downButton(buttonDown,100);
bool ignorePauseButton = false;
bool ignoreUpButton = false;
bool ignoreDownButton = false;

uint8_t numberOfCards = 0;

bool isPlaying() { return !digitalRead(busyPin); }


//Funktion um die Antworten der HTML Seite auszuwerten
void handleRestart(){
  // Restart ESP
  ESP.restart();
}
void handleSetup(){
  server.send ( 200, "text/html", SetupPage());
  if (server.args() > 0 ) { // Arguments were received
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      
      Serial.print("Vom Server wurde folgendes empfangen: "); // Display the argument
      Serial.print(server.argName(i)); // Display the argument
      Serial.print("=");
      Serial.println(server.arg(i));
      
      if(server.argName(i) == "ssid" ){
        Serial.print("Speichere SSID: ");
        Serial.println(server.arg(i));
        preferences.putString("SSID", server.arg(i));
   
      }
        
      else if(server.argName(i) == "pw"){
        Serial.print("Speichere PW: ");
        Serial.println(server.arg(i));
        preferences.putString("Password", server.arg(i));

      }


      
       
    }
     
  }

}
void handleRoot(){ 
     server.send ( 200, "text/html", getPage() );
  if (server.args() > 0 ) { // Arguments were received
    for ( uint8_t i = 0; i < server.args(); i++ ) {

      Serial.print("Vom Server wurde folgendes empfangen: "); // Display the argument
      Serial.print(server.argName(i)); // Display the argument
      Serial.print("=");
      Serial.println(server.arg(i));
     
      if(server.argName(i) == "appt-time-off" ){

          TimerOFF = server.arg(i);
          char charBuf[TimerOFF.length() + 1];
          TimerOFF.toCharArray(charBuf, TimerOFF.length()+1);
          TMR_OFF_HH = atof(strtok(charBuf, ":"));
          TMR_OFF_MM = atof(strtok(NULL, ":"));
          Serial.print("Die eingelesene Zeit für TimerOFF: ");
          Serial.print(TMR_OFF_HH);
          Serial.print(":");
          Serial.println(TMR_OFF_MM);
          TMR_OFF_REP = 0;
          TMP_OFFTIME = false;
          
      }
        
      else if(server.argName(i) == "cb_tmr_off"){
        TMR_OFF_REP = 1;
        Serial.println("Für den TimerOFF wurde eine Wiederholung eingestellt");
        }

      else if(server.argName(i) == "appt-time-on"){
        
          TimerON = server.arg(i);
          char charBuf[TimerON.length() + 1];
          TimerON.toCharArray(charBuf, TimerON.length()+1);
          TMR_ON_HH = atof(strtok(charBuf, ":"));
          TMR_ON_MM = atof(strtok(NULL, ":"));
          Serial.print("Die eingelesene Zeit für TimerON: ");
          Serial.print(TMR_ON_HH);
          Serial.print(":");
          Serial.println(TMR_ON_MM);
          TMR_ON_REP = 0;
          TMP_ONTIME = false;
        }

      else if(server.argName(i) == "cb_tmr_on"){
        TMR_ON_REP = 1;
        Serial.println("Für den TimerON wurde eine Wiederholung eingestellt");
        }

      else if(server.argName(i) == "akt_volume"){
        myDFPlayer.volume(server.arg(i).toInt());
        akt_Volume = myDFPlayer.readVolume();
        Serial.println("Die aktuelle Lütstärke wurde geändert");
        }
      else if(server.argName(i) == "max_volume"){
        max_Volume = server.arg(i).toInt();
        Serial.println("Die maximale Lütstärke wurde geändert");
        }
      else if(server.argName(i) == "LED_color"){
        Serial.println("Die Farbe der LEDs wird geändert: " + server.arg(i));
         String Color = server.arg(i);
         char *ptr;
         char charBuf[Color.length() + 1];
         Color.toCharArray(charBuf, Color.length()+1);
         unsigned long col = strtol(charBuf,&ptr,16);
        fill_solid(leds, NUM_LEDS, col); // Farbe aller LEDs ändern
        FastLED.show(); 
      }
      else if(server.argName(i) == "LED_bri"){
        Serial.println("Die Helligkeit der LEDs wird geändert ");
        Serial.println("Helligkeit = " + server.arg(i));
        FastLED.setBrightness(server.arg(i).toInt());
        FastLED.show(); 
      }
      else if(server.argName(i) == "cb_SleepLight_on"){
          //CODE HERE
          if(server.arg(i).toInt()==1){
              SleepLight = true;
          }
      }
      else if(server.argName(i) == "cb_SleepLight_off"){
          if(server.arg(i).toInt()==0){
              SleepLight = false;
          }
      }
      else if(server.argName(i) == "cb_WakeUpLight_on"){
          if(server.arg(i).toInt()==1){
              WakeUpLight = true;
          }
          
      }
      else if(server.argName(i) == "cb_WakeUpLight_off"){
          if(server.arg(i).toInt()==0){
              WakeUpLight = false;
          }
      }

      
       
      }
     
    }
}
    


//===================================================================================
//Ab hier Aktionen welche bei drücken eines Button auf der HTML Seite ausgelöst werden
void handlePrev() { 
 Serial.println("handlePrev");
 myDFPlayer.previous();
 server.send(200, "text/html", getPage());
}
 
void handlePlay() { 
 Serial.println("handlePlay");
 myDFPlayer.start();
 server.send(200, "text/html", getPage()); 
}

void handlePause() { 
 Serial.println("handlePause");
 myDFPlayer.pause();
 server.send(200, "text/html", getPage()); 
}

void handleNext() { 
 Serial.println("handleNext");
 myDFPlayer.next();
 server.send(200, "text/html", getPage()); 
}

void handleVol_up() { 
 Serial.println("handleVol+");
 myDFPlayer.volumeUp();
 akt_Volume = myDFPlayer.readVolume();
 server.send(200, "text/html", getPage()); 
}
 
void handleVol_down() { 
 Serial.println("handleVol-");
 myDFPlayer.volumeDown();
 akt_Volume = myDFPlayer.readVolume();
 server.send(200, "text/html", getPage()); 
}


void handleEQ_NORM() { 
 Serial.println("handleEQ_Norm");
 myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
 server.send(200, "text/html", getPage()); 
}
 
void handleEQ_POP() { 
 Serial.println("handleEQ_POP");
 myDFPlayer.EQ(DFPLAYER_EQ_POP);
 server.send(200, "text/html", getPage()); 
}

void handleEQ_ROCK() { 
 Serial.println("handleEQ_ROCK");
 myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
 server.send(200, "text/html", getPage()); 
}

void handleEQ_CLASSIC() { 
 Serial.println("handleEQ_CLASSIC");
 myDFPlayer.EQ(DFPLAYER_EQ_CLASSIC);
 server.send(200, "text/html", getPage()); 
}

void handleEQ_BASS() { 
 Serial.println("handleEQ_BASE");
 myDFPlayer.EQ(DFPLAYER_EQ_BASS);
 server.send(200, "text/html", getPage()); 
}
 
void handleEQ_JAZZ() { 
 Serial.println("handleEQ_JAZZ");
 myDFPlayer.EQ(DFPLAYER_EQ_JAZZ);
 server.send(200, "text/html", getPage()); 
}
//==============================Sonnenaufgang Simulation============================================================
void sunrise() {
  
   
  if(debug) Serial.println("sunrise() wird ausgeführt");
  CRGBPalette256 sunrisePal = sunrise_gp;
  CRGB color = ColorFromPalette(sunrisePal, heatIndex);
  // fill the entire strip with the current color
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
  heatIndex++;
  if(heatIndex==255){
    heatIndex=0;
    startSR = false;
  } 
  
  
}
//==========================================================================================
//Funktion um die Ein- /Ausschalttimer auszuwerten
void TimeCompare(){
 
    if(debug) Serial.println("TimeCompare() wird ausgeführt");
    timeClient.update();
    int NTP_HH = timeClient.getHours();
    int NTP_MM = timeClient.getMinutes();
    //Serial.println("Die aktuelle NTP Zeit:" + String(NTP_HH) +":"+ String(NTP_MM) );
    
    
  
    if((TMR_OFF_MM == NTP_MM)and (TMP_OFFTIME==false)and (TMR_OFF_HH == NTP_HH)){

        //Abschalttimer
        myDFPlayer.pause();
        delay(100);
        myDFPlayer.playMp3Folder(902); //Verabschiedung spielen
        //myDFPlayer.outputDevice(DFPLAYER_DEVICE_SLEEP);
        delay(10000);
        while(isPlaying())
        myDFPlayer.stop();
        if(TMR_OFF_REP == 0){TMP_OFFTIME = true;}
        Serial.println("Die Wiedergabe wurde durch den OFF-Timer gestoppt.");
      
    }

    else if((TMR_ON_MM == NTP_MM)and (TMP_ONTIME==false)and (TMR_ON_HH == NTP_HH)){

        myDFPlayer.playMp3Folder(903); //Begrüßung spielen
        //myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
        if(TMR_ON_REP == 0){TMP_ONTIME = true;}
        if(WakeUpLight == true){startSR = true;}
        Serial.println("Die Wiedergabe wurde durch den ON-Timer gestartet.");
      
    }
    
    
}
//======================WiFi=========================================================
int WiFi_RouterNetworkConnect(char* txtSSID, char* txtPassword)
{
  int success = 1;
  
  // connect to WiFi network
  // see https://www.arduino.cc/en/Reference/WiFiBegin
  
  WiFi.begin(txtSSID, txtPassword);
  
  // we wait until connection is established
  // or 10 seconds are gone
  
  int WiFiConnectTimeOut = 0;
  while ((WiFi.status() != WL_CONNECTED) && (WiFiConnectTimeOut < 10))
  {
    delay(1000);
    WiFiConnectTimeOut++;
  }

  // not connected
  if (WiFi.status() != WL_CONNECTED)
  {
    success = -1;
  }

  // print out local address of ESP32 in Router network (LAN)
  Serial.println(WiFi.localIP());
  if(debug) Serial.print("WiFi Connect to AP");
  if(debug) Serial.println(String(success));
  return success;
}

// Disconnect from router network and return 1 (success) or -1 (no success)
int WiFi_RouterNetworkDisconnect()
{
  int success = -1;
  
  WiFi.disconnect();
  

  int WiFiConnectTimeOut = 0;
  while ((WiFi.status() == WL_CONNECTED) && (WiFiConnectTimeOut < 10))
  {
    delay(1000);
    WiFiConnectTimeOut++;
  }

  // not connected
  if (WiFi.status() != WL_CONNECTED)
  {
    success = 1;
  }
  
  Serial.println("Disconnected.");
  
  return success;
}


// Initialize Soft Access Point with ESP32
// ESP32 establishes its own WiFi network, one can choose the SSID
int WiFi_AccessPointStart(char* AccessPointNetworkSSID)
{ 
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 4, 1);    // Hier wird IP bestimmt
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("TonUINO");  // Name  des Access Points
  delay(500);
  if (debug)  Serial.println("Starte AP");
  if (debug)  Serial.print("IP Adresse ");      //Ausgabe aktueller IP des Servers
  if (debug)  Serial.println(WiFi.softAPIP());

  //Ansage das ein Access-Point geöffnet wird
  myDFPlayer.playMp3Folder(901);

  server.on("/", handleSetup);                // INI wifimanager Index Webseite senden
  server.on("/restart", []() {                 // INI wifimanager Index Webseite senden
    server.send(200, "text/plain","ESP Reset wird durchgeführt");
    handleRestart();
  });

  server.begin();
  if(debug)  Serial.println("HTTP Server gestarted");
  while (1){
    server.handleClient();                 // Wird endlos ausgeführt damit das WLAN Setup erfolgen kann
    if(digitalRead(buttonPause)==0)break; //Bricht die Warteschleife ab sobald die Play/Pause Taste gedrückt wurde
  }
  
 
  return 1;
}
//===================================================================================


void setup() {
//======================ISR TIMER====================================================
// Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Use 1st timer of 4 (counted from zero).
  // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
  // info).
  timer = timerBegin(0, 80, true);

  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer, true);

  // Set alarm to call onTimer function every second (value in microseconds).
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, 1000000, true);

//WS2812b Konfigurieren
//===================================================================================
// tell FastLED about the LED strip configuration
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);  //Helligkeit einstellen
  fill_solid(leds, NUM_LEDS, CRGB::Black); // Farbe aller LEDs ändern
  FastLED.show();

//===================================================================================
  
  Serial.begin(115200);
  SPI.begin();
  mySoftwareSerial.begin(9600, SERIAL_8N1, 16, 17);  // speed, type, RX, TX
  randomSeed(analogRead(33)); // Zufallsgenerator initialisieren
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();

  
  
  pinMode(dfpMute, OUTPUT);
  digitalWrite(dfpMute, LOW);

  

  // Knöpfe mit PullUp
  pinMode(headphonePin, INPUT_PULLUP);
  pinMode(buttonPause, INPUT_PULLUP);
  pinMode(buttonUp, INPUT_PULLUP);
  pinMode(buttonDown, INPUT_PULLUP);

  // Busy Pin
  pinMode(busyPin, INPUT);

    for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  // RESET --- ALLE DREI KNÖPFE BEIM STARTEN GEDRÜCKT HALTEN -> alle bekannten
  // Karten werden gelöscht
  if (digitalRead(buttonPause) == LOW && digitalRead(buttonUp) == LOW &&
      digitalRead(buttonDown) == LOW) {
    Serial.println(F("Reset -> EEPROM wird gelöscht"));
    for (int i = 0; i < EEPROM.length(); i++) {
      EEPROM.write(i, 0);
    }
  }

  Serial.println("TonUINO V3.0 auf ESP32 Basis");
  Serial.println("Original V2.0: T. Voss, Erweitert V3.0: C. Ulbrich");

  Serial.println();
  Serial.println(F("DFRobot DFPlayer Mini"));
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  myDFPlayer.begin(mySoftwareSerial,false,true);
 /*
  if (!myDFPlayer.begin(mySoftwareSerial)) {  //Use softwareSerial to communicate with mp3.
    
    Serial.println(myDFPlayer.readType(),HEX);
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while(true);
  }
  Serial.println(F("DFPlayer Mini online."));
*/
  
  myDFPlayer.setTimeOut(500); //Set serial communictaion time out 500ms
  delay(100);
  //----Set volume----
  myDFPlayer.volume(10);  //Set volume value (0~30).
  //myDFPlayer.volumeUp(); //Volume Up
  //myDFPlayer.volumeDown(); //Volume Down
  delay(100);
  //----Set different EQ----
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
//  myDFPlayer.EQ(DFPLAYER_EQ_POP);
//  myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
//  myDFPlayer.EQ(DFPLAYER_EQ_JAZZ);
//  myDFPlayer.EQ(DFPLAYER_EQ_CLASSIC);
//  myDFPlayer.EQ(DFPLAYER_EQ_BASS);
 delay(100); 
  //----Set device we use SD as default----
//  myDFPlayer.outputDevice(DFPLAYER_DEVICE_U_DISK);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
//  myDFPlayer.outputDevice(DFPLAYER_DEVICE_AUX);
//  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SLEEP);
//  myDFPlayer.outputDevice(DFPLAYER_DEVICE_FLASH);
  
  //----Mp3 control----
//  myDFPlayer.sleep();     //sleep
//  myDFPlayer.reset();     //Reset the module
//  myDFPlayer.enableDAC();  //Enable On-chip DAC
//  myDFPlayer.disableDAC();  //Disable On-chip DAC
//  myDFPlayer.outputSetting(true, 15); //output setting, enable the output and set the gain to 15

  //----Read imformation----
  Serial.println("");
  Serial.println(F("readState--------------------"));
  Serial.println(myDFPlayer.readState()); //read mp3 state
  Serial.println(F("readVolume--------------------"));
  Serial.println(myDFPlayer.readVolume()); //read current volume
  //Serial.println(F("readEQ--------------------"));
  //Serial.println(myDFPlayer.readEQ()); //read EQ setting
  Serial.println(F("readFileCounts--------------------"));
  Serial.println(myDFPlayer.readFileCounts()); //read all file counts in SD card
  Serial.println(F("readCurrentFileNumber--------------------"));
  Serial.println(myDFPlayer.readCurrentFileNumber()); //read current play file number
  Serial.println(F("readFileCountsInFolder--------------------"));
  Serial.println(myDFPlayer.readFileCountsInFolder(3)); //read fill counts in folder SD:/03
  Serial.println(F("--------------------"));
  delay(2000);
  //Begrüßung abspielen, das überbrückt auch die Zeit des WLAN Connect
  myDFPlayer.playMp3Folder(900);
  
  preferences.begin("my-wifi",false);
  if(debug)WiFi.mode(WIFI_AP_STA);
  // takeout 2 Strings out of the Non-volatile storage
  String strSSID = preferences.getString("SSID", "");
  String strPassword = preferences.getString("Password", "");

  // convert it to char*
  char* txtSSID = const_cast<char*>(strSSID.c_str());
  char* txtPassword = const_cast<char*>(strPassword.c_str());   // https://coderwall.com/p/zfmwsg/arduino-string-to-char 


  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to SSID: ");
  Serial.print(txtSSID);
  Serial.print(" with the following PW:  ");
  Serial.println(txtPassword);

   // try to connect to the LAN
   success = WiFi_RouterNetworkConnect(txtSSID, txtPassword);
  if (success == 1)
  {
          fill_solid(leds, NUM_LEDS, CRGB::Blue); // Farbe aller LEDs ändern
          FastLED.show();      
  }
  else
  {
          fill_solid(leds, NUM_LEDS, CRGB::Red); // Farbe aller LEDs ändern
          FastLED.show();  
  }
  
  // Start access point"
  if(success== -1)WiFi_AccessPointStart("ESP32_TonUINO");
  
  Serial.println ( "HTTP server started" );


  //NTP Client starten, das Offset zur Empfangenen Zeit einstellen
  if(success == 1)timeClient.begin();
  if(success == 1)timeClient.setTimeOffset(+3600); //+1h Offset
  if(success == 1)timeClient.update();
  
  //Verweise für den Empfang von HTML Client informationen
  server.on ( "/", handleRoot );
  server.on ("/play", handlePlay);
  server.on ("/pause", handlePause);
  server.on ("/prev", handlePrev);
  server.on ("/next", handleNext);
  server.on ("/vol+", handleVol_up);
  server.on ("/vol-", handleVol_down);
  server.on ("/eq_base", handleEQ_BASS);
  server.on ("/eq_pop", handleEQ_POP);
  server.on ("/eq_rock", handleEQ_ROCK);
  server.on ("/eq_classic", handleEQ_CLASSIC);
  server.on ("/eq_jazz", handleEQ_JAZZ);
  server.on ("/eq_norm", handleEQ_NORM);
  server.on("/setup", handleSetup);
  
  
  server.begin();
  if(success==1)startTimer();
  Serial.println ( "===============////////////// ================" );
  Serial.println ( "===============/ SETUP ENDE / ================" );
  Serial.println ( "===============/ //////////// ================" );
}


//==============SETUP ENDE================================


void loop(){
 
  do {
    
    if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE){//Timer Interrupt Routine
      uint32_t isrCount = 0, isrTime = 0;
      // Read the interrupt count and time
      portENTER_CRITICAL(&timerMux);
      isrCount = isrCounter;
      isrTime = lastIsrAt;
      portEXIT_CRITICAL(&timerMux);
      //Ab hier Funktionen für Timer

      if(success == 1) TimeCompare(); //Abfrage der Zeit im Sekundentakt
      if(startSR == true) sunrise();
      

    }
    server.handleClient();
    
    
if (!isPlaying()) {
  if (myDFPlayer.available()) {
      printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
  }
}
    // Buttons werden nun über JS_Button gehandelt, dadurch kann jede Taste
    // doppelt belegt werden
    pauseButton.read();
    upButton.read();
    downButton.read();
    
    //Erkennung ob ein Kopfhörer eingesteckt ist, "headphoneIn" verriegelt jeweils die Abfrage so das sie nur einmal durchlaufen wird
    if ((digitalRead(headphonePin)== 1) && (headphoneIn == 0)){

      Serial.println("Kopfhörer wurde eingesteckt");
      digitalWrite(dfpMute, HIGH);
      headphoneIn = 1;
      last_max_Volume = max_Volume; // Das letzte max. Volume merken
      last_Volume = myDFPlayer.readVolume();
      max_Volume = 10;
      if(myDFPlayer.readVolume() >= max_Volume){
          myDFPlayer.volume(10);
      }
      
    } else if ((digitalRead(headphonePin)== 0) && (headphoneIn == 1)){
      Serial.println("Kopfhörer wurde entfernt");
      headphoneIn = 0;
      max_Volume = last_max_Volume;
      myDFPlayer.volume(last_Volume);
      }

 if (pauseButton.wasReleased()) {
      if (ignorePauseButton == false){
        if (isPlaying()){
          myDFPlayer.pause();
          fill_solid(leds, NUM_LEDS, CRGB::Black); // Farbe aller LEDs ändern
          FastLED.show();
          startSR = false;
           heatIndex=0;
        }else{
          myDFPlayer.start();
          ignorePauseButton = false;
    }
   }
 }else if (pauseButton.pressedFor(LONG_PRESS) &&
               ignorePauseButton == false){
      Serial.println(F("Pause taste wurde lang gedrückt"));
      if (isPlaying()){
        myDFPlayer.advertise(track);}
      else {
        knownCard = false;
        myDFPlayer.playMp3Folder(800);
        Serial.println(F("Karte resetten..."));
        resetCard();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
      ignorePauseButton = true;
    } 

    if (upButton.pressedFor(LONG_PRESS)) {
      //Serial.println(F("Volume Up"));
      //myDFPlayer.volumeUp();
      nextTrack();
      ignoreUpButton = true;
      delay(1000);
    } else if (upButton.wasReleased()) {
      if (!ignoreUpButton){
        //nextTrack();
        if(myDFPlayer.readVolume() <= max_Volume){
          myDFPlayer.volumeUp();
          }
      }else{
        ignoreUpButton = false;}
    }

    if (downButton.pressedFor(LONG_PRESS)) {
      //Serial.println(F("Volume Down"));
      //myDFPlayer.volumeDown();
      previousTrack();
      ignoreDownButton = true;
      delay(1000);
    } else if (downButton.wasReleased()) {
      if (!ignoreDownButton)
        //previousTrack();
        myDFPlayer.volumeDown();
      else
        ignoreDownButton = false;
    }
    

    
    // Ende der Buttons
  } while (!mfrc522.PICC_IsNewCardPresent());

  // RFID Karte wurde aufgelegt

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  if (readCard(&myCard) == true) {
    if (myCard.cookie == 322417479 && myCard.folder != 0 && myCard.mode != 0) {

      knownCard = true;
      numTracksInFolder = myDFPlayer.readFileCountsInFolder(myCard.folder);

      // Hörspielmodus: eine zufällige Datei aus dem Ordner
      if (myCard.mode == 1) {
        Serial.println(F("Hörspielmodus -> zufälligen Track wiedergeben"));
        track = random(1, numTracksInFolder + 1);
        Serial.println(track);
        myDFPlayer.playFolder(myCard.folder, track);
        fill_solid(leds, NUM_LEDS, myCard.color); // Farbe aller LEDs ändern
        FastLED.show();
      }
      // Album Modus: kompletten Ordner spielen
      if (myCard.mode == 2) {
        Serial.println(F("Album Modus -> kompletten Ordner wiedergeben"));
        track = 1;
        myDFPlayer.playFolder(myCard.folder, track);
        fill_solid(leds, NUM_LEDS, myCard.color); // Farbe aller LEDs ändern
        FastLED.show();
      }
      // Party Modus: Ordner in zufälliger Reihenfolge
      if (myCard.mode == 3) {
        Serial.println(
            F("Party Modus -> Ordner in zufälliger Reihenfolge wiedergeben"));
        track = random(1, numTracksInFolder + 1);
        myDFPlayer.playFolder(myCard.folder, track);
        fill_solid(leds, NUM_LEDS, myCard.color); // Farbe aller LEDs ändern
      }
      // Einzel Modus: eine Datei aus dem Ordner abspielen
      if (myCard.mode == 4) {
        Serial.println(
            F("Einzel Modus -> eine Datei aus dem Odrdner abspielen"));
        track = myCard.special;
        myDFPlayer.playFolder(myCard.folder, track);
        fill_solid(leds, NUM_LEDS, myCard.color); // Farbe aller LEDs ändern
        FastLED.show();
      }
      // Hörbuch Modus: kompletten Ordner spielen und Fortschritt merken
      if (myCard.mode == 5) {
        Serial.println(F("Hörbuch Modus -> kompletten Ordner spielen und "
                         "Fortschritt merken"));
        track = EEPROM.read(myCard.folder);
        if(track==0)track=1;
        myDFPlayer.playFolder(myCard.folder, track);
        fill_solid(leds, NUM_LEDS, myCard.color); // Farbe aller LEDs ändern
        FastLED.show();
      }
    }

    // Neue Karte konfigurieren
    else {
      knownCard = false;
      setupCard();
    }
  }
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
}


int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview , int previewFromFolder) {
  int returnValue = 0;
  if (startMessage != 0)
    myDFPlayer.playMp3Folder(startMessage);
  do {
    pauseButton.read();
    upButton.read();
    downButton.read();
    //myDFPlayer.loop();
    if (pauseButton.wasPressed()) {
      if (returnValue != 0)
        return returnValue;
      delay(1000);
    }

    if (upButton.pressedFor(LONG_PRESS)) {
      returnValue = fmin(returnValue + 10, numberOfOptions);
      myDFPlayer.playMp3Folder(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          myDFPlayer.playFolder(returnValue, 1);
        else
          myDFPlayer.playFolder(previewFromFolder, returnValue);
      }
      ignoreUpButton = true;
    } else if (upButton.wasReleased()) {
      if (!ignoreUpButton) {
        returnValue = fmin(returnValue + 1, numberOfOptions);
        myDFPlayer.playMp3Folder(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            myDFPlayer.playFolder(returnValue, 1);
          else
            myDFPlayer.playFolder(previewFromFolder, returnValue);
        }
      } else
        ignoreUpButton = false;
    }
    
    if (downButton.pressedFor(LONG_PRESS)) {
      returnValue = fmax(returnValue - 10, 1);
      myDFPlayer.playMp3Folder(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          myDFPlayer.playFolder(returnValue, 1);
        else
          myDFPlayer.playFolder(previewFromFolder, returnValue);
      }
      ignoreDownButton = true;
    } else if (downButton.wasReleased()) {
      if (!ignoreDownButton) {
        returnValue = fmax(returnValue - 1, 1);
        myDFPlayer.playMp3Folder(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            myDFPlayer.playFolder(returnValue, 1);
          else
            myDFPlayer.playFolder(previewFromFolder, returnValue);
        }
      } else
        ignoreDownButton = false;
    }
  } while (true);
}

void resetCard() {
  do {
    pauseButton.read();
    upButton.read();
    downButton.read();

    if (upButton.wasReleased() || downButton.wasReleased()) {
      Serial.print(F("Abgebrochen!"));
      myDFPlayer.playMp3Folder(802);
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  Serial.print(F("Karte wird neu Konfiguriert!"));
  setupCard();
}

void setupCard() {
  myDFPlayer.pause();
  Serial.print(F("Neue Karte konfigurieren"));

  // Ordner abfragen
  myCard.folder = voiceMenu(99, 300, 0, true);

  // Wiedergabemodus abfragen
  myCard.mode = voiceMenu(6, 310, 310);

 
  // Farbe abfragen
  myCard.color = voiceMenu(7,600,600);
  switch (myCard.color) {
  case 4:
    myCard.color = CRGB::LawnGreen;
    break;
  case 3:
    myCard.color = CRGB::Yellow;
    break;
  case 6:
    myCard.color = CRGB::White;
    break;
  case 7:
    myCard.color = CRGB::Plum;
    break;
  case 2:
    myCard.color = CRGB::OrangeRed;
    break;
  case 5:
    myCard.color = CRGB::LightSkyBlue;
    break;
  case 1:
    myCard.color = CRGB::Black;
    break;
}
  

  // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
  EEPROM.write(myCard.folder,1);

  // Einzelmodus -> Datei abfragen
  if (myCard.mode == 4)
    myCard.special = voiceMenu(myDFPlayer.readFileCountsInFolder(myCard.folder), 320, 0,
                               true, myCard.folder);

  // Admin Funktionen
  if (myCard.mode == 6)
    myCard.special = voiceMenu(3, 316, 320);

  // Karte ist konfiguriert -> speichern
  writeCard(myCard);
}

bool readCard(nfcTagObject *nfcTag) {
  bool returnValue = true;
  // Show some details of the PICC (that is: the tag/card)
  Serial.print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println();
  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.println(mfrc522.PICC_GetTypeName(piccType));

  byte buffer[18];
  byte size = sizeof(buffer);

  // Authenticate using key A
  Serial.println(F("Authenticating using key A..."));
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    returnValue = false;
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return returnValue;
  }

  // Show the whole sector as it currently is
  Serial.println(F("Current data in sector:"));
  mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
  Serial.println();

  // Read data from the block
  Serial.print(F("Reading data from block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    returnValue = false;
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
  }
  Serial.print(F("Data in block "));
  Serial.print(blockAddr);
  Serial.println(F(":"));
  dump_byte_array(buffer, 20);
  Serial.println();
  Serial.println();

  uint32_t tempCookie;
  tempCookie = (uint32_t)buffer[0] << 24;
  tempCookie += (uint32_t)buffer[1] << 16;
  tempCookie += (uint32_t)buffer[2] << 8;
  tempCookie += (uint32_t)buffer[3];

  uint32_t tempColor;
  tempColor = (uint32_t)buffer[8] << 24;
  tempColor += (uint32_t)buffer[9] << 16;
  tempColor += (uint32_t)buffer[10] << 8;
  tempColor += (uint32_t)buffer[11];

  nfcTag->cookie = tempCookie;
  nfcTag->version = buffer[4];
  nfcTag->folder = buffer[5];
  nfcTag->mode = buffer[6];
  nfcTag->special = buffer[7];
  nfcTag->color = tempColor;

  return returnValue;
}

void writeCard(nfcTagObject nfcTag) {
  MFRC522::PICC_Type mifareType;

uint8_t bytes[4];

bytes[0] = (nfcTag.color >> 0)  & 0xFF;
bytes[1] = (nfcTag.color >> 8)  & 0xFF;
bytes[2] = (nfcTag.color >> 16) & 0xFF;
bytes[3] = (nfcTag.color >> 24) & 0xFF;

  byte buffer[20] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to
                                             // identify our nfc tags
                     0x01,                   // version 1
                     nfcTag.folder,          // the folder picked by the user
                     nfcTag.mode,    // the playback mode picked by the user
                     nfcTag.special, // track or function for admin cards
                     bytes[3],  //Farbe welche eingeschaltet werden soll
                     bytes[2],  //Farbe welche eingeschaltet werden soll
                     bytes[1],  //Farbe welche eingeschaltet werden soll
                     bytes[0],  //Farbe welche eingeschaltet werden soll
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  byte size = sizeof(buffer);

  mifareType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  // Authenticate using key B
  Serial.println(F("Authenticating again using key B..."));
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    myDFPlayer.playMp3Folder(401);
    return;
  }

  // Write data to the block
  Serial.print(F("Writing data into block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  dump_byte_array(buffer, 20);
  Serial.println();
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(blockAddr, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Write() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
      myDFPlayer.playMp3Folder(401);
  }
  else
    myDFPlayer.playMp3Folder(400);
  Serial.println();
  delay(100);
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void startTimer(){

  // Start an alarm
  timerAlarmEnable(timer);
}

void stoppTimer(){
  timerEnd(timer);
  timer = NULL;
}

void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      Mp3Notify::OnPlayFinished(track);
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

