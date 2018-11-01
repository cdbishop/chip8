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


  // opcode 1NNN: goto NNN;
  void opcode1NNN_Goto(chip8& cpu, unsigned short opcode) {
    cpu.pc = opcode & (unsigned short)0x0FFF;
  }

  // opcode 2NNN: call subroutine at NNN
  void opcode2NNN_Subroutine(chip8& cpu, unsigned short opcode) {
    // save the current stack location for when the subroutine completes
    cpu.stack[cpu.sp] = cpu.pc;
    ++cpu.sp;
    cpu.pc = opcode & 0x0FFF;
  }

  // opcode 3XNN: if(register[x] == NN skip next instruction
  void opcode3XNN_BranchIfEqToVal(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] == (opcode & 0x00FF)) {
      cpu.pc += 2;
    }
    else {
      ++cpu.pc;
    }
  }

  // opcode 4XNN: if(register[x] != NN skip next instruction
  void opcode4XNN_BranchIfNEq(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] != (opcode & 0x00FF)) {
      cpu.pc += 2;
    }
    else {
      ++cpu.pc;
    }
  }

  // opcode 5XY0: skip next instruction if register[x] == register[y]
  void opcode5XYN_BranchIfEqReg(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] == cpu.registers[(opcode & 0x00F0) >> 4]) {
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
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }


  // opcode 8XY1: Sets register[x] to register[x] | register[y]
  void opcode8XY1_RegisterOrEq(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x0F00) >> 8] | cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY2: Sets the register[x] to register[x] & register[y]
  void opcode8XY2_RegisterAndEq(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x0F00) >> 8] & cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY3: Sets the register[x] to register[x] ^ register[y]
  void opcode8XY3_RegisterXorEq(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x0F00) >> 8] ^ cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY4: add register[y] to register[x], store in register[x]
  void opcode8XY4_AddRegCarry(chip8& cpu, unsigned short opcode) {
    // if value in Y is > max value - value in X, then it will overflow
    if (cpu.registers[(opcode & 0x00F0) >> 4] > (unsigned char)(0xFF - cpu.registers[(opcode & 0x0F00) >> 8])) {
      // set carry
      cpu.registers[0xF] = 1;
    }
    else {
      //clear carry
      cpu.registers[0xF] = 0;
    }

    //do the addition
    cpu.registers[(opcode & 0x0F00) >> 8] += cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }

  // opcode 8XY5: add subtract register[y] from from register[x] (register[x] -= from register[y]), set register[0xF] to 0 if theres a borrow (e.g. register[y] > register[x])
  void opcode8XY5_SubRegCarry(chip8& cpu, unsigned short opcode) {
    //carry = 0 if borrow, e.g. register[y] > register[x]
    if (cpu.registers[(opcode & 0x00F0) >> 4] > (unsigned char)cpu.registers[(opcode & 0x0F00) >> 8]) {
      cpu.registers[0xF] = 0;
    }
    else {
      cpu.registers[0xF] = 1;
    }

    cpu.registers[(opcode & 0x0F00) >> 8] -= cpu.registers[(opcode & 0x00F0) >> 4];
  }
  
  // opcode ANNN: set I to NNN
  void opcodeANNN_SetIndex(chip8& cpu, unsigned short opcode) {
    cpu.index = opcode & 0xFFF;
    cpu.pc += 2;
  }
}

void chip8Initialize(chip8& cpu)
{
  cpu.pc = PROGRAM_OFFSET;
  cpu.opcode = 0;
  cpu.index = 0;
  cpu.sp = 0;

  // Clear display
  std::memset(cpu.gfx, 0, sizeof(char) * GfxDisplaySize);

  // Clear stack
  std::memset(cpu.stack, 0, sizeof(unsigned short) * MaxStackSize);

  // Clear registers V0-VF
  std::memset(cpu.registers, 0, sizeof(unsigned char) * NumRegisters);

  // Clear memory
  std::memset(cpu.memory, 0, sizeof(unsigned char) * MaxMemory);

  // load font into memory
  for (unsigned char i = 0; i < FONT_BUFFER_SIZE; ++i) {
    cpu.memory[i + FONT_MEMORY_OFFSET] = chip8_fontset[i];
  }

  // reset timers
  cpu.delay_timer = 0;
  cpu.sound_timer = 0;
}

void chip8LoadRom(chip8& cpu, const std::string & file)
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

namespace test {

  void testResult(bool pass) {
    std::cout << (pass ? "PASSED" : "FAILED");
  }

  void jump(chip8& cpu) {
    std::cout << "Test: Jump = ";
    chip8Initialize(cpu);
    // JUMP PC should equal NNN in 1NNN
    const unsigned short instruction = 0x1ABC;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    chip8Cycle(cpu);
    unsigned short expected = (instruction & 0x0FFF);
    testResult(cpu.pc == expected);

    std::cout << std::endl;
  }

