// Forward struct definition placed before auto-generated prototypes
struct Point3D { int x; int y; int z; };

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <FastLED.h>
#include <MPU6050.h>

/**********  LIVING CREATURE SYSTEM  **********/

enum CubeMood {
  MOOD_CALM,
  MOOD_HAPPY,
  MOOD_SLEEPY
};

struct CreatureState {
  CubeMood mood;
  int energy;
  int boredom;
  unsigned long lastInteraction;
};

CreatureState creature;

unsigned long lastCreatureUpdateMs = 0;

// מצב אישונים (עיניים זזות: אישון = בלוק 2x2 בתוך אזור 3x3 של כל עין)
int leftPupilX = 6, leftPupilY = 6;   // פינת עליונה-שמאלית של בלוק האישון בעין שמאל (x=5..6,y=5..6)
int rightPupilX = 1, rightPupilY = 6; // פינת עליונה-שמאלית בעין ימין (x=0..1,y=5..6)
unsigned long lastPupilMoveMs = 0;

void updatePupils(unsigned long now) {
  int interval;
  switch (creature.mood) {
    case MOOD_HAPPY: interval = 700; break;  // זז מהר יותר כששמחה
    case MOOD_CALM:  interval = 1500; break; // רגוע - זז לאט
    case MOOD_SLEEPY: interval = 3000; break; // כמעט לא זז
    default: interval = 1500; break;
  }

  if (now - lastPupilMoveMs < (unsigned long)interval) return;
  lastPupilMoveMs = now;

  if (creature.mood == MOOD_SLEEPY) {
    // בשינה קבוע במרכז התחתון של העין (מרגיש "סגור")
    leftPupilX = 6; leftPupilY = 6;
    rightPupilX = 1; rightPupilY = 6;
    return;
  }

  // ארבעה כיוונים אפשריים משותפים לשתי העיניים (למעלה-שמאל, למעלה-ימין, למטה-שמאל, למטה-ימין)
  uint8_t choice = random8(4);
  switch (choice) {
    case 0: // למעלה-שמאל
      leftPupilX = 5; leftPupilY = 5;
      rightPupilX = 0; rightPupilY = 5;
      break;
    case 1: // למעלה-ימין
      leftPupilX = 6; leftPupilY = 5;
      rightPupilX = 1; rightPupilY = 5;
      break;
    case 2: // למטה-שמאל
      leftPupilX = 5; leftPupilY = 6;
      rightPupilX = 0; rightPupilY = 6;
      break;
    default: // למטה-ימין
      leftPupilX = 6; leftPupilY = 6;
      rightPupilX = 1; rightPupilY = 6;
      break;
  }
}

int16_t c_ax, c_ay, c_az;

#define NUM_LEDS 384
#define LED_DATA_PIN 2 // לדוגמה: D4 = GPIO2
CRGB leds[NUM_LEDS];

// OLED דרך I2C תוכנתי (SW I2C)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0, /* clock=*/ 14, /* data=*/ 12, /* reset=*/ U8X8_PIN_NONE
);

// MPU6050
MPU6050 mpu;
bool mpuConnected = false;

// כיול MPU6050
struct CalibrationData {
  int16_t ax, ay, az;  // תאוצה כשהפאה למעלה
  bool calibrated;
};
CalibrationData faceCalibration[6];  // כיול לכל 6 פאות
bool calibrationComplete = false;

// מבנה נקודה תלת-ממדית (הגדרה כבר קדימה בתחילת הקובץ כדי למנוע בעיית פרוטוטייפ)

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

// משתני המשחק Snake
#define MAX_SNAKE_LENGTH 384
Point3D snake[MAX_SNAKE_LENGTH];
int snakeLength = 1;
Point3D direction = {1, 0, 0};  // ימינה
unsigned long lastMoveTime = 0;
int moveDelay = 40;

// משתני אוכל - 5 פריטים
const int MAX_FOOD = 5;
Point3D food[MAX_FOOD];
bool foodExists[MAX_FOOD];
int stuckCounter = 0;  // מונה תקיעות

// מצב תצוגה ומחזוריות
enum GameMode { WATER, FIRE, SNOW, SNAKE, GAME_OVER, CLOCK };
GameMode currentMode = WATER;
unsigned long modeStartTime = 0;

// שעון
unsigned long startMillis = 0;  // זמן התחלה
int clockHours = 0;
int clockMinutes = 0;
int clockSeconds = 0;

// משתנים לאנימציית אש
byte fireData[8][8][8];  // מטריצת חום לכל נקודה
float fireIntensity = 0.0;  // עוצמת האש 0-1 (גדלה הדרגתית)

// משתנים לאנימציית שלג
byte snowHeight[8][8][6];  // גובה השלג על כל פאה (6 פאות, 8x8 פיקסלים)
struct Snowflake {
  float x, y, z;  // מיקום עשרוני לתנועה חלקה
  int targetFace;  // פאה שאליה פתית השלג צריכה להגיע
  bool active;
};
const int MAX_SNOWFLAKES = 30;
Snowflake snowflakes[MAX_SNOWFLAKES];

void initFaceMapping()
{
  // FRONT - פאה קדמית (Z=0)
  faceMapping[FRONT].swapXY = true;   // X ו-Y מתחלפים
  faceMapping[FRONT].flipX = true;    // הפוך X (בגלל serpentine)
  faceMapping[FRONT].flipY = false;   // הסרת ההיפוך

  // LEFT - פאה שמאלית (X=0)
  faceMapping[LEFT].swapXY = false;
  faceMapping[LEFT].flipX = false;    // הסרת ההיפוך של X
  faceMapping[LEFT].flipY = true;

  // RIGHT - פאה ימנית (X=7)
  faceMapping[RIGHT].swapXY = true;
  faceMapping[RIGHT].flipX = true;    // חזרה להיפוך X
  faceMapping[RIGHT].flipY = false;    // הפוך Y

  // BOTTOM - פאה תחתונה (Y=0)
  faceMapping[BOTTOM].swapXY = true;  // X ו-Z מתחלפים
  faceMapping[BOTTOM].flipX = false;
  faceMapping[BOTTOM].flipY = false;

  // TOP - פאה עליונה (Y=7)
  faceMapping[TOP].swapXY = false;
  faceMapping[TOP].flipX = false;
  faceMapping[TOP].flipY = false;

  // BACK - פאה אחורית (Z=7)
  faceMapping[BACK].swapXY = true;
  faceMapping[BACK].flipX = true;     // חזרה להיפוך X
  faceMapping[BACK].flipY = false;     // הפוך Y
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
  if (z == 7)  return ledFromFaceXY(BACK,   7-x, y);     // BACK - מראה של FRONT
  if (x == 0)  return ledFromFaceXY(LEFT,   z, y);       // LEFT
  if (x == 7)  return ledFromFaceXY(RIGHT,  z, y);       // RIGHT
  if (y == 7)  return ledFromFaceXY(TOP,    x, z);       // TOP
  if (y == 0)  return ledFromFaceXY(BOTTOM, x, z);       // BOTTOM

  return -1;  // לא על הפאה – לא חוקי
}
void setLedXYZ(int x, int y, int z, CRGB color) {
  int ledIndex = ledFromXYZ(x, y, z);
  if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
    leds[ledIndex] = color;
  }
}

const int FACE_Z = 0;

void clearCubeLeds(const CRGB &c) {
  fill_solid(leds, NUM_LEDS, c);
}

// דפוס פנים רגוע סימטרי 8x8 (שורה 0 = עליונה y=7, ביט 7 = x=7 שמאל)
uint8_t calmFacePattern[8] = {
  0b11100111, // y7: O O O . . O O O
  0b11100111, // y6: O O O . . O O O
  0b11100111, // y5: O O O . . O O O
  0b00000000, // y4: . . . . . . . .
  0b00000000, // y3: . . . . . . . .
  0b01111110, // y2: . O O O O O O .
  0b00111100, // y1: . . O O O O . .
  0b00000000  // y0: . . . . . . . .
};

