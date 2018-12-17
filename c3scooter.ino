#include <TM1637Display.h>
#include <FastLED.h>
#include <MPU6050.h>

// Constants
const int     kVersion        = 1;
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
//    Serial.print("Turn on after ");
//    Serial.print((current_ms - park_dot_ms));
//    Serial.println();
    park_dot_state = 1;
    park_dot_ms = current_ms;
  } else if (park_dot_state == 1 && (current_ms - park_dot_ms) > 100) {
    // turn it off
    display.setSegments(kwordBlnk);
//    Serial.print("Turn off after ");
//    Serial.print((current_ms - park_dot_ms));
//    Serial.println();
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
    park_flash_ms = 0;
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

  /*
  Serial.print("Time diff is ");
  Serial.print(current_ms - park_gyro_ms);
  Serial.println();
  */
  
  if ((current_ms - park_gyro_ms) > 50) {

  
    Wire.beginTransmission(kGyroMpuAddr);
    //Wire.write(0x3B); // start with 0x3B (ACCEL_XOUT_H)
    Wire.write(0x43); 
    Wire.endTransmission(false);
    Wire.requestFrom(kGyroMpuAddr, 7*2, true); // request 14 registers
  
    // "Wire.read()<<8 | Wire.read();" means two registers are read and stored in the same variable
    /*
    accel_x = Wire.read()<<8 | Wire.read(); // reading registers: 0x3B (ACCEL_XOUT_H) and 0x3C (ACCEL_XOUT_L)
    
    accel_y = Wire.read()<<8 | Wire.read(); // reading registers: 0x3D (ACCEL_YOUT_H) and 0x3E (ACCEL_YOUT_L)
    
    accel_z = Wire.read()<<8 | Wire.read(); // reading registers: 0x3F (ACCEL_ZOUT_H) and 0x40 (ACCEL_ZOUT_L)
    temperature = Wire.read()<<8 | Wire.read(); // reading registers: 0x41 (TEMP_OUT_H) and 0x42 (TEMP_OUT_L)
    */
    gyro_x = Wire.read()<<8 | Wire.read(); // reading registers: 0x43 (GYRO_XOUT_H) and 0x44 (GYRO_XOUT_L)
    gyro_y = Wire.read()<<8 | Wire.read(); // reading registers: 0x45 (GYRO_YOUT_H) and 0x46 (GYRO_YOUT_L)
    gyro_z = Wire.read()<<8 | Wire.read(); // reading registers: 0x47 (GYRO_ZOUT_H) and 0x48 (GYRO_ZOUT_L)
    
    //accel_total = 0;
    //accel_total = abs(accel_x_prev - accel_x) + abs(accel_y_prev - accel_y) + abs(accel_z_prev - accel_z);
    gyro_total = abs(gyro_x_prev - gyro_x) + abs(gyro_y_prev - gyro_y) + abs(gyro_z_prev - gyro_z);
    /*
    if (accel_x_prev > accel_x) {
      accel_calc = accel_x_prev - accel_x;
    } else {
      
    }*/
    

    //Serial.print("TEST "); Serial.print(accel_total);
    //Serial.print("Gyro Total "); Serial.print(gyro_total);
    /*
    Serial.print("aX = "); Serial.print(convert_int16_to_str(accel_x));
    
    Serial.print(" | aY = "); Serial.print(convert_int16_to_str(accel_y));
    
    Serial.print(" | aZ = "); Serial.print(convert_int16_to_str(accel_z));
    // the following equation was taken from the documentation [MPU-6000/MPU-6050 Register Map and Description, p.30]
    Serial.print(" | tmp = "); Serial.print(temperature/340.00+36.53);
    Serial.print(" | gX = "); Serial.print(convert_int16_to_str(gyro_x));
    Serial.print(" | gY = "); Serial.print(convert_int16_to_str(gyro_y));
    Serial.print(" | gZ = "); Serial.print(convert_int16_to_str(gyro_z));
    Serial.println();
    accel_x_prev = accel_x;
    accel_y_prev = accel_y;
    accel_z_prev = accel_z;
    */

    // use gyros to calculate height of alert bar
    if ((gyro_total > 250 && park_gyro_alert == 0) || (gyro_total > 150 && park_gyro_alert > 0)) {
      if (park_gyro_alert < kLedNum) {
        park_gyro_alert+=2;
        if (park_flash_state == 1) {
          fill_solid( &(leds[i]), park_gyro_alert, CRGB::Red );
        }
      }
    } else if (gyro_total < 100 && park_gyro_alert > 0) {
      park_gyro_alert--;
      if (park_flash_state == 1) {
        fill_solid( &(leds[i]), park_gyro_alert, CRGB::Red );
        leds[park_gyro_alert] = CRGB::Black;
      }
    } 

    // Alert flashing
    if ((current_ms - park_flash_ms) > 200) {
      if (park_flash_state == 0) {
        fill_solid( &(leds[i]), park_gyro_alert, CRGB::White );
        park_flash_state = 1;
      } else {
        // Only flash over certain threshold
        if (park_gyro_alert > 40) {
          fill_solid( &(leds[i]), kLedNum, CRGB::Black );
          park_flash_state = 0;
        }
      }
      park_flash_ms = current_ms;
    } 
    FastLED.show();

    if (park_gyro_alert == kLedNum) {
      displaySos();
      for(j = 0; j < 30; j++) {
        fill_solid( &(leds[i]), kLedNum, CRGB::White );
        FastLED.show();
        delay(50);
        fill_solid( &(leds[i]), kLedNum, CRGB::Black );
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
