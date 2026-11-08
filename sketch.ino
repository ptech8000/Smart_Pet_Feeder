#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>

// ---------------- LCD & RTC ----------------
LiquidCrystal_I2C lcd(0x27, 20, 4);   // 20 columns x 4 rows - change address if needed
RTC_DS1307 rtc;

// ---------------- Keypad ----------------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {8, 9, 10, 11};
byte colPins[COLS] = {A0, A1, A2, A3};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------- Stepper (A4988) ----------------
const int STEP_PIN = 12;
const int DIR_PIN  = 13;
int stepDelayMicros = 1000;   // speed of each step pulse (lower = faster)

// ---------------- Other I/O ----------------
const int BUTTON_PIN  = 2;   // manual feed
const int BUZZER_PIN  = 3;
const int BOWL_SWITCH = 4;   // LOW = food present, HIGH = empty (adjust to your switch wiring)

// ---------------- Settings stored in EEPROM ----------------
struct Settings {
  byte numSchedules;     // how many feed times are active (max 4)
  byte hour[4];
  byte minute[4];
  int  portionSteps;     // steps per portion
  byte magic;            // used to check EEPROM has been initialized
};
Settings settings;
const byte EEPROM_MAGIC = 77;

// ---------------- State ----------------
enum Menu { HOME, MENU_MAIN, SET_TIME, SET_SCHEDULE, SET_PORTION, MANUAL_FEED_MENU };
Menu currentMenu = HOME;

unsigned long lastLCDUpdate = 0;
byte lastFedMinuteMark = 255; // prevents double-feeding within same minute

// ---- Home-screen incremental redraw tracking ----
byte lastPawCol = 255;        // 255 = "not drawn yet", forces first update to place it
bool lastBowlEmpty = true;    // forces bowl line to be written on first update too

// =========================================================
// ----------------- CUSTOM LCD CHARACTERS ------------------
// Slots 0-4: five progressive fill levels for an animated
// progress bar (1..5 columns of a 5x8 cell shaded in).
// Slot 5: a small paw icon used for splash/idle animation.
byte barLvl1[8] = {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b10000};
byte barLvl2[8] = {0b11000,0b11000,0b11000,0b11000,0b11000,0b11000,0b11000,0b11000};
byte barLvl3[8] = {0b11100,0b11100,0b11100,0b11100,0b11100,0b11100,0b11100,0b11100};
byte barLvl4[8] = {0b11110,0b11110,0b11110,0b11110,0b11110,0b11110,0b11110,0b11110};
byte barLvl5[8] = {0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111};
byte pawIcon[8] = {0b01010,0b01010,0b00000,0b10001,0b11111,0b11111,0b01110,0b00000};

const byte CHAR_BAR1 = 0;
const byte CHAR_BAR2 = 1;
const byte CHAR_BAR3 = 2;
const byte CHAR_BAR4 = 3;
const byte CHAR_BAR5 = 4;
const byte CHAR_PAW  = 5;

void createCustomChars() {
  lcd.createChar(CHAR_BAR1, barLvl1);
  lcd.createChar(CHAR_BAR2, barLvl2);
  lcd.createChar(CHAR_BAR3, barLvl3);
  lcd.createChar(CHAR_BAR4, barLvl4);
  lcd.createChar(CHAR_BAR5, barLvl5);
  lcd.createChar(CHAR_PAW,  pawIcon);
}

// =========================================================
void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BOWL_SWITCH, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  createCustomChars();

  if (!rtc.begin()) {
    lcd.setCursor(0, 0);
    lcd.print("RTC NOT FOUND!");
    while (1) delay(1000);
  }

  if (!rtc.isrunning()) {
    // Set RTC to compile time on first run / after the RTC stopped
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  loadSettings();
  bootAnimation();
  showHomeScreen();
}

// =========================================================
void loop() {
  checkSchedule();
  handleManualButton();
  handleKeypad();

  if (millis() - lastLCDUpdate > 1000 && currentMenu == HOME) {
    updateHomeScreen();   // lightweight per-second refresh, no full clear()
    lastLCDUpdate = millis();
  }
}