// ===============================
// Render Minecraft Steve on FRONT
// ===============================
// Draw Steve's face on the FRONT 8x8 (x=0..7, y=0..7)
void drawSteveFaceOnFrontFace() {
  // High-contrast Steve palette for LED display (skin darker, more tanned)
  CRGB hairColor   = CRGB(60, 40, 20);    // dark brown hair & beard
  CRGB skinColor   = CRGB(165, 110, 80);  // very tanned, darker skin tone
  CRGB eyeWhite    = CRGB(255, 255, 255); // pure strong white
  CRGB eyePupil    = CRGB(40, 80, 200);   // bright blue pupil
  CRGB mouthColor  = CRGB(50, 30, 15);    // darker than hair to stand out
  CRGB emptyColor  = CRGB::Black;         // רקע מתחת לראש (y=0)

  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      CRGB c = emptyColor; // ברירת מחדל: רקע

      // אזור הראש (y>=1)
      if (y >= 1) {
        if (y >= 6) { // שתי שורות עליונות שיער
          c = hairColor;
        } else {
          c = skinColor; // שורות עור
        }
      }

      // עיניים (שורות y=4 ו-y=3, טווח x=2..5)
      if ((y == 4 || y == 3) && (x >= 2 && x <= 5)) {
        c = eyeWhite;
        // אישונים עליונים פנימיים (y=4, x=3 ו-x=4)
        if (y == 4 && (x == 3 || x == 4)) {
          c = eyePupil;
        }
      }

      // פה בשורה y=2, טווח x=2..5
      if (y == 2 && (x >= 2 && x <= 5)) {
        c = mouthColor;
      }

      setLedXYZ(x, y, FACE_Z, c);
    }
  }
}

// ======================================
// Minecraft Steve-like head on FRONT face
// ======================================
// 8x8 character map for the head (top row is index 0)
const char steveHead[8][9] = {
  "HHHHHHHH", // y7: top row
  "HHHHHHHH", // y6
  "HSSSSSSH", // y5
  "SSSSSSSS", // y4
  "SWBSSBWS", // y3
  "SSSNNSSS", // y2
  "SSHSSHSS", // y1
  "SSHHHHSS"  // y0: bottom row
};

// Render the Steve head onto the FRONT face (z = FACE_Z)
void drawSteveHeadOnFrontFace() {
  // High-contrast Steve palette for LED display (skin darker, more tanned)
  CRGB hairColor    = CRGB(60, 40, 20);    // H = hair
  CRGB skinColor    = CRGB(165, 110, 80);  // S = very tanned, darker
  CRGB eyeWhite     = CRGB(255, 255, 255); // W = white
  CRGB eyeBlue      = CRGB(40, 80, 200);   // B = blue pupil
  CRGB mouthColor   = CRGB(50, 30, 15);    // N = mouth
  CRGB defaultColor = CRGB::Black;         // fallback

  for (int y = 0; y < 8; y++) {
    int row = 7 - y; // steveHead[0] is top (y=7)
    for (int x = 0; x < 8; x++) {
      char cell = steveHead[row][x];
      CRGB c = defaultColor;
      switch (cell) {
        case 'H': c = hairColor;  break;
        case 'S': c = skinColor;  break;
        case 'W': c = eyeWhite;   break;
        case 'B': c = eyeBlue;    break;
        case 'N': c = mouthColor; break;
        default:  c = defaultColor; break;
      }
      setLedXYZ(x, y, FACE_Z, c);
    }
  }
}

// ======================================
// Steve SIDE and BACK face patterns
// ======================================
const char steveSideFace[8][9] = {
  "HHHHHHHH", // y7
  "HHHHHHHH", // y6
  "HHHHHHHH", // y5
  "HHHHHHHH", // y4
  "HHHHHSSS", // y3
  "HHHHHSSS", // y2
  "HHHHSSSS", // y1
  "SSSSSSSS"  // y0
};

const char steveBackFace[8][9] = {
  "HHHHHHHH", // y7
  "HHHHHHHH", // y6
  "HHHHHHHH", // y5
  "HHHHHHHH", // y4
  "HHHHHHHH", // y3
  "HHHHHHHH", // y2
  "HHHHHHHH", // y1
  "SSSSSSSS"  // y0 (neck)
};

// RIGHT SIDE = x = 7 (no mirroring)
void drawSteveRightSide() {
  // High-contrast Steve palette for LED display
  CRGB hairColor   = CRGB(60, 40, 20);
  CRGB skinColor   = CRGB(165, 110, 80);

  // Rotate pattern 90 degrees clockwise on the RIGHT face
  // Face coordinates: u = z (0..7), v = y (0..7)
  // CW rotation source mapping: source(x_s, y_s) = (v, 7 - u)
  // Array indices: row = 7 - y_s = u, col = x_s = v
  // Due to physical LED orientation, apply an extra horizontal flip on Y
  for (int y = 0; y < 8; y++) {
    for (int z = 0; z < 8; z++) {
      char cell = steveSideFace[z][7 - y];
      CRGB c = (cell == 'H') ? hairColor : skinColor;
      setLedXYZ(7, y, z, c);
    }
  }
}

// LEFT SIDE = x = 0 (mirrored horizontally)
void drawSteveLeftSide() {
  // High-contrast Steve palette for LED display
  CRGB hairColor   = CRGB(60, 40, 20);
  CRGB skinColor   = CRGB(165, 110, 80);

  for (int y = 0; y < 8; y++) {
    int row = 7 - y;
    for (int z = 0; z < 8; z++) {
      int mz = 7 - z;  // mirror horizontally
      char cell = steveSideFace[row][mz];
      CRGB c = (cell == 'H') ? hairColor : skinColor;
      setLedXYZ(0, y, z, c);
    }
  }
}

// BACK = z = 7 (no mirroring)
void drawSteveBackFace() {
  // High-contrast Steve palette for LED display
  CRGB hairColor   = CRGB(60, 40, 20);
  CRGB skinColor   = CRGB(165, 110, 80);

  for (int y = 0; y < 8; y++) {
    int row = 7 - y;
    for (int x = 0; x < 8; x++) {
      char cell = steveBackFace[row][x];
      CRGB c = (cell == 'H') ? hairColor : skinColor;
      setLedXYZ(x, y, 7, c);
    }
  }
}

// ======================================
// Steve TOP and BOTTOM face renderers
// ======================================
// TOP (y = 7): hair seen from above, mostly hair with a slight front trim
void drawSteveTopFace() {
  CRGB hairColor = CRGB(60, 40, 20);
  // Render full 8x8 as hair on TOP face
  for (int z = 0; z < 8; z++) {
    for (int x = 0; x < 8; x++) {
      int idx = ledFromFaceXY(TOP, x, z);
      if (idx >= 0 && idx < NUM_LEDS) {
        leds[idx] = hairColor;
      }
    }
  }
}

// BOTTOM (y = 0): neck/base seen from below
void drawSteveBottomFace() {
  CRGB skinColor = CRGB(165, 110, 80);
  // Render full 8x8 as skin on BOTTOM face
  for (int z = 0; z < 8; z++) {
    for (int x = 0; x < 8; x++) {
      int idx = ledFromFaceXY(BOTTOM, x, z);
      if (idx >= 0 && idx < NUM_LEDS) {
        leds[idx] = skinColor;
      }
    }
  }
}

// Render full Steve head across all faces
void showSteveHeadOnAllFaces() {
  // FRONT
  drawSteveHeadOnFrontFace();
  // SIDES
  drawSteveRightSide();
  drawSteveLeftSide();
  // BACK
  drawSteveBackFace();
  // TOP & BOTTOM
  drawSteveTopFace();
  drawSteveBottomFace();
}

