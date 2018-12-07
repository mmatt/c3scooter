#include <TM1637Display.h>
#include <FastLED.h>

// Constants
const int kDisplayCLKpin  = 3;
const int kdisplayDIOpin  = 2;
const int kledDINpin      = 6;
const int kledNum         = 89;

// Variables
unsigned int i;

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

#define SIG_B   0b01111111
#define SIG_Y   0b01101110
#define SIG_E   0b01111001
#define BLANK   0x00
#define DOTS    0b10000000

#define LIN_U   0b00000001
#define LIN_M   0b01000000
#define LIN_D   0b00001000

const uint8_t strBYE_[] = { SIG_B, SIG_Y, SIG_E, BLANK };
const uint8_t str_BYE[] = { BLANK, SIG_B, SIG_Y, SIG_E };
const uint8_t strLINU[] = { LIN_U, LIN_U, LIN_U, LIN_U };
const uint8_t strLINM[] = { LIN_M, LIN_M, LIN_M, LIN_M };
const uint8_t strLIND[] = { LIN_D, LIN_D, LIN_D, LIN_D };
const uint8_t strDOTS[] = { BLANK, DOTS,  BLANK, BLANK };
const uint8_t strBLCK[] = { BLANK, BLANK, BLANK, BLANK };

void brightDown(int data) {
  for (i = 7; i > 0; i--) {
    display.setBrightness(i);
    display.setSegments(data);
    delay(250);
  }
  display.setBrightness(7, false);
  display.setSegments(data);
}

void brightUp() {
  for (i = 0; i < 7; i++) {
    display.setBrightness(i);
    delay(100);
  }
}

int extractDigit(size_t digit, int n) {
  int mask = 1;
  Serial.print("Extracting: ");
  Serial.print(n);
  Serial.println();
  while ( digit -- > 0 )
    mask *= 10;
  Serial.print("Masking with mask: ");
  Serial.print(mask);
  Serial.println();
  if (n < mask / 10) { // insufficient digits
    Serial.print("Return 0");
    Serial.println();
    return display.encodeDigit(0);
  }
  while ( n >= mask )
    n /= mask;
  Serial.print("Extracted: ");
  Serial.print(n % 10);
  Serial.println();
  return display.encodeDigit(n % 10);
}

void printTimer() {
  unsigned int up, hrs, mins;
  uint8_t timer[] = { 0xff, 0xff, 0xff, 0xff };

  // Read milliseconds and add half a second  to round minutes
  up = (millis() / 1000) + 30;
  mins = up / 60;
  hrs  = mins / 60;
  mins = mins % 60;

  Serial.print("Uptime: ");
  Serial.print(hrs);
  Serial.print(":");
  Serial.print(mins);
  Serial.println();
  
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

  // Blinking dots
  for (i = 0; i < 2; i++) {
    timer[1] = timer[1] + 128;
    display.setSegments(timer);
    delay(1000);
    timer[1] = timer[1] - 128;
    display.setSegments(timer);
    delay(1000);
  }
  display.clear();
}

void glitchC3() {

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

  delay( 3000 ); // power-up safety delay
  FastLED.addLeds<WS2812B, kledDINpin, GRB>(leds, kledNum).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );
}

void loop() {
  
  display.setBrightness(7);
  for (i = 0; i < 5; i++) {
    display.setSegments(strLINU);
    delay(250);
    display.setSegments(strLINM);
    delay(250);
    display.setSegments(strLIND);
    delay(250);
  }
  display.setSegments(strBYE_);
  delay(450);
  display.setSegments(str_BYE);
  delay(200);
  brightDown(str_BYE);
  delay(100);
  display.setBrightness(7, true);
  for (i = 0; i < 2; i++) {
    display.setSegments(strDOTS);
    delay(100);
    display.setSegments(strBLCK);
    delay(2000);
  }

  printTimer();
  delay(1000);
  

  for(int i = 0; i < kledNum; i++) {
    leds[i] = CRGB::Blue;    // set our current dot to red
    FastLED.show();
    leds[i] = CRGB::Black;  // set our current dot to black before we continue
  }
}