// =========================================================
// ------------------- BOOT ANIMATION -----------------------
// Fills a 16-cell progress bar smoothly (using the 5-level
// partial-block custom chars) while showing a walking paw,
// then hands off to the home screen.
void bootAnimation() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Smart Pet Feeder");
  lcd.setCursor(0, 1);
  lcd.print("--------------------"); // 20-char divider

  const byte barCols = 16;      // number of full-block cells in the bar
  const byte totalSteps = barCols * 5; // 5 fill levels per cell

  for (byte s = 1; s <= totalSteps; s++) {
    byte fullCells = (s - 1) / 5;
    byte partialLevel = ((s - 1) % 5) + 1; // 1..5

    lcd.setCursor(2, 2);
    for (byte c = 0; c < fullCells; c++) lcd.write(CHAR_BAR5);
    lcd.write((byte)(CHAR_BAR1 + (partialLevel - 1)));
    for (byte c = fullCells + 1; c < barCols; c++) lcd.print(" ");

    // Walking paw on the row below, moves roughly with the bar
    lcd.setCursor(0, 3);
    lcd.print("Loading             "); // clears old paw position
    byte pawCol = 8 + (s / 12) % 4;     // gentle side-to-side motion
    lcd.setCursor(pawCol, 3);
    lcd.write(CHAR_PAW);

    delay(15);
  }

  lcd.setCursor(0, 3);
  lcd.print(" Ready!             ");
  delay(500);
}

// =========================================================
// ------------------- EEPROM SETTINGS --------------------
void loadSettings() {
  EEPROM.get(0, settings);
  if (settings.magic != EEPROM_MAGIC) {
    // First run defaults
    settings.numSchedules = 1;
    settings.hour[0] = 8;
    settings.minute[0] = 0;
    settings.portionSteps = 400;  // roughly one full motor rev on a 1.8deg/step motor
    settings.magic = EEPROM_MAGIC;
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
}

// =========================================================
// ------------------- SCHEDULE CHECK ----------------------
void checkSchedule() {
  DateTime now = rtc.now();

  for (byte i = 0; i < settings.numSchedules; i++) {
    if (now.hour() == settings.hour[i] && now.minute() == settings.minute[i]) {
      if (lastFedMinuteMark != now.minute()) {
        dispenseFood();
        lastFedMinuteMark = now.minute();
      }
    }
  }

  // Reset the guard once we're outside any scheduled minute
  bool inScheduleMinute = false;
  for (byte i = 0; i < settings.numSchedules; i++) {
    if (now.hour() == settings.hour[i] && now.minute() == settings.minute[i]) inScheduleMinute = true;
  }
  if (!inScheduleMinute) lastFedMinuteMark = 255;
}

// =========================================================
// ------------------- MANUAL BUTTON -----------------------
void handleManualButton() {
  static unsigned long lastPress = 0;
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastPress > 800) {
    lastPress = millis();
    dispenseFood();
  }
}

// =========================================================
// ------------------- DISPENSE FOOD ------------------------
void dispenseFood() {
  if (digitalRead(BOWL_SWITCH) == HIGH) {
    // HIGH = empty, per BOWL_SWITCH wiring comment above
    emptyHopperAlert();
    showHomeScreen();
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dispensing food...");
  lcd.setCursor(0, 1);
  lcd.print("--------------------");

  digitalWrite(DIR_PIN, HIGH); // set rotation direction (flip if needed)

  const byte barCols = 18;     // bar width, leaves 1-col margin each side
  // Update the LCD roughly 90 times over the whole dispense (barCols*5 levels)
  int totalLevels = barCols * 5;
  int stepsPerLevel = max(1, settings.portionSteps / totalLevels);
  int levelsShown = 0;

  for (int i = 0; i < settings.portionSteps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(stepDelayMicros);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepDelayMicros);

    if (i % stepsPerLevel == 0 && levelsShown < totalLevels) {
      levelsShown++;
      byte fullCells = (levelsShown - 1) / 5;
      byte partialLevel = ((levelsShown - 1) % 5) + 1;

      lcd.setCursor(1, 2);
      for (byte c = 0; c < fullCells; c++) lcd.write(CHAR_BAR5);
      lcd.write((byte)(CHAR_BAR1 + (partialLevel - 1)));

      // Bouncing paw indicator on the last row while we wait
      lcd.setCursor(0, 3);
      lcd.print("   Feeding");
      byte dots = (levelsShown / 3) % 4;
      for (byte d = 0; d < dots; d++) lcd.print(".");
      for (byte d = dots; d < 3; d++) lcd.print(" ");
      lcd.print(" ");
      lcd.write(CHAR_PAW);
    }
  }

  // Fill the bar completely so it always ends full, regardless of rounding
  lcd.setCursor(1, 2);
  for (byte c = 0; c < barCols; c++) lcd.write(CHAR_BAR5);

  confirmBeep();
  successFlash();
  delay(600);
}

