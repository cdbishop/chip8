#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <cmath>
#include <ctime>
#include <cstdlib>

#include "chip8.h"

void Render(sf::RenderWindow &window, sf::RectangleShape &sq);
sf::RectangleShape createPixel(float x, float y);

int main()
{
  const int screenWidth = 640;
  const int screenHeight = 320;

  // Create the window of the application
  sf::RenderWindow window(sf::VideoMode(screenWidth, screenHeight, 32), "SFML Chip8",
    sf::Style::Titlebar | sf::Style::Close);
  window.setVerticalSyncEnabled(true);

  chip8 cpu;
  chip8Initialize(cpu);
  chip8LoadGame(cpu, "BC_test.ch8");

  sf::RectangleShape sq = createPixel(10, 10);

  sf::Clock clock;
  while (window.isOpen())
  {
    // Handle events
    sf::Event event;
    while (window.pollEvent(event))
    {
      // Window closed or escape key pressed: exit
      if ((event.type == sf::Event::Closed) ||
        ((event.type == sf::Event::KeyPressed) && (event.key.code == sf::Keyboard::Escape)))
      {
        window.close();
        break;
      }
    }

    Render(window, sq);
  }

  return 0;
}

void Render(sf::RenderWindow &window, sf::RectangleShape &sq)
{
  // Clear the window
  window.clear(sf::Color(0, 0, 0));

  // Draw the paddles and the ball
  window.draw(sq);

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
