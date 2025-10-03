#define CONFIG_LITTLEFS_SPIFFS_COMPAT

#include <Arduino.h>
#include <NimBLEDevice.h> // github NimBLE
#include <SPI.h>
#include <Wire.h>
#include <string>
#include <LittleFS.h>
#include <SectionRanges.h>
#include <SparkPresetControl.h>


#include "src/SparkButtonHandler.h"
#include "src/SparkDataControl.h"
#include "src/SparkDisplayControl.h"
#include "src/SparkLEDControl.h"
#include "src/SparkPresetControl.h"

#ifndef AMP_MODE_SWITCH_PIN
#define AMP_MODE_SWITCH_PIN 34    // <- change to the GPIO you wired; SPST to GND
#endif

using namespace std;

// Device Info Definitions
const string DEVICE_NAME = "Ignitron";

// Control classes
SparkDataControl spark_dc;
SparkButtonHandler spark_bh;
SparkLEDControl spark_led;
SparkDisplayControl sparkDisplay;
SparkPresetControl &presetControl = SparkPresetControl::getInstance();

unsigned long lastInitialPresetTimestamp = 0;
unsigned long currentTimestamp = 0;
int initialRequestInterval = 3000;

// Check for initial boot
bool isInitBoot;
OperationMode operationMode = SPARK_MODE_APP;

/////////////////////////////////////////////////////////
//
// INIT AND RUN
//
/////////////////////////////////////////////////////////

void setup() {

    Serial.begin(115200);
    while (!Serial)
        ;

    Serial.println("Initializing");
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount failed");
        return;
    }
    
    // Build section ranges from PresetList.txt and set initial window
    SectionRanges::get().loadFromPresetList("/PresetList.txt");
    const SectionRange* initialSec = SectionRanges::get().current();
    if (initialSec) {
      SparkPresetControl::getInstance().setActiveSectionBankWindow(initialSec->startBank, initialSec->endBank);
      // Optional: ensure we start on the section's first bank
      SparkPresetControl::getInstance().setBank(initialSec->startBank);
    }

    // Debounced "Hold 3 while in APP mode" to cycle sections
static unsigned long lastSectionSwitchMs = 0;
static const unsigned long sectionSwitchDebounceMs = 600; // tweak if you like

void handleSectionSwitchIfNeeded() {
  // Replace with your real flags/functions if their names differ
  extern bool appModeActive;        // or however you track APP mode
  if (!appModeActive) return;

  if (isHeld(SWITCH_3)) {           // use your actual read fn & constant here
    unsigned long now = millis();
    if (now - lastSectionSwitchMs >= sectionSwitchDebounceMs) {
      lastSectionSwitchMs = now;

      auto& sec = SectionRanges::get();
      int next = (sec.currentIndex() + 1) % sec.count();
      sec.setCurrentIndex(next);

      const SectionRange* cur = sec.current();
      if (cur) {
        SparkPresetControl::getInstance().setActiveSectionBankWindow(
            cur->startBank, cur->endBank);
        SparkPresetControl::getInstance().setBank(cur->startBank);
            sparkDisplay.showSection(cur->label);   // <-- NEW: flash "Section: <name>" on OLED


        // Optional: blink feedback showing section #
        // for (int i = 0; i <= next; i++) { blinkLED(); delay(150); }
      }
    }
  }
}


    // spark_dc = new SparkDataControl();
    spark_bh.setDataControl(&spark_dc);
    operationMode = spark_bh.checkBootOperationMode();

    // --- Amp Mode toggle on GPIO35 ---   // mk
     pinMode(AMP_MODE_SWITCH_PIN, INPUT);  // external 10k pull-up to 3.3V, switch to GND

     int _ampToggleState = digitalRead(AMP_MODE_SWITCH_PIN);  // HIGH=open, LOW=closed

     if (_ampToggleState == LOW) {
         operationMode = SPARK_MODE_AMP;   // force Amp Mode
         Serial.println("Amp toggle ON → forcing AMP mode");
     } else {
         Serial.println("Amp toggle OFF → normal boot");
     }



    // Setting operation mode before initializing
    operationMode = spark_dc.init(operationMode);
    spark_bh.configureButtons();
    Serial.printf("Operation mode: %d\n", operationMode);

    switch (operationMode) {
    case SPARK_MODE_APP:
        Serial.println("======= Entering APP mode =======");
        break;
    case SPARK_MODE_AMP:
        Serial.println("======= Entering AMP mode =======");
        break;
    case SPARK_MODE_KEYBOARD:
        Serial.println("======= Entering Keyboard mode =======");
        break;
    }

    sparkDisplay.setDataControl(&spark_dc);
    spark_dc.setDisplayControl(&sparkDisplay);
    sparkDisplay.init(operationMode);
    // Assigning data control to buttons;
    spark_bh.setDataControl(&spark_dc);
    // Initializing control classes
    spark_led.setDataControl(&spark_dc);

    Serial.println("Initialization done.");
}

