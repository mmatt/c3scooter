#include <FastLED.h>
#include <MPU6050.h>
#include <TM1637Display.h>

// Constants
const int     kVersion              = 2;
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
const uint8_t kwordXxc3[]           = { 0b01001111, 0b01101101, 0b00111001, 0b01001111 };
const uint8_t kwordXyc3[]           = { 0b01111001, 0b01011010, 0b00001111, 0b01111001 };
const uint8_t kwordDots[]           = { 0x00, 0b10000000, 0x00, 0x00 };
const uint8_t kwordBlnk[]           = { 0x00, 0x00, 0x00, 0x00 };

// Variables
unsigned int  i, j;
unsigned long current_ms            = 0;

unsigned long button_ms             = 0;
unsigned int  button_state          = 0;
unsigned int  button_block          = 2000;

unsigned int  park_mode             = 1;
unsigned long park_dot_ms           = 0;
unsigned int  park_dot_state        = 0;
unsigned long park_flash_ms         = 0;
unsigned int  park_flash_state      = 0;
unsigned long park_gyro_ms          = 0;
unsigned int  park_gyro_alert       = 0;
unsigned long park_sos_ms           = 0;
unsigned int  park_sos_state        = 0;
unsigned int  park_twinkle_chance   = 25;

unsigned int  drive_glitch          = 0;
unsigned long drive_dot_ms          = 0;
unsigned int  drive_dot_state       = 0;
unsigned long drive_sparkle_ms      = 0;
unsigned long drive_effect_ms       = 0;
unsigned int  drive_effect_num      = 1;
unsigned int  drive_effect_hold_ms  = 10000;

int16_t       gyro_x, gyro_y, gyro_z;
int16_t       gyro_x_prev, gyro_y_prev, gyro_z_prev;
int16_t       gyro_total;

#define twinkle_peak       CRGB(32,0,32)
#define twinkle_base       CRGB(0,0,0)
#define twinkle_up   CRGB(random8(5),random8(2),random8(5))
#define twinkle_down CRGB(1,1,1)

uint8_t       twinkle_state[kLedNum];
enum          { isDark, getBrighter, getDimmer };

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
      setPixel(Pixel,0xff,0xff,0xff);
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
  }
}

void CylonBounce(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay){

  for(int i = 0; i < kLedNum-EyeSize-2; i++) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    FastLED.show();
    delay(SpeedDelay);
  }

  delay(ReturnDelay);

  for(int i = kLedNum-EyeSize-2; i > 0; i--) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+EyeSize+1, red/10, green/10, blue/10);
    FastLED.show();
    delay(SpeedDelay);
  }
  
  delay(ReturnDelay);
}

void rainbow_beat() {
  
  uint8_t beatA = beatsin8(17, 0, 255);                        // Starting hue
  uint8_t beatB = beatsin8(13, 0, 255);
  fill_rainbow(leds, kLedNum, (beatA+beatB)/2, 8);            // Use FastLED's fill_rainbow routine.
  FastLED.show();
} 

void rainbow_march(uint8_t thisdelay, uint8_t deltahue) {     // The fill_rainbow call doesn't support brightness levels.

  uint8_t thishue = millis()*(255-thisdelay)/255;             // To change the rate, add a beat or something to the result. 'thisdelay' must be a fixed value.
  
// thishue = beat8(50);                                       // This uses a FastLED sawtooth generator. Again, the '50' should not change on the fly.
// thishue = beatsin8(50,0,255);                              // This can change speeds on the fly. You can also add these to each other.
  
  fill_rainbow(leds, kLedNum, thishue, deltahue);            // Use FastLED's fill_rainbow routine.
  FastLED.show();

} // rainbow_march()


// Use qsuba for smooth pixel colouring and qsubd for non-smooth pixel colouring
#define qsubd(x, b)  ((x>b)?b:0)                              // Digital unsigned subtraction macro. if result <0, then => 0. Otherwise, take on fixed value.
#define qsuba(x, b)  ((x>b)?x-b:0)                            // Analog Unsigned subtraction macro. if result <0, then => 0

CRGBPalette16 currentPalette = LavaColors_p;
CRGBPalette16 targetPalette = LavaColors_p;
TBlendType    currentBlending = LINEARBLEND;