// פונקציה משופרת שצובעת את כל הלדים הרלוונטיים לנקודה
void setAllLedsXYZ(int x, int y, int z, CRGB color) {
  
  // ספירת כמה צירים על הקצה (0 או 7)
  int onEdge = 0;
  if (x == 0 || x == 7) onEdge++;
  if (y == 0 || y == 7) onEdge++;
  if (z == 0 || z == 7) onEdge++;
  
  // אם לא על פני השטח בכלל - צא
  if (onEdge == 0) return;
  
  // צבע את הפאה הראשית
  int mainLed = ledFromXYZ(x, y, z);
  if (mainLed >= 0 && mainLed < NUM_LEDS) {
    leds[mainLed] = color;
  }
  
  // קצה (2 פאות) - צבע את הפאה השנייה
  if (onEdge == 2) {
    // קצה תחתון קדמי
    if (y == 0 && z == 0) {
      leds[ledFromFaceXY(BOTTOM, x, 0)] = color;
      leds[ledFromFaceXY(FRONT, x, 0)] = color;
    }
    // קצה תחתון אחורי
    else if (y == 0 && z == 7) {
      leds[ledFromFaceXY(BOTTOM, x, 7)] = color;
      leds[ledFromFaceXY(BACK, 7-x, 0)] = color;
    }
    // קצה תחתון שמאלי
    else if (y == 0 && x == 0) {
      leds[ledFromFaceXY(BOTTOM, 0, z)] = color;
      leds[ledFromFaceXY(LEFT, z, 0)] = color;
    }
    // קצה תחתון ימני
    else if (y == 0 && x == 7) {
      leds[ledFromFaceXY(BOTTOM, 7, z)] = color;
      leds[ledFromFaceXY(RIGHT, z, 0)] = color;
    }
    // קצה עליון קדמי
    else if (y == 7 && z == 0) {
      leds[ledFromFaceXY(TOP, x, 0)] = color;
      leds[ledFromFaceXY(FRONT, x, 7)] = color;
    }
    // קצה עליון אחורי
    else if (y == 7 && z == 7) {
      leds[ledFromFaceXY(TOP, x, 7)] = color;
      leds[ledFromFaceXY(BACK, 7-x, 7)] = color;
    }
    // קצה עליון שמאלי
    else if (y == 7 && x == 0) {
      leds[ledFromFaceXY(TOP, 0, z)] = color;
      leds[ledFromFaceXY(LEFT, z, 7)] = color;
    }
    // קצה עליון ימני
    else if (y == 7 && x == 7) {
      leds[ledFromFaceXY(TOP, 7, z)] = color;
      leds[ledFromFaceXY(RIGHT, z, 7)] = color;
    }
    // קצה אנכי קדמי שמאלי
    else if (z == 0 && x == 0) {
      leds[ledFromFaceXY(FRONT, 0, y)] = color;
      leds[ledFromFaceXY(LEFT, 0, y)] = color;
    }
    // קצה אנכי קדמי ימני
    else if (z == 0 && x == 7) {
      leds[ledFromFaceXY(FRONT, 7, y)] = color;
      leds[ledFromFaceXY(RIGHT, 0, y)] = color;
    }
    // קצה אנכי אחורי שמאלי
    else if (z == 7 && x == 0) {
      leds[ledFromFaceXY(BACK, 7, y)] = color;
      leds[ledFromFaceXY(LEFT, 7, y)] = color;
    }
    // קצה אנכי אחורי ימני
    else if (z == 7 && x == 7) {
      leds[ledFromFaceXY(BACK, 0, y)] = color;
      leds[ledFromFaceXY(RIGHT, 7, y)] = color;
    }
  }
  
  // פינה (3 פאות)
  else if (onEdge == 3) {
    // 8 פינות של הקובייה
    if (x == 0 && y == 0 && z == 0) {
      leds[ledFromFaceXY(FRONT, 0, 0)] = color;
      leds[ledFromFaceXY(LEFT, 0, 0)] = color;
      leds[ledFromFaceXY(BOTTOM, 0, 0)] = color;
    }
    else if (x == 7 && y == 0 && z == 0) {
      leds[ledFromFaceXY(FRONT, 7, 0)] = color;
      leds[ledFromFaceXY(RIGHT, 0, 0)] = color;
      leds[ledFromFaceXY(BOTTOM, 7, 0)] = color;
    }
    else if (x == 0 && y == 7 && z == 0) {
      leds[ledFromFaceXY(FRONT, 0, 7)] = color;
      leds[ledFromFaceXY(LEFT, 0, 7)] = color;
      leds[ledFromFaceXY(TOP, 0, 0)] = color;
    }
    else if (x == 7 && y == 7 && z == 0) {
      leds[ledFromFaceXY(FRONT, 7, 7)] = color;
      leds[ledFromFaceXY(RIGHT, 0, 7)] = color;
      leds[ledFromFaceXY(TOP, 7, 0)] = color;
    }
    else if (x == 0 && y == 0 && z == 7) {
      leds[ledFromFaceXY(BACK, 7, 0)] = color;
      leds[ledFromFaceXY(LEFT, 7, 0)] = color;
      leds[ledFromFaceXY(BOTTOM, 0, 7)] = color;
    }
    else if (x == 7 && y == 0 && z == 7) {
      leds[ledFromFaceXY(BACK, 0, 0)] = color;
      leds[ledFromFaceXY(RIGHT, 7, 0)] = color;
      leds[ledFromFaceXY(BOTTOM, 7, 7)] = color;
    }
    else if (x == 0 && y == 7 && z == 7) {
      leds[ledFromFaceXY(BACK, 7, 7)] = color;
      leds[ledFromFaceXY(LEFT, 7, 7)] = color;
      leds[ledFromFaceXY(TOP, 0, 7)] = color;
    }
    else if (x == 7 && y == 7 && z == 7) {
      leds[ledFromFaceXY(BACK, 0, 7)] = color;
      leds[ledFromFaceXY(RIGHT, 7, 7)] = color;
      leds[ledFromFaceXY(TOP, 7, 7)] = color;
    }
  }
}

// משחק Snake על הקובייה

void testAllFacesEdges() {
  FastLED.clear();
  
  // 12 קצוות של הקובייה - כל קצה משותף לשתי פאות
  
  // קצוות אופקיים תחתונים (Y=0)
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(FRONT, i, 0)] = CRGB::White;
    leds[ledFromFaceXY(BOTTOM, i, 0)] = CRGB::White;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(BACK, i, 0)] = CRGB::Red;
    leds[ledFromFaceXY(BOTTOM, i, 7)] = CRGB::Red;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(LEFT, i, 0)] = CRGB::Yellow;
    leds[ledFromFaceXY(BOTTOM, 0, i)] = CRGB::Yellow;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(RIGHT, i, 0)] = CRGB::Cyan;
    leds[ledFromFaceXY(BOTTOM, 7, i)] = CRGB::Cyan;
  }
  
  // קצוות אופקיים עליונים (Y=7)
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(FRONT, i, 7)] = CRGB::Blue;
    leds[ledFromFaceXY(TOP, i, 0)] = CRGB::Blue;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(BACK, i, 7)] = CRGB::Green;
    leds[ledFromFaceXY(TOP, i, 7)] = CRGB::Green;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(LEFT, i, 7)] = CRGB::Magenta;
    leds[ledFromFaceXY(TOP, 0, i)] = CRGB::Magenta;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(RIGHT, i, 7)] = CRGB::Orange;
    leds[ledFromFaceXY(TOP, 7, i)] = CRGB::Orange;
  }
  
  // קצוות אנכיים (מחברים למעלה למטה)
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(FRONT, 0, i)] = CRGB::Purple;
    leds[ledFromFaceXY(LEFT, 0, i)] = CRGB::Purple;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(FRONT, 7, i)] = CRGB::Pink;
    leds[ledFromFaceXY(RIGHT, 7, i)] = CRGB::Pink;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(BACK, 0, i)] = CRGB::Brown;
    leds[ledFromFaceXY(LEFT, 7, i)] = CRGB::Brown;
  }
  for (int i = 0; i < 8; i++) {
    leds[ledFromFaceXY(BACK, 7, i)] = CRGB::Teal;
    leds[ledFromFaceXY(RIGHT, 0, i)] = CRGB::Teal;
  }

  FastLED.show();
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_10x20_tr);
  u8g2.setCursor(0, 30);
  u8g2.print("12 Edges");
  u8g2.setCursor(0, 50);
  u8g2.print("Matched");
  u8g2.sendBuffer();

  delay(10000);
}

// בדיקה: מדליק כל פאה בצבע שונה לשנייה
void testAllFaces() {
  CRGB faceColors[6] = {
    CRGB::Red,    // TOP
    CRGB::Green,  // RIGHT
    CRGB::Blue,   // FRONT
    CRGB::Yellow, // LEFT
    CRGB::Cyan,   // BACK
    CRGB::Magenta // BOTTOM
  };

  String faceNames[6] = {"TOP", "RIGHT", "FRONT", "LEFT", "BACK", "BOTTOM"};

  for (int face = 0; face < 6; face++) {
    FastLED.clear();
    
    // הדלקת כל הלדים בפאה
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        int ledIndex = ledFromFaceXY(face, x, y);
        if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
          leds[ledIndex] = faceColors[face];
        }
      }
    }
    
    FastLED.show();

    // הצגה על המסך
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.setCursor(10, 30);
    u8g2.print("Face: ");
    u8g2.setCursor(10, 50);
    u8g2.print(faceNames[face]);
    u8g2.sendBuffer();

    delay(2500);
  }
}