void loop() {
    handleSerialCommands();   // mk it will react to LISTPRESETS

    // Methods to call only in APP mode
    if (operationMode == SPARK_MODE_APP) {
        while (!(spark_dc.checkBLEConnection())) {
            sparkDisplay.update(spark_dc.isInitBoot());
            spark_led.updateLEDs();
            spark_bh.readButtons();
        }

        if (spark_dc.isInitBoot()) {
            spark_dc.getSerialNumber();
            spark_dc.isInitBoot() = false;
        }
    }

    if (operationMode != SPARK_MODE_KEYBOARD) {
        spark_dc.checkForUpdates();
    }

    // Reading button input
    spark_bh.configureButtons();
    spark_bh.readButtons();

    // 🔹 NEW: handle section switching (Hold Switch 3 in APP mode)
    handleSectionSwitchIfNeeded();

#ifdef ENABLE_BATTERY_STATUS_INDICATOR
    spark_dc.updateBatteryLevel();
#endif

    spark_led.updateLEDs();
    sparkDisplay.update();
}


// === BEGIN: LISTPRESETS serial support =======================================

// Case-insensitive “.json” check  //mk
static bool hasJsonExt(const char *name) {
  if (!name) return false;
  size_t len = strlen(name);
  if (len < 5) return false;
  const char *ext = name + (len - 5);
  return ext[0] == '.' &&
         (ext[1] == 'j' || ext[1] == 'J') &&
         (ext[2] == 's' || ext[2] == 'S') &&
         (ext[3] == 'o' || ext[3] == 'O') &&
         (ext[4] == 'n' || ext[4] == 'N');
}

// Dump entire JSON file to a single line (removes CR/LF/TAB)
static void printJsonFileSingleLine(File &f) {
  Serial.print("JSON STRING: ");
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\r' || c == '\n' || c == '\t') continue; // keep it one line for the Python regex
    Serial.write(c);
  }
  Serial.println();
}

// List every *.json at the LittleFS root and print in the exact format your tool expects
static void listAllPresets() {
  // Optional markers so your Python tool can switch to an event-based end condition if you want later
  Serial.println("LISTPRESETS_START");

  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("⚠️ Could not open LittleFS root");
    Serial.println("LISTPRESETS_DONE");
    return;
  }

  while (true) {
    File f = root.openNextFile();
    if (!f) break;                         // no more entries

    if (!f.isDirectory()) {
      const char *name = f.name();         // likely includes leading '/'
      if (name && hasJsonExt(name)) {
        // Keep wording EXACT—your Python regex looks for this line:
        Serial.print("Reading preset filename: ");
        Serial.println(name);               // e.g. "/MyPreset.json"

        // print the JSON on ONE LINE for robust parsing on the PC
        printJsonFileSingleLine(f);
      }
    }

    f.close();
  }

  Serial.println("LISTPRESETS_DONE");
}

// Robust line-buffered serial command reader
static void handleSerialCommands() {
  static String buf;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      String cmd = buf;
      buf = "";
      cmd.trim();
      if (cmd.length() == 0) return;

      // Uppercase for simple matching
      String u = cmd;
      u.toUpperCase();

      if (u == "LISTPRESETS") {
        listAllPresets();
      }
	  if (u == "LISTBANKS") {
    File f = LittleFS.open("/PresetList.txt");
    if (f) {
        Serial.println("LISTBANKS_START");
        while (f.available()) {
            char c = f.read();
            if (c == '\r') continue; // normalize line endings
            Serial.write(c);
        }
        Serial.println("LISTBANKS_DONE");
        f.close();
    } else {
        Serial.println("⚠️ PresetList.txt not found");
        Serial.println("LISTBANKS_DONE");
    }
}

      // Add future commands here (e.g., "GETPRESET <name>", "DELETEPRESET <name>", etc.)
    } else {
      // Accumulate until newline
      buf += c;
      if (buf.length() > 256) {
        buf.remove(0, buf.length() - 256); // prevent runaway buffer
      }
    }
  }
}

// === END: LISTPRESETS serial support =========================================