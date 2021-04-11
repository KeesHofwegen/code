/*

*/

#include <SI4735.h>
#include <Tiny4kOLED.h>
#include "font16x32digits.h"
#include "Rotary.h"
#include "KHO16X32DIGITS.h"
#include "symbolFonts.h"

const DCfont *defaulFont     = FONT8X16;
const DCfont *digit8x16Font  = FONT8X16DIGITS;



void convertToChar(uint16_t , char *, uint8_t );
void printValue(int , int , char *, char *, const DCfont *);

// Test it with patch_init.h or patch_full.h. Do not try load both.
#include "patch_init.h" // SSB patch for whole SSBRX initialization string
// #include "patch_full.h"    // SSB patch for whole SSBRX full download

const uint16_t size_content = sizeof ssb_patch_content; // see ssb_patch_content in patch_full.h or patch_init.h

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

// OLED Diaplay constants
#define I2C_ADDRESS 0x3C  // Check your I2C bus address (0X3D is also very commom) 
#define RST_PIN -1        // Define proper RST_PIN if required.

#define RESET_PIN 12

// Enconder PINs --> you may have to invert pins 2 and 3 to get the right clockwise and counterclockwise rotation.
#define ENCODER_PIN_A 2
#define ENCODER_PIN_B 3

// Buttons controllers
#define MODE_SWITCH 4      // Switch MODE (Am/LSB/USB)
#define BANDWIDTH_BUTTON 5 // Used to select the banddwith. Values: 1.2, 2.2, 3.0, 4.0, 0.5, 1.0 kHz
#define VOL_UP 6           // Volume Up
#define VOL_DOWN 7         // Volume Down
#define BAND_BUTTON_UP 8   // Next band
#define BAND_BUTTON_DOWN 9 // Previous band
#define AGC_SWITCH 11      // Switch AGC ON/OF
#define STEP_SWITCH 10     // Used to select the increment or decrement frequency step (1, 5 or 10 kHz)
#define BFO_SWITCH 14      // Used to select the enconder control (BFO or VFO)

#define MIN_ELAPSED_TIME      100
#define MIN_ELAPSED_RSSI_TIME 150
#define DEFAULT_VOLUME        20// change it for your favorite sound volume

#define FM 0
#define LSB 1
#define USB 2
#define AM 3
#define LW 4

#define SSB 1

const char *bandModeDesc[] = {"FM ", "LSB", "USB", "AM "};
uint8_t currentMode = FM;


bool fmStereo = true;

long elapsedRSSI = millis();
long elapsedButton = millis();

// Encoder control variables
volatile int encoderCount = 0;

// Some variables to check the SI4735 status
uint16_t currentFrequency;
uint16_t previousFrequency;
uint8_t currentStep = 1;

#define LARGEFONT oled.setFont(FONT8X16);
#define SMALLFONT oled.setFont(FONT6X8);

typedef struct
{
  const char *bandName; // Band description
  uint8_t bandType;     // Band type (FM, MW or SW)
  uint16_t minimumFreq; // Minimum frequency of the band
  uint16_t maximumFreq; // maximum frequency of the band
  uint16_t currentFreq; // Default frequency or current frequency
  uint16_t currentStep; // Defeult step (increment and decrement)
} Band;

/**
    Band table. You can customize this table for your own band plan
*/
Band band[] = {
  {"VHF", FM_BAND_TYPE, 6400, 10800,  10670, 10},
  {"MW1", MW_BAND_TYPE,   150,  1720,   810, 10},
  {"MW2", MW_BAND_TYPE,  1700,  3500,  2500, 5},
  {"80M", MW_BAND_TYPE,  3500,  4000,  3700, 1},
  {"SW1", SW_BAND_TYPE,  4000,  5500,  4885, 5},
  {"SW2", SW_BAND_TYPE,  5500,  6500,  6000, 5},
  {"40M", SW_BAND_TYPE,  6500,  7300,  7100, 1},
  {"SW3", SW_BAND_TYPE,  7200,  8000,  7200, 5},
  {"SW4", SW_BAND_TYPE,  9000, 11000,  9500, 5},
  {"SW5", SW_BAND_TYPE, 11100, 13000, 11900, 5},
  {"SW6", SW_BAND_TYPE, 13000, 14000, 13500, 5},
  {"20M", SW_BAND_TYPE, 14000, 15000, 14200, 1},
  {"SW7", SW_BAND_TYPE, 15000, 17000, 15300, 5},
  {"SW8", SW_BAND_TYPE, 17000, 18000, 17500, 5},
  {"15M", SW_BAND_TYPE, 20000, 21400, 21100, 1},
  {"SW9", SW_BAND_TYPE, 21400, 22800, 21500, 5},
  {"CB ", SW_BAND_TYPE, 26000, 28000, 27500, 1},
  {"10M", SW_BAND_TYPE, 28000, 30000, 28400, 1},
  {"ALL", SW_BAND_TYPE, 150, 30000, 15000, 1}    // All band. LW, MW and SW (from 150kHz to 30MHz)
}; // Super band: 150kHz to 30MHz

