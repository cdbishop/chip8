#pragma once
#pragma once
#include <string>

struct chip8 {
  unsigned short opcode;

  /*
    0x000 - 0x1FF - Chip 8 interpreter(contains font set in emu)
    0x050 - 0x0A0 - Used for the built in 4x5 pixel font set(0 - F)
    0x200 - 0xFFF - Program ROM and work RAM
  */
  unsigned char memory[4096];
  unsigned char registers[16];

  unsigned short index;
  unsigned short pc;

  unsigned char gfx[64 * 32];

  unsigned char delay_timer;
  unsigned char sound_timer;

  unsigned short stack[16];
  unsigned short sp;

  unsigned char key[16];
};

void chip8Initialize(chip8& cpu);
void chip8LoadGame(chip8& cpu, const std::string& file);
void chip8Cycle(chip8& cpu);
