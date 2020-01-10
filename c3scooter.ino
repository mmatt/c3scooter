#include <FastLED.h>
#include <MPU6050.h>
#include <TM1637Display.h>

// Constants
const int     kVersion              = 5;
const int     kDisplayClkPin        = 8;
const int     kDisplayDioPin        = 7;
const int     kGyroMpuAddr          = 0x68;
const int     kLedDinPin            = 9;
const int     kButtonDinPin         = 10;
const int     kLedNum               = 80;
const int     kLedBrightness        = 196;

const uint8_t kwordBye_[]           = { 0b01111111, 0b01101110, 0b01111001, 0x00 };
const uint8_t kword_bye[]           = { 0x00, 0b01111111, 0b01101110, 0b01111001 };
const uint8_t kwordHey_[]           = { 0b01110110, 0b01111001, 0b01101110, 0x00 };
const uint8_t kword_hey[]           = { 0x00, 0b01110110, 0b01111001, 0b01101110 };
const uint8_t kwordSos_[]           = { 0b01101101, 0b00111111, 0b01101101, 0x00 };
const uint8_t kword_sos[]           = { 0x00, 0b01101101, 0b00111111, 0b01101101 };
const uint8_t kwordXxc3[]           = { 0b01001111, 0b00000111, 0b00111001, 0b01001111 };
const uint8_t kwordDots[]           = { 0x00, 0b10000000, 0x00, 0x00 };
const uint8_t kwordBlnk[]           = { 0x00, 0x00, 0x00, 0x00 };

// Variables
unsigned int  i, j;
unsigned long current_ms            = 0;

unsigned long button_ms             = 0;
unsigned int  button_state          = 0;
unsigned int  button_block          = 1000;

unsigned int  park_mode             = 0;
unsigned long park_dot_ms           = 0;
unsigned int  park_dot_state        = 0;
unsigned long park_flash_ms         = 0;
unsigned int  park_flash_state      = 0;
unsigned long park_gyro_ms          = 0;
unsigned int  park_gyro_alert       = 0;
unsigned long park_sos_ms           = 0;
unsigned int  park_sos_state        = 0;
unsigned int  park_twinkle_chance   = 25;

unsigned long glitch_ms             = 0;
unsigned int  glitch_random         = 0;
uint8_t       glitched_word[]       = { 0x00, 0x00, 0x00, 0x00 };
uint8_t       random_byte[]         = { 0x00, 0x00, 0x00, 0x00 };

unsigned long drive_dot_ms          = 0;
unsigned int  drive_dot_state       = 0;
unsigned long drive_sparkle_ms      = 0;
unsigned long drive_effect_ms       = 0;
unsigned int  drive_effect_num      = 1;
unsigned int  drive_effect_hold_ms  = 20000;

int16_t       gyro_x, gyro_y, gyro_z;
int16_t       gyro_x_prev, gyro_y_prev, gyro_z_prev;
int16_t       gyro_total;

uint8_t       twinkle_state[kLedNum];
enum          { isDark, getBrighter, getDimmer };

#define twinkle_peak  CRGB(32,0,32)
#define twinkle_base  CRGB(0,0,0)
#define twinkle_up    CRGB(random8(5),random8(2),random8(5))
#define twinkle_down  CRGB(1,1,1)
// Use qsuba for smooth pixel colouring and qsubd for non-smooth pixel colouring
#define qsubd(x, b)   ((x>b)?b:0)    // Digital unsigned subtraction macro. if result <0, then => 0. Otherwise, take on fixed value.
#define qsuba(x, b)   ((x>b)?x-b:0)  // Analog Unsigned subtraction macro. if result <0, then => 0

CRGB leds[kLedNum];
CRGBPalette16 currentPalette = LavaColors_p;
CRGBPalette16 targetPalette = LavaColors_p;

TBlendType    currentBlending = LINEARBLEND;

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
  for (int i=0; i<4; i++) {
      if (random(10) > 6) {
        if (random(10) > 6) {
          random_byte[i] = getRandomByte();
        }
        glitched_word[i] = kwordXxc3[i] & random_byte[i];
      } else {
        glitched_word[i] = kwordXxc3[i];
      }
    }
    display.setSegments(glitched_word);
    //Serial.print((current_ms - glitch_ms)); Serial.print(" vs "); Serial.print(glitch_random + ((glitch_random/100) * 4));
    //Serial.println();
    if ((current_ms - glitch_ms) > glitch_random + ((glitch_random/100) * 4)) {
      glitch_ms = current_ms;
      glitch_random = 0;
      display.setSegments(kwordXxc3);
    }
}

