# TonUINO-ESP32
_TonUINO V2.0 Code für ESP32 mit Erweiterungen_

**Aktuell Umgesetzt:**
  
  - Webinterface zur Fernsteuerung
    - Play, Pause, Start, Stop, Lautstärke
    - Equalizer einstellen
    - max. Lautstärke einstellen
    - Ausschalttimer
    - Einschalttimer
    
  - Ambientlight mit WS2812 LED´s
    - ändern der Farbe über Webinterface
    
  - Kopfhörererkennung
    - reduziert die Lautstärke wenn Kopfhörer gesteckt
    - begrenz die Lautstärke wenn Kopfhörer gesteckt
    - schaltet auf vorherige Lautstärke zurück wenn Kopfhörer nicht mehr gesteckt
    
 **Noch offen und geplant:**
 
  - Speichern der Einstellungen im EEPROM / Flash
  - Tags verwalten
  - Sprachmenü und Struktur auf Tag erweitern um Ambilight Farbe zu speichern
  - Sonnenauf- und Untergang simulation für Ein- / Ausschalttimer
    - Funktion bereits im Code, muss debugged werden
  - AP-Modus um WLAN konfigurieren zu können ohne dies (SSID, PW) im Code fest zu verankern
