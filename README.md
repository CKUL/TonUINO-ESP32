# [TonUINO-ESP32](http://discourse.voss.earth/t/esp32-port-inkl-webinterface/399)
_[TonUINO V2.0](https://www.voss.earth/tonuino/) Code für ESP32 mit Erweiterungen, es gibt zu dem Projekt ebenfalls eine [Discourse Seite](http://discourse.voss.earth/)_

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
    
  - Sprachmenü und Struktur auf Tag erweitert um Ambilight Farbe zu speichern
    
  - Funktion der Hoch- Runter- Tasten geändert
    - kurzes Drücken = Lautstärke +/-
    - langes Drücken = Lied +/-
    
 **Noch offen und geplant:**
 
  - Speichern der Einstellungen im EEPROM / Flash
  - Tags verwalten
  
  - Sonnenauf- und Untergang simulation für Ein- / Ausschalttimer
    - Funktion bereits im Code, muss debugged werden
  - AP-Modus um WLAN konfigurieren zu können ohne dies (SSID, PW) im Code fest zu verankern
