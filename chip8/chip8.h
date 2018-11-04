#pragma once
#pragma once
#include <string>

static const unsigned short MaxMemory = 4096;
static const unsigned char NumRegisters = 16;
static const unsigned char GfxDisplayWidth = 64;
static const unsigned char GfxDisplayHeight = 32;
static const unsigned short GfxDisplaySize = GfxDisplayWidth * GfxDisplayHeight;
static const unsigned char MaxStackSize = 16;
static const unsigned char MaxNumKeys = 16;

struct chip8 {
  unsigned short opcode;

  /*
    0x000 - 0x1FF - Chip 8 interpreter(contains font set in emu)
    0x050 - 0x0A0 - Used for the built in 4x5 pixel font set(0 - F)
    0x200 - 0xFFF - Program ROM and work RAM
  */
  unsigned char memory[MaxMemory];
  unsigned char registers[NumRegisters];

  unsigned short index;
  unsigned short pc;

  unsigned char gfx[GfxDisplaySize];

  unsigned char delay_timer;
  unsigned char sound_timer;

  unsigned short stack[MaxStackSize];
  unsigned short sp;

  unsigned char key[MaxNumKeys];
};

void chip8Initialize(chip8& cpu);
void chip8LoadRom(chip8& cpu, const std::string& file);
void chip8Cycle(chip8& cpu);

void chip8test(chip8& cpu);
void chip8testRender(chip8& cpu);
