#pragma once

#include <sstream>
#include <string>

enum class LogLevel { Debug, Info, Warning, Error };

class ILogger {
public:
  virtual ~ILogger() = default;

  virtual void Log(LogLevel level, const std::string &message) = 0;
};

class LogStream {
public:
  LogStream(ILogger *logger, LogLevel level)
      : m_logger(logger), m_level(level) {}

  ~LogStream() {
    if (m_logger)
      m_logger->Log(m_level, m_stream.str());
  }

  template <typename T> LogStream &operator<<(const T &value) {
    m_stream << value;
    return *this;
  }

private:
  ILogger *m_logger;
  LogLevel m_level;
  std::ostringstream m_stream;
};

class Logger {
public:
  static void SetLogger(ILogger *logger);

  static LogStream Debug();
  static LogStream Info();
  static LogStream Warning();
  static LogStream Error();

private:
  static ILogger *s_logger;
};
