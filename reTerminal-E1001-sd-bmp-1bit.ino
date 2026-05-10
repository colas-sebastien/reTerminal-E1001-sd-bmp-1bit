#include <SD.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_7C.h>
#include <cmath>

// === Pin Definitions ===
// ePaper Display
#define EPD_SCK_PIN  7
#define EPD_MOSI_PIN 9
#define EPD_CS_PIN   10
#define EPD_DC_PIN   11
#define EPD_RES_PIN  12
#define EPD_BUSY_PIN 13

// SD Card
#define SD_EN_PIN   16
#define SD_DET_PIN  15
#define SD_CS_PIN   14
#define SD_MISO_PIN 8

// Serial Port
#define SERIAL_RX 44
#define SERIAL_TX 43

// File to display
const char* BMP_FILENAME = "/ok.bmp";

// === ePaper Driver Selection ===
// reTerminal E1001 (7.5'' B&W)
#define GxEPD2_DISPLAY_CLASS GxEPD2_BW
#define GxEPD2_DRIVER_CLASS GxEPD2_750_GDEY075T7

// For displays with RAM limitations
#define MAX_DISPLAY_BUFFER_SIZE 16000
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

// === Global Objects ===
SPIClass hspi(HSPI);

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
    display(GxEPD2_DRIVER_CLASS(/*CS=*/EPD_CS_PIN, /*DC=*/EPD_DC_PIN, /*RST=*/EPD_RES_PIN, /*BUSY=*/EPD_BUSY_PIN));

// === BMP Drawing Function ===
// Helper functions to read values from the BMP file
uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

// This function reads a BMP file and draws it to the screen.
// It includes robust error checking and a color-matching algorithm.
void drawBmp(const char *filename, int16_t x, int16_t y) {
  File bmpFile;
  int32_t bmpWidth, bmpHeight;
  uint16_t bmpDepth;
  uint32_t bmpImageoffset;
  bool flip = true;

  if ((x >= display.width()) || (y >= display.height())) return;

  Serial1.print("Loading image '");
  Serial1.print(filename);
  Serial1.println("'");

  bmpFile = SD.open(filename, FILE_READ);
  if (!bmpFile) {
    Serial1.println("File not found");
    return;
  }

  if (read16(bmpFile) != 0x4D42) {
    Serial1.println("Not a valid BMP file");
    bmpFile.close();
    return;
  }

  read32(bmpFile);
  read32(bmpFile);
  bmpImageoffset = read32(bmpFile);
  read32(bmpFile);
  bmpWidth = read32(bmpFile);
  bmpHeight = read32(bmpFile);
  
  if (read16(bmpFile) != 1) {
    Serial1.println("Unsupported BMP format (planes)");
    bmpFile.close();
    return;
  }
  
  bmpDepth = read16(bmpFile);
  uint32_t compression = read32(bmpFile);

  if (compression != 0) {
    if (compression == 3) {
      Serial1.println("Error: BMP file uses BI_BITFIELDS compression.");
      Serial1.println("This example only supports uncompressed BMPs.");
      Serial1.println("Please re-save the image with standard R8G8B8 (24-bit) or A8R8G8B8 (32-bit) format.");
    } else {
      Serial1.printf("Unsupported BMP format. Depth: %d, Compression: %d\n", bmpDepth, compression);
    }
    bmpFile.close();
    return;
  }

  if (bmpDepth != 1) {
      Serial1.printf("Unsupported BMP bit depth: %d. Only 1 bit is supported.\n", bmpDepth);
      bmpFile.close();
      return;
  }

  if (bmpHeight < 0) {
    bmpHeight = -bmpHeight;
    flip = false;
  }

  Serial1.printf("Image: %d x %d, %d-bit\n", bmpWidth, bmpHeight, bmpDepth);

  display.setPartialWindow(x, y, bmpWidth, bmpHeight);

  uint32_t rowSize = (ceil((float)bmpDepth*bmpWidth/32))*4;
  uint8_t sdbuffer[rowSize];
  uint8_t byte;
  uint8_t mask;
  
  display.firstPage();
  do {
        uint32_t rowpos = bmpImageoffset;
        for (int16_t row = 0; row < bmpHeight; row++) {                    
          bmpFile.seek(rowpos);
          bmpFile.read(sdbuffer, rowSize);
          for (int16_t col=0; col < bmpWidth; col++)
          {
            if (col%8==0)
            {
              byte = sdbuffer[col/8];
              mask=0b10000000;
            }
            if (byte&mask)
            {
              display.drawPixel(x+col,bmpHeight-(y+row+1),GxEPD_BLACK);
            }
            
            mask=mask>>1;
          }
          rowpos+=rowSize;
        }
  } while (display.nextPage());
  bmpFile.close();
  Serial1.println("Done!");
}


void setup() {
  Serial1.begin(115200, SERIAL_8N1, SERIAL_RX, SERIAL_TX);
  while (!Serial1) delay(10);
  delay(2000); // A small delay to allow Serial Monitor to connect
  Serial1.println("--- ePaper SD Card BMP Example ---");
  Serial1.println("---  Color on 1 bit for E1001  ---");

  // Initialize shared SPI bus
  hspi.begin(EPD_SCK_PIN, SD_MISO_PIN, EPD_MOSI_PIN, -1);

  // Initialize Display
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200);
  display.setRotation(0);
  display.fillScreen(GxEPD_WHITE);
  display.hibernate(); // Power down display until needed

  // Initialize SD Card
  pinMode(SD_EN_PIN, OUTPUT);
  digitalWrite(SD_EN_PIN, HIGH);
  pinMode(SD_DET_PIN, INPUT_PULLUP);
  delay(100);

  if (digitalRead(SD_DET_PIN) == HIGH) {
    Serial1.println("No SD card detected. Please insert a card.");
    display.firstPage();
    do {
      display.setCursor(10, 20);
      display.print("No SD card detected.");
    } while(display.nextPage());
    return;
  }

  Serial1.println("SD card detected, attempting to mount...");
  if (!SD.begin(SD_CS_PIN, hspi)) {
    Serial1.println("SD Card Mount Failed!");
    display.firstPage();
    do {
      display.setCursor(10, 20);
      display.print("SD Card Mount Failed!");
    } while(display.nextPage());
    return;
  }
  Serial1.println("SD card mounted successfully.");

  // Draw the BMP from the SD card
  drawBmp(BMP_FILENAME, 0, 0);

  display.hibernate(); // Power down display after drawing
}

void loop() {
  // Nothing to do here for this example
}