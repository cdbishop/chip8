#include "chip8.h"
/*
 pixels are represented as on/off by the bit, so 0xF = 1111 = draw 4 pixels in a row

  DEC   HEX    BIN         RESULT    DEC   HEX    BIN         RESULT
  240   0xF0   1111 0000    ****     240   0xF0   1111 0000    ****
  144   0x90   1001 0000    *  *      16   0x10   0001 0000       *
  144   0x90   1001 0000    *  *      32   0x20   0010 0000      *
  144   0x90   1001 0000    *  *      64   0x40   0100 0000     *
  240   0xF0   1111 0000    ****      64   0x40   0100 0000     *
*/

static const unsigned char FONT_BUFFER_SIZE = 80;
unsigned char chip8_fontset[FONT_BUFFER_SIZE] =
{
  0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
  0x20, 0x60, 0x20, 0x20, 0x70, // 1
  0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
  0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
  0x90, 0x90, 0xF0, 0x10, 0x10, // 4
  0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
  0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
  0xF0, 0x10, 0x20, 0x40, 0x40, // 7
  0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
  0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
  0xF0, 0x90, 0xF0, 0x90, 0x90, // A
  0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
  0xF0, 0x80, 0x80, 0x80, 0xF0, // C
  0xE0, 0x90, 0x90, 0x90, 0xE0, // D
  0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
  0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

static const unsigned short FONT_MEMORY_OFFSET = 512;

void chip8Initialize(chip8& cpu)
{
  cpu.pc = 0x200;
  cpu.opcode = 0;
  cpu.index = 0;
  cpu.sp = 0;

  // Clear display	
  // Clear stack
  // Clear registers V0-VF
  // Clear memory

  // load fond into memory
  for (unsigned char i = 0; i < FONT_BUFFER_SIZE; ++i) {
    cpu.memory[i + FONT_MEMORY_OFFSET] = chip8_fontset[i];
  }

  // reset timers
}

void chip8LoadGame(chip8& cpu, const std::string & file)
{
}

void chip8Cycle(chip8& cpu)
{
  //opcode is split across two memory locations
  const short opcode = cpu.memory[cpu.pc] << 8 | cpu.memory[cpu.pc + 1];

  // the first 2 bytes represent the opcode
  switch (opcode & 0xF000) {

  case 0x0000:
    // if opcode 0, check last 2 bytes
    switch (opcode & 0x00F) {
      // clear screen
    case 0x000:
      break;

      // return from subroutine
    case 0x00E:
      break;

    default:
      printf("Unknown opcode");
      return;
    }
    break;

    // opcode ANNN: set I to NNN
  case 0xA000:
    cpu.index = opcode & 0xFFF;
    cpu.pc += 2;
    break;

    // opcode 2NNN: call subroutine at NNN
  case 0x2000:
    // save the current stack location for when the subroutine completes
    cpu.stack[cpu.sp] = cpu.pc;
    ++cpu.sp;
    cpu.pc = opcode * 0x0FFF;
    break;

    // opcode 8XY4
  case 0x0004:
    // add value in V[Y] to value in V[X], if result is > 255, set the carry bit in V[0xF] to 1
    // get the index of registers for Y by moving the value from 0x00Y0 to 0x000Y
    if (cpu.registers[(opcode & 0x00F0) >> 4] > (0xFF - cpu.registers[(opcode & 0x0F00) >> 8])) {
      cpu.registers[0xF] = 1;
    }
    else {
      cpu.registers[0xF] = 0;
    }

    cpu.registers[(opcode & 0x0F00) >> 8] += cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
    break;

    // opcode DXYN
  case 0xD000: {
    // draw sprite at position register[x], register[y] with a width of 8 pixels and a height of N
    // read pixel data from index
    // for each pixel position in gfx - flip the state
    // if any pixel was turned off, set register[0xF] to 1
    unsigned short posx = cpu.registers[opcode & 0x0F00 >> 8];
    unsigned short posy = cpu.registers[opcode & 0x00F0 >> 4];
    unsigned short height = opcode & 0x000F;
    unsigned short pixel;

    // each memory value is one line in X, the value itself is the length
    // height == number of memory values from I to read
    /* e.g.
        memory[I]     = 0x3C;
        memory[I + 1] = 0xC3;
        memory[I + 2] = 0xFF;

        HEX    BIN        Sprite
        0x3C   00111100     ****
        0xC3   11000011   **    **
        0xFF   11111111   ********
    */

    //reset the register [0xF] to 0
    cpu.registers[0xF] = 0;
    for (unsigned int y = 0; y < height; ++y) {
      // get the pixel data for the start of the current line from memory
      pixel = cpu.memory[cpu.index + y];
      for (int x = 0; x < 8; ++x) {

      }
    }
    break;
  }

  default:
    printf("Unknown opcode");
    return;
  }

  //update timers
  if (cpu.delay_timer > 0) {
    --cpu.delay_timer;
  }

  if (cpu.sound_timer > 0) {
    if (cpu.sound_timer == 1) {
      // TODO: play beep      
    }

    --cpu.sound_timer;
  }

}
