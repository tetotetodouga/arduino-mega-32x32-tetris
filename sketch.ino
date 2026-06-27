#include <EEPROM.h>

#define DIN_PIN 51
#define CLK_PIN 52

#define JOY_Y A0
#define JOY_X A1
#define JOY_BTN 2

#define BUZZER_PIN 6

#define SCREEN_W 32
#define SCREEN_H 32

#define GAME_W 10
#define GAME_H 20

#define FIELD_X 1
#define FIELD_Y 9

#define NUM_ROWS 4

const int CS_PINS[NUM_ROWS] = {53, 49, 48, 47};

const bool REVERSE_CHAIN_ORDER = false;
const bool MIRROR_X = false;

bool board[GAME_H][GAME_W];
uint32_t screenRows[SCREEN_H];

int pieceType;
int pieceRot;
int pieceX;
int pieceY;
int nextPiece;

int bag[7];
int bagIndex = 7;

unsigned long lastDropTime = 0;
unsigned long lastInputTime = 0;

int dropInterval = 700;
const int inputCooldown = 115;

bool gameOver = false;
bool buttonPrev = false;
bool upPrev = false;

int score = 0;
int highScore = 0;
int linesCleared = 0;

const byte SHAPES[7][4][4] = {
  // I
  {
    { B0000, B1111, B0000, B0000 },
    { B0010, B0010, B0010, B0010 },
    { B0000, B1111, B0000, B0000 },
    { B0010, B0010, B0010, B0010 }
  },

  // O
  {
    { B0110, B0110, B0000, B0000 },
    { B0110, B0110, B0000, B0000 },
    { B0110, B0110, B0000, B0000 },
    { B0110, B0110, B0000, B0000 }
  },

  // T
  {
    { B0100, B1110, B0000, B0000 },
    { B0100, B0110, B0100, B0000 },
    { B0000, B1110, B0100, B0000 },
    { B0100, B1100, B0100, B0000 }
  },

  // S
  {
    { B0110, B1100, B0000, B0000 },
    { B0100, B0110, B0010, B0000 },
    { B0110, B1100, B0000, B0000 },
    { B0100, B0110, B0010, B0000 }
  },

  // Z
  {
    { B1100, B0110, B0000, B0000 },
    { B0010, B0110, B0100, B0000 },
    { B1100, B0110, B0000, B0000 },
    { B0010, B0110, B0100, B0000 }
  },

  // J
  {
    { B1000, B1110, B0000, B0000 },
    { B0110, B0100, B0100, B0000 },
    { B0000, B1110, B0010, B0000 },
    { B0100, B0100, B1100, B0000 }
  },

  // L
  {
    { B0010, B1110, B0000, B0000 },
    { B0100, B0100, B0110, B0000 },
    { B0000, B1110, B1000, B0000 },
    { B1100, B0100, B0100, B0000 }
  }
};

const byte DIGITS[10][5] = {
  { B111, B101, B101, B101, B111 },
  { B010, B110, B010, B010, B111 },
  { B111, B001, B111, B100, B111 },
  { B111, B001, B111, B001, B111 },
  { B101, B101, B111, B001, B001 },
  { B111, B100, B111, B001, B111 },
  { B111, B100, B111, B101, B111 },
  { B111, B001, B001, B001, B001 },
  { B111, B101, B111, B101, B111 },
  { B111, B101, B111, B001, B111 }
};

const byte LETTER_T[5] = { B111, B010, B010, B010, B010 };
const byte LETTER_E[5] = { B111, B100, B110, B100, B111 };
const byte LETTER_R[5] = { B110, B101, B110, B101, B101 };
const byte LETTER_I[5] = { B111, B010, B010, B010, B111 };
const byte LETTER_S[5] = { B111, B100, B111, B001, B111 };