void matrix_ray_loop() {
                                                                // This section changes the delay, which adjusts how fast the 'rays' are travelling down the length of the strand.
    EVERY_N_MILLIS_I(thisTimer,100) {                           // This only sets the Initial timer delay. To change this value, you need to use thisTimer.setPeriod(); You could also call it thatTimer and so on.
      uint8_t timeval = beatsin8(10,20,50);                     // Create/modify a variable based on the beastsin8() function.
      thisTimer.setPeriod(timeval);                             // Use that as our update timer value.
  
      matrix_ray(millis()>>4);                                  // This is the main function that's called. We could have not passed the millis()>>4, but it's a quick example of passing an argument.
    }
  
    EVERY_N_MILLISECONDS(100) {                                 // Fixed rate of a palette blending capability.
      uint8_t maxChanges = 24; 
      nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);
    }
  
    EVERY_N_SECONDS(5) {                                        // Change the target palette to a random one every 5 seconds.
      static uint8_t baseC = random8();                         // You can use this as a baseline colour if you want similar hues in the next line.
      targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 192, random8(128,255)), CHSV(random8(), 255, random8(128,255)));
    }
    
    FastLED.show();
  
    Serial.println(LEDS.getFPS());
  
}

void matrix_ray(uint8_t colorIndex) {                                                 // Send a PWM'd sinewave instead of a random happening of LED's down the strand.

  static uint8_t thisdir = 0;                                                         // We could change the direction if we want to. Static means to assign that value only once.
  static int thisphase = 0;                                                           // Phase change value gets calculated. Static means to assign that value only once.
  uint8_t thiscutoff;                                                                 // You can change the cutoff value to display this wave. Lower value = longer wave.

  thisphase += beatsin8(1,20, 50);                                                    // You can change direction and speed individually.
  thiscutoff = beatsin8(50,164,248);                                                  // This variable is used for the PWM of the lighting with the qsubd command below.
  
  int thisbright = qsubd(cubicwave8(thisphase), thiscutoff);                          // It's PWM time. qsubd sets a minimum value called thiscutoff. If < thiscutoff, then thisbright = 0. Otherwise, thisbright = thiscutoff.
 
  if (thisdir == 0) {                                                                 // Depending on the direction, we'll put that brightness in one end of the array. Andrew Tuline wrote this.
    leds[0] = ColorFromPalette(currentPalette, colorIndex, thisbright, currentBlending); 
    memmove(leds+1, leds, (kLedNum-1)*3);                                            // Oh look, the FastLED method of copying LED values up/down the strand.
  } else {
    leds[kLedNum-1] = ColorFromPalette( currentPalette, colorIndex, thisbright, currentBlending);
    memmove(leds, leds+1, (kLedNum-1)*3);    
  }

} // matrix_ray()

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
    twinkleStars();
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
    if ((current_ms - drive_effect_ms) > drive_effect_hold_ms) {
      // change to next effect
      drive_effect_num++;
      if (drive_effect_num > 7) {
        drive_effect_num = 1;
      }
      drive_effect_ms = current_ms;
    }
    switch (drive_effect_num) {
      case 1:
        snowSparkle(20, 20);
        break;
      case 2:
        rainbowCycle(0);
        break;
      case 3:
        meteorRain(0xff,0xff,0xff,10, 64, true, 30);
        break;
      case 4:
        CylonBounce(0xff, 0, 0, 15, 10, 50);
        break;
      case 5:
        rainbow_beat();
        break;
      case 6:
        rainbow_march(200, 10);
        break;
      case 7:
        matrix_ray_loop();
        break;
    }
    //snowSparkle(20, 20);
    //rainbowCycle(0);
    //meteorRain(0xff,0xff,0xff,10, 64, true, 30);
    //CylonBounce(0xff, 0, 0, 15, 10, 50);
    //rainbow_beat();
    //rainbow_march(200, 10);
    //matrix_ray_loop();
    /*
    for(i = 0; i < kLedNum; i++) {
      leds[i] = CRGB::Red;    // set our current dot to red
      FastLED.show();
      leds[i] = CRGB::Black;  // set our current dot to black before we continue
    }*/
  }

  
  //delay(100);
  
  /*
  Serial.print("This loop took ");
  Serial.print((millis() - current_ms));
  Serial.println();
  */
  
}
