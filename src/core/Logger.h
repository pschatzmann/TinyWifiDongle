#pragma once
/**
 * @file Logger.h
 * @brief Printf-style logging through the global TWD_LOGGER object.
 */
#include <Print.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef TWD_PRINTF_FORMAT
#define TWD_PRINTF_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#endif

namespace twd {

/**
 * @brief Printf-style logger with log levels; use the global TWD_LOGGER object.
 *
 * Discards everything until the sketch calls begin(), so components can always
 * log without checking whether logging is wanted. Each line is handed to the
 * output with a single write() call, as `[dongle] <level letter> <text>`.
 */
class LoggerClass {
 public:
  /// Severity of a log line; lines below the level set with begin() are discarded.
  enum class LogLevel : uint8_t { Debug, Info, Warning, Error };

  /// Lines of `level` and above are written to `out` (e.g. Serial); call before starting the dongle.
  void begin(Print& out, LogLevel level = LogLevel::Info) {
    level_ = level;
    out_ = &out;
  }
  /// Stop logging.
  void end() { out_ = nullptr; }
  /// Waits until the output has sent what was written (e.g. before a restart).
  void flush() {
    Print* out = out_;
    if (out) out->flush();
  }

  /// Lowest level that is written.
  void setLevel(LogLevel level) { level_ = level; }
  LogLevel level() const { return level_; }

  /// True if lines of `level` are written somewhere.
  bool enabled(LogLevel level = LogLevel::Info) const { return out_ != nullptr && level >= level_; }

  /// Formats and writes one line of `level`; does nothing if that level is not enabled.
  void log(LogLevel level, const char* fmt, ...) TWD_PRINTF_FORMAT(3, 4) {
    va_list ap;
    va_start(ap, fmt);
    vlog(level, fmt, ap);
    va_end(ap);
  }
  void debug(const char* fmt, ...) TWD_PRINTF_FORMAT(2, 3) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Debug, fmt, ap);
    va_end(ap);
  }
  void info(const char* fmt, ...) TWD_PRINTF_FORMAT(2, 3) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Info, fmt, ap);
    va_end(ap);
  }
  void warning(const char* fmt, ...) TWD_PRINTF_FORMAT(2, 3) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Warning, fmt, ap);
    va_end(ap);
  }
  void error(const char* fmt, ...) TWD_PRINTF_FORMAT(2, 3) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Error, fmt, ap);
    va_end(ap);
  }

  /// va_list variant of log().
  void vlog(LogLevel level, const char* fmt, va_list ap) {
    Print* out = out_;
    if (!out || level < level_) return;
    static constexpr char kPrefix[] = "[dongle] ? ";
    char buf[256];
    size_t len = sizeof(kPrefix) - 1;
    memcpy(buf, kPrefix, len);
    buf[len - 2] = "DIWE"[(int)level];
    int n = vsnprintf(buf + len, sizeof(buf) - len - 2, fmt, ap);
    if (n < 0) return;
    len += (size_t)n < sizeof(buf) - len - 2 ? (size_t)n : sizeof(buf) - len - 3;
    buf[len++] = '\r';
    buf[len++] = '\n';
    out->write((const uint8_t*)buf, len);
  }

 private:
  Print* volatile out_ = nullptr;
  LogLevel level_ = LogLevel::Info;
};

}  // namespace twd

/// The logger used by all components; configure it in the sketch with TWD_LOGGER.begin(Serial).
static twd::LoggerClass TWD_LOGGER;
