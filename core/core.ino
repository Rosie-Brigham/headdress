#include "Adafruit_NeoPixel.h"
#include <PDM.h>

// LED configuration
#define PIXEL_PIN 2
#define JEWEL1_PIXELS 7    // First jewel (7 LEDs)
#define RING1_PIXELS 12    // First ring (12 LEDs)
#define RING2_PIXELS 16    // Second ring (16 LEDs)
#define JEWEL2_PIXELS 7    // Second jewel (7 LEDs)
#define TOTAL_PIXELS 42    // Total (7 + 12 + 16 + 7)

// Pixel index ranges
#define RING1_START 7     // Where first ring starts
#define RING2_START 19    // Where second ring starts
#define JEWEL2_START 35   // Where second jewel starts

// Flow effect settings
#define FLOW_SPEED 3      // How many pixels to advance per update
#define HISTORY_LENGTH TOTAL_PIXELS  // Match with TOTAL_PIXELS

// PDM buffer size
#define SAMPLES 256  
short sampleBuffer[SAMPLES];
volatile int samplesRead;

// Smoothing variables
#define SMOOTH_SAMPLES 12  
float volumeHistory[SMOOTH_SAMPLES];
float pitchHistory[SMOOTH_SAMPLES];
int historyIndex = 0;
float smoothedVolume = 3;
float smoothedPitch = 3;

// Volume response settings
#define VOLUME_THRESHOLD 40  // Minimum volume to trigger changes
#define LOW_VOLUME_SMOOTH 0.05f
#define HIGH_VOLUME_SMOOTH 0.3f

// Previous values for smoothing
float lastBrightness = 50;  
float lastHue = 0;

// Volume tracking for auto-scaling
float maxVolume = 100;
float minVolume = 0;

// Color history for flow effect
uint32_t colorHistory[HISTORY_LENGTH];

// Time-based mode switching variables
unsigned long quietPeriodStartTime = 0;
unsigned long loudPeriodStartTime = 0;
#define QUIET_PERIOD_REQUIRED 5000  // Need 5 seconds of quiet to switch to slow cycle mode
#define LOUD_PERIOD_REQUIRED 1000   // Need 1 second of loudness to switch to reactive mode
bool inSlowCycleMode = true;        // Start in slow cycle mode
float colorCycleHue = 0;
unsigned long lastColorChangeTime = 0;
#define COLOR_CYCLE_SPEED 5  // Milliseconds per hue step

Adafruit_NeoPixel pixels(TOTAL_PIXELS, PIXEL_PIN, NEO_RGB + NEO_KHZ800);

void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(sampleBuffer, bytesAvailable);
  samplesRead = bytesAvailable / 2;
}

void updateColorHistory(uint32_t newColor) {
  // Shift all colors one position
  for(int i = HISTORY_LENGTH - 1; i > 0; i--) {
    colorHistory[i] = colorHistory[i-1];
  }
  colorHistory[0] = newColor;
}

float calculateVolume() {
  long sum = 0;
  for (int i = 0; i < samplesRead; i++) {
    sum += abs(sampleBuffer[i]);
  }
  float volume = (float)sum / samplesRead;
  
  // More aggressive scaling for bass frequencies
  if (smoothedPitch < 500) {  // Bass range
    volume *= 1.5f;
  }
  
  if (volume > maxVolume) maxVolume = volume * 1.02f;
  if (volume < minVolume) minVolume = volume * 0.98f;
  
  maxVolume *= 0.9998f;
  minVolume *= 1.0002f;
  
  return volume;
}

float estimatePitch() {
  int zeroCrossings = 0;
  for (int i = 1; i < samplesRead; i++) {
    if ((sampleBuffer[i] > 0 && sampleBuffer[i - 1] <= 0) ||
        (sampleBuffer[i] < 0 && sampleBuffer[i - 1] >= 0)) {
      zeroCrossings++;
    }
  }
  float timeWindow = samplesRead / 16000.0f;
  return zeroCrossings / (2 * timeWindow);
}

float getAverage(float history[], int length) {
  float sum = 0;
  for(int i = 0; i < length; i++) {
    sum += history[i];
  }
  return sum / length;
}

float smoothValue(float current, float target, float factor) {
  return current + (target - current) * factor;
}

uint32_t frequencyToColor(float freq) {
  int hue = constrain(map(freq, 200, 2000, 0, 255), 0, 255);
  lastHue = smoothValue(lastHue, hue, 0.1f);
  hue = (int)lastHue;
  
  float h = hue / 255.0f;
  uint8_t sector = h * 6;
  float f = h * 6 - sector;
  uint8_t p = 0;
  uint8_t q = 255 * (1 - f);
  uint8_t t = 255 * f;
  uint8_t v = 255;

  switch (sector) {
    case 0: return pixels.Color(v, t, p);
    case 1: return pixels.Color(q, v, p);
    case 2: return pixels.Color(p, v, t);
    case 3: return pixels.Color(p, q, v);
    case 4: return pixels.Color(t, p, v);
    default: return pixels.Color(v, p, q);
  }
}

// Function to create colors for slow cycling mode
uint32_t hueToColor(int hue) {
  float h = hue / 255.0f;
  uint8_t sector = h * 6;
  float f = h * 6 - sector;
  uint8_t p = 0;
  uint8_t q = 255 * (1 - f);
  uint8_t t = 255 * f;
  uint8_t v = 255;

  switch (sector) {
    case 0: return pixels.Color(v, t, p);
    case 1: return pixels.Color(q, v, p);
    case 2: return pixels.Color(p, v, t);
    case 3: return pixels.Color(p, q, v);
    case 4: return pixels.Color(t, p, v);
    default: return pixels.Color(v, p, q);
  }
}

