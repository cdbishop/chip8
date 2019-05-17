#include "chip8.h"

#include <fstream>
#include <vector>
#include <iostream>

#include <string>
#include <time.h>
#include <set>
#include <thread>
#include <mutex>
#include <chrono>

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

static const unsigned short FONT_MEMORY_OFFSET = 0x50;
static const unsigned short PROGRAM_OFFSET = 0x200;

static std::mutex io_mutex;
static int cycleCount = 0;

namespace debug {

  void dumpMemory(chip8& cpu, std::ofstream& file) {
    file << "--------------------------------------------------" << std::endl;
    for (unsigned short i = 0; i < MaxMemory; ++i) {
      file << std::hex << (int)cpu.memory[i] << std::dec;
      if ((i % 50) == 0) {
        file << std::endl;
      }
    }
    file << std::endl;
    file << "--------------------------------------------------" << std::endl;
  }

  void dumpGraphics(chip8& cpu, std::ofstream& file) {
    for (unsigned char x = 0; x < GfxDisplayWidth; ++x) {
      file << "-";
    }
    file << std::hex << std::endl;

    for (unsigned char y = 0; y < GfxDisplayHeight; ++y) {
      for (unsigned char x = 0; x < GfxDisplayWidth; ++x) {
        file << cpu.gfx[x + (y * GfxDisplayWidth)];
      }
      file << std::dec << std::endl;
    }

    for (unsigned char x = 0; x < GfxDisplayWidth; ++x) {
      file << "-";
    }
    file << std::endl;
  }

  void dumpRegisters(chip8& cpu, std::ofstream& file) {
    file << "---------" << std::hex << std::endl;
    for (unsigned char i = 0; i < NumRegisters; ++i) {
      file << "Register: " << (int)i << " = " << (int)cpu.registers[i] << std::endl;
    }
    file << "---------" << std::hex << std::endl;
  }

  void dumpState(chip8& cpu, std::ofstream& file) {
    file << "Graphics: " << std::endl;
    dumpGraphics(cpu, file);
    file << "Memory: " << std::endl;
    dumpMemory(cpu, file);
    file << "Registers: " << std::endl;
    dumpRegisters(cpu, file);

    file << std::hex << "Index: " << cpu.index << std::dec << std::endl;
    file << std::hex << "PC: " << cpu.pc << std::dec << std::endl;
  }
}

namespace impl {

  void opcode00E0_ClearScreen(chip8& cpu) {
    std::memset(cpu.gfx, 0, sizeof(char) * GfxDisplaySize);
    cpu.pc += 2;
    cpu.draw_flag = true;
  }

