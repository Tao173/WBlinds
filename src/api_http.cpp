#include "api_http.h"
#include "defines.h"
#include "datagram.h"
#include "api_http_websocket.h"
#include <ESPAsyncWebServer.h>

// Errors
const char* errPrefix = "{\"error\":\"";
const char* errSuffix = "\"}";
char errStr[50] = "";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

BlindsHTTPAPI::BlindsHTTPAPI() {
   WLOG_I(TAG);
}

BlindsHTTPAPI::~BlindsHTTPAPI() {
   State::getInstance()->Detach(this);
   ws.cleanupClients();
};

bool isIp(String str) {
   for (size_t i = 0; i < str.length(); i++) {
      int c = str.charAt(i);
      if (c != '.' && (c < '0' || c > '9')) {
         return false;
      }
   }
   return true;
}

bool captivePortal(AsyncWebServerRequest* request) {
   if (ON_STA_FILTER(request)) return false; // only serve captive in AP mode
   String hostH;
   if (!request->hasHeader("Host")) return false;
   hostH = request->getHeader("Host")->value();

   if (!isIp(hostH) && hostH.indexOf(mDnsName) < 0) {
      AsyncWebServerResponse* response = request->beginResponse(302);
      response->addHeader(F("Location"), F("http://4.3.2.1/settings?tab=general"));
      request->send(response);
      return true;
   }
   return false;
}

bool configRedirect(AsyncWebServerRequest* request) {
   WLOG_D(TAG, "ON_STA_FILTER(request): %i", ON_STA_FILTER(request));

   if (ON_STA_FILTER(request)) return false; // only redirect in AP mode
   const String& url = request->url();
   if (needsConfig && url.indexOf("/settings") < 0 && url.indexOf("tab=gen") < 0) {
      request->redirect("/settings?tab=gen");
      request->send(302);
      return true;
   }
   return false;
}

void BlindsHTTPAPI::handleEvent(const StateEvent& event) {
   // TODO: send data over websocket to connected web clients
   WLOG_I(TAG, "event mask: %i", event.flags_.mask_);

   // auto b = datagramToWSMessage()
   String m = Datagram::packString(event);
   WLOG_I(TAG, "DG message: %s", m.c_str());

   ws.textAll(m);
}

static char* errorJson(const char* msg) {
   strcpy(errStr, errPrefix);
   strcat(errStr, msg);
   strcat(errStr, errSuffix);
   return errStr;
};

static String getContentType(AsyncWebServerRequest* request, String filename) {
   if (request->hasArg("download")) return "application/octet-stream";
   else if (filename.endsWith(".htm")) return "text/html";
   else if (filename.endsWith(".html")) return "text/html";
   //  else if(filename.endsWith(".css")) return "text/css";
   //  else if(filename.endsWith(".js")) return "application/javascript";
   else if (filename.endsWith(".json")) return "application/json";
   else if (filename.endsWith(".png")) return "image/png";
   //  else if(filename.endsWith(".gif")) return "image/gif";
   else if (filename.endsWith(".jpg")) return "image/jpeg";
   else if (filename.endsWith(".ico")) return "image/x-icon";
   //  else if(filename.endsWith(".xml")) return "text/xml";
   //  else if(filename.endsWith(".pdf")) return "application/x-pdf";
   //  else if(filename.endsWith(".zip")) return "application/x-zip";
   //  else if(filename.endsWith(".gz")) return "application/x-gzip";
   return "text/plain";
}

static bool handleFileRead(AsyncWebServerRequest* request, String path) {
   WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());

   if (path.endsWith("/")) path += "index.html";
   String contentType = getContentType(request, path);
   if (LITTLEFS.exists(path)) {
      WLOG_I(TAG, "exists");
      request->send(LITTLEFS, path, contentType);
      return true;
   }
   return false;
}


static bool handleIfNoneMatchCacheHeader(AsyncWebServerRequest* request, String etag) {
   WLOG_I(TAG, "%s (%d args), etag %s", request->url().c_str(), request->params(), etag);

   AsyncWebHeader* header = request->getHeader("If-None-Match");
   if (header && header->value() == etag) {
      WLOG_D(TAG, "matched etag, 304");
      request->send(304);
      return true;
   }
   return false;
}

static void setCacheControlHeaders(AsyncWebServerResponse* response, String etag) {
   if (response == nullptr) {
      WLOG_D(TAG, "response nullptr");
      return;
   }
   WLOG_D(TAG, "add response cache headers");
   response->addHeader(F("Cache-Control"), "no-cache");
   response->addHeader(F("ETag"), etag);
}

static void serveIndex(AsyncWebServerRequest* request) {
   if (handleFileRead(request, "/index.html")) return;

   if (handleIfNoneMatchCacheHeader(request, String(VERSION))) return;

   AsyncWebServerResponse* response = request->beginResponse_P(200, stdBlinds::MT_HTML, PAGE_index, PAGE_index_L);

   response->addHeader(F("Content-Encoding"), "gzip");
   setCacheControlHeaders(response, String(VERSION));

   request->send(response);
}

static void setCORSHeaders(AsyncWebServerResponse* response) {
   if (response == nullptr) {
      return;
   }
   response->addHeader(F("Access-Control-Allow-Origin"), "*");
}

// static void handleNotFound(AsyncWebServerRequest* request) {
//    WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());

//    request->send(404);
// }

// void BlindsHTTPAPI::serveBackground(AsyncWebServerRequest* request) {
//    if (handleFileRead(request, "/bg.jpg")) return;

//    if (handleIfNoneMatchCacheHeader(request)) return;

//    AsyncWebServerResponse* response = request->beginResponse_P(200, stdBlinds::MT_JPG, IMG_background, IMG_background_L);