// בדיקה: הפאה העליונה והקצוות הסמוכים אליה
void testTopFace() {
  FastLED.clear();

  // הפאה העליונה (TOP) - אדום
  for (int z = 0; z < 8; z++) {
    for (int x = 0; x < 8; x++) {
      leds[ledFromFaceXY(TOP, x, z)] = CRGB::Red;
    }
  }

  // קצה קדמי-עליון (FRONT y=7 + TOP z=0) - כחול
  for (int x = 0; x < 8; x++) {
    leds[ledFromFaceXY(FRONT, x, 7)] = CRGB::Blue;
    leds[ledFromFaceXY(TOP, x, 0)] = CRGB::Blue;
  }

  // קצה אחורי-עליון (BACK y=7 + TOP z=7) - ירוק
  for (int x = 0; x < 8; x++) {
    leds[ledFromFaceXY(BACK, x, 7)] = CRGB::Green;
    leds[ledFromFaceXY(TOP, x, 7)] = CRGB::Green;
  }

  // קצה שמאלי-עליון (LEFT y=7 + TOP x=0) - צהוב
  for (int z = 0; z < 8; z++) {
    leds[ledFromFaceXY(LEFT, z, 7)] = CRGB::Yellow;
    leds[ledFromFaceXY(TOP, 0, z)] = CRGB::Yellow;
  }

  // קצה ימני-עליון (RIGHT y=7 + TOP x=7) - ציאן
  for (int z = 0; z < 8; z++) {
    leds[ledFromFaceXY(RIGHT, z, 7)] = CRGB::Cyan;
    leds[ledFromFaceXY(TOP, 7, z)] = CRGB::Cyan;
  }

  FastLED.show();
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_10x20_tr);
  u8g2.setCursor(5, 20);
  u8g2.print("TOP + Edges");
  u8g2.setCursor(0, 40);
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.print("B:Front G:Back");
  u8g2.setCursor(0, 55);
  u8g2.print("Y:Left C:Right");
  u8g2.sendBuffer();

  delay(5000);
}

// בדיקה: הפאה התחתונה והקצוות הסמוכים אליה
void testBottomFace() {
  FastLED.clear();

  // הפאה התחתונה (BOTTOM) - לבן
  for (int z = 0; z < 8; z++) {
    for (int x = 0; x < 8; x++) {
      leds[ledFromFaceXY(BOTTOM, x, z)] = CRGB::White;
    }
  }

  // קצה קדמי-תחתון (FRONT y=0 + BOTTOM z=0) - כחול
  for (int x = 0; x < 8; x++) {
    leds[ledFromFaceXY(FRONT, x, 0)] = CRGB::Blue;
    leds[ledFromFaceXY(BOTTOM, x, 0)] = CRGB::Blue;
  }

  // קצה אחורי-תחתון (BACK y=0 + BOTTOM z=7) - ירוק
  for (int x = 0; x < 8; x++) {
    leds[ledFromFaceXY(BACK, x, 0)] = CRGB::Green;
    leds[ledFromFaceXY(BOTTOM, x, 7)] = CRGB::Green;
  }

  // קצה שמאלי-תחתון (LEFT y=0 + BOTTOM x=0) - צהוב
  for (int z = 0; z < 8; z++) {
    leds[ledFromFaceXY(LEFT, z, 0)] = CRGB::Yellow;
    leds[ledFromFaceXY(BOTTOM, 0, z)] = CRGB::Yellow;
  }

  // קצה ימני-תחתון (RIGHT y=0 + BOTTOM x=7) - ציאן
  for (int z = 0; z < 8; z++) {
    leds[ledFromFaceXY(RIGHT, z, 0)] = CRGB::Cyan;
    leds[ledFromFaceXY(BOTTOM, 7, z)] = CRGB::Cyan;
  }

  FastLED.show();
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_10x20_tr);
  u8g2.setCursor(0, 20);
  u8g2.print("BOTTOM+Edges");
  u8g2.setCursor(0, 40);
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.print("B:Front G:Back");
  u8g2.setCursor(0, 55);
  u8g2.print("Y:Left C:Right");
  u8g2.sendBuffer();

  delay(5000);
}

// בדיקה מקיפה
void runFullTest() {
  testAllFacesEdges();
}


void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== Tesseract Starting ===");
  
  initFaceMapping();

  // הפעלת המסך
  u8g2.begin();

  // הצגת זמן קימפול
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.setCursor(0, 15);
  u8g2.print("Compiled:");
  u8g2.setCursor(0, 30);
  u8g2.print(__DATE__);
  u8g2.setCursor(0, 45);
  u8g2.print(__TIME__);
  u8g2.sendBuffer();
  delay(2000);
  
  // בדיקת חיבור MPU6050
  Serial.println("Initializing MPU6050...");
  Wire.begin();
  mpu.initialize();
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.setCursor(0, 15);
  u8g2.print("MPU6050:");
  
  if (mpu.testConnection()) {
    mpuConnected = true;
    Serial.println("MPU6050 connected!");
    u8g2.setCursor(0, 35);
    u8g2.print("Connected!");
    u8g2.setCursor(0, 55);
    u8g2.print("ID: 0x68");
  } else {
    mpuConnected = false;
    Serial.println("MPU6050 NOT found!");
    u8g2.setCursor(0, 35);
    u8g2.print("NOT Found");
    u8g2.setCursor(0, 55);
    u8g2.print("Check wiring");
  }
  u8g2.sendBuffer();
  delay(3000);

  // הפעלת הלדים
  FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(30);  // ערך בין 0 ל־255
  FastLED.clear();
  FastLED.show();

  // אתחול Snake
  randomSeed(analogRead(0));
  initSnake();
  
  // אתחול מטריצת האש - הכל כבוי
  for (int x = 0; x < 8; x++) {
    for (int y = 0; y < 8; y++) {
      for (int z = 0; z < 8; z++) {
        fireData[x][y][z] = 0;
      }
    }
  }
  
  // אתחול שלג - הכל ריק
  for (int x = 0; x < 8; x++) {
    for (int z = 0; z < 8; z++) {
      for (int face = 0; face < 6; face++) {
        snowHeight[x][z][face] = 0;
      }
    }
  }
  
  // אתחול פתיתי שלג
  for (int i = 0; i < MAX_SNOWFLAKES; i++) {
    snowflakes[i].active = false;
  }
  
  // התחלת מחזור
  currentMode = FIRE;
  modeStartTime = millis();
  
  // כיול MPU6050
  if (mpuConnected) {
    calibrateMPU();
  }
  
  // אתחול שעון - מתחיל מ-00:00
  startMillis = millis();
  clockHours = 0;
  clockMinutes = 0;
  clockSeconds = 0;

  initCreature();
}

// Snake חדש עם חוקים ברורים
void initSnake() {
  // בחירת פאה אקראית להתחלה
  int startFace = random(6);
  int startX, startY, startZ;
  Point3D dir;
  
  // יצירת נחש בפאה אקראית עם כיוון התחלתי
  if (startFace == 0) { // FRONT (z=0)
    startX = random(2, 6);
    startY = random(2, 6);
    startZ = 0;
    dir = {0, 0, 1};  // כיוון פנימה
  } else if (startFace == 1) { // BACK (z=7)
    startX = random(2, 6);
    startY = random(2, 6);
    startZ = 7;
    dir = {0, 0, -1};
  } else if (startFace == 2) { // LEFT (x=0)
    startX = 0;
    startY = random(2, 6);
    startZ = random(2, 6);
    dir = {1, 0, 0};
  } else if (startFace == 3) { // RIGHT (x=7)
    startX = 7;
    startY = random(2, 6);
    startZ = random(2, 6);
    dir = {-1, 0, 0};
  } else if (startFace == 4) { // BOTTOM (y=0)
    startX = random(2, 6);
    startY = 0;
    startZ = random(2, 6);
    dir = {0, 1, 0};
  } else { // TOP (y=7)
    startX = random(2, 6);
    startY = 7;
    startZ = random(2, 6);
    dir = {0, -1, 0};
  }
  
  // הגדרת הראש ואורך התחלתי
  snake[0] = {startX, startY, startZ};
  snakeLength = 5;
  
  // יצירת גוף הנחש - לאורך הכיוון ההפוך
  for (int i = 1; i < snakeLength; i++) {
    snake[i].x = snake[i-1].x - dir.x;
    snake[i].y = snake[i-1].y - dir.y;
    snake[i].z = snake[i-1].z - dir.z;
    
    // וידוא שהנקודה על הפאה
    if (!isOnSurface(snake[i])) {
      // אם יצאנו מהפאה, נשאר באותה נקודה
      snake[i] = snake[i-1];
    }
  }
  
  direction = dir;
  
  // אתחול מערך האוכל
  for (int i = 0; i < MAX_FOOD; i++) {
    foodExists[i] = false;
  }
  
  // יצירת 5 פריטי אוכל התחלתיים
  for (int i = 0; i < MAX_FOOD; i++) {
    placeFood();
  }
  
  stuckCounter = 0;
}