void sendChainCommand(int csPin, byte address, byte values[4]) {
  digitalWrite(csPin, LOW);

  if (REVERSE_CHAIN_ORDER) {
    for (int m = 0; m < 4; m++) {
      shiftOut(DIN_PIN, CLK_PIN, MSBFIRST, address);
      shiftOut(DIN_PIN, CLK_PIN, MSBFIRST, values[m]);
    }
  } else {
    for (int m = 3; m >= 0; m--) {
      shiftOut(DIN_PIN, CLK_PIN, MSBFIRST, address);
      shiftOut(DIN_PIN, CLK_PIN, MSBFIRST, values[m]);
    }
  }

  digitalWrite(csPin, HIGH);
}

void sendSameToChain(int csPin, byte address, byte value) {
  byte values[4] = { value, value, value, value };
  sendChainCommand(csPin, address, values);
}

void max7219Init() {
  pinMode(DIN_PIN, OUTPUT);
  pinMode(CLK_PIN, OUTPUT);

  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(CS_PINS[i], OUTPUT);
    digitalWrite(CS_PINS[i], HIGH);
  }

  digitalWrite(CLK_PIN, LOW);

  for (int band = 0; band < NUM_ROWS; band++) {
    int cs = CS_PINS[band];

    sendSameToChain(cs, 0x0F, 0x00);
    sendSameToChain(cs, 0x0C, 0x01);
    sendSameToChain(cs, 0x09, 0x00);
    sendSameToChain(cs, 0x0B, 0x07);
    sendSameToChain(cs, 0x0A, 0x08);

    for (byte row = 1; row <= 8; row++) {
      sendSameToChain(cs, row, 0x00);
    }
  }
}

void clearScreenBuffer() {
  for (int y = 0; y < SCREEN_H; y++) {
    screenRows[y] = 0;
  }
}

void setPixel(int x, int y, bool state) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
    return;
  }

  if (MIRROR_X) {
    x = SCREEN_W - 1 - x;
  }

  uint32_t mask = ((uint32_t)1 << (31 - x));

  if (state) {
    screenRows[y] |= mask;
  } else {
    screenRows[y] &= ~mask;
  }
}

void refreshDisplay() {
  for (int band = 0; band < NUM_ROWS; band++) {
    int cs = CS_PINS[band];

    for (int localRow = 0; localRow < 8; localRow++) {
      int y = band * 8 + localRow;
      byte values[4] = {0, 0, 0, 0};

      for (int module = 0; module < 4; module++) {
        byte data = 0;

        for (int col = 0; col < 8; col++) {
          int x = module * 8 + col;
          uint32_t mask = ((uint32_t)1 << (31 - x));

          if (screenRows[y] & mask) {
            data |= (1 << (7 - col));
          }
        }

        values[module] = data;
      }

      sendChainCommand(cs, localRow + 1, values);
    }
  }
}

void clearMatrix() {
  clearScreenBuffer();
  refreshDisplay();
}

void drawRect(int x, int y, int w, int h) {
  for (int i = 0; i < w; i++) {
    setPixel(x + i, y, true);
    setPixel(x + i, y + h - 1, true);
  }

  for (int i = 0; i < h; i++) {
    setPixel(x, y + i, true);
    setPixel(x + w - 1, y + i, true);
  }
}

void drawDigit(int digit, int x, int y) {
  if (digit < 0 || digit > 9) {
    return;
  }

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (DIGITS[digit][row] & (B100 >> col)) {
        setPixel(x + col, y + row, true);
      }
    }
  }
}

void drawDigitMirrored(int digit, int x, int y) {
  if (digit < 0 || digit > 9) {
    return;
  }

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (DIGITS[digit][row] & (B100 >> col)) {
        setPixel(x + (2 - col), y + row, true);
      }
    }
  }
}

void drawNumber(int value, int x, int y, int digits) {
  int div = 1;

  for (int i = 1; i < digits; i++) {
    div *= 10;
  }

  for (int i = 0; i < digits; i++) {
    int d = (value / div) % 10;
    drawDigit(d, x + i * 4, y);
    div /= 10;
  }
}

