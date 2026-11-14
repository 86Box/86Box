/**
 * Copyright (c) 2011-2025 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#pragma once
/**
 * \file
 * \brief Stream/Print like replacement for non-Arduino systems.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../SdFatConfig.h"
#ifndef F
#if defined(__AVR__)
#include <avr/pgmspace.h>
class __FlashStringHelper;
#define F(string_literal) \
  (reinterpret_cast<const __FlashStringHelper *>(PSTR(string_literal)))
#else  // defined(__AVR__)
#define F(str) (str)
#endif  // defined(__AVR__)
#endif  // F
#ifdef BIN
#undef BIN
#endif  // BIN
#define BIN 2
#define OCT 8
#define DEC 10
#define HEX 16

class PrintBasic {
 public:
  PrintBasic() : m_error(0) {}

  void clearWriteError() { setWriteError(0); }
  int getWriteError() { return m_error; }
  virtual void flush() { /* Empty implementation for backward compatibility */ }
  size_t print(char c) { return write(c); }
  size_t print(const char *str) { return write(str); }
  size_t print(const __FlashStringHelper *str) {
#ifdef __AVR__
    PGM_P p = reinterpret_cast<PGM_P>(str);
    size_t n = 0;
    for (uint8_t c; (c = pgm_read_byte(p + n)) && write(c); n++) {
    }
    return n;
#else   // __AVR__
    return print(reinterpret_cast<const char *>(str));
#endif  // __AVR__
  }
  size_t println(const __FlashStringHelper *str) {
#ifdef __AVR__
    return print(str) + println();
#else   // __AVR__
    return println(reinterpret_cast<const char *>(str));
#endif  // __AVR__
  }
  size_t print(double n, uint8_t prec = 2) { return printDouble(n, prec); }
  size_t print(signed char n, uint8_t base = 10) {
    return print((long)n, base);
  }
  size_t print(unsigned char n, uint8_t base = 10) {
    return print((unsigned long)n, base);
  }
  size_t print(int n, uint8_t base = 10) { return print((long)n, base); }
  size_t print(unsigned int n, uint8_t base = 10) {
    return print((unsigned long)n, base);
  }
  size_t print(long n, uint8_t base = 10);
  size_t print(unsigned long n, uint8_t base = 10) { return printNum(n, base); }
  size_t println() { return write("\r\n"); }
  size_t println(char c) { return write(c) + println(); }
  size_t println(const char *str) { return print(str) + println(); }
  size_t println(double n, uint8_t prec = 2) {
    return print(n, prec) + println();
  }
  size_t println(signed char n, uint8_t base = 10) {
    return print(n, base) + println();
  }
  size_t println(unsigned char n, uint8_t base = 10) {
    return print(n, base) + println();
  }
  size_t println(int n, uint8_t base = 10) {
    return print(n, base) + println();
  }
  size_t println(unsigned int n, uint8_t base = 10) {
    return print(n, base) + println();
  }
  size_t println(long n, uint8_t base = 10) {
    return print(n, base) + println();
  }
  size_t println(unsigned long n, uint8_t base = 10) {
    return print(n, base) + println();
  }
  size_t write(const char *str) { return write(str, strlen(str)); }
  virtual size_t write(uint8_t b) = 0;

  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t i;
    for (i = 0; i < size; i++) {
      if (!write(buffer[i])) break;
    }
    return i;
  }
  size_t write(const char *buffer, size_t size) {
    return write(reinterpret_cast<const uint8_t *>(buffer), size);
  }

 protected:
  void setWriteError(int err = 1) { m_error = err; }

 private:
  size_t printDouble(double n, uint8_t prec);
  size_t printNum(unsigned long n, uint8_t base);
  int m_error;
};
//------------------------------------------------------------------------------
class StreamBasic : public PrintBasic {
 public:
  virtual int available() = 0;
  virtual int peek() = 0;
  virtual int read() = 0;
};