// יצירת אוכל במיקום אקראי
void placeFood() {
  // מציאת מיקום ריק במערך האוכל
  int emptySlot = -1;
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!foodExists[i]) {
      emptySlot = i;
      break;
    }
  }
  
  if (emptySlot == -1) return;  // כל המקומות תפוסים
  
  int maxAttempts = 100;
  int attempts = 0;
  Point3D newFood;
  
  do {
    // בחירת פאה אקראית
    int face = random(6);
    
    if (face == 0) { // FRONT
      newFood = {random(8), random(8), 0};
    } else if (face == 1) { // BACK
      newFood = {random(8), random(8), 7};
    } else if (face == 2) { // LEFT
      newFood = {0, random(8), random(8)};
    } else if (face == 3) { // RIGHT
      newFood = {7, random(8), random(8)};
    } else if (face == 4) { // BOTTOM
      newFood = {random(8), 0, random(8)};
    } else { // TOP
      newFood = {random(8), 7, random(8)};
    }
    
    // בדיקה שלא על הנחש ולא על אוכל אחר
    bool validPos = !isOnSnake(newFood);
    for (int i = 0; i < MAX_FOOD && validPos; i++) {
      if (i != emptySlot && foodExists[i] && pointsEqual(newFood, food[i])) {
        validPos = false;
      }
    }
    
    if (validPos) {
      food[emptySlot] = newFood;
      foodExists[emptySlot] = true;
      return;
    }
    
    attempts++;
  } while (attempts < maxAttempts);
}

// בדיקה אם שתי נקודות זהות
bool pointsEqual(Point3D a, Point3D b) {
  return (a.x == b.x && a.y == b.y && a.z == b.z);
}

// בדיקה אם נקודה על פני הקובייה (חייב להיות על פאה)
bool isOnSurface(Point3D p) {
  // בדיקת גבולות
  if (p.x < 0 || p.x > 7 || p.y < 0 || p.y > 7 || p.z < 0 || p.z > 7) {
    return false;
  }
  
  // חייב להיות לפחות אחד מהצירים על הקצה (0 או 7)
  return (p.x == 0 || p.x == 7 || p.y == 0 || p.y == 7 || p.z == 0 || p.z == 7);
}

// בדיקה אם הנקודה תתנגש בגוף הנחש
bool willCollideWithBody(Point3D p) {
  // לא בודקים את הזנב (אינדקס אחרון) כי הוא יזוז
  for (int i = 0; i < snakeLength - 1; i++) {
    if (snake[i].x == p.x && snake[i].y == p.y && snake[i].z == p.z) {
      return true;
    }
  }
  return false;
}

// בדיקה אם הנקודה תופסת את כל גוף הנחש (כולל הזנב)
bool isOnSnake(Point3D p) {
  for (int i = 0; i < snakeLength; i++) {
    if (snake[i].x == p.x && snake[i].y == p.y && snake[i].z == p.z) {
      return true;
    }
  }
  return false;
}

// מציאת כל התנועות האפשריות
void getPossibleMoves(Point3D head, Point3D* moves, int& moveCount) {
  moveCount = 0;
  
  // 6 כיוונים אפשריים (אחד בכל פעם)
  Point3D directions[6] = {
    {1, 0, 0},   // +X
    {-1, 0, 0},  // -X
    {0, 1, 0},   // +Y
    {0, -1, 0},  // -Y
    {0, 0, 1},   // +Z
    {0, 0, -1}   // -Z
  };
  
  for (int i = 0; i < 6; i++) {
    Point3D newPos = {
      head.x + directions[i].x,
      head.y + directions[i].y,
      head.z + directions[i].z
    };
    
    // בדיקה אם התנועה חוקית
    if (isOnSurface(newPos) && !willCollideWithBody(newPos)) {
      moves[moveCount] = directions[i];
      moveCount++;
    }
  }
}

// חישוב מרחק מנהטן בין שתי נקודות (משוקלל על פני השטח)
int manhattanDistance(Point3D a, Point3D b) {
  return abs(a.x - b.x) + abs(a.y - b.y) + abs(a.z - b.z);
}

// בחירת כיוון חדש חכם - מכוון לעבר האוכל
void findNewDirection() {
  Point3D head = snake[0];
  Point3D possibleMoves[6];
  int moveCount = 0;
  
  getPossibleMoves(head, possibleMoves, moveCount);
  
  if (moveCount == 0) {
    // אין תנועות אפשריות - נשאר במקום
    direction = {0, 0, 0};
    return;
  }
  
  // אם יש אוכל, נסה להתקדם לקרוב ביותר
  Point3D closestFood = {-1, -1, -1};
  int closestDistance = 9999;
  bool foundFood = false;
  
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foodExists[i]) {
      int dist = manhattanDistance(head, food[i]);
      if (dist < closestDistance) {
        closestDistance = dist;
        closestFood = food[i];
        foundFood = true;
      }
    }
  }
  
  if (foundFood) {
    // מציאת הכיוון שמקרב לאוכל
    int bestMoveIndex = -1;
    int bestDistance = 9999;
    
    for (int i = 0; i < moveCount; i++) {
      Point3D testPos = {
        head.x + possibleMoves[i].x,
        head.y + possibleMoves[i].y,
        head.z + possibleMoves[i].z
      };
      
      int dist = manhattanDistance(testPos, closestFood);
      
      if (dist < bestDistance) {
        bestDistance = dist;
        bestMoveIndex = i;
      }
    }
    
    if (bestMoveIndex >= 0) {
      // 90% סיכוי ללכת לכיוון האוכל
      if (random(100) < 90) {
        direction = possibleMoves[bestMoveIndex];
        return;
      }
    }
  }
  
  // ניסיון להמשיך באותו כיוון אם אפשר
  Point3D currentDirection = direction;
  Point3D nextPos = {
    head.x + currentDirection.x,
    head.y + currentDirection.y,
    head.z + currentDirection.z
  };
  
  if (isOnSurface(nextPos) && !willCollideWithBody(nextPos)) {
    // הכיוון הנוכחי טוב - ממשיכים בו
    // 70% סיכוי להמשיך באותו כיוון
    if (random(100) < 70) {
      return;
    }
  }
  
  // בחירת כיוון אקראי מהתנועות האפשריות
  int chosenMove = random(moveCount);
  direction = possibleMoves[chosenMove];
}

void moveSnake() {
  Point3D head = snake[0];
  
  // בדיקה אם הכיוון הנוכחי עדיין תקין
  Point3D nextPos = {
    head.x + direction.x,
    head.y + direction.y,
    head.z + direction.z
  };
  
  // אם הכיוון הנוכחי לא תקין - חייב למצוא כיוון חדש
  if (!isOnSurface(nextPos) || willCollideWithBody(nextPos)) {
    findNewDirection();
  } else {
    // גם אם הכיוון תקין, נותנים סיכוי לשינוי (למגוון)
    if (random(100) < 10) {
      findNewDirection();
    }
  }
  
  // אם אין תנועה אפשרית - נתקענו!
  if (direction.x == 0 && direction.y == 0 && direction.z == 0) {
    stuckCounter++;
    if (stuckCounter > 5) {
      gameOverAnimation();
      initSnake();
    }
    return;
  }
  
  // חישוב ראש חדש (עכשיו הכיוון תקין!)
  Point3D newHead = {
    snake[0].x + direction.x,
    snake[0].y + direction.y,
    snake[0].z + direction.z
  };
  
  // בדיקה אם אכלנו אוכל כלשהו
  int eatenFoodIndex = -1;
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foodExists[i] && pointsEqual(newHead, food[i])) {
      eatenFoodIndex = i;
      break;
    }
  }
  
  bool ateFood = (eatenFoodIndex >= 0);
  
  // אם אכלנו אוכל - שמור את הזנב הישן
  Point3D oldTail;
  if (ateFood && snakeLength < MAX_SNAKE_LENGTH) {
    oldTail = snake[snakeLength - 1];
  }
  
  // הזזת כל הגוף אחד אחורה
  for (int i = snakeLength - 1; i > 0; i--) {
    snake[i] = snake[i - 1];
  }
  
  // הצבת הראש החדש
  snake[0] = newHead;
  
  // איפוס מונה תקיעות אם זזנו
  if (!pointsEqual(snake[0], snake[1])) {
    stuckCounter = 0;
  } else {
    stuckCounter++;
    if (stuckCounter > 5) {
      gameOverAnimation();
      initSnake();
    }
  }
  
  // אם אכלנו אוכל - גדל!
  if (ateFood && snakeLength < MAX_SNAKE_LENGTH) {
    snake[snakeLength] = oldTail;
    snakeLength++;
    foodExists[eatenFoodIndex] = false;  // סימון שהאוכל נאכל
    placeFood();  // יצירת אוכל חדש
    
    if (snakeLength % 5 == 0 && moveDelay > 3) {
      moveDelay -= 1;
    }
  }
}

// אנימציה כשהנחש נתקע
void gameOverAnimation() {
  // גל של צבעים על כל הקובייה
  for (int wave = 0; wave < 3; wave++) {
    for (int hue = 0; hue < 256; hue += 8) {
      for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
          for (int z = 0; z < 8; z++) {
            Point3D surfWave = {x, y, z};
            if (isOnSurface(surfWave)) {
              int brightness = sin8((hue + x * 20 + y * 20 + z * 20) % 256);
              CRGB color = CHSV(hue, 255, brightness);
              setAllLedsXYZ(x, y, z, color);
            }
          }
        }
      }
      FastLED.show();
      delay(20);
    }
  }
  
  // פלאש לבן
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::White;
  }
  FastLED.show();
  delay(200);
  
  FastLED.clear();
  FastLED.show();
  delay(500);
}

