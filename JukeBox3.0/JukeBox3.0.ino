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
//========================================================================
//Hier nur VAriablen zum Testen

int l = 0;
bool headphoneIn =0 ;
//Variablen zum Speichern von EInstellungen
//========================================================================

unsigned long last_color = 0xFFFFFF;
unsigned int last_Volume;


//========================================================================
//WS2812b Einstellungen

#define DATA_PIN 2 //signal pin 
#define NUM_LEDS 2 //number of LEDs in your strip
#define BRIGHTNESS 32  //brightness  (max 254) 
#define LED_TYPE WS2811  // I know we are using ws2812, but it's ok!
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];



//========================================================================
//NTP Variablen
WebServer server ( 80 );
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
};

nfcTagObject myCard;
void resetCard(void);
bool readCard(nfcTagObject *nfcTag);
void setupCard(void);
static void nextTrack();
int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0);

bool knownCard = false;
uint16_t numTracksInFolder;
uint16_t track;
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

// Replace with your network credentials
const char* ssid     = "yourSSID";
const char* password = "yourPW";

// Set web server port number to 80
//WiFiServer server(80);

// Variable to store the HTTP request
String header;

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
        Serial.println("Die Lütstärke wurde geändert");
        }
      else if(server.argName(i) == "max_volume"){
        max_Volume = server.arg(i).toInt();
        }
      else if(server.argName(i) == "LED_color"){
        Serial.println("Die Farbe der LEDs wird geändert ");
         String Color = server.arg(i);
         char *ptr;
         char charBuf[Color.length() + 1];
         Color.toCharArray(charBuf, Color.length()+1);
         unsigned long col = strtol(charBuf,&ptr,16);
        leds[1] = col;
        FastLED.show(); 
      }
      else if(server.argName(i) == "LED_bri"){
        Serial.println("Die Helligkeit der LEDs wird geändert ");
        Serial.println("Helligkeit = " + server.arg(i).toInt());
        FastLED.setBrightness(server.arg(i).toInt());
        FastLED.show(); 
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
//==========================================================================================

//==========================================================================================
//Funktion um die Ein- /Ausschalttimer auszuwerten
void TimeCompare(){
  
    timeClient.update();
    int NTP_HH = timeClient.getHours();
    int NTP_MM = timeClient.getMinutes();
    //Serial.println("Die aktuelle NTP Zeit:" + String(NTP_HH) +":"+ String(NTP_MM) );
    
    
  
    if((TMR_OFF_MM == NTP_MM)and (TMP_OFFTIME==false)and (TMR_OFF_HH == NTP_HH)){

        myDFPlayer.pause();
        if(TMR_OFF_REP == 0){TMP_OFFTIME = true;}
        Serial.println("Die Wiedergabe wurde durch den OFF-Timer gestoppt.");
      
    }

    else if((TMR_ON_MM == NTP_MM)and (TMP_ONTIME==false)and (TMR_ON_HH == NTP_HH)){

        myDFPlayer.start();
        if(TMR_ON_REP == 0){TMP_ONTIME = true;}
        Serial.println("Die Wiedergabe wurde durch den ON-Timer gestartet.");
      
    }
    
    
}

void setup() {

//WS2812b Konfigurieren
//===================================================================================
// tell FastLED about the LED strip configuration
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
//=====================TEST=======================
  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
 leds[0] = CRGB::Red;
  FastLED.show();
  delay(1000);
 leds[1] = CRGB::Green;
  FastLED.show();
    delay(1000);
 leds[0] = CRGB::Blue;
  FastLED.show();
//===================================================================================
  
  Serial.begin(115200);
  SPI.begin();
  mySoftwareSerial.begin(9600, SERIAL_8N1, 16, 17);  // speed, type, RX, TX
  randomSeed(analogRead(33)); // Zufallsgenerator initialisieren
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();

  
  pinMode(headphonePin, INPUT_PULLUP);

  

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
  Serial.println("Original: T. Voss, Erweitert: C. Ulbrich");

  Serial.println();
  Serial.println(F("DFRobot DFPlayer Mini"));
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));
  
  if (!myDFPlayer.begin(mySoftwareSerial)) {  //Use softwareSerial to communicate with mp3.
    
    Serial.println(myDFPlayer.readType(),HEX);
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while(true);
  }
  Serial.println(F("DFPlayer Mini online."));

  myDFPlayer.setTimeOut(500); //Set serial communictaion time out 500ms
  
  //----Set volume----
  myDFPlayer.volume(10);  //Set volume value (0~30).
  myDFPlayer.volumeUp(); //Volume Up
  myDFPlayer.volumeDown(); //Volume Down
  
  //----Set different EQ----
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
//  myDFPlayer.EQ(DFPLAYER_EQ_POP);
//  myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
//  myDFPlayer.EQ(DFPLAYER_EQ_JAZZ);
//  myDFPlayer.EQ(DFPLAYER_EQ_CLASSIC);
//  myDFPlayer.EQ(DFPLAYER_EQ_BASS);
  
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
  
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  //NTP Client starten, das Offset zur Empfangenen Zeit einstellen
  timeClient.begin();
  timeClient.setTimeOffset(+7200); //+2h Offset
  timeClient.update();
  
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
  
  server.begin();
  Serial1.println ( "HTTP server started" );
}


