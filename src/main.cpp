#include "Haki/Core/Game.h"
#include "Haki/Logging/ConsoleLogger.h"
#include "Haki/Logging/Logger.h"

int main(int argc, char const *argv[]) {

  ConsoleLogger console;
  Logger::SetLogger(&console);

  Logger::Debug() << "Powered By Haki Engine";

  try {
    Haki::Game game("Sandbox", 1280, 720);
    return game.Run() ? 0 : 1;
  } catch (const std::exception &error) {
    Logger::Error() << error.what();
    return 1;
  }
}
