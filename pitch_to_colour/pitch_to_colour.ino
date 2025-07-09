#include <Adafruit_NeoPixel.h>

#define SENSOR_PIN A0         // Pin for sound sensor
#define PIXEL_PIN 6           // Pin for Neopixels
#define NUM_PIXELS 16         // Number of Neopixel LEDs
#define SAMPLE_WINDOW 50      // Sample window width in ms (50 milliseconds)
#define TRANSITION_SPEED 0.1  // Speed of color transition (lower is slower, higher is faster)
#define NOISE_THRESHOLD 10    // Threshold for ignoring low signals
#define BRIGHTNESS_SCALE 255   // Scale for brightness adjustment
#define SMOOTHING_FACTOR 0.1  // Smoothing factor for brightness

// Declare our NeoPixel strip object
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Variables to store the current and target colors
uint8_t currentRed = 0, currentGreen = 0, currentBlue = 0;
uint8_t targetRed = 0, targetGreen = 0, targetBlue = 0;

// Variables for brightness smoothing
int currentBrightness = 0;
int targetBrightness = 0;

void setup() {
  Serial.begin(9600);  // Initialize serial communication
  strip.begin();       // Initialize the NeoPixel strip
  strip.show();        // Initialize all pixels to 'off'
}

void loop() {
  unsigned long startMillis = millis();  // Start of sample window
  unsigned int peakToPeak = 0;   // peak-to-peak level

  unsigned int signalMax = 0;
  unsigned int signalMin = 1024;

  // collect data for 50 mS
  while (millis() - startMillis < SAMPLE_WINDOW) {
    int sample = analogRead(SENSOR_PIN);
    if (sample < 1024) {  // toss out spurious readings
      if (sample > signalMax) {
        signalMax = sample;  // save just the max levels
      } else if (sample < signalMin) {
        signalMin = sample;  // save just the min levels
      }
    }
  }

  peakToPeak = signalMax - signalMin;  // max - min = peak-peak amplitude

  // Noise gate
  if (peakToPeak < NOISE_THRESHOLD) {
    peakToPeak = 0;
  }

  // Map peak-to-peak amplitude to brightness
  targetBrightness = map(peakToPeak, 0, 1023, 0, BRIGHTNESS_SCALE);
  targetBrightness = constrain(targetBrightness, 0, BRIGHTNESS_SCALE);

  // Use peak-to-peak amplitude as a simple approximation of frequency
  // Adjust these values based on your observations
  int mappedFrequency = map(peakToPeak, 0, 1023, 0, 255);
  mappedFrequency = constrain(mappedFrequency, 0, 255);

  // Print the amplitude and brightness for debugging
  Serial.print("Amplitude: ");
  Serial.print(peakToPeak);
  Serial.print(", Brightness: ");
  Serial.println(targetBrightness);

  // Get the target color based on the mapped frequency
  uint32_t newColor = Wheel(mappedFrequency);  

  // Extract the target red, green, and blue components
  targetRed = (newColor >> 16) & 0xFF;
  targetGreen = (newColor >> 8) & 0xFF;
  targetBlue = newColor & 0xFF;

  // Gradually blend the current color towards the target color
  currentRed = easeColor(currentRed, targetRed);
  currentGreen = easeColor(currentGreen, targetGreen);
  currentBlue = easeColor(currentBlue, targetBlue);

  // Smoothly adjust brightness
  currentBrightness = currentBrightness + (targetBrightness - currentBrightness) * SMOOTHING_FACTOR;
  currentBrightness = constrain(currentBrightness, 0, BRIGHTNESS_SCALE);

  // Apply brightness to the current color
  uint8_t r = (currentRed * currentBrightness) / BRIGHTNESS_SCALE;
  uint8_t g = (currentGreen * currentBrightness) / BRIGHTNESS_SCALE;
  uint8_t b = (currentBlue * currentBrightness) / BRIGHTNESS_SCALE;

  // Set the entire strip to the calculated color
  uint32_t blendedColor = strip.Color(r, g, b);
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, blendedColor);
  }

  strip.show();  // Update the strip with the new color
}

// Function to gradually ease the color component towards the target value
uint8_t easeColor(uint8_t current, uint8_t target) {
  return current + (target - current) * TRANSITION_SPEED;
}

// Input a value 0 to 255 to get a color value.
// The colors are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);  // Red to Blue
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);  // Blue to Green
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);    // Green to Red
}
