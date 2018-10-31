#include "chip8.h"

#include <fstream>
#include <vector>
#include <iostream>

#include <string>
#include <windows.h>

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
static const unsigned short PROGRAM_OFFSET = FONT_MEMORY_OFFSET + FONT_BUFFER_SIZE;

namespace impl {

  // opcode ANNN: set I to NNN
  void opcode00E0_ClearScreen(chip8& cpu) {
    printf("opcode: 00E0 - ClearScreen: Not implemented!");
  }

  void opcode00EE_SubroutineReturn(chip8& cpu) {
    //grab the saved address (where we wish to return) from the stack
    cpu.pc = cpu.stack[cpu.sp];
    // reset the value
    cpu.stack[cpu.sp] = 0;
    // reduce the stack pointer
    --cpu.sp;
  }

  void opcodeANNN_SetIndex(chip8& cpu, unsigned short opcode) {
    cpu.index = opcode & 0xFFF;
    cpu.pc += 2;
  }

  // opcode 1NNN: goto NNN;
  void opcode1NNN_Goto(chip8 cpu, unsigned short opcode) {
    printf("opcode: 1NNN - Goto: Not implemented!");
  }

  // opcode 2NNN: call subroutine at NNN
  void opcode2NNN_Subroutine(chip8& cpu, unsigned short opcode) {
    // save the current stack location for when the subroutine completes
    cpu.stack[cpu.sp] = cpu.pc;
    ++cpu.sp;
    cpu.pc = opcode * 0x0FFF;
  }

  // opcode 3XNN: if(register[x] == NN skip next instruction
  void opcode3XNN_BranchIfEqToVal(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] == opcode & 0x00FF) {
      cpu.pc += 2;
    }
    else {
      ++cpu.pc;
    }
  }

  // opcode 4XNN: if(register[x] != NN skip next instruction
  void opcode4XNN_BranchIfNEq(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00 >> 8)] != opcode & 0x00FF) {
      cpu.pc += 2;
    }
    else {
      ++cpu.pc;
    }
  }

  // opcode 5XY0: skip next instruction if register[x] == register[y]
  void opcode5XYN_BranchIfEqReg(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[opcode & 0x0F00 >> 8] == cpu.registers[opcode & 0x0F00]) {
      cpu.pc += 2;
    }
    else {
      ++cpu.pc;
    }
  }

  // opcode 6XNN: set the register[x] to NN
  void opcode6XNN_SetReg(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = (opcode & 0x00FF);
    cpu.pc += 2;
  }

  // opcode 7XNN: register[x] += NN - carry flag is not changed
  void opcode7XNN_AddRegNoCarry(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] += (opcode & 0x00FF);
    cpu.pc += 2;
  }

  // opcode 8XY0: register[x] = register[y]
  void opcode8XY0_SetReg(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[opcode & 0x00F0 >> 4];
    cpu.pc += 2;
  }


  // opcode 8XY1: Sets register[x] to register[x] | register[y]
  void opcode8XY1_RegisterOrEq(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x0F00) >> 8] | cpu.registers[opcode & 0x00F0 >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY2: Sets the register[x] to register[x] & register[y]
  void opcode8XY2_RegisterAndEq(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x0F00) >> 8] & cpu.registers[opcode & 0x00F0 >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY3: Sets the register[x] to register[x] ^ register[y]
  void opcode8XY3_RegisterXorEq(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x0F00) >> 8] ^ cpu.registers[opcode & 0x00F0 >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY4: add register[y] to register[x], store in register[x]
  void opcode8XY4_AddRegCarry(chip8& cpu, unsigned short opcode) {
    // if value in Y is > max value - value in X, then it will overflow
    if (cpu.registers[(opcode & 0x00F0) >> 4] > (0xFF - cpu.registers[(opcode & 0x0F00) >> 8])) {
      // set carry
      cpu.registers[0xF] = 1;
    }
    else {
      //clear carry
      cpu.registers[0xF] = 1;
    }

    //do the addition
    cpu.registers[(opcode & 0x0F00) >> 8] += cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY5: add subtract register[y] from from register[x] (register[x] -= from register[y]), set register[0xF] to 0 if theres a borrow (e.g. register[y] > register[x])
  void opcode8XY5_SubRegCarry(chip8& cpu, unsigned short opcode) {
    //carry = 0 if borrow, e.g. register[y] > register[x]
    if (cpu.registers[(opcode & 0x00F0) >> 4] > cpu.registers[(opcode & 0x0F00) >> 8]) {
      cpu.registers[0xF] = 0;
    }
    else {
      cpu.registers[0xF] = 1;
    }

    cpu.registers[(opcode & 0x0F00) >> 8] -= cpu.registers[(opcode & 0x00F0) >> 4];
  }
}

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

  // load font into memory
  for (unsigned char i = 0; i < FONT_BUFFER_SIZE; ++i) {
    cpu.memory[i + FONT_MEMORY_OFFSET] = chip8_fontset[i];
  }

  // reset timers
}