uint8_t getRandomByte() {
  static uint32_t buf = 0;
  static uint8_t idx = 0;
  if (idx > 2)
  {
    buf = random();  // refill 31 bits
    idx = 0;
  }
  uint8_t t = buf & 0xFF;
  buf >>= 8;
  idx++;
  return t;
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

    //Serial.print("Gyro total "); Serial.print(gyro_total);
    //Serial.println();

    // Increase alertness on thershold (higher first step)
    if ((gyro_total > 300 && park_gyro_alert == 0) || (gyro_total > 200 && park_gyro_alert > 0)) {
      // Reset to black on start
      if (park_gyro_alert == 0) {
        fill_solid( leds, kLedNum, CRGB::Black );
      }
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

    //Serial.print("Now at: "); Serial.print(park_gyro_alert);
    //Serial.println();

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
      for (j = 0; j < 30; j++) {
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

void setAll(byte red, byte green, byte blue) {
  for(int i = 0; i < kLedNum; i++ ) {
    setPixel(i, red, green, blue); 
  }
  FastLED.show();
}

void setPixel(int Pixel, byte red, byte green, byte blue) {
  leds[Pixel].r = red;
  leds[Pixel].g = green;
  leds[Pixel].b = blue;
}

void twinkleStars() {
  if (park_gyro_alert == 0) {
    for (j = 0; j < kLedNum; j++) {
      if (twinkle_state[j] == isDark) {
        if( random16() < park_twinkle_chance) {
          twinkle_state[j] = getBrighter;
        }
      } else if( twinkle_state[j] == getBrighter ) {
        // this pixels is currently: GettingBrighter
        // so if it's at peak color, switch it to getting dimmer again
        if( leds[j] >= CRGB(random8(),random8(),random8()) ) {
          twinkle_state[j] = getDimmer;
        } else {
          leds[j] += twinkle_up;
        }
      } else {
        if( leds[j] <= twinkle_base ) {
          leds[j] = twinkle_base; // reset to exact base color, in case we overshot
          twinkle_state[j] = isDark;
        } else {
          // otherwise, just keep dimming it down:
          leds[j] -= twinkle_down;
        }
      }
    }
    FastLED.show();
    delay(20);
  }
}

void snowSparkle(int sparkleDelay, int sparkleChance) {
  if ((current_ms - drive_sparkle_ms) > random(100,1000)) {
    if( random16() < sparkleChance) {
      setAll(0x10, 0x10, 0x10);
      int Pixel = random(kLedNum);
      setPixel(Pixel,0xff,0x00,0x64);
      FastLED.show();
      delay(sparkleDelay);
      setPixel(Pixel,0x10, 0x10, 0x10);
      FastLED.show();
      drive_sparkle_ms = current_ms;
    }
  }
}

void rainbowCycle(int speedDelay) {
  byte *c;
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< kLedNum; i++) {
      c=Wheel(((i * 256 / kLedNum) + j) & 255);
      setPixel(i, *c, *(c+1), *(c+2));
    }
    FastLED.show();
    delay(speedDelay);
    if (digitalRead(kButtonDinPin) == HIGH) {
      break;
    }
  }
}

byte * Wheel(byte WheelPos) {
  static byte c[3];
  if(WheelPos < 85) {
   c[0]=WheelPos * 3;
   c[1]=255 - WheelPos * 3;
   c[2]=0;
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   c[0]=255 - WheelPos * 3;
   c[1]=0;
   c[2]=WheelPos * 3;
  } else {
   WheelPos -= 170;
   c[0]=0;
   c[1]=WheelPos * 3;
   c[2]=255 - WheelPos * 3;
  }
  return c;
}

void meteorRain(byte red, byte green, byte blue, byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay) {  
  setAll(0,0,0);
  for(int i = 0; i < kLedNum+kLedNum; i++) {
    // fade brightness all LEDs one step
    for(int j=0; j<kLedNum; j++) {
      if( (!meteorRandomDecay) || (random(10)>5) ) {
        leds[j].fadeToBlackBy( meteorTrailDecay );
      }
    }
    // draw meteor
    for(int j = 0; j < meteorSize; j++) {
      if( ( i-j <kLedNum) && (i-j>=0) ) {
        setPixel(i-j, red, green, blue);
      } 
    }
    FastLED.show();
    delay(SpeedDelay);
    // Exit loop when button is pressed
    if (digitalRead(kButtonDinPin) == HIGH) {
      break;
    }
  }
}

void rainbow_beat() {
  uint8_t beatA = beatsin8(17, 0, 255);                        // Starting hue
  uint8_t beatB = beatsin8(13, 0, 255);
  fill_rainbow(leds, kLedNum, (beatA+beatB)/2, 8);            // Use FastLED's fill_rainbow routine.
  FastLED.show();
} 

void rainbow_march(uint8_t thisdelay, uint8_t deltahue) {
  uint8_t thishue = millis()*(255-thisdelay)/255;
  fill_rainbow(leds, kLedNum, thishue, deltahue);
  FastLED.show();
}

void matrixRayLoop() {
    EVERY_N_MILLIS_I(thisTimer,100) {
      uint8_t timeval = beatsin8(10,20,50);
      thisTimer.setPeriod(timeval);
      matrixRay(millis()>>4);
    }
    EVERY_N_MILLISECONDS(100) {
      uint8_t maxChanges = 24; 
      nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);
    }
    EVERY_N_SECONDS(5) {
      static uint8_t baseC = random8();
      targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 192, random8(128,255)), CHSV(random8(), 255, random8(128,255)));
    }
    
    FastLED.show();
}