  void subroutine(chip8& cpu) {
    std::cout << "Test: Subroutine = ";
    chip8Initialize(cpu);
    // cur pc should be persisted in stack at sp
    const unsigned short instruction = 0x2ABC;
    cpu.memory[PROGRAM_OFFSET + 4] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 5] = (unsigned char)(instruction & 0xFF);
    cpu.pc = PROGRAM_OFFSET + 4;
    chip8Cycle(cpu);
    unsigned short expected = (instruction & 0x0FFF);
    testResult((cpu.pc == expected) && (cpu.stack[cpu.sp - 1] == PROGRAM_OFFSET + 4));

    std::cout << std::endl;
  }

  void BranchIfEqToVal(chip8& cpu) {
    std::cout << "Test: BranchIfEqToVal = ";
    chip8Initialize(cpu);
    // skip next instruction if register[1] == 55
    const unsigned short instruction = 0x3155;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x55;
    unsigned short expected = cpu.pc + 2;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // should not skip as register != 55
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 99;
    unsigned short expected2 = cpu.pc + 1;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void BranchIfNEqToVal(chip8& cpu) {
    std::cout << "Test: BranchIfNEqToVal = ";
    chip8Initialize(cpu);
    // should not skip next instruction if register[1] == 55
    const unsigned short instruction = 0x4155;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x55;
    unsigned short expected = cpu.pc + 1;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // should skip as register != 55
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 99;
    unsigned short expected2 = cpu.pc + 2;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void BranchIfEqReg(chip8& cpu) {
    std::cout << "Test: BranchIfEqReg = ";
    chip8Initialize(cpu);
    // skip next instruction if register[1] == register[2]
    const unsigned short instruction = 0x5120;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x55;
    cpu.registers[2] = 0x55;
    unsigned short expected = cpu.pc + 2;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // should not skip as register != 55
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x99;
    cpu.registers[2] = 0x55;
    unsigned short expected2 = cpu.pc + 1;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void RegLoad(chip8& cpu) {
    std::cout << "Test: RegLoad = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x6244;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0x44);
    std::cout << std::endl;
  }

  void AddRegNoCarry(chip8& cpu) {
    std::cout << "Test: AddRegNoCarry = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x7301;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    chip8Cycle(cpu);
    testResult(cpu.registers[3] == 0x01 && cpu.registers[0xF] == 0);

    std::cout << ", ";
    chip8Initialize(cpu);
    const unsigned short instruction2 = 0x73FE;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction2 >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction2 & 0xFF);
    cpu.registers[3] = 0x5;
    chip8Cycle(cpu);
    testResult(cpu.registers[3] == 0x03 && cpu.registers[0xF] == 0);

    std::cout << std::endl;
  }

  void SetReg(chip8& cpu) {
    std::cout << "Test: SetReg = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8230;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[3] = 0x5;
    chip8Cycle(cpu);    
    testResult(cpu.registers[2] == cpu.registers[3]);

    std::cout << std::endl;
  }

  void RegOrEq(chip8& cpu) {
    std::cout << "Test: RegOrEq = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8231;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x9;
    cpu.registers[3] = 0x2;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0xb);

    std::cout << std::endl;
  }

  void RegAndEq(chip8& cpu) {
    std::cout << "Test: RegAndEq = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8232;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x9;
    cpu.registers[3] = 0xF;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0x9);

    std::cout << ", ";
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x9;
    cpu.registers[3] = 0x2;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0);

    std::cout << std::endl;
  }

  void RegXorEq(chip8& cpu) {
    std::cout << "Test: RegXorEq = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8233;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x9;
    cpu.registers[3] = 0xF;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0x6);

    std::cout << ", ";
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x9;
    cpu.registers[3] = 0x2;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0xb);

    std::cout << std::endl;
  }

  void AddRegCarry(chip8& cpu) {
    std::cout << "Test: AddRegCarry = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8124;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x4;
    cpu.registers[2] = 0x4;
    chip8Cycle(cpu);
    testResult(cpu.registers[1] == 0x8 && cpu.registers[0xF] == 0);

    std::cout << ", ";
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x4;
    cpu.registers[2] = 0xFF;
    chip8Cycle(cpu);
    testResult(cpu.registers[1] == 0x3 && cpu.registers[0xF] == 1);

    std::cout << std::endl;
  }

  void SubRegCarry(chip8& cpu) {
    std::cout << "Test: SubRegCarry = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8125;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x4;
    cpu.registers[2] = 0x2;
    chip8Cycle(cpu);
    testResult(cpu.registers[1] == 0x2 && cpu.registers[0xF] == 1);

    std::cout << ", ";
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x4;
    cpu.registers[2] = 0xFF;
    chip8Cycle(cpu);
    testResult(cpu.registers[1] == 0x5 && cpu.registers[0xF] == 0);

    std::cout << std::endl;
  }
}

void chip8test(chip8& cpu) {
  
  //test::displayClear(cpu);
  test::jump(cpu);
  test::subroutine(cpu);
  //test::subroutineReturn(cpu);
  test::BranchIfEqToVal(cpu);
  test::BranchIfNEqToVal(cpu);
  test::BranchIfEqReg(cpu);
  test::RegLoad(cpu);
  test::AddRegNoCarry(cpu);
  test::SetReg(cpu);
  test::RegOrEq(cpu);
  test::RegAndEq(cpu);
  test::RegXorEq(cpu);
  test::AddRegCarry(cpu);
  test::SubRegCarry(cpu);

}