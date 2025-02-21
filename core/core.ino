#include <Adafruit_NeoPixel.h>
#include <PDM.h>

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
#define VOLUME_THRESHOLD 30  // Minimum volume to trigger changes
#define LOW_VOLUME_SMOOTH 0.05  // More smoothing at low volumes
#define HIGH_VOLUME_SMOOTH 0.3  // Less smoothing at high volumes

// Previous values for smoothing
float lastBrightness = 50;  
float lastHue = 0;

// Volume tracking for auto-scaling
float maxVolume = 100;
float minVolume = 0;

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(TOTAL_PIXELS, PIXEL_PIN, NEO_RGB + NEO_KHZ800);

void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(sampleBuffer, bytesAvailable);
  samplesRead = bytesAvailable / 2;
}

void setup() {
  Serial.begin(9600);
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(SAMPLES);
  PDM.setGain(35);
  
  if (!PDM.begin(1, 16000)) {
    Serial.println("Failed to start PDM!");
    while (1); // If PDM fails, halt
  }

  pixels.begin();
  pixels.setBrightness(50);
  pixels.show();
  
  // Initialize smoothing arrays
  for(int i = 0; i < SMOOTH_SAMPLES; i++) {
    volumeHistory[i] = 0;
    pitchHistory[i] = 0;
  }
}

float calculateVolume() {
  long sum = 0;
  for (int i = 0; i < samplesRead; i++) {
    sum += abs(sampleBuffer[i]);
  }
  float volume = (float)sum / samplesRead;
  
  // More aggressive scaling for bass frequencies
  if (smoothedPitch < 500) {  // Bass range
    volume *= 1.5;  // Amplify bass response
  }
  
  if (volume > maxVolume) maxVolume = volume * 1.02;  // Slower increase
  if (volume < minVolume) minVolume = volume * 0.98;  // Slower decrease
  
  maxVolume *= 0.9998;  // Slower decay
  minVolume *= 1.0002;  // Slower rise
  
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
  float timeWindow = samplesRead / 16000.0;
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
  lastHue = smoothValue(lastHue, hue, 0.1);
  hue = (int)lastHue;
  
  float h = hue / 255.0;
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
      float targetBrightness = (normalizedVolume * 95) + 5;
      
      // Dynamic smoothing factor based on volume
      float smoothFactor;
      if (normalizedVolume < 0.3) {  // Low volume
        smoothFactor = LOW_VOLUME_SMOOTH;
      } else {
        // Gradually increase smoothing factor with volume
        smoothFactor = map(normalizedVolume * 100, 30, 100, 
                          LOW_VOLUME_SMOOTH * 100, HIGH_VOLUME_SMOOTH * 100) / 100.0;
      }
      
      lastBrightness = smoothValue(lastBrightness, targetBrightness, smoothFactor);
    }
    
    pixels.setBrightness((int)lastBrightness);
    uint32_t ringColor = frequencyToColor(smoothedPitch);
    
    // Set first jewel pixels (0-6)
    pixels.setPixelColor(0, pixels.Color(255, 255, 255)); // Center white
    for(int i = 1; i < JEWEL1_PIXELS; i++) {
      pixels.setPixelColor(i, ringColor);
    }
    
    // Set first ring pixels (7-18)
    for(int i = RING1_START; i < RING2_START; i++) {
      pixels.setPixelColor(i, ringColor);
    }
    
    // Set second ring pixels (19-34)
    for(int i = RING2_START; i < JEWEL2_START; i++) {
      pixels.setPixelColor(i, ringColor);
    }
    
    // Set second jewel pixels (35-41)
    pixels.setPixelColor(JEWEL2_START, pixels.Color(255, 255, 255)); // Center white
    for(int i = JEWEL2_START + 1; i < TOTAL_PIXELS; i++) {
      pixels.setPixelColor(i, ringColor);
    }
    
    pixels.show();
    samplesRead = 0;
  }
}