// Quick celebratory flash across row 3 when a portion finishes
void successFlash() {
  lcd.setCursor(0, 0);
  lcd.print("Dispensing food...  ");
  lcd.setCursor(0, 3);
  lcd.print("     Done!  ");
  lcd.write(CHAR_PAW);
  lcd.print("      ");

  for (byte blink = 0; blink < 2; blink++) {
    lcd.noBacklight();
    delay(120);
    lcd.backlight();
    delay(120);
  }
}

void confirmBeep() {
  tone(BUZZER_PIN, 2000, 150);
  delay(200);
  tone(BUZZER_PIN, 2500, 150);
}

void errorBeep() {
  tone(BUZZER_PIN, 400, 400);
}

// Animated "hopper empty" warning: flashing text + a small
// ASCII spinner so it reads as active feedback, not a freeze.
void emptyHopperAlert() {
  const char spinnerFrames[4] = {'|', '/', '-', '\\'};
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Hopper empty!");
  lcd.setCursor(0, 1);
  lcd.print("   Refill needed");
  errorBeep();

  for (byte i = 0; i < 16; i++) {
    lcd.setCursor(0, 3);
    lcd.print(i % 2 == 0 ? "  !! ADD FOOD !!    " : "                    ");
    lcd.setCursor(18, 3);
    lcd.print(spinnerFrames[i % 4]);
    delay(125);
  }
  lcd.setCursor(0, 3);
  lcd.print("                    ");
}

// =========================================================
// ------------------- HOME SCREEN --------------------------
// Full redraw: only called when we *enter* the home screen
// (boot, after a menu action, after dispensing, etc). It
// clears the whole display once and draws every element,
// then records where the paw and bowl status ended up so
// the per-second updateHomeScreen() below can pick up from
// here without needing to clear() again.
void showHomeScreen() {
  DateTime now = rtc.now();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("== Smart Pet Feeder ");

  lcd.setCursor(0, 1);
  char buf[21];
  sprintf(buf, "%02d:%02d:%02d   %02d/%02d/%02d",
          now.hour(), now.minute(), now.second(),
          now.day(), now.month(), now.year() % 100);
  lcd.print(buf);

  lcd.setCursor(0, 2);
  lcd.print("Next feed: ");
  if (settings.numSchedules > 0) {
    char t[6];
    sprintf(t, "%02d:%02d", settings.hour[0], settings.minute[0]);
    lcd.print(t);
    lcd.print("     ");
  } else {
    lcd.print("none      ");
  }

  lcd.setCursor(0, 3);
  bool empty = digitalRead(BOWL_SWITCH) == HIGH;
  lcd.print(empty ? "Bowl: EMPTY " : "Bowl: OK    ");
  lastBowlEmpty = empty;

  // Idle animation: a small paw that gently walks back and
  // forth in the free space at the end of row 3, driven by
  // the current second so it moves once per LCD refresh.
  byte cyclePos = now.second() % 8;
  byte pawCol = (cyclePos <= 4) ? (12 + cyclePos) : (12 + (8 - cyclePos));
  lcd.setCursor(pawCol, 3);
  lcd.write(CHAR_PAW);
  lastPawCol = pawCol;
}

// ------------------- HOME SCREEN (lightweight tick) --------
// Called once a second while idling on the home screen.
// Never calls lcd.clear() and never rewrites text that
// hasn't changed, so the screen stops flickering: only the
// clock digits update every call, the bowl line updates
// only when the reading actually flips, and the paw's old
// cell is blanked individually instead of repainting row 3.
void updateHomeScreen() {
  DateTime now = rtc.now();

  // Clock digits (row 1) - always changes, safe to rewrite every tick
  lcd.setCursor(0, 1);
  char buf[21];
  sprintf(buf, "%02d:%02d:%02d   %02d/%02d/%02d",
          now.hour(), now.minute(), now.second(),
          now.day(), now.month(), now.year() % 100);
  lcd.print(buf);

  // Bowl status (row 3) - only touch the LCD if it actually changed
  bool empty = digitalRead(BOWL_SWITCH) == HIGH;
  if (empty != lastBowlEmpty) {
    lcd.setCursor(0, 3);
    lcd.print(empty ? "Bowl: EMPTY " : "Bowl: OK    ");
    lastBowlEmpty = empty;
  }

  // Walking paw - erase old cell, draw new cell, nothing else
  byte cyclePos = now.second() % 8;
  byte pawCol = (cyclePos <= 4) ? (12 + cyclePos) : (12 + (8 - cyclePos));
  if (pawCol != lastPawCol) {
    if (lastPawCol != 255) {
      lcd.setCursor(lastPawCol, 3);
      lcd.print(" ");
    }
    lcd.setCursor(pawCol, 3);
    lcd.write(CHAR_PAW);
    lastPawCol = pawCol;
  }
}