void drawNumberMirrored(int value, int x, int y, int digits) {
  // Для твоей ориентации матрицы надо зеркалить и цифры, и порядок цифр.
  int div = 1;

  for (int i = 0; i < digits; i++) {
    int d = (value / div) % 10;
    drawDigitMirrored(d, x + i * 4, y);
    div *= 10;
  }
}

void drawLetter(const byte letter[5], int x, int y) {
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (letter[row] & (B100 >> col)) {
        setPixel(x + col, y + row, true);
      }
    }
  }
}

void drawLetterMirrored(const byte letter[5], int x, int y) {
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (letter[row] & (B100 >> col)) {
        setPixel(x + (2 - col), y + row, true);
      }
    }
  }
}

void drawTetrisTitle() {
  int x = 4;
  int y = 1;

  drawLetterMirrored(LETTER_S, x, y);
  x += 4;

  drawLetterMirrored(LETTER_I, x, y);
  x += 4;

  drawLetterMirrored(LETTER_R, x, y);
  x += 4;

  drawLetterMirrored(LETTER_T, x, y);
  x += 4;

  drawLetterMirrored(LETTER_E, x, y);
  x += 4;

  drawLetterMirrored(LETTER_T, x, y);
}

bool shapeCell(int type, int rot, int x, int y) {
  return SHAPES[type][rot][y] & (B1000 >> x);
}

void drawShapeOnScreen(int type, int rot, int sx, int sy) {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (shapeCell(type, rot, x, y)) {
        setPixel(sx + x, sy + y, true);
      }
    }
  }
}

void drawHud() {
  drawTetrisTitle();

  // NEXT figure
  drawRect(15, 9, 10, 8);
  drawShapeOnScreen(nextPiece, 0, 18, 11);

  // SCORE: цифры без рамки, исправлены под зеркальную ориентацию
  drawNumberMirrored(score % 10000, 15, 24, 4);
}

void drawGame() {
  clearScreenBuffer();

  // Field
  drawRect(0, 8, 12, 22);

  for (int y = 0; y < GAME_H; y++) {
    for (int x = 0; x < GAME_W; x++) {
      if (board[y][x]) {
        setPixel(FIELD_X + x, FIELD_Y + y, true);
      }
    }
  }

  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (!shapeCell(pieceType, pieceRot, x, y)) {
        continue;
      }

      int bx = pieceX + x;
      int by = pieceY + y;

      if (bx >= 0 && bx < GAME_W && by >= 0 && by < GAME_H) {
        setPixel(FIELD_X + bx, FIELD_Y + by, true);
      }
    }
  }

  drawHud();
  refreshDisplay();
}

void setup() {
  Serial.begin(9600);

  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  max7219Init();
  randomSeed(analogRead(A5));

  EEPROM.get(0, highScore);

  if (highScore < 0 || highScore > 30000) {
    highScore = 0;
  }

  startupAnimation();
  resetGame();

  Serial.println("Arduino Mega Tetris v3.4 fixed mirrored score started");
}

void loop() {
  readSerialCommands();
  handleButton();

  if (gameOver) {
    return;
  }

  readControls();

  if (millis() - lastDropTime >= (unsigned long)dropInterval) {
    lastDropTime = millis();
    stepDown(false);
  }
}

void readSerialCommands() {
  if (Serial.available()) {
    char c = Serial.read();

    if (c == 'R' || c == 'r') {
      highScore = 0;
      EEPROM.put(0, highScore);
      Serial.println("High score reset");
      printStats();
    }
  }
}