void drawSnake() {
  FastLED.clear();
  
  // צבעים שונים לכל אוכל
  uint8_t foodHues[MAX_FOOD] = {0, 160, 96, 192, 32};  // אדום, כחול, ירוק, סגול, כתום
  
  // ציור זוהר מסביב לאוכל
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foodExists[i]) {
      uint8_t hue = foodHues[i];
      
      // ציור האוכל עצמו (פועם)
      uint8_t brightness = beatsin8(60, 100, 255);  // פעימה חלקה
      setAllLedsXYZ(food[i].x, food[i].y, food[i].z, CHSV(hue, 255, brightness));
      
      // זוהר מסביב - בדיקת כל השכנים
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dy == 0 && dz == 0) continue;  // לא האוכל עצמו
            
            int nx = food[i].x + dx;
            int ny = food[i].y + dy;
            int nz = food[i].z + dz;
            
            Point3D neighbor = {nx, ny, nz};
            if (isOnSurface(neighbor)) {
              // זוהר באותו צבע של האוכל, עוצמה נמוכה יותר
              uint8_t glowBrightness = beatsin8(60, 30, 100);
              CRGB glowColor = CHSV(hue, 200, glowBrightness);
              
              // שימוש ב-setAllLedsXYZ כדי לטפל נכון בקצוות ובפינות
              // אבל רק להוסיף, לא להחליף
              int ledIndex = ledFromXYZ(nx, ny, nz);
              if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
                leds[ledIndex] += glowColor;
              }
              
              // בדיקה אם זה קצה או פינה - צריך להוסיף גם ללדים האחרים
              int onEdge = 0;
              if (nx == 0 || nx == 7) onEdge++;
              if (ny == 0 || ny == 7) onEdge++;
              if (nz == 0 || nz == 7) onEdge++;
              
              if (onEdge >= 2) {
                // זה קצה או פינה - צריך למצוא את כל הלדים הרלוונטיים
                std::vector<int> indices;
                
                if (nx == 0) indices.push_back(ledFromFaceXY(LEFT, nz, ny));
                if (nx == 7) indices.push_back(ledFromFaceXY(RIGHT, nz, ny));
                if (ny == 0) indices.push_back(ledFromFaceXY(BOTTOM, nx, nz));
                if (ny == 7) indices.push_back(ledFromFaceXY(TOP, nx, nz));
                if (nz == 0) indices.push_back(ledFromFaceXY(FRONT, nx, ny));
                if (nz == 7) indices.push_back(ledFromFaceXY(BACK, nx, ny));
                
                // הוספת הזוהר לכל הלדים הרלוונטיים
                for (int idx : indices) {
                  if (idx >= 0 && idx < NUM_LEDS) {
                    leds[idx] += glowColor;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  
  // ציור הנחש עם גרדיאנט צבעים
  for (int i = 0; i < snakeLength; i++) {
    CRGB color;
    if (i == 0) {
      color = CRGB::Yellow;  // ראש צהוב
    } else {
      // גרדיאנט מירוק לכחול
      int hue = map(i, 1, snakeLength - 1, 96, 160);
      color = CHSV(hue, 255, 220);
    }
    setAllLedsXYZ(snake[i].x, snake[i].y, snake[i].z, color);
  }
  
  FastLED.show();
}

void runSnakeGame() {
  if (millis() - lastMoveTime > moveDelay) {
    moveSnake();
    drawSnake();
    lastMoveTime = millis();
    
    // // הצגת מידע על המסך
    // u8g2.clearBuffer();
    // u8g2.setFont(u8g2_font_10x20_tr);
    // u8g2.setCursor(10, 30);
    // u8g2.print("Snake: ");
    // u8g2.print(snakeLength);
    // u8g2.setCursor(10, 50);
    // u8g2.print("Pos:");
    // u8g2.print(snake[0].x);
    // u8g2.print(",");
    // u8g2.print(snake[0].y);
    // u8g2.print(",");
    // u8g2.print(snake[0].z);
    // u8g2.sendBuffer();
  }
}

// אנימציית אש על הקובייה
void runFireAnimation() {
  // הדלקה הדרגתית של האש
  if (fireIntensity < 1.0) {
    fireIntensity += 0.003;  // גדילה איטית
    if (fireIntensity > 1.0) fireIntensity = 1.0;
  }
  
  // עדכון האש - התחלה מלמטה
  
  // קירור כל השכבות כלפי מעלה (האש עולה)
  for (int y = 7; y > 0; y--) {
    for (int x = 0; x < 8; x++) {
      for (int z = 0; z < 8; z++) {
        // כל שכבה מקבלת את הערך מהשכבה מתחת עם הפחתה
        int cooling = random(3, 12);  // קירור איטי יותר (היה 5-20)
        int newHeat = fireData[x][y-1][z] > cooling ? fireData[x][y-1][z] - cooling : 0;
        
        // ערבוב עם השכנים לאפקט טבעי יותר
        int neighborAvg = 0;
        int neighborCount = 0;
        if (x > 0) { neighborAvg += fireData[x-1][y-1][z]; neighborCount++; }
        if (x < 7) { neighborAvg += fireData[x+1][y-1][z]; neighborCount++; }
        if (z > 0) { neighborAvg += fireData[x][y-1][z-1]; neighborCount++; }
        if (z < 7) { neighborAvg += fireData[x][y-1][z+1]; neighborCount++; }
        
        if (neighborCount > 0) {
          neighborAvg /= neighborCount;
          newHeat = (newHeat * 2 + neighborAvg) / 3;  // משוקלל
        }
        
        fireData[x][y][z] = newHeat;
      }
    }
  }
  
  // יצירת חום חדש בתחתית (y=0) - אש חזקה לפי העוצמה
  for (int x = 0; x < 8; x++) {
    for (int z = 0; z < 8; z++) {
      // אש אינטנסיבית בתחתית לפי העוצמה
      int baseHeat = fireIntensity * 255;
      if (random(100) < 80 * fireIntensity) {
        fireData[x][0][z] = random(baseHeat * 0.85, baseHeat);
      } else {
        fireData[x][0][z] = random(baseHeat * 0.7, baseHeat * 0.85);
      }
    }
  }
  
  // פיזור אנכי - אש בפאות האנכיות (4 הפאות הצדדיות)
  for (int y = 1; y < 8; y++) {
    // FRONT (z=0)
    for (int x = 0; x < 8; x++) {
      int baseHeat = fireData[x][0][0];
      int cooling = y * random(10, 18);  // קירור איטי יותר (היה 15-25)
      fireData[x][y][0] = baseHeat > cooling ? baseHeat - cooling : random(50, 100);
    }
    
    // BACK (z=7)
    for (int x = 0; x < 8; x++) {
      int baseHeat = fireData[x][0][7];
      int cooling = y * random(10, 18);  // קירור איטי יותר
      fireData[x][y][7] = baseHeat > cooling ? baseHeat - cooling : random(50, 100);
    }
    
    // LEFT (x=0)
    for (int z = 0; z < 8; z++) {
      int baseHeat = fireData[0][0][z];
      int cooling = y * random(10, 18);  // קירור איטי יותר
      fireData[0][y][z] = baseHeat > cooling ? baseHeat - cooling : random(50, 100);
    }
    
    // RIGHT (x=7)
    for (int z = 0; z < 8; z++) {
      int baseHeat = fireData[7][0][z];
      int cooling = y * random(10, 18);  // קירור איטי יותר
      fireData[7][y][z] = baseHeat > cooling ? baseHeat - cooling : random(50, 100);
    }
  }
  
  // ציור האש על הקובייה
  FastLED.clear();
  
  for (int x = 0; x < 8; x++) {
    for (int y = 0; y < 8; y++) {
      for (int z = 0; z < 8; z++) {
        Point3D surfFire = {x, y, z};
        if (isOnSurface(surfFire)) {
          CRGB color;
          
          // הפאה העליונה (y=7) - כתום-צהוב חם
          if (y == 7) {
            uint8_t phase = (millis() / 20 + x * 30 + z * 30) % 256;
            uint8_t brightness = beatsin8(40, 200, 255, 0, phase);
            color = CHSV(20, 255, brightness);  // כתום-צהוב
          } else {
            // שאר הפאות - צבעי אש אמיתיים
            byte heat = fireData[x][y][z];
            
            // רק אם יש חום - אחרת נשאר שחור
            if (heat > 10) {
              // המרת חום לצבעי אש: אדום -> כתום -> צהוב
              if (heat < 85) {
                // אדום כהה עד אדום בוהק
                color = CRGB(heat * 3, 0, 0);
              } else if (heat < 170) {
                // אדום לכתום
                byte orange = (heat - 85) * 3;
                color = CRGB(255, orange, 0);
              } else {
                // כתום לצהוב-לבן
                byte yellow = (heat - 170) * 3;
                color = CRGB(255, 255, yellow);
              }
            } else {
              // ללא חום - שחור
              color = CRGB::Black;
            }
          }
          
          setAllLedsXYZ(x, y, z, color);
        }
      }
    }
  }
  
  FastLED.show();
  delay(60);  // האטה משמעותית - מ-25 ל-60ms
}

// אנימציית שלג יורד ונערם
void runSnowAnimation() {
  // יצירת פתיתי שלג חדשים
  if (random(100) < 40) {  // 40% סיכוי בכל פריים
    for (int i = 0; i < MAX_SNOWFLAKES; i++) {
      if (!snowflakes[i].active) {
        // יצירת פתית שלג חדשה בחלק העליון
        snowflakes[i].x = random(0, 800) / 100.0;  // 0-7.99
        snowflakes[i].y = 7.5;  // מתחיל מלמעלה
        snowflakes[i].z = random(0, 800) / 100.0;
        snowflakes[i].active = true;
        break;
      }
    }
  }
  
  // עדכון פתיתי שלג
  for (int i = 0; i < MAX_SNOWFLAKES; i++) {
    if (snowflakes[i].active) {
      // נפילה איטית עם תנודה קלה
      snowflakes[i].y -= 0.08;  // מהירות נפילה
      snowflakes[i].x += (random(-10, 10)) / 100.0;  // תנודה
      snowflakes[i].z += (random(-10, 10)) / 100.0;
      
      // שמירה בגבולות
      if (snowflakes[i].x < 0) snowflakes[i].x = 0;
      if (snowflakes[i].x > 7.99) snowflakes[i].x = 7.99;
      if (snowflakes[i].z < 0) snowflakes[i].z = 0;
      if (snowflakes[i].z > 7.99) snowflakes[i].z = 7.99;
      
      // בדיקה אם הגיע לקרקע או לשלג קיים
      int ix = (int)snowflakes[i].x;
      int iz = (int)snowflakes[i].z;
      
      // בדיקה על הפאה התחתונה (y=0)
      if (snowflakes[i].y <= snowHeight[ix][iz][4] / 8.0) {
        // נחת על שלג קיים
        if (snowHeight[ix][iz][4] < 8) {
          snowHeight[ix][iz][4]++;
        }
        snowflakes[i].active = false;
      }
    }
  }
  
  // ציור השלג
  FastLED.clear();
  
  // ציור השלג שנערם על הפאה התחתונה
  for (int x = 0; x < 8; x++) {
    for (int z = 0; z < 8; z++) {
      for (int y = 0; y < snowHeight[x][z][4] && y < 8; y++) {
        setAllLedsXYZ(x, y, z, CRGB::White);
      }
    }
  }
  
  // ציור פתיתי שלג מעופפים
  for (int i = 0; i < MAX_SNOWFLAKES; i++) {
    if (snowflakes[i].active) {
      int sx = (int)snowflakes[i].x;
      int sy = (int)snowflakes[i].y;
      int sz = (int)snowflakes[i].z;
      
      if (sx >= 0 && sx < 8 && sy >= 0 && sy < 8 && sz >= 0 && sz < 8) {
        Point3D surfSnow = {sx, sy, sz};
        if (isOnSurface(surfSnow)) {
          uint8_t twinkle = beatsin8(80, 180, 255, 0, i * 20);
          setAllLedsXYZ(sx, sy, sz, CRGB(twinkle, twinkle, twinkle));
        }
      }
    }
  }
  
  FastLED.show();
  delay(40);
}

// איפוס מצבים לאנימציות
void resetFireAnimation() {
  for (int x = 0; x < 8; x++) {
    for (int y = 0; y < 8; y++) {
      for (int z = 0; z < 8; z++) {
        fireData[x][y][z] = 0;
      }
    }
  }
  fireIntensity = 0.0;
}

void resetSnowAnimation() {
  for (int x = 0; x < 8; x++) {
    for (int z = 0; z < 8; z++) {
      for (int face = 0; face < 6; face++) {
        snowHeight[x][z][face] = 0;
      }
    }
  }
  for (int i = 0; i < MAX_SNOWFLAKES; i++) {
    snowflakes[i].active = false;
  }
}

void creatureReadAccel() {
  mpu.getAcceleration(&c_ax, &c_ay, &c_az);
}

long creatureMotionMagnitude() {
  return (long)abs(c_ax) + (long)abs(c_ay) + (long)abs(c_az);
}

void initCreature() {
  creature.mood = MOOD_CALM;
  creature.energy = 70;
  creature.boredom = 20;
  creature.lastInteraction = millis();
}

void updateCreature() {
  unsigned long now = millis();

  if (now - lastCreatureUpdateMs < 100) return;
  lastCreatureUpdateMs = now;

  creatureReadAccel();
  long motion = creatureMotionMagnitude();

  bool strongShake = motion > 60000;
  bool gentleMove  = motion > 20000 && motion <= 60000;
  bool still       = motion <= 20000;

  unsigned long sinceInteraction = now - creature.lastInteraction;

  if (strongShake) {
    creature.energy = max(creature.energy - 5, 0);
    creature.boredom = max(creature.boredom - 5, 0);
    creature.mood = MOOD_CALM;
    creature.lastInteraction = now;
  }

  if (gentleMove) {
    creature.energy = min(creature.energy + 2, 100);
    creature.boredom = max(creature.boredom - 5, 0);
    creature.mood = MOOD_HAPPY;
    creature.lastInteraction = now;
  }

  if (still) {
    creature.boredom = min(creature.boredom + 1, 100);
    creature.energy = max(creature.energy - 1, 0);

    if (sinceInteraction > 60000 && creature.boredom > 60) {
      creature.mood = MOOD_SLEEPY;
    } else {
      creature.mood = MOOD_CALM;
    }
  }

  // עדכון תזוזת אישונים לאחר קביעת מצב הרוח
  updatePupils(now);
}

// אנימציית מים שמגיבה לשיפוע
void runWaterAnimation() {
  if (!mpuConnected || !calibrationComplete) {
    // אם אין MPU או לא כיילנו - פשוט חצי כחול חצי שחור
    FastLED.clear();
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 4; y++) {
        for (int z = 0; z < 8; z++) {
          Point3D surfWater = {x, y, z};
          if (isOnSurface(surfWater)) {
            setAllLedsXYZ(x, y, z, CRGB::Blue);
          }
        }
      }
    }
    FastLED.show();
    delay(50);
    return;
  }
  
  // קריאת תאוצה מה-MPU
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  FastLED.clear();
  
  // חישוב דמיון לכיול TOP - אם דומה מספיק, הפאה TOP למעלה
  if (faceCalibration[0].calibrated) {
    // חישוב מכפלה סקלרית (dot product) עם וקטור הכיול
    long dotProduct = (long)ax * faceCalibration[0].ax + 
                      (long)ay * faceCalibration[0].ay + 
                      (long)az * faceCalibration[0].az;
    
    // נרמול - חישוב גודל הווקטורים
    long magCurrent = (long)ax * ax + (long)ay * ay + (long)az * az;
    long magCalib = (long)faceCalibration[0].ax * faceCalibration[0].ax + 
                    (long)faceCalibration[0].ay * faceCalibration[0].ay + 
                    (long)faceCalibration[0].az * faceCalibration[0].az;
    
    // חישוב קוסינוס הזווית (מנורמל)
    float cosAngle = dotProduct / (sqrt(magCurrent) * sqrt(magCalib));
    
    // אם הזווית קטנה מספיק - הפאה למעלה
    if (cosAngle > 0.5) {  // זווית < 60 מעלות
      // צביעת הפאה TOP
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          int ledIndex = ledFromFaceXY(0, x, y);  // 0 = TOP
          if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
            leds[ledIndex] = CRGB::Cyan;
          }
        }
      }
    }
  }
  
  FastLED.show();
  delay(30);
}

