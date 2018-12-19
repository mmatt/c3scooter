#include <FastLED.h>
#include <MPU6050.h>
#include <TM1637Display.h>

// Constants
const int     kVersion        = 2;
const int     kDisplayClkPin  = 8;
const int     kDisplayDioPin  = 7;
const int     kGyroMpuAddr    = 0x68;
const int     kLedDinPin      = 9;
const int     kButtonDinPin   = 10;
const int     kLedNum         = 80;
const int     kLedBrightness  = 64;

const uint8_t kwordBye_[]     = { 0b01111111, 0b01101110, 0b01111001, 0x00 };
const uint8_t kword_bye[]     = { 0x00, 0b01111111, 0b01101110, 0b01111001 };
const uint8_t kwordHey_[]     = { 0b01110110, 0b01111001, 0b01101110, 0x00 };
const uint8_t kword_hey[]     = { 0x00, 0b01110110, 0b01111001, 0b01101110 };
const uint8_t kwordSos_[]     = { 0b01101101, 0b00111111, 0b01101101, 0x00 };
const uint8_t kword_sos[]     = { 0x00, 0b01101101, 0b00111111, 0b01101101 };
const uint8_t kwordDots[]     = { 0x00, 0b10000000, 0x00, 0x00 };
const uint8_t kwordBlnk[]     = { 0x00, 0x00, 0x00, 0x00 };

// Variables
unsigned int  i, j;
unsigned long current_ms      = 0;

unsigned long button_ms       = 0;
unsigned int  button_state    = 0;
unsigned int  button_block    = 2000;

unsigned int  park_mode       = 1;
unsigned long park_dot_ms     = 0;
unsigned int  park_dot_state  = 0;
unsigned long park_flash_ms   = 0;
unsigned int  park_flash_state= 0;
unsigned long park_gyro_ms    = 0;
unsigned int  park_gyro_alert = 0;
unsigned long park_sos_ms     = 0;
unsigned int  park_sos_state  = 0;

unsigned int  drive_glitch    = 0;
unsigned long drive_dot_ms    = 0;
unsigned int  drive_dot_state = 0;

int16_t       accel_x, accel_y, accel_z;
int16_t       accel_x_prev, accel_y_prev, accel_z_prev;
int16_t       accel_total, accel_calc;
int16_t       gyro_x, gyro_y, gyro_z;
int16_t       gyro_x_prev, gyro_y_prev, gyro_z_prev;
int16_t       gyro_total, gyro_calc;
int16_t       temperature;

CRGB leds[kLedNum];

////////////////////////////
// 7-SEGMENT DISPLAY CODE //
////////////////////////////

TM1637Display display(kDisplayClkPin, kDisplayDioPin);

//
//  |-A-|
//  F   B  X
//  |-G-|
//  E   C  X
//  |-D-|
//
// Bits are set: XGFEDCBA
// setBrightness: 0 to 7

int extractDigit(size_t digit, int n) {
  int mask = 1;
  while ( digit -- > 0 )
    mask *= 10;
  if (n < mask / 10) { // insufficient digits
    return display.encodeDigit(0);
  }
  while ( n >= mask )
    n /= mask;
  return display.encodeDigit(n % 10);
}


void glitchC3() {

}

void displayShiftPark() {
  display.setSegments(kwordBye_);
  delay(450);
  display.setSegments(kword_bye);
  delay(200);
}

void displayShiftDrive() {
  display.setSegments(kwordHey_);
  delay(450);
  display.setSegments(kword_hey);
  delay(200);
}

void displayPark() {
  if (park_sos_ms > 0) {
    displaySos();
  } else if (park_dot_state == 0 && (current_ms - park_dot_ms) > 2000) {
    // turn it on
    display.setSegments(kwordDots);
    park_dot_state = 1;
    park_dot_ms = current_ms;
  } else if (park_dot_state == 1 && (current_ms - park_dot_ms) > 100) {
    // turn it off
    display.setSegments(kwordBlnk);
    park_dot_state = 0;
    park_dot_ms = current_ms;
  }
}

void displayDrive() {
  if ((current_ms - drive_dot_ms) > 1000) {
    
    unsigned int up, hrs, mins;
    uint8_t timer[] = { 0xff, 0xff, 0xff, 0xff };
  
    // Add half a second to round minutes
    mins = ((current_ms / 1000) + 30) / 60;
    hrs  = mins / 60;
    mins = mins % 60;
    
    if (hrs >= 0 && hrs < 10) {
      timer[0] = 0x00;
      timer[1] = extractDigit(1, hrs);
    } else {
      timer[0] = extractDigit(1, hrs);
      timer[1] = extractDigit(2, hrs);
    }
    if (mins >= 0 && mins < 10) {
      timer[2] = display.encodeDigit(0);
      timer[3] = extractDigit(1, mins);
    } else {
      timer[2] = extractDigit(1, mins);
      timer[3] = extractDigit(2, mins);
    }

    if (drive_dot_state == 0) {
      timer[1] = timer[1] + 128;
      drive_dot_state = 1;
    } else {
      drive_dot_state = 0;
    }
    drive_dot_ms = current_ms;
    display.setSegments(timer);

    // Reset SOS in drive mode 
    park_sos_ms = 0;
  }
}

void displaySos() {
  if ((current_ms - park_sos_ms) > 250) {
    if (park_sos_state == 0) {
      display.setSegments(kwordSos_);
      park_sos_state = 1;
    } else {
      display.setSegments(kword_sos);
      park_sos_state = 0;
    }
    park_sos_ms = current_ms;
  }
}

