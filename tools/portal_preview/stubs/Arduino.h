// Bouchon PC minimal du core Arduino : juste ce qu'utilise la génération de
// page de src/webconfig.cpp (String, F(), Serial, ESP...).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>

#define RTC_DATA_ATTR
class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))

class String {
  public:
    String() {}
    String(const char* s) : v(s ? s : "") {}
    String(const std::string& s) : v(s) {}
    String(const __FlashStringHelper* s) : v(reinterpret_cast<const char*>(s)) {}
    String(char c) : v(1, c) {}
    String(int n) : v(std::to_string(n)) {}
    String(unsigned n) : v(std::to_string(n)) {}
    String(long n) : v(std::to_string(n)) {}
    String(unsigned long n) : v(std::to_string(n)) {}
    String(double f, int dec = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", dec, f); v = b; }
    void reserve(size_t n) { v.reserve(n); }
    unsigned length() const { return (unsigned)v.size(); }
    const char* c_str() const { return v.c_str(); }
    String& operator+=(const String& o) { v += o.v; return *this; }
    String& operator+=(const char* o) { v += o; return *this; }
    String& operator+=(const __FlashStringHelper* o) { v += reinterpret_cast<const char*>(o); return *this; }
    String& operator+=(char c) { v += c; return *this; }
    bool operator==(const String& o) const { return v == o.v; }
    bool operator==(const char* o) const { return v == o; }
    bool operator!=(const char* o) const { return v != o; }
    int indexOf(char c) const { auto p = v.find(c); return p == std::string::npos ? -1 : (int)p; }
    String substring(unsigned a) const { return a >= v.size() ? String() : String(v.substr(a)); }
    String substring(unsigned a, unsigned b) const { return a >= v.size() ? String() : String(v.substr(a, b - a)); }
    long toInt() const { return atol(v.c_str()); }
    float toFloat() const { return (float)atof(v.c_str()); }
    std::string v;
};
inline String operator+(String a, const String& b) { a += b; return a; }
inline String operator+(String a, const char* b) { a += b; return a; }
inline String operator+(String a, const __FlashStringHelper* b) { a += b; return a; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }

struct SerialStub {
    void println(const char* s = "") { fprintf(stderr, "%s\n", s); }
    void println(const String& s) { fprintf(stderr, "%s\n", s.c_str()); }
    void print(const char* s) { fprintf(stderr, "%s", s); }
    void printf(const char* f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
};
extern SerialStub Serial;

struct EspStub {
    uint32_t getFreeHeap() { return 182 * 1024; }
    uint64_t getEfuseMac() { return 0xA1B2; }
    void restart() { exit(0); }
};
extern EspStub ESP;

inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
inline uint32_t esp_random() { return 48213907; }