  void opcode00EE_SubroutineReturn(chip8& cpu) {
    //grab the saved address (where we wish to return) from the stack
    --cpu.sp;
    cpu.pc = cpu.stack[cpu.sp];
    cpu.pc += 2;    
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
      cpu.pc += 4;
    }
    else {
      cpu.pc += 2;
    }
  }

  // opcode 4XNN: if(register[x] != NN skip next instruction
  void opcode4XNN_BranchIfNEq(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] != (opcode & 0x00FF)) {
      cpu.pc += 4;
    }
    else {
      cpu.pc += 2;
    }
  }

  // opcode 5XY0: skip next instruction if register[x] == register[y]
  void opcode5XYN_BranchIfEqReg(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] == cpu.registers[(opcode & 0x00F0) >> 4]) {
      cpu.pc += 4;
    }
    else {
      cpu.pc += 2;
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

  // opcode 8XY5: subtract register[y] from from register[x] (register[x] -= from register[y]), set register[0xF] to 0 if theres a borrow (e.g. register[y] > register[x])
  void opcode8XY5_SubEqRegCarry(chip8& cpu, unsigned short opcode) {
    //carry = 0 if borrow, e.g. register[y] > register[x]
    if (cpu.registers[(opcode & 0x00F0) >> 4] > (unsigned char)cpu.registers[(opcode & 0x0F00) >> 8]) {
      cpu.registers[0xF] = 0;
    }
    else {
      cpu.registers[0xF] = 1;
    }

    cpu.registers[(opcode & 0x0F00) >> 8] -= cpu.registers[(opcode & 0x00F0) >> 4];
    cpu.pc += 2;
  }

  // store lest significant bit (lowest value) of register[x] in register[0xF] and shift register[x] right by 1 (>> 1)
  void opcode8XY6_RegShiftRight(chip8& cpu, unsigned short opcode) {
    cpu.registers[0xF] = cpu.registers[(opcode & 0xF00) >> 8] & 1;
    cpu.registers[(opcode & 0xF00) >> 8] >>= 1;
    cpu.pc += 2;
  }

  // register[x] = register[y] - register[x], set register[0xF] = 0 if theres a borrow
  void opcode8XY7_SubRegCarry(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] > (unsigned char)cpu.registers[(opcode & 0x00F0) >> 4]) {
      cpu.registers[0xF] = 0;
    }
    else {
      cpu.registers[0xF] = 1;
    }

    cpu.registers[(opcode & 0x0F00) >> 8] = cpu.registers[(opcode & 0x00F0) >> 4] - cpu.registers[(opcode & 0x0F00) >> 8];
    cpu.pc += 2;
  }

  // store most significant big (highest bit) of register[x] in register[0xF] and shift register[x] left by 1
  void opcode8XYE_RegShiftLeft(chip8& cpu, unsigned short opcode) {
    //cpu.registers[0xF] = (cpu.registers[(opcode & 0x0F00) >> 8] >> 4) & 0xF;
    cpu.registers[0xF] = cpu.registers[(opcode & 0x0F00) >> 8] >> 7;
    cpu.registers[(opcode & 0x0F00) >> 8] <<= 1;
    cpu.pc += 2;
  }

  // skip next instruction if register[x] != register[y]
  void opcode9XY0_BranchIfNEqReg(chip8& cpu, unsigned short opcode) {
    if (cpu.registers[(opcode & 0x0F00) >> 8] != cpu.registers[(opcode & 0x00F0) >> 4]) {
      cpu.pc += 4;
    }
    else {
      cpu.pc += 2;
    }
  }
  
  // opcode ANNN: set I to NNN
  void opcodeANNN_SetIndex(chip8& cpu, unsigned short opcode) {
    cpu.index = (unsigned short)(opcode & 0xFFF);
    cpu.pc += 2;
  }

  // opcode BNNN: jump to address NNN + register[0]
  void opcodeBNNN_JumpToAddr(chip8& cpu, unsigned short opcode) {
    cpu.pc = cpu.registers[0] + opcode & 0xFFF;
  }

  // opcode CXNN: set register[x] to rand() & NN - where rand is 0 - 255
  void opcodeCXNN_Rand(chip8& cpu, unsigned short opcode) {
    cpu.registers[(opcode & 0x0F00) >> 8] = (rand() % 256) & (opcode & 0xFF);
    cpu.pc += 2;
  }

  void opcodeDXYN_Draw(chip8& cpu, unsigned short opcode) {
    // draw sprite at position register[x], register[y] with a width of 8 pixels and a height of N
    // read pixel data from index
    // for each pixel position in gfx - flip the state
    // if any pixel was turned off, set register[0xF] to 1
    unsigned short posx = cpu.registers[(opcode & 0x0F00) >> 8];
    unsigned short posy = cpu.registers[(opcode & 0x00F0) >> 4];
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
        if ((pixel & (0x80 >> x)) != 0) {
          if (cpu.gfx[(posx + x + ((posy + y) * GfxDisplayWidth))] == 1) {
            cpu.registers[0xF] = 1;
          }

          cpu.gfx[posx + x + ((posy + y) * GfxDisplayWidth)] ^= 1;
        }
      }
    }

    // TODO: signal draw
    cpu.pc += 2;
    cpu.draw_flag = true;
  }

  void opcodeEX9E_SkipIfKeyPressed(chip8& cpu, unsigned short opcode) {
    // opcode: EX9E - key index stored in register[x], if pressed skip next instruction
    if (cpu.key[cpu.registers[(opcode & 0x0F00) >> 8]] != 0) {
      cpu.pc += 4;
    }
    else {
      cpu.pc += 2;
    }
  }

  void opcodeEX9E_SkipIfKeyNotPressed(chip8& cpu, unsigned short opcode) {
    // opcode: EX9E - key index stored in register[x], if pressed skip next instruction
    if (cpu.key[cpu.registers[(opcode & 0x0F00) >> 8]] == 0) {
      cpu.pc += 4;
    }
    else {
      cpu.pc += 2;
    }
  }

  void opcodeFX07_GetDelay(chip8& cpu, unsigned short opcode) {
    // opcode: FX07 - register[x] = delay_timer
    cpu.registers[(opcode & 0xF00) >> 8] = cpu.delay_timer;
    cpu.pc += 2;
  }

  void opcodeFX0A_WaitForKey(chip8& cpu, unsigned short opcode) {
    // opcode: FX0A - wait for any key, store in FX0A
    bool got_key = false;
    unsigned char key = 0;

    while (!got_key) {      
      {
        std::lock_guard<std::mutex> lk(io_mutex);
        for (unsigned char idx = 0; idx < MaxNumKeys; ++idx) {
          if (cpu.key[idx] != 0) {
            got_key = true;
            key = idx;
            break;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cpu.registers[(opcode & 0xF00) >> 8] = key;
    cpu.pc += 2;
  }

  void opcodeFX15_SetDelayTimer(chip8& cpu, unsigned short opcode) {
    cpu.delay_timer = cpu.registers[(opcode & 0xF00) >> 8];
    cpu.pc += 2;
  }

  void opcodeFX18_SetSoundTimer(chip8& cpu, unsigned short opcode) {
    cpu.sound_timer = cpu.registers[(opcode & 0xF00) >> 8];
    cpu.pc += 2;
  }

  void opcodeFX1E_AddIndex(chip8& cpu, unsigned short opcode) {
    cpu.index += cpu.registers[(opcode & 0xF00) >> 8];
    cpu.pc += 2;
  }

  void opcodeFX29_SetSpriteAddr(chip8& cpu, unsigned short opcode) {
    cpu.index = FONT_MEMORY_OFFSET + cpu.registers[(opcode & 0xF00) >> 8];
    cpu.pc += 2;
  }

  void opcodeFX33_BCD(chip8& cpu, unsigned short opcode) {
    // opcode FX33 - store the binary coded repr of register[x] as separate units in memory starting at I
    // so 100's at memory[index]
    // so 10's at memory[index + 1]
    // so 1's at memory[index + 2]

    // get the unit value
    //254 / 100 = 2
    cpu.memory[cpu.index] = (cpu.registers[(opcode & 0xF00) >> 8] / 100);
    //254 / 10 = 25 % 10 = 5
    cpu.memory[cpu.index + 1] = (cpu.registers[(opcode & 0xF00) >> 8] / 10) % 10;
    //254 % 100 = 54 % 10 = 4
    cpu.memory[cpu.index + 2] = (cpu.registers[(opcode & 0xF00) >> 8] % 100) % 10;
    cpu.pc += 2;
  }

  void opcodeFX55_RegDump(chip8& cpu, unsigned short opcode) {
    // store values cpu.register[0] to cpu.register[x] in memory starting at cpu.index, then cpu.index+1, cpu.index+2
    // cpu.index does not change
    for (unsigned char i = 0; i <= (opcode & 0xF00) >> 8; ++i) {
      cpu.memory[cpu.index + i] = cpu.registers[i];
    }
    cpu.pc += 2;
  }

  void opcodeFX65_RegLoad(chip8& cpu, unsigned short opcode) {
    // load values cpu.register[0] to cpu.register[x] from memory starting at cpu.index, then cpu.index+1, cpu.index+2
    // cpu.index does not change
    for (unsigned char i = 0; i <= (opcode & 0xF00) >> 8; ++i) {
      cpu.registers[i] = cpu.memory[cpu.index + i];
    }
    cpu.pc += 2;
  }
}

void chip8Initialize(chip8& cpu)
{
  cpu.pc = PROGRAM_OFFSET;
  cpu.opcode = 0;
  cpu.index = 0;
  cpu.sp = 0;
  cpu.draw_flag = false;

  // Clear display
  std::memset(cpu.gfx, 0, sizeof(char) * GfxDisplaySize);

  // Clear stack
  std::memset(cpu.stack, 0, sizeof(unsigned short) * MaxStackSize);

  // Clear registers V0-VF
  std::memset(cpu.registers, 0, sizeof(unsigned char) * NumRegisters);

  // Clear memory
  std::memset(cpu.memory, 0, sizeof(unsigned char) * MaxMemory);

  // clear key states
  std::memset(cpu.key, 0, sizeof(unsigned char) * MaxNumKeys);

  // load font into memory
  for (unsigned char i = 0; i < FONT_BUFFER_SIZE; ++i) {
    cpu.memory[i + FONT_MEMORY_OFFSET] = chip8_fontset[i];
  }

  // reset timers
  cpu.delay_timer = 0;
  cpu.sound_timer = 0;

  // init rand
  srand((unsigned int)time(NULL));
}

void chip8LoadRom(chip8& cpu, const std::string & file)
{
  std::ifstream input;
  input.open(file, std::ios::binary | std::ios::ate);
  std::ifstream::pos_type len = input.tellg();

  //if (len > 0x200) {
  //  throw std::runtime_error("Program too large");
  //}

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

  // *** DEBUG ***
 /* std::cout << std::hex << opcode << std::dec << std::endl;
  std::ofstream outfile;
  outfile.open("instructions.txt", std::ios_base::app);
  outfile << "*************************************************************" << std::endl;
  outfile << "Instruction: " << std::hex << opcode << std::dec << std::endl;
  outfile << "cycle: " << cycleCount << std::endl;*/
  // *************

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
      impl::opcode8XY5_SubEqRegCarry(cpu, opcode);
      break;

    case 0x0006:
      impl::opcode8XY6_RegShiftRight(cpu, opcode);
      break;

    case 0x0007:
      impl::opcode8XY7_SubRegCarry(cpu, opcode);
      break;

    case 0x000E:
      impl::opcode8XYE_RegShiftLeft(cpu, opcode);
      break;

    default:
      printf("Unknown opcode");
      return;
    }
    break;

  case 0x9000:
    impl::opcode9XY0_BranchIfNEqReg(cpu, opcode);
    break;
    
  case 0xA000:
    impl::opcodeANNN_SetIndex(cpu, opcode);
    break;

  case 0xB000:
    impl::opcodeBNNN_JumpToAddr(cpu, opcode);
    break;

  case 0xC000:
    impl::opcodeCXNN_Rand(cpu, opcode);
    break;

  case 0xD000: {
    impl::opcodeDXYN_Draw(cpu, opcode);
    break;
  }

  case 0xE000: {
    switch (opcode & 0x000F) {
      case 0x000E:
        impl::opcodeEX9E_SkipIfKeyPressed(cpu, opcode);
        break;

      case 0x0001:
        impl::opcodeEX9E_SkipIfKeyNotPressed(cpu, opcode);
        break;
    }
    break;
  }

  case 0xF000:
    switch (opcode & 0x00FF) {
      case 0x0007:
        impl::opcodeFX07_GetDelay(cpu, opcode);
        break;

      case 0x000A:
        impl::opcodeFX0A_WaitForKey(cpu, opcode);
        break;

      case 0x0015:
        impl::opcodeFX15_SetDelayTimer(cpu, opcode);
        break;

      case 0x0018:
        impl::opcodeFX18_SetSoundTimer(cpu, opcode);
        break;

      case 0x001E:
        impl::opcodeFX1E_AddIndex(cpu, opcode);
        break;

      case 0x0029:
        impl::opcodeFX29_SetSpriteAddr(cpu, opcode);
        break;

      case 0x0033:
        impl::opcodeFX33_BCD(cpu, opcode);
        break;

      case 0x0055:
        impl::opcodeFX55_RegDump(cpu, opcode);
        break;

      case 0x0065:
        impl::opcodeFX65_RegLoad(cpu, opcode);
        break;
    }
    break;
  
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

  //debug::dumpState(cpu, outfile);
  //outfile.close();
  cycleCount++;
}

namespace test {

  void testResult(bool pass) {
    std::cout << (pass ? "PASSED" : "FAILED");
  }

  void displayClear(chip8& cpu) {
    std::cout << "Test: Display clear = ";
    chip8Initialize(cpu);    
    const unsigned short instruction = 0x00E0;
    cpu.memory[PROGRAM_OFFSET + 4] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 5] = (unsigned char)(instruction & 0xFF);
    
    // fill the screen with pixels
    for (unsigned short i = 0; i < GfxDisplaySize; ++i) {
      cpu.gfx[i] = 1;
    }
    chip8Cycle(cpu);

    // screen should be cleared
    bool passed = true;
    for (unsigned short i = 0; i < GfxDisplaySize; ++i) {
      passed &= (cpu.gfx[i] == 0);
    }
    testResult(passed);
    std::cout << std::endl;
  }

  void subroutineReturn(chip8 cpu) {
    std::cout << "Test: subroutineReturn = ";
    // subroutine return pops address off stack, sets pc to addr and reduces sp
    chip8Initialize(cpu);    
    const unsigned short instruction = 0x00EE;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.sp++;
    cpu.stack[cpu.sp] = 0xFAB;
    chip8Cycle(cpu);
    testResult(cpu.sp == 0 && cpu.pc == 0xFAB);
    std::cout << std::endl;
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
    unsigned short expected = cpu.pc + 4;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // should not skip as register != 55
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 99;
    unsigned short expected2 = cpu.pc + 2;
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
    unsigned short expected = cpu.pc + 2;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // should skip as register != 55
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 99;
    unsigned short expected2 = cpu.pc + 4;
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
    unsigned short expected = cpu.pc + 4;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // should not skip as register != 55
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x99;
    cpu.registers[2] = 0x55;
    unsigned short expected2 = cpu.pc + 2;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void RegSet(chip8& cpu) {
    std::cout << "Test: RegSet = ";
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

  void SubEqRegCarry(chip8& cpu) {
    std::cout << "Test: SubEqRegCarry = ";
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

  void RegShiftRight(chip8& cpu) {
    std::cout << "Test: RegShiftRight = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8206;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x03;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0x01 && cpu.registers[0xF] == 1);

    std::cout << ", ";
    chip8Initialize(cpu);    
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x04;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0x02 && cpu.registers[0xF] == 0);

    std::cout << std::endl;
  }

  void SubRegCarry(chip8& cpu) {
    std::cout << "Test: SubRegCarry = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x8127;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0x2;
    cpu.registers[2] = 0x6;
    chip8Cycle(cpu);
    testResult(cpu.registers[1] == 0x4 && cpu.registers[0xF] == 1);

    std::cout << ", ";
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[1] = 0xFF;
    cpu.registers[2] = 0x4;
    chip8Cycle(cpu);
    testResult(cpu.registers[1] == 0x5 && cpu.registers[0xF] == 0);

    std::cout << std::endl;
  }

  void RegShiftLeft(chip8& cpu) {
    std::cout << "Test: RegShiftLeft = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0x820E;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0xFF;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0xFE && cpu.registers[0xF] ==0xF);

    std::cout << ", ";
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0xB;
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0x16 && cpu.registers[0xF] == 0);

    std::cout << std::endl;
  }

  void BranchIfNEqReg(chip8& cpu) {
    std::cout << "Test: BranchIfEqReg = ";
    chip8Initialize(cpu);
    // skip next instruction if register[1] != register[2]
    const unsigned short instruction = 0x9120;
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
    unsigned short expected2 = cpu.pc + 4;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void SetIndex(chip8& cpu) {
    std::cout << "Test: SetIndex = ";
    chip8Initialize(cpu);
    // skip next instruction if register[1] != register[2]
    const unsigned short instruction = 0xA123;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    unsigned short expected = 0x123;
    chip8Cycle(cpu);
    testResult(cpu.index == expected);

    std::cout << std::endl;
  }

  void JumpToAddr(chip8& cpu) {
    std::cout << "Test: JumpToAddr = ";
    chip8Initialize(cpu);
    // jump to addr NNN in BNNN
    const unsigned short instruction = 0xB123;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    unsigned short expected = 0x123;
    chip8Cycle(cpu);
    testResult(cpu.pc == expected);

    std::cout << std::endl;
  }

  void draw(chip8& cpu) {
    // 0xDXYN - (x,y) with height of N, sprite data starts at I
    // each memory address is 1 byt - 8 bits, draw pixel if 1, so

    std::cout << "Test: draw = ";
    chip8Initialize(cpu);    
    const unsigned short instruction = 0xD238;
    cpu.registers[2] = 0x2;
    cpu.registers[3] = 0x2;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    /*
        memory[I]     = 0xFF;
        memory[I + 1] = 0x17;
        memory[I + 2] = 0x17;
        memory[I + 2] = 0x17;

        HEX    BIN        Sprite
        0xFF   11111111   ********
        0x17   00011000      **       
        0x17   00011000      **   
        0x17   00011000      **
    */
    cpu.index = PROGRAM_OFFSET + 2;
    cpu.memory[cpu.index] = 0xFF;
    cpu.memory[cpu.index + 1] = 0x18;
    cpu.memory[cpu.index + 2] = 0x18;
    cpu.memory[cpu.index + 3] = 0x18;

    chip8Cycle(cpu);
    
    std::set<unsigned short> indices = {
      (GfxDisplayWidth * 2) + 2,
      (GfxDisplayWidth * 2) + 3,
      (GfxDisplayWidth * 2) + 4,
      (GfxDisplayWidth * 2) + 5,
      (GfxDisplayWidth * 2) + 6,
      (GfxDisplayWidth * 2) + 7,
      (GfxDisplayWidth * 2) + 8,
      (GfxDisplayWidth * 2) + 9,
      (GfxDisplayWidth * 3) + 5,
      (GfxDisplayWidth * 3) + 6,
      (GfxDisplayWidth * 4) + 5,
      (GfxDisplayWidth * 4) + 6,
      (GfxDisplayWidth * 5) + 5,
      (GfxDisplayWidth * 5) + 6,
    };

    bool pass = true;
    for (unsigned char x = 0; x < GfxDisplayWidth; ++x) {
      for (unsigned char y = 0; y < GfxDisplayHeight; ++y) {
        const unsigned char idx = y * GfxDisplayWidth + x;
        if (indices.count(idx)) {
          pass &= (cpu.gfx[idx] == 1);
        }
        else {
          pass &= (cpu.gfx[idx] == 0);
        }
      }
    }

    testResult(pass);

    std::cout << std::endl;
  }

  void SkipIfKeyPressed(chip8& cpu) {
    // EX9E: skip next instruction if cpu.key[cpu.register[X]] == 1
    std::cout << "Test: SkipIfKeyPressed = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xE29E;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // keyboard input is keys  0 - F
    cpu.registers[2] = 0xD;
    cpu.key[0xD] = 1;    
    unsigned short expected = cpu.pc + 4;
    chip8Cycle(cpu);    
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // dont skip if key not pressed
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // keyboard input is keys  0 - F
    cpu.registers[2] = 0xD;    
    unsigned short expected2 = cpu.pc + 2;
    chip8Cycle(cpu);    
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void SkipIfKeyNotPressed(chip8& cpu) {
    // EXA1: skip next instruction if cpu.key[cpu.register[X]] == 1
    std::cout << "Test: SkipIfKeyNotPressed = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xE2A1;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // keyboard input is keys  0 - F
    cpu.registers[2] = 0xD;
    cpu.key[0xD] = 1;
    unsigned short expected = cpu.pc + 2;
    chip8Cycle(cpu);    
    testResult(cpu.pc == expected);

    std::cout << ", ";
    // dont skip if key not pressed
    chip8Initialize(cpu);
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // keyboard input is keys  0 - F
    cpu.registers[2] = 0xD;
    unsigned short expected2 = cpu.pc + 4;
    chip8Cycle(cpu);    
    testResult(cpu.pc == expected2);

    std::cout << std::endl;
  }

  void ReadDelayTimer(chip8& cpu) {
    // FX07 - set cpu.register[x] = cpu.delay_timer
    std::cout << "Test: ReadDelayTimer = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF207;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // set timer
    cpu.delay_timer = 0xF;    
    chip8Cycle(cpu);
    testResult(cpu.registers[2] == 0xF);

    std::cout << std::endl;
  }

  void WaitForKey(chip8& cpu) {
    // FX0A - set cpu.register[x] to next key pressed
    std::cout << "Test: ReadDelayTimer = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF20A;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);

    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point end;

    // to test the wait - use a sep thread and sleep
    auto waitForInput = std::thread([&]() {
      start = std::chrono::high_resolution_clock::now();
      chip8Cycle(cpu);
      end = std::chrono::high_resolution_clock::now();
    });

    auto setInput = std::thread([&]() {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      std::lock_guard<std::mutex> lk(io_mutex);
      cpu.key[0xA] = 1;
    });

    setInput.join();
    waitForInput.join();

    bool waited = std::chrono::duration_cast<std::chrono::seconds>(end - start).count() >= 2;
    bool gotKey = cpu.registers[2] == 0xA;
    testResult(waited & gotKey);
   
    std::cout << std::endl;
  }

  void SetDelayTimer(chip8& cpu) {
    // opcode FX15 - set delay timer to value in register[x]    
    std::cout << "Test: SetDelayTimer = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF215;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);        
    cpu.registers[2] = 0xF;
    chip8Cycle(cpu);
    // after setting - delay timer will always decrement by 1 within same cycle
    testResult(cpu.delay_timer == (0x0F - 1));

    std::cout << std::endl;
  }

  void SetSoundTimer(chip8& cpu) {
    // opcode FX18 - set sound timer to value in register[x]    
    std::cout << "Test: SetSoundTimer = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF218;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0xF;
    chip8Cycle(cpu);
    // after setting - sound timer will always decrement by 1 within same cycle
    testResult(cpu.sound_timer == (0x0F - 1));

    std::cout << std::endl;
  }

  void AddIndex(chip8& cpu) {
    // opcode FX1E 0 add cpu.register[x] to cpu.index
    std::cout << "Test: AddIndex = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF21E;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0xF;
    const unsigned short expected = cpu.index + 0xF;
    chip8Cycle(cpu);
    testResult(cpu.index == expected);

    std::cout << std::endl;
  }

  void SetSpriteAddr(chip8& cpu) {
    // opcode FX29 - Set index to location of sprite for character in register[x]
    std::cout << "Test: SetSpriteAddr = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF229;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    cpu.registers[2] = 0x7;
    const unsigned short expected = FONT_MEMORY_OFFSET + 0x7;
    chip8Cycle(cpu);
    testResult(cpu.index == expected);

    std::cout << std::endl;
  }

  void RegDump(chip8& cpu) {
    std::cout << "Test: RegDump = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF755;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // TEST: dump the registers 0 to 7 in memory
    // Set registers to store the index of themselves as test data
    for (unsigned char i = 0; i < 8; ++i) {
      cpu.registers[i] = i;
    }
    cpu.index = PROGRAM_OFFSET + 2;
    chip8Cycle(cpu);
    bool passed = true;
    for (unsigned short i = cpu.index; i < cpu.index + 7; ++i) {
      passed &= (cpu.memory[i] == cpu.registers[i - cpu.index]);
    }

    testResult(passed);

    std::cout << std::endl;
  }

  void RegLoad(chip8& cpu) {
    std::cout << "Test: RegLoad = ";
    chip8Initialize(cpu);
    const unsigned short instruction = 0xF765;
    cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
    cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
    // TEST DATA: set the values in memory from program_offset + 2 to program_offset + 2 + 7
    // then load that into the registers
    for (unsigned char i = 0; i < 8; ++i) {
      cpu.memory[PROGRAM_OFFSET + 2 + i] = i;
    }
    cpu.index = PROGRAM_OFFSET + 2;
    chip8Cycle(cpu);
    bool passed = true;
    for (unsigned short i = 0; i < 8; ++i) {
      //passed &= (cpu.memory[i] == cpu.registers[i - cpu.index]);
      passed &= (cpu.registers[i] == cpu.memory[PROGRAM_OFFSET + 2 + i]);
    }

    testResult(passed);

    std::cout << std::endl;
  }
}

void chip8test(chip8& cpu) {
  
  test::displayClear(cpu);
  test::subroutineReturn(cpu);
  test::jump(cpu);
  test::subroutine(cpu);
  test::BranchIfEqToVal(cpu);
  test::BranchIfNEqToVal(cpu);
  test::BranchIfEqReg(cpu);
  test::RegSet(cpu);
  test::AddRegNoCarry(cpu);
  test::SetReg(cpu);
  test::RegOrEq(cpu);
  test::RegAndEq(cpu);
  test::RegXorEq(cpu);
  test::AddRegCarry(cpu);
  test::SubEqRegCarry(cpu);
  test::RegShiftRight(cpu);
  test::SubRegCarry(cpu);
  test::RegShiftLeft(cpu);
  test::BranchIfNEqReg(cpu);
  test::SetIndex(cpu);
  test::JumpToAddr(cpu);
  test::draw(cpu);
  test::SkipIfKeyPressed(cpu);
  test::SkipIfKeyNotPressed(cpu);
  test::ReadDelayTimer(cpu);
  test::WaitForKey(cpu);
  test::SetDelayTimer(cpu);
  test::SetSoundTimer(cpu);
  test::AddIndex(cpu);
  test::SetSpriteAddr(cpu);
  test::RegDump(cpu);
  test::RegLoad(cpu);
}

void chip8testRender(chip8& cpu) {
  std::cout << "Test: draw = ";
  chip8Initialize(cpu);
  const unsigned short instruction = 0xD238;
  cpu.registers[2] = 0x2;
  cpu.registers[3] = 0x2;
  cpu.memory[PROGRAM_OFFSET] = (unsigned char)(instruction >> 8);
  cpu.memory[PROGRAM_OFFSET + 1] = (unsigned char)(instruction & 0xFF);
  /*
      memory[I]     = 0xFF;
      memory[I + 1] = 0x17;
      memory[I + 2] = 0x17;
      memory[I + 2] = 0x17;

      HEX    BIN        Sprite
      0xFF   11111111   ********
      0x17   00011000      **
      0x17   00011000      **
      0x17   00011000      **
  */
  cpu.index = PROGRAM_OFFSET + 2;
  cpu.memory[cpu.index] = 0xFF;
  cpu.memory[cpu.index + 1] = 0x18;
  cpu.memory[cpu.index + 2] = 0x18;
  cpu.memory[cpu.index + 3] = 0x18;

  chip8Cycle(cpu);

  std::set<unsigned short> indices = {
    (GfxDisplayWidth * 2) + 2,
    (GfxDisplayWidth * 2) + 3,
    (GfxDisplayWidth * 2) + 4,
    (GfxDisplayWidth * 2) + 5,
    (GfxDisplayWidth * 2) + 6,
    (GfxDisplayWidth * 2) + 7,
    (GfxDisplayWidth * 2) + 8,
    (GfxDisplayWidth * 2) + 9,
    (GfxDisplayWidth * 3) + 5,
    (GfxDisplayWidth * 3) + 6,
    (GfxDisplayWidth * 4) + 5,
    (GfxDisplayWidth * 4) + 6,
    (GfxDisplayWidth * 5) + 5,
    (GfxDisplayWidth * 5) + 6,
  };
}