void setup() {
  Serial.begin(9600);
  Serial.print("this is the Core file");
  // Initialize PDM
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(SAMPLES);
  PDM.setGain(35);
  
  if (!PDM.begin(1, 16000)) {
    Serial.println("Failed to start PDM!");
    while (1); // If PDM fails, halt
  }

  // Initialize NeoPixels
  pixels.begin();
  pixels.setBrightness(50);
  pixels.show();
  
  // Initialize smoothing arrays
  for(int i = 0; i < SMOOTH_SAMPLES; i++) {
    volumeHistory[i] = 0;
    pitchHistory[i] = 0;
  }

  // Initialize color history
  for(int i = 0; i < HISTORY_LENGTH; i++) {
    colorHistory[i] = pixels.Color(0, 0, 0);
  }
}

void loop() {
  if (samplesRead) {
    float currentVolume = calculateVolume();
    float currentPitch = estimatePitch();
    
    volumeHistory[historyIndex] = currentVolume;
    pitchHistory[historyIndex] = currentPitch;
    historyIndex = (historyIndex + 1) % SMOOTH_SAMPLES;
    
    smoothedVolume = getAverage(volumeHistory, SMOOTH_SAMPLES);
    smoothedPitch = getAverage(pitchHistory, SMOOTH_SAMPLES);
    
    float volumeRange = maxVolume - minVolume;
    if (volumeRange > 0) {
      float normalizedVolume = (smoothedVolume - minVolume) / volumeRange;
      float targetBrightness = (normalizedVolume * 95.0f) + 5.0f;
      
      float smoothFactor;
      if (normalizedVolume < 0.3f) {
        smoothFactor = LOW_VOLUME_SMOOTH;
      } else {
        smoothFactor = map(normalizedVolume * 100, 30, 100, 
                          LOW_VOLUME_SMOOTH * 100, HIGH_VOLUME_SMOOTH * 100) / 100.0f;
      }
      
      lastBrightness = smoothValue(lastBrightness, targetBrightness, smoothFactor);
    }
    
    pixels.setBrightness((int)lastBrightness);
    
    // Time-based mode switching logic
    unsigned long currentTime = millis();
    
    // Determine if we're in a quiet or loud period
    if (smoothedVolume < VOLUME_THRESHOLD) {
      // Reset the loud period counter when it's quiet
      loudPeriodStartTime = currentTime;
      
      // Check if we've been quiet long enough to switch modes
      if (!inSlowCycleMode && quietPeriodStartTime == 0) {
        quietPeriodStartTime = currentTime;  // Start counting quiet time
      } 
      else if (!inSlowCycleMode && (currentTime - quietPeriodStartTime) > QUIET_PERIOD_REQUIRED) {
        inSlowCycleMode = true;  // Switch to slow cycle after enough quiet time
        Serial.println("Switching to slow cycle mode");
      }
    } 
    else {
      // Reset the quiet period counter when it's loud
      quietPeriodStartTime = 0;
      
      // Check if we've been loud long enough to switch modes
      if (inSlowCycleMode && loudPeriodStartTime == 0) {
        loudPeriodStartTime = currentTime;  // Start counting loud time
      } 
      else if (inSlowCycleMode && (currentTime - loudPeriodStartTime) > LOUD_PERIOD_REQUIRED) {
        inSlowCycleMode = false;  // Switch to reactive after enough loud time
        Serial.println("Switching to reactive mode");
      }
    }
    
    // Apply appropriate color effect based on current mode
    uint32_t newColor;
    if (inSlowCycleMode) {
      // Slow color cycling in quiet mode
      if (currentTime - lastColorChangeTime > COLOR_CYCLE_SPEED) {
        lastColorChangeTime = currentTime;
        colorCycleHue = fmod(colorCycleHue + 0.1f, 256.0f);
        newColor = hueToColor((int)colorCycleHue);
      } else {
        // Use the previous color if not time to change yet
        newColor = colorHistory[0];
      }
    } else {
      // Reactive color based on pitch in loud mode
      newColor = frequencyToColor(smoothedPitch);
    }
    
    // Update the color history with the new color
    updateColorHistory(newColor);
    
    // Set first jewel pixels (0-6)
    for(int i = 0; i < JEWEL1_PIXELS; i++) {
      pixels.setPixelColor(i, colorHistory[i]);
    }
    
    // Set first ring pixels (7-18)
    for(int i = RING1_START; i < RING2_START; i++) {
      pixels.setPixelColor(i, colorHistory[i]);
    }
    
    // Set second ring pixels (19-34) with a wiping effect
    for (int i = 0; i < RING2_PIXELS; i++) {
      // Calculate position in the color history based on distance from center
      int colorIndex;
      
      // For first half of the ring (0-7)
      if (i < RING2_PIXELS/2) {
        colorIndex = i;
      } 
      // For second half of the ring (8-15)
      else {
        colorIndex = RING2_PIXELS - 1 - i;  // Mirror the index
      }
      
      // Add offset to get proper color from history
      colorIndex += RING1_START;  // Offset to get colors after the first ring
      
      // Set the LED color
      pixels.setPixelColor(RING2_START + i, colorHistory[colorIndex]);
    }
    
    // Set second jewel pixels (35-41)
    for(int i = JEWEL2_START; i < TOTAL_PIXELS; i++) {
      pixels.setPixelColor(i, colorHistory[i]);
    }
    
    Serial.print("Volume: ");
    Serial.print(smoothedVolume);
    Serial.print(", Mode: ");
    Serial.println(inSlowCycleMode ? "Slow cycle" : "Reactive");
    
    pixels.show();
    samplesRead = 0;
  }
}