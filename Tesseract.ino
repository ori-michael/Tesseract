#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <FastLED.h>

#define NUM_LEDS 384
#define LED_DATA_PIN 2 // לדוגמה: D4 = GPIO2
CRGB leds[NUM_LEDS];

// OLED דרך I2C תוכנתי (SW I2C)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0, /* clock=*/ 14, /* data=*/ 12, /* reset=*/ U8X8_PIN_NONE
);

struct FaceMapping {
  int offset;
  bool flipX;
  bool flipY;
  bool swapXY;
};

enum CubeFace {
  TOP = 0,
  RIGHT,
  FRONT,/*good*/
  LEFT,/*good*/
  BACK,
  BOTTOM, /*good*/
  NUM_OF_FACES
};

FaceMapping faceMapping[NUM_OF_FACES];

void initFaceMapping()
{
  faceMapping[FRONT].swapXY=false;
  faceMapping[FRONT].flipX=true;
  faceMapping[FRONT].flipY =false;

  faceMapping[LEFT].swapXY=false;
  faceMapping[LEFT].flipX=false;
  faceMapping[LEFT].flipY =true;

  faceMapping[RIGHT].swapXY=true;
  faceMapping[RIGHT].flipX=true;
  faceMapping[RIGHT].flipY =false;

  faceMapping[BOTTOM].swapXY=false;
  faceMapping[BOTTOM].flipX=false;
  faceMapping[BOTTOM].flipY =false;
}

void swap(int& a, int& b) {
  int temp = a;
  a = b;
  b = temp;
}



const int FACE_LED_OFFSET[] = {0, 64, 128, 192, 256, 320};

int ledFromFaceXY(int face, int x, int y) {
  if (faceMapping[face].swapXY) swap(x, y);
  if (faceMapping[face].flipX) x = 7 - x;
  if (faceMapping[face].flipY) y = 7 - y;
  
  if(y%2)
  {
    x=7-x;
  }
  return FACE_LED_OFFSET[face] + (y * 8 + x);  // אם הלדים הולכים שורה אחרי שורה
}

int ledFromXYZ(int x, int y, int z) {
  if (z == 0)  return ledFromFaceXY(FRONT,  x, y);       // FRONT
  if (z == 7)  return ledFromFaceXY(BACK,   x, y);       // BACK (הפוך ב-X)
  if (x == 0)  return ledFromFaceXY(LEFT,   y, z);       // LEFT
  if (x == 7)  return ledFromFaceXY(RIGHT,  y, z);       // RIGHT
  if (y == 7)  return ledFromFaceXY(TOP,    z, x);       // TOP
  if (y == 0)  return ledFromFaceXY(BOTTOM, z, x);       // BOTTOM

  return -1;  // לא על הפאה – לא חוקי
}
void lightIndex(int index)
{
  leds[index] = CRGB::White;
}


void lightLed(int x,int y,int z)
{
  if(z==0){lightIndex(ledFromFaceXY(BOTTOM, x, y));}//BOTTOM
  if(z==7){}//TOP
  if(x==0){lightIndex(ledFromFaceXY(LEFT, y, z));}//LEFT
  if(x==7){lightIndex(ledFromFaceXY(RIGHT, y, z));}//RIGHT
  if(y==0){lightIndex(ledFromFaceXY(FRONT, z, x));}//FRONT
  if(y==7){}//BACK
  
}

void testXYZMapping() {
  for (int face = 0; face < 6; face++) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        int xx = 0, yy = 0, zz = 0;
        String faceName;

        switch (face) {
          case 0: xx = x; yy = y; zz = 0; faceName = "FRONT"; break;
          case 1: xx = x; yy = y; zz = 7; faceName = "BACK"; break;
          case 2: xx = 0; yy = y; zz = x; faceName = "LEFT"; break;
          case 3: xx = 7; yy = y; zz = x; faceName = "RIGHT"; break;
          case 4: xx = x; yy = 7; zz = 7 - y; faceName = "TOP"; break;
          case 5: xx = x; yy = 0; zz = y; faceName = "BOTTOM"; break;
        }

        // int index = ledFromXYZ(xx, yy, zz);
        xx=0;
        yy=5;
        zz=1;
        lightLed(7,5,1);
        lightLed(7,6,1);
        lightLed(7,7,1);
        // int index = ledFromXYZ(0, 0, 0);

        // הצגת מידע במסך
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_7x14B_tr);
        u8g2.setCursor(0, 20);
        u8g2.print("Face: "); u8g2.print(faceName);
        u8g2.setCursor(0, 40);
        u8g2.print("XYZ: ");
        u8g2.print(xx); u8g2.print(",");
        u8g2.print(yy); u8g2.print(",");
        u8g2.print(zz);
        u8g2.setCursor(0, 60);
        // u8g2.print("Led num:"); u8g2.print(index);

        u8g2.sendBuffer();

        // הדלקת לד
        // FastLED.clear();
        // if (index >= 0 && index < NUM_LEDS) {
        //   leds[index] = CRGB::White;
        // }
        FastLED.show();
        delay(150000);
      }
    }
  }
}


void setup() {
  initFaceMapping();

  // הפעלת המסך
  u8g2.begin();

  // הפעלת הלדים
  FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(30);  // ערך בין 0 ל־255
  FastLED.clear();
  FastLED.show();
}

void loop() {
  // תצוגת טקסט במסך
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0, 20, "VERY Hello from cube!");
  u8g2.sendBuffer();

  // אפקט לד פשוט
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(100);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(100);

  testXYZMapping(); 
}
