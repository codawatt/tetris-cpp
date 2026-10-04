/*
 * Linux port of "OneLoneCoder.com - Command Line Tetris"
 * Original work Copyright (C) 2018 Javidx9
 * Original source: https://github.com/OneLoneCoder/Javidx9/blob/master/SimplyCode/OneLoneCoder_Tetris.cpp
 * Modified 2026: ported Windows console/input/output code to Linux/POSIX terminal handling and added ANSI 256-color rendering.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <iostream>
#include <thread>
#include <unordered_map> // dictionary
#include <chrono>
#include <vector>
#include <utility> // NOTE do we actually need it?

#include "linux_console.hpp"
#include <cstdlib>
#include <cwchar>
//#include <stdio.h>

int nScreenWidth = 80;  // Console Screen Size X (columns)
int nScreenHeight = 30; // Console Screen Size Y (rows)
std::wstring tetromino[7];
int nFieldWidth = 12;
int nFieldHeight = 18;
unsigned char *pField = nullptr;

// ============================================================
// Tetromino Colors
//
// ANSI 256-color palette values.
// Change these values to customize the pieces.
//
// https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit
// ============================================================
std::unordered_map<int, unsigned char> TETROMINO_COLORS = {
    {0,  51}, // I - bright cyan
    {1, 201}, // T - bright purple
    {2, 226}, // O - bright yellow
    {3,  46}, // S - bright green
    {4, 196}, // Z - bright red
    {5, 208}, // L - orange
    {6,  33}, // J - bright blue
};
int Rotate(int px, int py, int r) {
  int pi = 0;
  switch (r % 4) {
  case 0:             // 0 degrees			// 0  1  2  3
    pi = py * 4 + px; // 4  5  6  7
    break;            // 8  9 10 11
                      // 12 13 14 15

  case 1:                    // 90 degrees			//12  8  4  0
    pi = 12 + py - (px * 4); // 13  9  5  1
    break;                   // 14 10  6  2
    // 15 11  7  3

  case 2:                    // 180 degrees			//15 14 13 12
    pi = 15 - (py * 4) - px; // 11 10  9  8
    break;                   // 7  6  5  4
                             // 3  2  1  0

  case 3:                   // 270 degrees			// 3  7 11 15
    pi = 3 - py + (px * 4); // 2  6 10 14
    break;                  // 1  5  9 13
  } // 0  4  8 12

  return pi;
}

bool DoesPieceFit(int nTetromino, int nRotation, int nPosX, int nPosY) {
  // All Field cells >0 are occupied
  for (int px = 0; px < 4; px++)
    for (int py = 0; py < 4; py++) {
      // Get index into piece
      int pi = Rotate(px, py, nRotation);

      // Get index into field
      int fi = (nPosY + py) * nFieldWidth + (nPosX + px);

      // Check that test is in bounds. Note out of bounds does
      // not necessarily mean a fail, as the long vertical piece
      // can have cells that lie outside the boundary, so we'll
      // just ignore them
      if (nPosX + px >= 0 && nPosX + px < nFieldWidth) {
        if (nPosY + py >= 0 && nPosY + py < nFieldHeight) {
          // In Bounds so do collision check
          if (tetromino[nTetromino][pi] != L'.' && pField[fi] != 0)
            return false; // fail on first hit
        }
      }
    }

  return true;
}

int main() {
  // Create Screen Buffer
  wchar_t *screen = new wchar_t[nScreenWidth * nScreenHeight];
  for (int i = 0; i < nScreenWidth * nScreenHeight; i++)
    screen[i] = L' ';
  linux_console::Console console(nScreenWidth, nScreenHeight);
  if (!console) {
    delete[] screen;
    return 1;
  }


  unsigned char *screenColor = new unsigned char[nScreenWidth * nScreenHeight]{};

  tetromino[0].append(L"..X...X...X...X."); // Tetronimos 4x4
  tetromino[1].append(L"..X..XX...X.....");
  tetromino[2].append(L".....XX..XX.....");
  tetromino[3].append(L"..X..XX..X......");
  tetromino[4].append(L".X...XX...X.....");
  tetromino[5].append(L".X...X...XX.....");
  tetromino[6].append(L"..X...X..XX.....");

  pField =
      new unsigned char[nFieldWidth * nFieldHeight]; // Create play field buffer
  for (int x = 0; x < nFieldWidth; x++)              // Board Boundary
    for (int y = 0; y < nFieldHeight; y++)
      pField[y * nFieldWidth + x] =
          (x == 0 || x == nFieldWidth - 1 || y == nFieldHeight - 1) ? 9 : 0;

  // Game Logic
  bool bKey[4];
  int nCurrentPiece = 0;
  int nCurrentRotation = 0;
  int nCurrentX = nFieldWidth / 2;
  int nCurrentY = 0;
  int nSpeed = 20;
  int nSpeedCount = 0;
  bool bForceDown = false;
  bool bRotateHold = true;
  bool bHardDropHold = true;
  bool bHoldKey = true;
  bool bHoldUsed = false;
  int nHeldPiece = -1;
  int nPieceCount = 0;
  int nScore = 0;
  std::vector<int> vLines;
  bool bGameOver = false;

  while (!bGameOver && !linux_console::interrupted) // Main Loop
  {
    // Timing =======================
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Small Step = 1 Game Tick
    nSpeedCount++;
    bForceDown = (nSpeedCount == nSpeed);

    // Input ========================
    console.poll();
    if (console.down('q'))
      break;
    bKey[0] = console.down(linux_console::Key::Right);
    bKey[1] = console.down(linux_console::Key::Left);
    bKey[2] = console.down(linux_console::Key::Down);
    bKey[3] = console.down('z');

    // Hard drop
    if (console.down(' ')) {
      if (bHardDropHold) {
        while(DoesPieceFit(nCurrentPiece, nCurrentRotation, nCurrentX, nCurrentY +1))
          nCurrentY++;
        bForceDown = true;
      }
      bHardDropHold = false;
    } else {
      bHardDropHold = true;
    }
    // Hold / swap piece - once per current tetromino
    if (console.down('c')) {
      if (bHoldKey && !bHoldUsed) {
        if (nHeldPiece == -1) {
          nHeldPiece = nCurrentPiece;
          nCurrentPiece = std::rand() % 7;
          // This is a new piece, so allow one swap with the piece
          // we just placed into HOLD.
          bHoldUsed = false;
        } else {
          std::swap(nCurrentPiece, nHeldPiece);
          bHoldUsed = true;
        }

        nCurrentX = nFieldWidth / 2;
        nCurrentY = 0;
        nCurrentRotation = 0;

        if (!DoesPieceFit(nCurrentPiece, nCurrentRotation,
                          nCurrentX, nCurrentY))
          bGameOver = true;
      }
      bHoldKey = false;
    } else {
      bHoldKey = true;
    }

    // Game Logic ===================

    // Handle player movement
    nCurrentX += (bKey[0] && DoesPieceFit(nCurrentPiece, nCurrentRotation,
                                          nCurrentX + 1, nCurrentY))
                     ? 1
                     : 0;
    nCurrentX -= (bKey[1] && DoesPieceFit(nCurrentPiece, nCurrentRotation,
                                          nCurrentX - 1, nCurrentY))
                     ? 1
                     : 0;
    nCurrentY += (bKey[2] && DoesPieceFit(nCurrentPiece, nCurrentRotation,
                                          nCurrentX, nCurrentY + 1))
                     ? 1
                     : 0;

    // Rotate, but latch to stop wild spinning
    if (bKey[3]) {
      nCurrentRotation +=
          (bRotateHold && DoesPieceFit(nCurrentPiece, nCurrentRotation + 1,
                                       nCurrentX, nCurrentY))
              ? 1
              : 0;
      bRotateHold = false;
    } else
      bRotateHold = true;

    // Force the piece down the playfield if it's time
    if (bForceDown) {
      // Update difficulty every 50 pieces
      nSpeedCount = 0;
      nPieceCount++;
      if (nPieceCount % 50 == 0)
        if (nSpeed >= 10)
          nSpeed--;

      // Test if piece can be moved down
      if (DoesPieceFit(nCurrentPiece, nCurrentRotation, nCurrentX,
                       nCurrentY + 1))
        nCurrentY++; // It can, so do it!
      else {
        // It can't! Lock the piece in place
        for (int px = 0; px < 4; px++)
          for (int py = 0; py < 4; py++)
            if (tetromino[nCurrentPiece][Rotate(px, py, nCurrentRotation)] !=
                L'.')
              pField[(nCurrentY + py) * nFieldWidth + (nCurrentX + px)] =
                  nCurrentPiece + 1;

        // Check for lines
        for (int py = 0; py < 4; py++)
          if (nCurrentY + py < nFieldHeight - 1) {
            bool bLine = true;
            for (int px = 1; px < nFieldWidth - 1; px++)
              bLine &= (pField[(nCurrentY + py) * nFieldWidth + px]) != 0;

            if (bLine) {
              // Remove Line, set to =
              for (int px = 1; px < nFieldWidth - 1; px++)
                pField[(nCurrentY + py) * nFieldWidth + px] = 8;
              vLines.push_back(nCurrentY + py);
            }
          }

        nScore += 25;
        if (!vLines.empty())
          nScore += (1 << vLines.size()) * 100;

        // Pick New Piece
        nCurrentX = nFieldWidth / 2;
        nCurrentY = 0;
        nCurrentRotation = 0;
        nCurrentPiece = std::rand() % 7;
        bHoldUsed = false;

        // If piece does not fit straight away, game over!
        bGameOver = !DoesPieceFit(nCurrentPiece, nCurrentRotation, nCurrentX,
                                  nCurrentY);
      }
    }

    // Display ======================
    // Reset all cells to the terminal's default color
   for (int i = 0; i < nScreenWidth * nScreenHeight; i++)
     screenColor[i] = 0;

    // Draw Field
    for (int x = 0; x < nFieldWidth; x++)
      for (int y = 0; y < nFieldHeight; y++) {
        int fieldIndex = y * nFieldWidth + x;
        int screenIndex = (y + 2) * nScreenWidth + (x + 2);

        unsigned char cell = pField[fieldIndex];

        screen[screenIndex] =
            L" ABCDEFG=#"[cell];

        // Field values 1..7 correspond to tetrominoes 0..6
        if (cell >= 1 && cell <= 7)
          screenColor[screenIndex] =
              TETROMINO_COLORS[cell - 1];
      }

    // Draw Current Piece
    for (int px = 0; px < 4; px++)
      for (int py = 0; py < 4; py++)
        if (tetromino[nCurrentPiece][Rotate(px, py, nCurrentRotation)] != L'.') {
          int screenIndex =
              (nCurrentY + py + 2) * nScreenWidth +
              (nCurrentX + px + 2);

          screen[screenIndex] = nCurrentPiece + 65;
          screenColor[screenIndex] =
              TETROMINO_COLORS[nCurrentPiece];
        }

    // Draw Score
    std::swprintf(&screen[2 * nScreenWidth + nFieldWidth + 6], 16, L"SCORE: %8d",
             nScore);
    // Draw held piece
    const int holdX = nFieldWidth + 6;
    const int holdY = 5;
    std::swprintf(&screen[4 * nScreenWidth + holdX], 8, L"HOLD:");
    
    // Clear previous HOLD preview
    for (int px = 0; px < 4; px++)
      for (int py = 0; py < 4; py++) {
        int screenIndex =
            (holdY + py) * nScreenWidth + (holdX + px);
        screen[screenIndex] = L' ';
        screenColor[screenIndex] = 0;
      }

    if (nHeldPiece != -1) {
      for (int px = 0; px < 4; px++)
        for (int py = 0; py < 4; py++)
          if (tetromino[nHeldPiece][Rotate(px, py, 0)] != L'.') {
            int screenIndex =
                (holdY + py) * nScreenWidth + (holdX + px);
            screen[screenIndex] = nHeldPiece + 65;
            screenColor[screenIndex] = TETROMINO_COLORS[nHeldPiece];
          }
    }
    // Animate Line Completion
    if (!vLines.empty()) {
      // Display Frame (cheekily to draw lines)
      console.draw(screen, screenColor);
      std::this_thread::sleep_for(std::chrono::milliseconds(400)); // Delay a bit

      for (auto &v : vLines)
        for (int px = 1; px < nFieldWidth - 1; px++) {
          for (int py = v; py > 0; py--)
            pField[py * nFieldWidth + px] = pField[(py - 1) * nFieldWidth + px];
          pField[px] = 0;
        }

      vLines.clear();
    }

    // Display Frame
    console.draw(screen, screenColor);
  }

  // Oh Dear
  delete[] screen;
  delete[] screenColor;
  delete[] pField;
  console.close();
  if (bGameOver)
    std::cout << "Game Over!! Score:" << nScore << std::endl;
  return 0;
}
