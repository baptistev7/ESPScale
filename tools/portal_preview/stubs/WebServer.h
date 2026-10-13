#pragma once
#include "Arduino.h"
#include <functional>
#include <map>
enum HTTPMethod { HTTP_GET, HTTP_POST };
// Serveur factice : les arguments de requête sont posés par le banc, la
// réponse est capturée dans `body` au lieu d'être envoyée.
class WebServer {
  public:
    explicit WebServer(int) {}
    std::map<std::string, std::string> args;
    String body;
    String arg(const char* n) { auto it = args.find(n); return it == args.end() ? String() : String(it->second); }
    bool hasArg(const char* n) { return args.count(n) != 0; }
    void send(int, const char*, const String& b) { body = b; }
    void sendHeader(const char*, const String&, bool = false) {}
    void on(const char*, HTTPMethod, std::function<void()>) {}
    void onNotFound(std::function<void()>) {}
    void begin() {}
    void stop() {}
    void handleClient() {}
};