//==============SETUP ENDE================================
void sunrise(int count){ 
  
  int rgb[2];
  
  if (count <= 44){
  rgb[0] = round(4.94 * count);
  rgb[1] = round(0.92 * count) + 45;
  rgb[2] = round(-3.06 * count) + 170;
  }
  else if ( 44 <= count && count <= 55){
  rgb[0] = round(5 * count);
  rgb[1] = round(2.5 * count) + 45;
  rgb[2] = round(-3.06 * count) + 170;
  }
  else{
  rgb[0] = 255;
  rgb[1] = round(3.58 * count);
  rgb[2] = round(2.02 * count) - 40;
  }
  
    leds[l].setRGB(rgb[0], rgb[1], rgb[2]);
    leds[2].setRGB(rgb[0], rgb[1], rgb[2]);
    FastLED.show();

}

void loop(){
 
  do {
    
    server.handleClient();

    TimeCompare();
    
    //myDFPlayer.loop();
    // Buttons werden nun über JS_Button gehandelt, dadurch kann jede Taste
    // doppelt belegt werden
    pauseButton.read();
    upButton.read();
    downButton.read();
    
    //Erkennung ob ein Kopfhörer eingesteckt ist
    if ((digitalRead(headphonePin)== 1) && (headphoneIn == 0)){

      headphoneIn = 1;
      myDFPlayer.volume(10);
      last_Volume = max_Volume;
      max_Volume = 10;
      
    } 
    else if ((digitalRead(headphonePin)== 0) && (headphoneIn == 1)){
      headphoneIn = 0;
      max_Volume = last_Volume;
      }

  
 if (pauseButton.wasReleased()) {
      if (ignorePauseButton == false){
        if (isPlaying()){
          myDFPlayer.pause();}
        else{
          myDFPlayer.start();}
      ignorePauseButton = false;
    } else if (pauseButton.pressedFor(LONG_PRESS) && ignorePauseButton == false) {
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
 }
    if (upButton.pressedFor(LONG_PRESS)) {
      //Serial.println(F("Volume Up"));
      //myDFPlayer.volumeUp();
      nextTrack();
      ignoreUpButton = true;
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
      }
      // Album Modus: kompletten Ordner spielen
      if (myCard.mode == 2) {
        Serial.println(F("Album Modus -> kompletten Ordner wiedergeben"));
        track = 1;
        myDFPlayer.playFolder(myCard.folder, track);
      }
      // Party Modus: Ordner in zufälliger Reihenfolge
      if (myCard.mode == 3) {
        Serial.println(
            F("Party Modus -> Ordner in zufälliger Reihenfolge wiedergeben"));
        track = random(1, numTracksInFolder + 1);
        myDFPlayer.playFolder(myCard.folder, track);
      }
      // Einzel Modus: eine Datei aus dem Ordner abspielen
      if (myCard.mode == 4) {
        Serial.println(
            F("Einzel Modus -> eine Datei aus dem Odrdner abspielen"));
        track = myCard.special;
        myDFPlayer.playFolder(myCard.folder, track);
      }
      // Hörbuch Modus: kompletten Ordner spielen und Fortschritt merken
      if (myCard.mode == 5) {
        Serial.println(F("Hörbuch Modus -> kompletten Ordner spielen und "
                         "Fortschritt merken"));
        track = EEPROM.read(myCard.folder);
        myDFPlayer.playFolder(myCard.folder, track);
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

  // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
  EEPROM.write(myCard.folder,1);

  // Einzelmodus -> Datei abfragen
  if (myCard.mode == 4)
    myCard.special = voiceMenu(myDFPlayer.readFileCountsInFolder(myCard.folder), 320, 0,
                               true, myCard.folder);

  // Admin Funktionen
  if (myCard.mode == 6)
    myCard.special = voiceMenu(3, 320, 320);

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
  dump_byte_array(buffer, 16);
  Serial.println();
  Serial.println();

  uint32_t tempCookie;
  tempCookie = (uint32_t)buffer[0] << 24;
  tempCookie += (uint32_t)buffer[1] << 16;
  tempCookie += (uint32_t)buffer[2] << 8;
  tempCookie += (uint32_t)buffer[3];

  nfcTag->cookie = tempCookie;
  nfcTag->version = buffer[4];
  nfcTag->folder = buffer[5];
  nfcTag->mode = buffer[6];
  nfcTag->special = buffer[7];

  return returnValue;
}

void writeCard(nfcTagObject nfcTag) {
  MFRC522::PICC_Type mifareType;
  byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to
                                             // identify our nfc tags
                     0x01,                   // version 1
                     nfcTag.folder,          // the folder picked by the user
                     nfcTag.mode,    // the playback mode picked by the user
                     nfcTag.special, // track or function for admin cards
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
  dump_byte_array(buffer, 16);
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
