#include "Haki/Core/Game.h"
#include "Haki/Logging/ConsoleLogger.h"
#include "Haki/Logging/Logger.h"

int main(int argc, char const *argv[]) {

  ConsoleLogger console;
  Logger::SetLogger(&console);

  Logger::Debug() << "Powered By Haki Engine";

  Haki::Game game("Sandbox", 1280, 720);

  return game.Run() ? 0 : 1;
}
