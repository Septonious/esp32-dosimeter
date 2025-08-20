#include <Adafruit_ST7735.h>
#include "CG_RadSens.h"

#define TFT_CS 0
#define TFT_RST 1
#define TFT_DC 2
#define soundPin 10

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
CG_RadSens radSens(RS_DEFAULT_I2C_ADDRESS); //0x66

//Options
float cpmPerUsv = 1000.0f; //For SBT-10A counter. Calibrated using DRGB ECO-1M dosimeter
long selfCPS = 0; //Mica counters sometimes have their own radiation. This value gets subtracted from CPS when CPM is lower than 350
int gcSurfaceArea = 36; //cm2, used for calculating particle density. 32 for SI-8B, 36 for SBT-10A
int timerCorrection = 25; //ms, used for correcting MCU timer
int convergence = 400; //CPS->uR/h, 380 for SI-8B, 400 for SBT-10A, 55 for SBT-11A, 110 for SBM-20

//Global Variables
float dose = 0.0f; //Accumulated dose
float maxDoseRate = 0.0f;
int cpsArray[60] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int hours = 0; //Uptime
int minutes = 0; //Uptime 
int seconds = 0; //Uptime
int graphX = 0; //Position of the current "pillar" on the CPS graph
long prvCPM = 0; //Previous CPM value
bool isOverloaded = false; //Triggers when the dosimeter exceeds maxCPS counts per second
bool isAlarmTriggered = false; //Triggers when the dose rate is over 20

void setup() {
  esp_reset_reason();
  Serial.begin(115200);
  Wire.begin();

  //Configure pins
  pinMode(3, INPUT);
  pinMode(soundPin, OUTPUT);

  //Init TFT display
  tft.initR(INITR_REDTAB);
  tft.setRotation(90);
  tft.fillScreen(ST77XX_BLACK);

  //Init geiger counter board
  radSens.init();
  radSens.setSensitivity(convergence);
  radSens.resetPulses();

  //Play sound on startup
  digitalWrite(soundPin, HIGH);
  delay(50);
  digitalWrite(soundPin, LOW);
  delay(50);
  digitalWrite(soundPin, HIGH);
  delay(50);
  digitalWrite(soundPin, LOW);
  delay(50);
}