//    response->addHeader(F("Content-Encoding"), "gzip");
//    setStaticContentCacheHeaders(response);

//    request->send(response);
// }

static void getState(AsyncWebServerRequest* request) {
   WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());

   bool fromFile = request->hasParam("f");
   if (fromFile) {
      if (!handleFileRead(request, "/state.json")) {
         return request->send(404);
      }
   };
   AsyncWebServerResponse* response = request->beginResponse(200, stdBlinds::MT_JSON, State::getInstance()->serialize());
   setCORSHeaders(response);
   request->send(response);
}

static void updateState(AsyncWebServerRequest* request, JsonVariant& json) {
   WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());

   JsonObject obj = json.as<JsonObject>();
   auto errCode = State::getInstance()->loadFromObject(nullptr, obj);
   if (errCode == stdBlinds::error_code_t::NoError) {
      return request->send(204);
   }
   char* err = errorJson(stdBlinds::ErrorMessage[errCode]);
   return request->send(400, stdBlinds::MT_JSON, err);
}

static void serveOps(AsyncWebServerRequest* request, bool post) {
   WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());
   if (post) {
      auto errCode = BlindsAPI::doOperation(request->arg("plain").c_str());
      if (errCode == stdBlinds::error_code_t::NoError) {
         return request->send(202);
      }
      char* err = errorJson(stdBlinds::ErrorMessage[errCode]);
      return request->send(400, stdBlinds::MT_JSON, err);
   }
   request->beginResponse_P(200, "text/html", PAGE_index, PAGE_index_L);
}

static void updateSettings(AsyncWebServerRequest* request, JsonVariant& json) {
   WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());
   JsonObject obj = json.as<JsonObject>();
   auto errCode = State::getInstance()->loadFromObject(nullptr, obj, true);
   if (errCode == stdBlinds::error_code_t::NoError) {
      return request->send(204);
   }
   char* err = errorJson(stdBlinds::ErrorMessage[errCode]);
   return request->send(400, stdBlinds::MT_JSON, err);
}

static void getSettings(AsyncWebServerRequest* request) {
   WLOG_I(TAG, "%s (%d args)", request->url().c_str(), request->params());
   auto state = State::getInstance();

   bool fromFile = request->hasParam("f");
   if (fromFile) {
      if (!handleFileRead(request, "/settings.json")) {
         return request->send(404);
      }
   };

   String data;
   String etag;
   if (request->hasArg("type")) {
      auto p = request->getParam("type")->value();
      if (!strcmp(p.c_str(), "mqtt")) {
         etag = state->getMqttEtag();
         if (handleIfNoneMatchCacheHeader(request, etag)) return;
         data = state->serializeSettings(setting_t::kMqtt);
      }
      else if (!strcmp(p.c_str(), "hw")) {
         etag = state->getHardwareEtag();
         if (handleIfNoneMatchCacheHeader(request, etag)) return;
         data = state->serializeSettings(setting_t::kHardware);
      }
      else if (!strcmp(p.c_str(), "gen")) {
         etag = state->getGeneralEtag();
         if (handleIfNoneMatchCacheHeader(request, etag)) return;
         data = state->serializeSettings(setting_t::kGeneral);
      }
      else {
         request->send(400, stdBlinds::MT_JSON, errorJson("Invalid type"));
      }
   }
   else {
      etag = state->getAllSettingsEtag();
      if (handleIfNoneMatchCacheHeader(request, etag)) return;
      data = state->serializeSettings(setting_t::kAll);
   }
   AsyncWebServerResponse* response = request->beginResponse(200, stdBlinds::MT_JSON, data);
   setCacheControlHeaders(response, etag);
   setCORSHeaders(response);
   request->send(response);
}

void BlindsHTTPAPI::init() {
   WLOG_I(TAG);

   EventFlags interestingFlags;
   interestingFlags.pos_ = true;
   interestingFlags.targetPos_ = true;
   interestingFlags.accel_ = true;

   State::getInstance()->Attach(this, interestingFlags);

   ws.onEvent(onEvent);
   server.addHandler(&ws);

   server.on("/api/state", HTTP_GET, getState);

   AsyncCallbackJsonWebHandler* stateHandler = new AsyncCallbackJsonWebHandler("/api/state", updateState);
   stateHandler->setMethod(HTTP_PUT);
   server.addHandler(stateHandler);

   // TODO: change this to use AsyncJsonResponse
   server.on("/api/settings", HTTP_GET, getSettings);
   AsyncCallbackJsonWebHandler* settingsHandler = new AsyncCallbackJsonWebHandler("/api/settings", updateSettings);
   settingsHandler->setMethod(HTTP_PUT);
   server.addHandler(settingsHandler);

   auto handler = [](AsyncWebServerRequest* request) {
      WLOG_D(TAG, "handle /, /settings, /routines: %s", request->url());
      if (captivePortal(request)) return;
      if (configRedirect(request)) return;
      return serveIndex(request);
   };
   server.on("/", HTTP_GET, handler);
   server.on("/settings", HTTP_GET, handler);
   server.on("/routines", HTTP_GET, handler);

   // server.onNotFound(handleNotFound);

   server.onNotFound([](AsyncWebServerRequest* request) {
      WLOG_D(TAG, "Handle not found: %s", request->url());
      if (captivePortal(request)) return;

      if (request->method() == HTTP_OPTIONS) {
         AsyncWebServerResponse* response = request->beginResponse(200);
         response->addHeader(F("Access-Control-Max-Age"), F("7200"));
         request->send(response);
         return;
      }

      request->send(404);
      }
   );

   server.begin();
}