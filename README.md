# Tesseract - LED Cube Controller

Interactive 8×8×8 LED cube with 384 WS2812 LEDs and 4 different animations.

## Hardware

### Main Components
- **ESP8266 (ESP-12S)** - Main microcontroller
- **384 × WS2812 LEDs** - 8×8×8 cube (6 faces of 8×8)
- **OLED SSD1306 128×64** - Display screen
- **MPU6050** - Accelerometer and gyroscope sensor

### Connections
- **LEDs**: GPIO2 (D4)
- **OLED**: Software I2C - Clock: GPIO14, Data: GPIO12
- **MPU6050**: Hardware I2C - SDA: GPIO4, SCL: GPIO5

## Required Libraries

```cpp
FastLED 3.10.3      // LED control
U8g2 2.35.30        // OLED display
Wire 1.0            // I2C communication
MPU6050             // Motion sensor
```

## Code Structure

### Face Mapping
The cube consists of 6 faces:
- `TOP` (0) - Top face
- `RIGHT` (1) - Right face
- `FRONT` (2) - Front face
- `LEFT` (3) - Left face
- `BACK` (4) - Back face
- `BOTTOM` (5) - Bottom face

Each face includes transformations (`swapXY`, `flipX`, `flipY`) to support serpentine layout.

### Available Animations

#### 1. 🔥 Fire
- Fire animation with gradual ignition
- Color gradient: black → red → orange → yellow → white
- 3D heat matrix (`fireData[8][8][8]`)
- Gradual intensity (`fireIntensity` 0.0-1.0)

**Functions:**
- `runFireAnimation()` - Animation loop
- `resetFireAnimation()` - Reset to initial state

#### 2. ❄️ Snow
- Up to 30 snowflakes
- Falling with horizontal drift
- Accumulation on bottom face
- Maximum snow height: 8 layers

**Structures:**
```cpp
struct Snowflake {
  float x, y, z;     // Float position
  int targetFace;    // Target face
  bool active;       // Active/inactive
};
```

**Functions:**
- `runSnowAnimation()` - Animation loop
- `resetSnowAnimation()` - Reset accumulated snow

#### 3. 🐍 Snake
Autonomous snake game on cube faces

**Features:**
- 5 food items in different colors (red, blue, green, purple, orange)
- Glow effect around food
- Random starting positions
- Smart navigation (simplified A*)
- Stuck detection + Game Over animation

**Parameters:**
- `MAX_FOOD = 5`
- `moveDelay = 40ms`
- `MAX_SNAKE_LENGTH = 384`

**Key Functions:**
- `initSnake()` - Initialize new game
- `moveSnake()` - Move the snake
- `findNewDirection()` - Navigation algorithm
- `placeFood()` - Create new food
- `gameOverAnimation()` - End game animation

#### 4. 💧 Water - with MPU6050
Detect top face using motion sensor

**Features:**
- Calibrate one face (TOP) on initial startup
- Real-time orientation detection
- Light up top face in cyan
- Dot product based calculation

**Calibration Structure:**
```cpp
struct CalibrationData {
  int16_t ax, ay, az;  // Gravity vector
  bool calibrated;     // Calibration status
};
```

**Detection Algorithm:**
1. Read current acceleration (ax, ay, az)
2. Calculate cos(θ) with calibration vector
3. If cos(θ) > 0.5 → face is up (angle < 60°)

**Functions:**
- `calibrateMPU()` - Calibration process (5 sec prep + 3 sec countdown)
- `runWaterAnimation()` - Detection and display

## MPU6050 Calibration Process

1. System startup
2. TOP face lights up in red
3. Display shows: "Calibrate: TOP Face UP!"
4. 5 seconds to flip the cube
5. Countdown 3-2-1 with blinks
6. Sample 10 readings and calculate average
7. Save calibration vector: `(ax, ay, az)`
8. Green confirmation: "SAVED!"
9. Rainbow animation
10. Start normal mode

## Operation Modes

### Current Mode: WATER Only
```cpp
void loop() {
  runWaterAnimation();
}
```

### Cyclic Mode (Available in Code)
15 seconds per animation cycle:
```
FIRE (15s) → SNOW (15s) → SNAKE (15s) → WATER (15s) → back to FIRE
```

## Helper Functions

### LED Mapping
```cpp
int ledFromXYZ(int x, int y, int z)           // Convert coordinates to LED
void setLedXYZ(int x, int y, int z, CRGB c)   // Set single LED
void setAllLedsXYZ(int x, int y, int z, CRGB) // Handle edges/corners
bool isOnSurface(Point3D p)                    // Check if on surface
```

### Geometry
```cpp
struct Point3D {
  int x, y, z;
};

bool pointsEqual(Point3D a, Point3D b)
int manhattanDistance(Point3D a, Point3D b)
```

## System Configuration

```cpp
#define NUM_LEDS 384
#define LED_DATA_PIN 2
```

**FastLED:**
- Type: WS2812
- Color format: GRB
- Brightness: 30/255

## File Structure

```
Tesseract/
├── Tesseract.ino      # Main code (~1600 lines)
└── README.md          # This documentation
```

## Running

1. Install libraries (FastLED, U8g2, MPU6050)
2. Connect ESP8266 + LEDs + OLED + MPU6050
3. Upload code
4. Calibrate MPU6050 (once per boot)
5. Watch the animations

## Future Development

- [ ] Calibrate all 6 faces (instead of just TOP)
- [ ] Save calibration to EEPROM
- [ ] Dynamic brightness control
- [ ] Additional animations (wave, spiral, matrix)
- [ ] WiFi mode + app control
- [ ] More interactive games

## License

Open project for educational and commercial use.

---

**Created:** December 2025  
**Platform:** ESP8266 + Arduino IDE  
**Version:** 1.0