void resetGame() {
  for (int y = 0; y < GAME_H; y++) {
    for (int x = 0; x < GAME_W; x++) {
      board[y][x] = false;
    }
  }

  score = 0;
  linesCleared = 0;
  dropInterval = 700;

  gameOver = false;

  bagIndex = 7;
  refillBag();

  nextPiece = nextPieceFromBag();

  buttonPrev = false;
  upPrev = false;

  lastDropTime = millis();
  lastInputTime = millis();

  spawnPiece();
  drawGame();
  playStartSound();

  Serial.println("New game");
  printStats();
}

void handleButton() {
  bool pressed = digitalRead(JOY_BTN) == LOW;

  if (pressed && !buttonPrev) {
    if (gameOver) {
      delay(200);
      resetGame();
    } else {
      tryRotate();
    }
  }

  buttonPrev = pressed;
}

void readControls() {
  int yValue = analogRead(JOY_Y);
  bool upActive = yValue < 300;

  if (upActive && !upPrev) {
    hardDrop();
  }

  upPrev = upActive;

  if (millis() - lastInputTime < inputCooldown) {
    return;
  }

  int xValue = analogRead(JOY_X);

  if (xValue < 300) {
    tryMove(-1, 0);
    lastInputTime = millis();
  } else if (xValue > 700) {
    tryMove(1, 0);
    lastInputTime = millis();
  } else if (yValue > 700) {
    stepDown(true);
    lastInputTime = millis();
  }
}

void refillBag() {
  for (int i = 0; i < 7; i++) {
    bag[i] = i;
  }

  for (int i = 6; i > 0; i--) {
    int j = random(0, i + 1);

    int temp = bag[i];
    bag[i] = bag[j];
    bag[j] = temp;
  }

  bagIndex = 0;
}

int nextPieceFromBag() {
  if (bagIndex >= 7) {
    refillBag();
  }

  int piece = bag[bagIndex];
  bagIndex++;

  return piece;
}

void spawnPiece() {
  pieceType = nextPiece;
  nextPiece = nextPieceFromBag();

  pieceRot = 0;
  pieceX = 3;
  pieceY = -1;

  if (!canPlace(pieceX, pieceY, pieceType, pieceRot)) {
    triggerGameOver();
  }
}

bool canPlace(int px, int py, int type, int rot) {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (!shapeCell(type, rot, x, y)) {
        continue;
      }

      int bx = px + x;
      int by = py + y;

      if (bx < 0 || bx >= GAME_W) {
        return false;
      }

      if (by >= GAME_H) {
        return false;
      }

      if (by >= 0 && board[by][bx]) {
        return false;
      }
    }
  }

  return true;
}

void tryMove(int dx, int dy) {
  if (canPlace(pieceX + dx, pieceY + dy, pieceType, pieceRot)) {
    pieceX += dx;
    pieceY += dy;
    drawGame();
  }
}

void tryRotate() {
  int newRot = (pieceRot + 1) % 4;

  if (canPlace(pieceX, pieceY, pieceType, newRot)) {
    pieceRot = newRot;
    tone(BUZZER_PIN, 900, 35);
  } else if (canPlace(pieceX - 1, pieceY, pieceType, newRot)) {
    pieceX--;
    pieceRot = newRot;
    tone(BUZZER_PIN, 900, 35);
  } else if (canPlace(pieceX + 1, pieceY, pieceType, newRot)) {
    pieceX++;
    pieceRot = newRot;
    tone(BUZZER_PIN, 900, 35);
  } else if (canPlace(pieceX - 2, pieceY, pieceType, newRot)) {
    pieceX -= 2;
    pieceRot = newRot;
    tone(BUZZER_PIN, 900, 35);
  } else if (canPlace(pieceX + 2, pieceY, pieceType, newRot)) {
    pieceX += 2;
    pieceRot = newRot;
    tone(BUZZER_PIN, 900, 35);
  }

  drawGame();
}

void stepDown(bool userDrop) {
  if (canPlace(pieceX, pieceY + 1, pieceType, pieceRot)) {
    pieceY++;

    if (userDrop) {
      score += 1;
    }
  } else {
    lockPiece();
    clearLines();
    spawnPiece();
  }

  if (!gameOver) {
    drawGame();
  }
}