void loop() {
  //Quickly fill the CPS array on startup
  if (seconds == 30) {
    for (int i = 30; i < 60; i++) {
      cpsArray[i - 30] = cpsArray[i];
    }
  }

  //Main calculations
  bool mSv = false; //Used for switching measurement units from uSv to mSv when the radiation gets very intense
  int batteryPercentage = min(int(min(max(analogRead(A3) / 684.0f - 2.5f, 0.0f), 1.65f) / 1.65f * 100.0f), 99); //Calculate battery percentage
  long cps = int(radSens.getNumberOfPulses());
       cps = max(cps - selfCPS * int(prvCPM < 350), long(0)); //Calculate counts per second
  long cpm = calculateCPM(cps);
  prvCPM = cpm;
  long density = cpm / gcSurfaceArea; //Calculate particle density
  float doseRate = float(cpm / cpmPerUsv); //Calculate current dose rate
  maxDoseRate = max(maxDoseRate, doseRate); //Calculate maximum dose rate observed since startup
  dose += (doseRate / 3600.0f) * 1000.0f * 0.873f; //uSv/h -> uSv/s -> nSv/s -> absorbed air equivalent dose in nSv

  //Resets pulse count every second, thus measuring CPS
  radSens.resetPulses();

  //Serial output
  Serial.print("Dose rate: ");
  Serial.println(doseRate);
  Serial.print("D/cm2/min: ");
  Serial.println(density);
  Serial.print("CPM: ");
  Serial.println(cpm);
  Serial.print("Dose: ");
  Serial.println(dose);
  Serial.print("Batt. percentage: ");
  Serial.println(batteryPercentage);
  for (int i = 0; i < 60; i++) {
    Serial.print(cpsArray[i]);
    Serial.print(" ");
  }
  Serial.println();
  Serial.println("--------");

  //Draw black rectangles in areas where the information needs to be updated every second
  tft.fillRect(0, 0, 128, 9, ST7735_BLACK);
  tft.fillRect(0, 11, 128, 45, ST7735_BLACK);
  tft.fillRect(25, 61, 103, 8, ST7735_BLACK);
  tft.fillRect(25, 71, 103, 8, ST7735_BLACK);
  tft.fillRect(25, 81, 103, 8, ST7735_BLACK);

  //Draw separation lines
  tft.drawLine(0, 10, 128, 10, ST7735_WHITE);
  tft.drawLine(0, 58, 128, 58, ST7735_WHITE);
  tft.drawLine(0, 90, 128, 90, ST7735_WHITE);

  //Output main information
  if (cps <= 10000 && !isOverloaded) {
    if (doseRate >= 0.00 && doseRate <= 9.99) {
      tft.setCursor(22, 17);
    } else if (doseRate >= 10.00 && doseRate <= 99.99) {
      tft.setCursor(16, 17);
    } else if (doseRate >= 100.00 && doseRate <= 999.99) {
      tft.setCursor(10, 17);
    } else {
      mSv = true;
      if (doseRate >= 1000.00 && doseRate <= 9999.99) {
        tft.setCursor(22, 17);
      } else {
        tft.setCursor(2, 17);
      }
    }

    if (doseRate <= 0.20) {
      tft.setTextColor(ST77XX_GREEN);
    } else if (doseRate <= 1.00) {
      tft.setTextColor(ST77XX_YELLOW);
    } else if (doseRate > 1.00) {
      tft.setTextColor(ST77XX_RED);
    }

    tft.setTextSize(2);
    if (mSv) {
      tft.print(doseRate / 1000.0f);
    } else {
      tft.print(doseRate);
    }
    tft.setTextSize(1);
    tft.print(" ");
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    if (mSv) {
      tft.println("mSv/h");
    } else {
      tft.println("uSv/h");
    }

    if (density <= 9) {
      tft.setCursor(34, 39);
    } else if (density >= 10 && density <= 99) {
      tft.setCursor(28, 39);
    } else if (density >= 100 && density <= 999) {
      tft.setCursor(22, 39);
    } else if (density >= 1000 && density <= 9999) {
      tft.setCursor(16, 39);
    } else if (density >= 10000 && density <= 99999) {
      tft.setCursor(12, 39);
    }

    if (density <= 10) {
      tft.setTextColor(ST77XX_GREEN);
    } else if (density <= 30) {
      tft.setTextColor(ST77XX_YELLOW);
    } else if (density > 30) {
      tft.setTextColor(ST77XX_RED);
    }

    tft.setTextSize(2);
    tft.print(density);
    tft.setTextSize(1);
    tft.print(" ");
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.println("D/cm2/m");
  } else {
    isOverloaded = true;
    tft.setCursor(17, 28);
    tft.setTextColor(ST7735_RED);
    tft.setTextSize(2);
    tft.println("OVERLOAD");
  }

  tft.setTextSize(1);
  tft.setCursor(2, 61);
  tft.setTextColor(ST7735_WHITE);
  tft.print("CPS/CPM: ");
  if (cps <= 10) {
    tft.setTextColor(ST77XX_GREEN);
  } else if (cps <= 30) {
    tft.setTextColor(ST77XX_YELLOW);
  } else if (cps > 30) {
    tft.setTextColor(ST77XX_RED);
  }
  tft.print(cps);
  tft.setTextColor(ST7735_WHITE);
  tft.print("/");
  if (cpm <= 200) {
    tft.setTextColor(ST77XX_GREEN);
  } else if (cpm <= 350) {
    tft.setTextColor(ST77XX_YELLOW);
  } else if (cpm > 350) {
    tft.setTextColor(ST77XX_RED);
  }
  tft.print(cpm);
  tft.setCursor(2, 71);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Max: ");
  if (maxDoseRate <= 0.20) {
    tft.setTextColor(ST77XX_GREEN);
  } else if (maxDoseRate <= 1.00) {
    tft.setTextColor(ST77XX_YELLOW);
  } else if (maxDoseRate > 1.00) {
    tft.setTextColor(ST77XX_RED);
  }
  if (mSv) {
    tft.print(maxDoseRate / 1000.0f);
    tft.setTextColor(ST7735_WHITE);
    tft.println(" mSv/h");
  } else {
    tft.print(maxDoseRate);
    tft.setTextColor(ST7735_WHITE);
    tft.println(" uSv/h");
  }
  tft.setCursor(2, 81);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Dose: ");
  tft.setTextColor(ST7735_CYAN);

  if (dose < 99.99) {
    tft.print(dose);
    tft.setTextColor(ST7735_WHITE);
    tft.println(" nSv");
  } else {
    tft.print(dose / 1000.0f);
    tft.setTextColor(ST7735_WHITE);
    tft.println(" uSv");
  }

  //Battery percentage indicator
  tft.setCursor(110, 0);
  if (batteryPercentage < 25) {
    tft.setTextColor(ST7735_RED);
  } else if (batteryPercentage < 50) {
    tft.setTextColor(ST7735_YELLOW);
  } else if (batteryPercentage <= 100) {
    tft.setTextColor(ST7735_GREEN);
  }
  tft.print(batteryPercentage);
  tft.setTextColor(ST7735_WHITE);
  tft.print("%");

  //Uptime
  tft.setCursor(2, 0);
  tft.setTextColor(ST7735_WHITE);
  tft.print(hours);
  tft.print(":");
  tft.print(minutes);
  tft.print(":");
  tft.println(seconds);

  //CPS graph
  graphX += 1;
  if (graphX == 128) {
    graphX = 0;
    tft.fillRect(0, 102, 128, 26, ST7735_BLACK);
  }

  int graphY = max(92, 127 - int(cps / max(min(8, 1 + int(cps / 25)), 1)));
  if (cps <= 10) {
    tft.drawLine(graphX, 128, graphX, graphY, ST7735_GREEN);
  } else if (cps <= 30) {
    tft.drawLine(graphX, 128, graphX, graphY, ST7735_YELLOW);
  } else if (cps > 30) {
    tft.drawLine(graphX, 128, graphX, graphY, ST7735_RED);
  }

  //Timer and alarms
  if ((doseRate > 0.20 && !isAlarmTriggered) || isOverloaded) {
    isAlarmTriggered = true;
    digitalWrite(soundPin, HIGH);
    delay(100);
    digitalWrite(soundPin, LOW);
    delay(50);
    digitalWrite(soundPin, HIGH);
    delay(100);
    digitalWrite(soundPin, LOW);
    delay(50);
    digitalWrite(soundPin, HIGH);
    delay(100);
    digitalWrite(soundPin, LOW);
    delay(600 - timerCorrection);
  } else {
    if (doseRate <= 0.20 && isAlarmTriggered) isAlarmTriggered = false;
    delay(1000 - timerCorrection);
  }

  //Uptime
  seconds += 1;
  if (seconds >= 60) {
    minutes += 1;
    seconds = 0;
  }
  if (minutes >= 60) {
    hours += 1;
    minutes = 0;
  }
}

long calculateCPM(long cps) {
  //Update the CPS array with the current CPS value
  if (cps < 50) {
    for (int i = 0; i < 59; i++) {
      cpsArray[i] = cpsArray[i+1];
    }
    cpsArray[59] = cps;
  } else {
    for (int i = 0; i < 58; i++) {
      cpsArray[i] = cpsArray[i+2];
      cpsArray[i+1] = cpsArray[i+2];
    }
    cpsArray[58] = cps;
    cpsArray[59] = cps;
  }

  //Fast CPM calculation when CPS suddenly increases or drops
  if (((cps > cpsArray[58] * 6) && (cpsArray[58] >= 3)) || ((cps * 2 < cpsArray[58]) && (cpsArray[58] + cpsArray[57] >= 75))) {
    for (int i = 0; i < 60; i++) {
      cpsArray[i] = cps;
    }
  }

  //Get CPM from the sum of CPS values stored in CPS array
  long cpm = 0;
  for (int i = 0; i < 60; i++) {
    cpm += cpsArray[i];
  }

  return cpm;
}