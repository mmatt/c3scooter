#include <TM1637Display.h>
#include <FastLED.h>

// Constants
const int kDisplayCLKpin  = 3;
const int kdisplayDIOpin  = 2;
const int kledDINpin      = 6;
const int kbuttonDINpin   = 5;
const int kledNum         = 89;

const uint8_t kwordBye_[] = { 0b01111111, 0b01101110, 0b01111001, 0x00 };
const uint8_t kword_bye[] = { 0x00, 0b01111111, 0b01101110, 0b01111001 };
const uint8_t kwordHey_[] = { 0b01110110, 0b01111001, 0b01101110, 0x00 };
const uint8_t kword_hey[] = { 0x00, 0b01110110, 0b01111001, 0b01101110 };
const uint8_t kwordDots[] = { 0x00, 0b10000000, 0x00, 0x00 };
const uint8_t kwordBlnk[] = { 0x00, 0x00, 0x00, 0x00 };

// Variables
unsigned int  i;
unsigned long current_ms      = 0;

unsigned long button_ms       = 0;
unsigned int  button_state    = 0;
unsigned int  button_block    = 2000;

unsigned int  park_mode       = 0;
unsigned long park_dot_ms     = 0;
unsigned int  park_dot_state  = 0;

unsigned int  drive_glitch    = 0;
unsigned long drive_dot_ms    = 0;
unsigned int  drive_dot_state = 0;

#define BRIGHTNESS  64
CRGB leds[kledNum];

////////////////////////////
// 7-SEGMENT DISPLAY CODE //
////////////////////////////

TM1637Display display(kDisplayCLKpin, kdisplayDIOpin);

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
//  Serial.print("Extracting: ");
//  Serial.print(n);
//  Serial.println();
  while ( digit -- > 0 )
    mask *= 10;
//  Serial.print("Masking with mask: ");
//  Serial.print(mask);
//  Serial.println();
  if (n < mask / 10) { // insufficient digits
//    Serial.print("Return 0");
//    Serial.println();
    return display.encodeDigit(0);
  }
  while ( n >= mask )
    n /= mask;
//  Serial.print("Extracted: ");
//  Serial.print(n % 10);
//  Serial.println();
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
  if (park_dot_state == 0 && (current_ms - park_dot_ms) > 2000) {
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
  }
}

////////////////
// LED STRIPE //
////////////////


///////////////
// MAIN LOOP //
///////////////

void setup() {
  Serial.begin(9600);
  Serial.print("Start");
  Serial.println();

  delay(3000);

  // Initialize LED strip  
  FastLED.addLeds<WS2812B, kledDINpin, GRB>(leds, kledNum).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );

  // Initialize 7 segment display
  display.setBrightness(7, true);

  pinMode(kbuttonDINpin, INPUT);
}

void loop() {
  current_ms = millis();
  
  button_state = digitalRead(kbuttonDINpin);
  if (button_state == HIGH && park_mode == 0 && (current_ms - button_ms) > button_block) {
    // enter parking mode
    displayShiftPark();
    park_mode = 1;
    button_ms = current_ms;
  } else if (button_state == HIGH && park_mode == 1 && (current_ms - button_ms) > button_block) {
    displayShiftDrive();
    park_mode = 0;
    button_ms = current_ms;
  }

  if (park_mode == 1) {
    displayPark();
    for(int i = 0; i < kledNum; i++) {
      leds[i] = CRGB::Blue;    // set our current dot to red
      FastLED.show();
      leds[i] = CRGB::Black;  // set our current dot to black before we continue
    }
  } else {
    displayDrive();
    for(int i = 0; i < kledNum; i++) {
      leds[i] = CRGB::Red;    // set our current dot to red
      FastLED.show();
      leds[i] = CRGB::Black;  // set our current dot to black before we continue
    }
  }

  
  //delay(1000);
  

  
  
  Serial.print("This loop took ");
  Serial.print((millis() - current_ms));
  Serial.println();  
}