///////////////
// GYROSCOPE //
///////////////

// SCL to pin A5
// SDA to pin A4

void readGyro() {
 
  if ((current_ms - park_gyro_ms) > 100) {
  
    Wire.beginTransmission(kGyroMpuAddr);
    Wire.write(0x43); 
    Wire.endTransmission(false);
    Wire.requestFrom(kGyroMpuAddr, 3*2, true); // request 6 registers
  
    gyro_x = Wire.read()<<8 | Wire.read(); // registers: 0x43 (GYRO_XOUT_H) and 0x44 (GYRO_XOUT_L)
    gyro_y = Wire.read()<<8 | Wire.read(); // registers: 0x45 (GYRO_YOUT_H) and 0x46 (GYRO_YOUT_L)
    gyro_z = Wire.read()<<8 | Wire.read(); // registers: 0x47 (GYRO_ZOUT_H) and 0x48 (GYRO_ZOUT_L)
    // Check if we have something to compare
    if (gyro_x_prev > 0 || gyro_y_prev > 0 || gyro_z_prev > 0) {
      gyro_total = abs(gyro_x_prev - gyro_x) + abs(gyro_y_prev - gyro_y) + abs(gyro_z_prev - gyro_z);
    }

    Serial.print("Gyro total "); Serial.print(gyro_total);
    Serial.println();

    // Increase alertness on thershold (higher first step)
    if ((gyro_total > 300 && park_gyro_alert == 0) || (gyro_total > 200 && park_gyro_alert > 0)) {
      if (park_gyro_alert < kLedNum) {
        park_gyro_alert+=2;
        Serial.print("Increase!");
        Serial.println();
      }
      fill_solid( leds, park_gyro_alert, CRGB::Red );
    // Decrease alerness
    } else if (gyro_total < 100 && park_gyro_alert > 0) {
      park_gyro_alert--;
      Serial.print("Decrease!");
      Serial.println();
      fill_solid( leds, park_gyro_alert, CRGB::Red );
      leds[park_gyro_alert] = CRGB::Black;
    } 

    Serial.print("Now at: "); Serial.print(park_gyro_alert);
    Serial.println();

    // Alert flashing
    if ((current_ms - park_flash_ms) > 200 && park_gyro_alert > 50) {
      if (park_flash_state == 0) {
        fill_solid( leds, park_gyro_alert, CRGB::White );
        park_flash_state = 1;
      } else {
        fill_solid( leds, kLedNum, CRGB::Black );
        park_flash_state = 0;
      }
      park_flash_ms = current_ms;
    } 
    FastLED.show();

    if (park_gyro_alert >= kLedNum) {
      park_gyro_alert = kLedNum;
      displaySos();
      for(j = 0; j < 30; j++) {
        fill_solid( leds, kLedNum, CRGB::White );
        FastLED.show();
        delay(50);
        fill_solid( leds, kLedNum, CRGB::Black );
        FastLED.show();
        delay(50);
      }
    }

    gyro_x_prev = gyro_x;
    gyro_y_prev = gyro_y;
    gyro_z_prev = gyro_z;
    //Serial.print("Gyro alert "); Serial.print(park_gyro_alert);
    //Serial.println();
    park_gyro_ms = current_ms;
  }
}

///////////////
// LED STRIP //
///////////////


///////////////
// MAIN LOOP //
///////////////

void setup() {
  Serial.begin(9600);
  Serial.print("Start");
  Serial.println();

  // Initialize 7 segment display
  display.setBrightness(2, true);

  // Show version number
  display.showNumberDecEx(kVersion, 0x80, true, 2, 1);
  delay(3000);

  // Initialize LED strip  
  FastLED.addLeds<WS2812B, kLedDinPin, GRB>(leds, kLedNum).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(kLedBrightness);

  // Initizialize button
  pinMode(kButtonDinPin, INPUT);

  // Initialize gyroscope communication
  Wire.begin();
  Wire.beginTransmission(kGyroMpuAddr);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0); // wake up the MPU-6050
  Wire.endTransmission(true);
}

void loop() {
  current_ms = millis();

  // Check button state
  button_state = digitalRead(kButtonDinPin);
  if (button_state == HIGH && park_mode == 0 && (current_ms - button_ms) > button_block) {
    // enter parking mode
    Serial.print("Enter parking mode");
  Serial.print((millis() - current_ms));
  Serial.println();
    displayShiftPark();
    park_mode = 1;
    button_ms = current_ms;
  } else if (button_state == HIGH && park_mode == 1 && (current_ms - button_ms) > button_block) {
        Serial.print("Enter drive mode");
  Serial.print((millis() - current_ms));
  Serial.println();
    displayShiftDrive();
    park_mode = 0;
    button_ms = current_ms;
  }

  // Act according to mode
  if (park_mode == 1) {
    // We are parked
    displayPark();
    readGyro();
    /*
    for(int i = 0; i < kLedNum; i++) {
      leds[i] = CRGB::Blue;    // set our current dot to red
      FastLED.show();
      leds[i] = CRGB::Black;  // set our current dot to black before we continue
    }
    */
  } else {
    // We're in drive
    displayDrive();
    for(i = 0; i < kLedNum; i++) {
      leds[i] = CRGB::Red;    // set our current dot to red
      FastLED.show();
      leds[i] = CRGB::Black;  // set our current dot to black before we continue
    }
  }

  
  //delay(100);
  
  /*
  Serial.print("This loop took ");
  Serial.print((millis() - current_ms));
  Serial.println();
  */
  
}