void chip8LoadGame(chip8& cpu, const std::string & file)
{
  std::ifstream input;
  input.open(file, std::ios::binary | std::ios::ate);
  std::ifstream::pos_type len = input.tellg();

  if (len > 0x200) {
    throw std::runtime_error("Program too large");
  }

  input.seekg(0, std::ios::beg);  

  //unsigned char* buffer = new unsigned char[len];
  //input.read((char*)buffer, len);
  input.read(reinterpret_cast<char*>(&cpu.memory[PROGRAM_OFFSET]), len);
  std::streamsize read = input.gcount();
  input.close();
}

void chip8Cycle(chip8& cpu)
{
  //opcode is split across two memory locations
  const unsigned short opcode = cpu.memory[cpu.pc] << 8 | cpu.memory[cpu.pc + 1];

  // the first 2 bytes represent the opcode
  switch (opcode & 0xF000) {
  case 0x0000:
    // if opcode 0, check last 2 bytes
    switch (opcode & 0x00F) {      
    case 0x000:
      impl::opcode00E0_ClearScreen(cpu);
      break;
      
    case 0x00E:
      impl::opcode00EE_SubroutineReturn(cpu);
      break;

    default:
      printf("Unknown opcode");
      return;
    }
    break;

  case 0xA000:    
    impl::opcodeANNN_SetIndex(cpu, opcode);
    break;

  case 0x1000:
    impl::opcode1NNN_Goto(cpu, opcode);
    break;
    
  case 0x2000:
    impl::opcode2NNN_Subroutine(cpu, opcode);
    break;

  case 0x3000:
    impl::opcode3XNN_BranchIfEqToVal(cpu, opcode);
    break;
        
  case 0x4000:
    impl::opcode4XNN_BranchIfNEq(cpu, opcode);
    break;
    
  case 0x5000:
    impl::opcode5XYN_BranchIfEqReg(cpu, opcode);    
    break;

  case 0x6000:
    impl::opcode6XNN_SetReg(cpu, opcode);
    break;
        
  case 0x7000:    
    impl::opcode7XNN_AddRegNoCarry(cpu, opcode);
    break;

  case 0x8000:
    switch (opcode & 0x000F) {      
    case 0x0000:      
      impl::opcode8XY0_SetReg(cpu, opcode);
      break;

    case 0x0001:
      impl::opcode8XY1_RegisterOrEq(cpu, opcode);
      break;

    case 0x0002:
      impl::opcode8XY2_RegisterAndEq(cpu, opcode);
      break;

    case 0x0003:
      impl::opcode8XY3_RegisterXorEq(cpu, opcode);
      break;

    case 0x0004:
      impl::opcode8XY4_AddRegCarry(cpu, opcode);
      break;

    case 0x0005:
      impl::opcode8XY5_SubRegCarry(cpu, opcode);
      break;

    default:
      printf("Unknown opcode");
      return;
    }
    break;

  
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
