#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <SFML/System.hpp>
#include <cmath>
#include <ctime>
#include <cstdlib>

#include "chip8.h"

void Render(sf::RenderWindow &window, chip8& cpu);
sf::RectangleShape createPixel(float x, float y);

const int screenWidth = 640;
const int screenHeight = 320;

const int PixelScaleX = screenWidth / GfxDisplayWidth;
const int PixelScaleY = screenHeight / GfxDisplayHeight;

int main()
{
  // Create the window of the application
  sf::RenderWindow window(sf::VideoMode(screenWidth, screenHeight, 32), "SFML Chip8",
    sf::Style::Titlebar | sf::Style::Close);
  window.setVerticalSyncEnabled(true);

  chip8 cpu;
  chip8Initialize(cpu);
  //chip8LoadRom(cpu, "BC_test.ch8");
  //chip8LoadRom(cpu, "ZeroDemo.ch8");
  chip8LoadRom(cpu, "PONG");

  //chip8test(cpu);
  //chip8testRender(cpu);

  sf::Clock clock;
  while (window.isOpen())
  {
    // handle events
    sf::Event event;
    while (window.pollEvent(event))
    {
      // window closed or escape key pressed: exit
      if ((event.type == sf::Event::Closed) ||
        ((event.type == sf::Event::KeyPressed) && (event.key.code == sf::Keyboard::Escape)))
      {
        window.close();
        break;
      }
    }

    chip8Cycle(cpu);
    if (cpu.draw_flag) {
      Render(window, cpu);
      cpu.draw_flag = false;
    }
  }

  return 0;
}

void Render(sf::RenderWindow &window, chip8& cpu)
{
  // Clear the window
  window.clear(sf::Color(0, 0, 0));

  // draw the pixels for the current state of cpu.gfx
  for (unsigned short y = 0; y < GfxDisplayHeight; ++y) {
    for (unsigned short x = 0; x < GfxDisplayWidth; ++x) {      
      if (cpu.gfx[y * GfxDisplayWidth + x]) {
        sf::RectangleShape sq = createPixel(x * PixelScaleX, y * PixelScaleY);
        window.draw(sq);
      }
    }
  }

  // Display things on screen
  window.display();
}

sf::RectangleShape createPixel(float x, float y) {
  sf::RectangleShape sq;
  sq.setPosition(sf::Vector2f(x, y));
  sq.setSize(sf::Vector2f(10, 10));
  sq.setFillColor(sf::Color(255, 255, 255));
  return sq;
}