const int lastBand = (sizeof band / sizeof(Band)) - 1;
int bandIdx = 0;

uint8_t rssi = 0;
uint8_t stereo = 1;
uint8_t volume = DEFAULT_VOLUME;

// Devices class declarations
Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);
SI4735 rx;

void setup()
{
  rx.setVolume(0);

  // Encoder pins
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);

  pinMode(BANDWIDTH_BUTTON, INPUT_PULLUP);
  pinMode(BAND_BUTTON_UP, INPUT_PULLUP);
  pinMode(BAND_BUTTON_DOWN, INPUT_PULLUP);
  pinMode(VOL_UP, INPUT_PULLUP);
  pinMode(VOL_DOWN, INPUT_PULLUP);
  pinMode(BFO_SWITCH, INPUT_PULLUP);
  pinMode(AGC_SWITCH, INPUT_PULLUP);
  pinMode(STEP_SWITCH, INPUT_PULLUP);
  pinMode(MODE_SWITCH, INPUT_PULLUP);

  oled.begin();
  oled.clear();
  oled.on();



  SMALLFONT
  // SPLASH HERE


  // Encoder interrupt
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

  // Uncomment the lines below if you experience some unstable behaviour. Default values were optimized to make the SSB patch load fast
  rx.setMaxDelayPowerUp(500);      // Time to the external crystal become stable after power up command (default is 10ms).
  rx.setMaxDelaySetFrequency(100); // Time needed to process the next frequency setup (default is 30 ms)

  rx.getDeviceI2CAddress(RESET_PIN); // Looks for the I2C bus address and set it.  Returns 0 if error
  rx.setup(RESET_PIN, FM_BAND_TYPE);

  delay(300);
  // Set up the radio for the current band (see index table variable bandIdx )
  useBand();

  currentFrequency = previousFrequency = rx.getFrequency();

  rx.setVolume(volume);
  oled.clear();
  showStatus();
}

#define kho16x32Digits_SPACE '+'
char bufferFreq[15];
void showFrequencyFM() {
  char tmp[7];
  char freq[8];
  convertToChar(currentFrequency, tmp, 5);


  freq[0] = (tmp[0] == '0') ? kho16x32Digits_SPACE : tmp[0];
  freq[1] = tmp[1];

  if (rx.isCurrentTuneFM())
  {
    freq[2] = tmp[2];
    freq[3] = '.';
    freq[4] = tmp[3];
  }
  //printValue( 0,0, bufferFreq , freq, &kho16x32Digits);

  oled.setFont(&kho16x32Digits);
  uint8_t  fontWidth = kho16x32Digits.width;
  int pix = 0;
  oled.setCursor(pix, 0);
  for (uint8_t  i = 0; i < 5; i++) {
    oled.print(freq[i]);
    pix += fontWidth;
    oled.setCursor(pix, 0);
  }
  SMALLFONT
  oled.setCursor(90, 3);
  oled.print("Mhz");
  oled.setCursor(90, 0);
  oled.print("FM");
}

void showFrequency() {
  if (band[bandIdx].bandType == FM_BAND_TYPE)  {
    showFrequencyFM();
  }
  SMALLFONT
}


/*
    Show some basic information on display
*/
void showStatus()
{

  showFrequency();
  showRSSI();
  showVolume();
}

/* *******************************
   Shows RSSI status
*/
void showRSSI()
{
  char c[2] = ".";

  int bars = ((rssi / 3.0) / 2.0) + 1;

  oled.setCursor(120, 0);

  oled.setCursor(120, 0);

  oled.setFont(&TinyOLED4ksymbolfont8x8);
  //oled.print(rssi);

  if (bars > 8) bars = 8;
  oled.print((char)bars);


  if (currentMode == FM)
  {
    oled.setCursor(120, 3);
    oled.print((rx.getCurrentPilot()) ? (char)9 : (char)0);
  }
  SMALLFONT
}

/*
   Shows the volume level on LCD
*/
void showVolume()
{
  //oled.setCursor(60, 3);
  //oled.print("  ");
  //oled.setCursor(60, 3);
  //oled.print(rx.getCurrentVolume());
}