void drawFrontPixel(int x, int y, const CRGB &color) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return;
  setLedXYZ(x, y, FACE_Z, color);
}

void drawEye2x2(int centerX, int centerY, const CRGB &color) {
  for (int dx = -1; dx <= 0; dx++) {
    for (int dy = -1; dy <= 0; dy++) {
      drawFrontPixel(centerX + dx, centerY + dy, color);
    }
  }
}

void drawMouthLine(int xStart, int xEnd, int y, const CRGB &color) {
  if (xStart > xEnd) {
    int tmp = xStart;
    xStart = xEnd;
    xEnd = tmp;
  }
  for (int x = xStart; x <= xEnd; x++) {
    drawFrontPixel(x, y, color);
  }
}

void drawSmilingMouth(int xStart, int xEnd, int baseY, const CRGB &color) {
  int innerStart = xStart + 2;
  int innerEnd = xEnd - 2;
  int upperY = min(baseY + 1, 7);
  int middleY = min(baseY + 2, 7);
  int lowerY = max(baseY - 1, 0);

  if (innerStart <= innerEnd) {
    drawMouthLine(innerStart, innerEnd, lowerY, color);
  }

  drawFrontPixel(xStart + 1, baseY, color);
  drawFrontPixel(xEnd - 1, baseY, color);
  drawFrontPixel(xStart, upperY, color);
  drawFrontPixel(xEnd, upperY, color);
  drawFrontPixel(xStart + 1, middleY, color);
  drawFrontPixel(xEnd - 1, middleY, color);
}