// =========================================================
// ------------------- KEYPAD MENU ---------------------------
void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  switch (currentMenu) {
    case HOME:
      if (key == 'A') { currentMenu = MENU_MAIN; showMainMenu(); }
      break;

    case MENU_MAIN:
      if (key == '1') { currentMenu = SET_TIME; setTimeMenu(); }
      else if (key == '2') { currentMenu = SET_SCHEDULE; setScheduleMenu(); }
      else if (key == '3') { currentMenu = SET_PORTION; setPortionMenu(); }
      else if (key == '4') { dispenseFood(); currentMenu = HOME; }
      else if (key == 'D') { currentMenu = HOME; showHomeScreen(); }
      break;

    default:
      // Any key returns to home from a sub-menu after action completes
      currentMenu = HOME;
      showHomeScreen();
      break;
  }
}

// =========================================================
void showMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("==== Main Menu =====");
  lcd.setCursor(0, 1); lcd.print("1:Time      2:Sched");
  lcd.setCursor(0, 2); lcd.print("3:Portion   4:Feed");
  lcd.setCursor(0, 3); lcd.print("D: Back");
}

// ---- Set RTC time: format HHMM then # to confirm ----
void setTimeMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Set time (HHMM)");
  lcd.setCursor(0, 1); lcd.print("Enter then press #");
  String input = "";
  while (true) {
    char k = keypad.waitForKey();
    if (k == '#') break;
    if (k == 'D') { currentMenu = HOME; showHomeScreen(); return; }
    if (isDigit(k) && input.length() < 4) {
      input += k;
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print(input);
    }
  }
  if (input.length() == 4) {
    int hh = input.substring(0, 2).toInt();
    int mm = input.substring(2, 4).toInt();
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), hh, mm, 0));
    lcd.setCursor(0, 3);
    lcd.print("Time updated!  ");
    confirmBeep();
    delay(1000);
  }
  currentMenu = HOME;
  showHomeScreen();
}

// ---- Set first feed schedule slot (extend similarly for more slots) ----
void setScheduleMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Set feed 1 (HHMM)");
  lcd.setCursor(0, 1); lcd.print("Enter then press #");
  String input = "";
  while (true) {
    char k = keypad.waitForKey();
    if (k == '#') break;
    if (k == 'D') { currentMenu = HOME; showHomeScreen(); return; }
    if (isDigit(k) && input.length() < 4) {
      input += k;
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print(input);
    }
  }
  if (input.length() == 4) {
    settings.numSchedules = 1; // this menu sets slot 0; expand for multiple slots as needed
    settings.hour[0] = input.substring(0, 2).toInt();
    settings.minute[0] = input.substring(2, 4).toInt();
    saveSettings();
    lcd.setCursor(0, 3);
    lcd.print("Schedule saved!");
    confirmBeep();
    delay(1000);
  }
  currentMenu = HOME;
  showHomeScreen();
}

// ---- Set portion size in steps ----
void setPortionMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Set portion (steps)");
  lcd.setCursor(0, 1); lcd.print("Current: ");
  lcd.print(settings.portionSteps);
  String input = "";
  while (true) {
    char k = keypad.waitForKey();
    if (k == '#') break;
    if (k == 'D') { currentMenu = HOME; showHomeScreen(); return; }
    if (isDigit(k) && input.length() < 4) {
      input += k;
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print(input);
    }
  }
  if (input.length() > 0) {
    settings.portionSteps = input.toInt();
    saveSettings();
    lcd.setCursor(0, 3);
    lcd.print("Portion saved!  ");
    confirmBeep();
    delay(1000);
  }
  currentMenu = HOME;
  showHomeScreen();
}