void matrixRay(uint8_t colorIndex) {
  static uint8_t thisdir = 0;
  static int thisphase = 0;
  uint8_t thiscutoff;

  thisphase += beatsin8(1,20, 50);
  thiscutoff = beatsin8(50,164,248);
  
  int thisbright = qsubd(cubicwave8(thisphase), thiscutoff);
 
  if (thisdir == 0) {
    leds[0] = ColorFromPalette(currentPalette, colorIndex, thisbright, currentBlending);
    memmove(leds+1, leds, (kLedNum-1)*3);
  } else {
    leds[kLedNum-1] = ColorFromPalette( currentPalette, colorIndex, thisbright, currentBlending);
    memmove(leds, leds+1, (kLedNum-1)*3);    
  }
}

void beatWaveLoop() {
  beatWave();
  EVERY_N_MILLISECONDS(100) {
    uint8_t maxChanges = 24; 
    nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);
  }
  EVERY_N_SECONDS(5) {
    targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 192, random8(128,255)), CHSV(random8(), 255, random8(128,255)));
  }
  FastLED.show();
}

void beatWave() {
  uint8_t wave1 = beatsin8(9, 0, 255);
  uint8_t wave2 = beatsin8(8, 0, 255);
  uint8_t wave3 = beatsin8(7, 0, 255);
  uint8_t wave4 = beatsin8(6, 0, 255);

  for (int i=0; i<kLedNum; i++) {
    leds[i] = ColorFromPalette( currentPalette, i+wave1+wave2+wave3+wave4, 255, currentBlending); 
  }
}

void blendWave() {
  CRGB clr1;
  CRGB clr2;
  uint8_t speed;
  uint8_t loc1;
  
  speed = beatsin8(6,0,255);

  clr1 = blend(CHSV(beatsin8(3,0,255),255,255), CHSV(beatsin8(4,0,255),255,255), speed);
  clr2 = blend(CHSV(beatsin8(4,0,255),255,255), CHSV(beatsin8(3,0,255),255,255), speed);

  loc1 = beatsin8(10,0,kLedNum-1);
  
  fill_gradient_RGB(leds, 0, clr2, loc1, clr1);
  fill_gradient_RGB(leds, loc1, clr2, kLedNum-1, clr1);

  FastLED.show();
}

void dotBeat() {
  uint8_t bpm = 30;

  uint8_t inner  = beatsin8(bpm, kLedNum/4, kLedNum/4*3);  // Move 1/4 to 3/4
  uint8_t outer  = beatsin8(bpm, 0, kLedNum-1);            // Move entire length
  uint8_t middle = beatsin8(bpm, kLedNum/3, kLedNum/3*2);  // Move 1/3 to 2/3

  leds[middle] = CRGB::Purple;
  leds[inner]  = CRGB::Blue;
  leds[outer]  = CRGB::Aqua;

  nscale8(leds, kLedNum, 224); // Trail behind the LED's. Lower => faster fade.
  FastLED.show();
}

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
    fill_solid(leds, kLedNum, CRGB::Black);
    FastLED.show(); 
    displayShiftPark();
    park_mode = 1;
    button_ms = current_ms;
  } else if (button_state == HIGH && park_mode == 1 && (current_ms - button_ms) > button_block) {
    // enter drive mode
    displayShiftDrive();
    park_mode = 0;
    button_ms = current_ms;
  }

  if (glitch_random == 0) {
    glitch_random = random(4000, 20000);
  }

  // Act according to mode
  if (park_mode == 1) {
    // We are parked
    if ((current_ms - glitch_ms) > glitch_random) {
      glitchC3();
    } else {
      displayPark();
    }
    readGyro();
    twinkleStars();
  } else {
    // We're in drive
    if ((current_ms - glitch_ms) > glitch_random) {
      glitchC3();
    } else {
      displayDrive();
    }
    if ((current_ms - drive_effect_ms) > drive_effect_hold_ms) {
      // change to next effect
      drive_effect_num++;
      if (drive_effect_num > 9) {
        drive_effect_num = 1;
      }
      drive_effect_ms = current_ms;
    }
    //drive_effect_num = 1;
    switch (drive_effect_num) {
      case 1:
        snowSparkle(20, 40);
        break;
      case 2:
        rainbowCycle(0);
        break;
      case 3:
        meteorRain(0xff,0xff,0xff,10, 64, true, 30);
        break;
      case 4:
        rainbow_beat();
        break;
      case 5:
        rainbow_march(200, 10);
        break;
      case 6:
        matrixRayLoop();
        break;
      case 7:
        beatWaveLoop();
        break;
      case 8:
        dotBeat();
        break;
      case 9:
        blendWave();
        break;
    }
  }
}