void hardDrop() {
  if (gameOver) {
    return;
  }

  int distance = 0;

  while (canPlace(pieceX, pieceY + 1, pieceType, pieceRot)) {
    pieceY++;
    distance++;
  }

  score += distance * 2;

  lockPiece();
  clearLines();
  spawnPiece();

  tone(BUZZER_PIN, 300, 60);

  if (!gameOver) {
    drawGame();
  }

  printStats();
}

void lockPiece() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (!shapeCell(pieceType, pieceRot, x, y)) {
        continue;
      }

      int bx = pieceX + x;
      int by = pieceY + y;

      if (bx >= 0 && bx < GAME_W && by >= 0 && by < GAME_H) {
        board[by][bx] = true;
      }
    }
  }
}

void clearLines() {
  int cleared = 0;

  for (int y = GAME_H - 1; y >= 0; y--) {
    bool full = true;

    for (int x = 0; x < GAME_W; x++) {
      if (!board[y][x]) {
        full = false;
        break;
      }
    }

    if (full) {
      cleared++;
      flashLine(y);

      for (int yy = y; yy > 0; yy--) {
        for (int x = 0; x < GAME_W; x++) {
          board[yy][x] = board[yy - 1][x];
        }
      }

      for (int x = 0; x < GAME_W; x++) {
        board[0][x] = false;
      }

      y++;
    }
  }

  if (cleared > 0) {
    linesCleared += cleared;

    int speedLevel = linesCleared / 10;
    dropInterval = 700 - speedLevel * 45;

    if (dropInterval < 120) {
      dropInterval = 120;
    }

    score += cleared * cleared * 10;
    playLineClearSound(cleared);
    printStats();
  }
}

void flashLine(int lineY) {
  int sy = FIELD_Y + lineY;

  for (int i = 0; i < 2; i++) {
    drawGame();

    for (int x = 0; x < GAME_W; x++) {
      setPixel(FIELD_X + x, sy, false);
    }

    refreshDisplay();
    delay(65);

    for (int x = 0; x < GAME_W; x++) {
      setPixel(FIELD_X + x, sy, true);
    }

    refreshDisplay();
    delay(65);
  }
}

void startupAnimation() {
  clearScreenBuffer();

  drawTetrisTitle();
  refreshDisplay();
  delay(600);

  clearMatrix();
  delay(120);
}

void triggerGameOver() {
  gameOver = true;

  if (score > highScore) {
    highScore = score;
    EEPROM.put(0, highScore);
    Serial.println("New high score saved");
  }

  Serial.println("Game over");
  printStats();

  playGameOverSound();

  for (int i = 0; i < 4; i++) {
    clearScreenBuffer();

    for (int p = 5; p < 27; p++) {
      setPixel(p, p, true);
      setPixel(31 - p, p, true);
    }

    refreshDisplay();
    delay(160);

    clearMatrix();
    delay(160);
  }

  Serial.println("Press joystick button to restart");
}

void playStartSound() {
  tone(BUZZER_PIN, 660, 70);
  delay(80);
  tone(BUZZER_PIN, 880, 90);
}

void playLineClearSound(int count) {
  for (int i = 0; i < count; i++) {
    tone(BUZZER_PIN, 900 + i * 120, 60);
    delay(75);
  }
}

void playGameOverSound() {
  tone(BUZZER_PIN, 500, 100);
  delay(130);
  tone(BUZZER_PIN, 350, 120);
  delay(150);
  tone(BUZZER_PIN, 220, 180);
}

void printStats() {
  Serial.print("Score: ");
  Serial.print(score);

  Serial.print(" | High score: ");
  Serial.print(highScore);

  Serial.print(" | Lines: ");
  Serial.print(linesCleared);

  Serial.print(" | Drop: ");
  Serial.print(dropInterval);

  Serial.println(" ms");
}