char *rdsMsg;
char *stationName;
char *rdsTime;
char bufferStatioName[50];
char bufferRdsMsg[100];
char bufferRdsTime[32];


void showRDSStation()
{
  oled.setCursor(80, 1);
  int i = strlen(stationName);
  oled.print(stationName);
  delay(100);
}


void checkRDS()
{
  rx.getRdsStatus();
  if (rx.getRdsReceived())
  {
    if (rx.getRdsSync() && rx.getRdsSyncFound())
    {
      stationName = rx.getRdsText0A();
      if (stationName != NULL)
        showRDSStation();
    }
  }
}

void useBand()
{

  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    currentMode = FM;
    rx.setTuneFrequencyAntennaCapacitor(0);
    rx.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
    rx.setRdsConfig(1, 2, 2, 2, 2);
  }


  delay(100);
  currentFrequency = band[bandIdx].currentFreq;
  currentStep = band[bandIdx].currentStep;
  showStatus();
}

void loop() {
  // Check if the encoder has moved.
  if (encoderCount != 0) {

    (encoderCount == 1) ? rx.frequencyUp() : rx.frequencyDown();
    // Show the current frequency only if it has changed
    currentFrequency = rx.getFrequency();
    showFrequencyFM();
  }
  (encoderCount = 0);


  // Check button commands
  if ((millis() - elapsedButton) > MIN_ELAPSED_TIME)  {
    // check if some button is pressed
    if (digitalRead(BANDWIDTH_BUTTON) == LOW)
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    else if (digitalRead(BAND_BUTTON_UP) == LOW)
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    else if (digitalRead(BAND_BUTTON_DOWN) == LOW)
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    else if (digitalRead(VOL_UP) == LOW)  {
      rx.volumeUp();
      volume = rx.getVolume();
      showVolume();
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(VOL_DOWN) == LOW)  {
      rx.volumeDown();
      volume = rx.getVolume();
      showVolume();
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(BFO_SWITCH) == LOW)  {

      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(AGC_SWITCH) == LOW)  {

    }
    else if (digitalRead(STEP_SWITCH) == LOW)  {

    }
    else if (digitalRead(MODE_SWITCH) == LOW)  {

    }
    elapsedButton = millis();
  }

  // Show RSSI status only if this condition has changed
  if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 9)
  {
    rx.getCurrentReceivedSignalQuality();
    int aux = rx.getCurrentRSSI();
    if (rssi != aux)
    {
      rssi = aux;
      showRSSI();
    }
    elapsedRSSI = millis();
  }

  if (currentMode == FM)  {
    checkRDS();
  }
  delay(10);
}

//////////////////////////////////////////////helper functions///////////////////////////////////////////////

// Use Rotary.h and Rotary.cpp implementation to process encoder via interrupt
void rotaryEncoder() { // rotary encoder events
  uint8_t encoderStatus = encoder.process();
  if (encoderStatus)
  {
    encoderStatus == DIR_CW ? encoderCount = 1 : encoderCount = -1;
  }
}

/**
   Prevents blinking during the frequency display.
   Erases the old digits if it has changed and print the new digit values.
*/
void printValue(int col, int line, char *oldValue, char *newValue, const DCfont *font)
{
  int c = col;
  char *pOld;
  char *pNew;

  //oled.setFont(font);
  //oled.print(*pNew);
  pOld = oldValue;
  pNew = newValue;

  // prints just changed digits
  while (*pOld && *pNew)
  {
    if (*pOld != *pNew)
    {
      // Writes new value
      oled.setCursor(c, line);
      oled.print(*pNew);
    }
    pOld++;
    pNew++;
    c++;
  }

  // Is there anything else to erase?
  while (*pOld)
  {
    oled.setCursor(c, line);
    oled.print(' ');
    pOld++;
    c++;
  }

  // Is there anything else to print?
  while (*pNew)
  {
    oled.setCursor(c, line);
    oled.print(*pNew);
    pNew++;
    c++;
  }

  // Save the current content to be tested next time
  strcpy(oldValue, newValue);
}




/**
  Converts a number to a char string and places leading zeros.
  It is useful to mitigate memory space used by sprintf or generic similar function
*/
void convertToChar(uint16_t value, char *strValue, uint8_t len)
{
  char d;
  for (int i = (len - 1); i >= 0; i--)
  {
    d = value % 10;
    value = value / 10;
    strValue[i] = d + 48;
  }
  strValue[len] = '\0';
}