void drawPatternFace(const uint8_t pattern[8], const CRGB &color) {
  for (int row = 0; row < 8; row++) {
    uint8_t line = pattern[row];
    int y = 7 - row; // row 0 is top (y=7)
    for (int bit = 0; bit < 8; bit++) {
      if (line & (1 << bit)) {
        int x = bit; // bit0 -> x=0 (right), bit7 -> x=7 (left) per spec
        drawFrontPixel(x, y, color);
      }
    }
  }
}

void drawLedFaceForMood(CubeMood mood) {
  CRGB bgColor;
  // לבן מוחלש לפנים (פחות מסנוור)
  CRGB faceColor = CRGB(70, 70, 70);

  switch (mood) {
    case MOOD_CALM:
      bgColor = CRGB(0, 0, 20);
      break;
    case MOOD_HAPPY:
      bgColor = CRGB(20, 10, 0);
      break;
    case MOOD_SLEEPY:
      bgColor = CRGB(0, 5, 10);
      break;
    default:
      bgColor = CRGB::Black;
      break;
  }

  clearCubeLeds(bgColor);

  int leftEyeCenterX  = 2;
  int rightEyeCenterX = 5;
  int eyesCenterY     = 5;

  int mouthY = 2;
  int mouthStartX = 1;
  int mouthEndX   = 6;

  if (mood == MOOD_SLEEPY) {
    drawMouthLine(leftEyeCenterX - 1, leftEyeCenterX, eyesCenterY, faceColor);
    drawMouthLine(rightEyeCenterX - 1, rightEyeCenterX, eyesCenterY, faceColor);
    drawMouthLine(3, 4, mouthY, faceColor);
    return;
  }

  if (mood == MOOD_CALM) {
    drawPatternFace(calmFacePattern, faceColor);
    // אישונים דינמיים (בלוק 2x2) מתוך updatePupils()
    for (int dx = 0; dx < 2; dx++) {
      for (int dy = 0; dy < 2; dy++) {
        setLedXYZ(leftPupilX + dx, leftPupilY + dy, FACE_Z, CRGB::Cyan);
        setLedXYZ(rightPupilX + dx, rightPupilY + dy, FACE_Z, CRGB::Cyan);
      }
    }
    return;
  }

  drawEye2x2(leftEyeCenterX,  eyesCenterY, faceColor);
  drawEye2x2(rightEyeCenterX, eyesCenterY, faceColor);

  if (mood == MOOD_HAPPY) {
    drawSmilingMouth(mouthStartX, mouthEndX, mouthY, faceColor);
    // היילייט עיניים עדין יותר
    drawFrontPixel(leftEyeCenterX,  eyesCenterY, CRGB(150, 150, 120));
    drawFrontPixel(rightEyeCenterX, eyesCenterY, CRGB(150, 150, 120));
  } else {
    drawMouthLine(mouthStartX, mouthEndX, mouthY, faceColor);
  }
}

void applyBreathingToFace() {
  uint8_t bri = beatsin8(5, 80, 255);
  FastLED.setBrightness(bri);
}

void showCalmAnimation() {
  uint8_t bri = beatsin8(5, 10, 80);
  CRGB calmColor = CRGB(0, 0, bri);
  fill_solid(leds, NUM_LEDS, calmColor);
}

void showHappyAnimation() {
  static uint8_t hue = 32;
  hue++;

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(hue, 220, 255);
  }
}

void showSleepyAnimation() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  for (int i = 0; i < 20; i++) {
    int idx = random16(NUM_LEDS);
    leds[idx] = CRGB(0, 10, 20);
  }
}

void showCreatureOnLeds() {
  // Render full Steve head on all faces at low brightness
  showSteveHeadOnAllFaces();
  FastLED.setBrightness(10);
}

void drawCalmFace() {
  u8g2.clearBuffer();
  u8g2.drawDisc(40, 24, 3);
  u8g2.drawDisc(88, 24, 3);
  u8g2.drawLine(48, 40, 80, 40);
}

void drawHappyFace() {
  u8g2.clearBuffer();
  u8g2.drawDisc(40, 24, 3);
  u8g2.drawDisc(88, 24, 3);
  u8g2.drawLine(48, 38, 64, 44);
  u8g2.drawLine(64, 44, 80, 38);
}

void drawSleepyFace() {
  u8g2.clearBuffer();
  u8g2.drawLine(34, 24, 46, 24);
  u8g2.drawLine(82, 24, 94, 24);
  u8g2.drawLine(56, 40, 72, 40);
}

void showCreatureOnOled() {
  switch (creature.mood) {
    case MOOD_CALM:
      drawCalmFace();
      break;
    case MOOD_HAPPY:
      drawHappyFace();
      break;
    case MOOD_SLEEPY:
      drawSleepyFace();
      break;
  }

  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setCursor(0, 60);
  u8g2.print("E:");
  u8g2.print(creature.energy);
  u8g2.print(" B:");
  u8g2.print(creature.boredom);

  u8g2.sendBuffer();
}

// כיול MPU6050 - רק פאה אחת
void calibrateMPU() {
  if (!mpuConnected) {
    calibrationComplete = true;
    return;
  }
  // דילוג על כיול כדי להציג את הפנים מהר יותר
  calibrationComplete = true;
  return;
}

// מטריצות ספרות 5x3 (0-9)
const byte digitPatterns[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
  {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
  {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
  {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
  {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
  {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
  {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
  {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
  {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
  {0b111, 0b101, 0b111, 0b001, 0b111}  // 9
};

// ציור ספרה בודדת על פאה
void drawDigit(int face, int digit, int startX, int startY, CRGB color) {
  if (digit < 0 || digit > 9) return;
  
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (digitPatterns[digit][row] & (1 << (2 - col))) {
        int x = startX + col;
        int y = startY + row;
        if (x >= 0 && x < 8 && y >= 0 && y < 8) {
          int ledIndex = ledFromFaceXY(face, x, y);
          if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
            leds[ledIndex] = color;
          }
        }
      }
    }
  }
}

// אנימציית שעון
void runClockAnimation() {
  // חישוב הזמן
  unsigned long elapsedSeconds = (millis() - startMillis) / 1000;
  clockSeconds = elapsedSeconds % 60;
  unsigned long totalMinutes = elapsedSeconds / 60;
  clockMinutes = (totalMinutes + clockMinutes) % 60;
  clockHours = (totalMinutes / 60 + clockHours) % 24;
  
  FastLED.clear();
  
  // צבע משתנה לפי השעה
  uint8_t hue = (clockHours * 10 + clockMinutes / 6) % 256;
  CRGB timeColor = CHSV(hue, 255, 200);
  
  // שעות - FRONT פאה
  int hoursFace = 2; // FRONT
  int h1 = clockHours / 10;
  int h2 = clockHours % 10;
  drawDigit(hoursFace, h1, 1, 0, timeColor);  // Y=0, X=1 (ספרה ראשונה)
  drawDigit(hoursFace, h2, 4, 0, timeColor);  // Y=0, X=4 (ספרה שנייה)
  
  // דקות - RIGHT פאה
  int minutesFace = 1; // RIGHT
  int m1 = clockMinutes / 10;
  int m2 = clockMinutes % 10;
  drawDigit(minutesFace, m1, 1, 0, timeColor);
  drawDigit(minutesFace, m2, 4, 0, timeColor);
  
  // שניות - BACK פאה
  int secondsFace = 4; // BACK
  int s1 = clockSeconds / 10;
  int s2 = clockSeconds % 10;
  drawDigit(secondsFace, s1, 1, 0, timeColor);
  drawDigit(secondsFace, s2, 4, 0, timeColor);
  
  FastLED.show();
  delay(100);
}

void loop() {
  updateCreature();
  showCreatureOnLeds();
  showCreatureOnOled();
  FastLED.show();
  delay(20);
}
