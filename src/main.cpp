#include <Arduino.h>
#include <MACPool.hpp>

#include "WiFi.h"
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <vector>
#include "string.h"
#include <SPI.h>

#ifdef PICO32
#include <TFT_eSPI.h> // Hardware TTGO Twristband
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
#endif

#ifdef HELTEC
#include "heltec.h"
#endif


int screen = 1; // screen on or off

float ARef = 1.0; // battery control

using namespace std;

const char* ssid     = "xxxx";         // The SSID (name) of the Wi-Fi network you want to connect to
const char* password = "xxxx";         // network password

unsigned int channel;

int SleepSecs = 300; // timer to wake up 

float weight = 1.0; // weigh the index with real time data


const wifi_promiscuous_filter_t filt={
    .filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA
};


vector<MACPool> listOfMAC;

int riskIndex = 0;
String riskValue = "low";

typedef struct {
  uint8_t mac[6];
} __attribute__((packed)) MacAddr;

typedef struct {
  int16_t fctl;
  int16_t duration;
  MacAddr da;
  MacAddr sa;
  MacAddr bssid;
  int16_t seqctl;
  unsigned char payload[];
} __attribute__((packed)) WifiMgmtHdr;

void sniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  
    wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t*)buf;
    WifiMgmtHdr *wh = (WifiMgmtHdr*)p->payload;
    int signal = p->rx_ctrl.rssi;

    MacAddr mac_add = (MacAddr)wh->sa;
    String sourceMac;
    for (int i = 0; i < sizeof(mac_add.mac); i++) {
        String macDigit = String(mac_add.mac[i], HEX);
        if (macDigit.length() == 1) {
            macDigit = "0" + macDigit;
        }
        
        sourceMac += macDigit;
        if (i != sizeof(mac_add.mac) - 1) {
          sourceMac += ":";
        }
    }

    sourceMac.toUpperCase();

    if (signal > -70) { // signal level threshold

      // Prevent duplicates
      for (int i = 0; i < listOfMAC.size(); i++) {
          if (sourceMac == listOfMAC[i].getMAC()) {
              listOfMAC[i].updateTime(millis()); // update the last time MAC found
              listOfMAC[i].updateNewMAC(false);
              return;
          }
      }

      // new MAC

      listOfMAC.push_back(MACPool(sourceMac,signal,millis(),true));

      //Serial.println(listOfMAC[listOfMAC.size()-1].getMAC());

      // purge outdated MACs

      for (auto it = listOfMAC.begin(); it != listOfMAC.end(); ) {
          if (millis()-it->getTime() > 300000) { // remove if older than 5min
              it = listOfMAC.erase(it);
          } else {
              ++it;
          }
      }

      // update the risk index

      int recentLowSingal = 0;
      int recentHighSingal = 0;
      for (int i = 0; i < listOfMAC.size(); i++) {
          if (millis()-listOfMAC[i].getTime() < 30000 && listOfMAC[i].getNewMAC()==true) {
              if (listOfMAC[i].getSignal() < -60) { recentLowSingal++; } else { recentHighSingal++; }
          }
          // new low and high signals from last 30sec
      }

      riskIndex = int(riskIndex + recentLowSingal + (2*recentHighSingal) * weight);

      // initial estimation -provisional- : activities with moderated risk: 800-1300 / hour
      // activities with low risk: < 800 / hour
      // aftivities with high risk: > 1300 / hour
      // if you are using the device 12 hour / day: the risk is low if < 9600, medium if >= 9600 <= 15600, high > 15600
      if (riskIndex<9600) { riskValue="low";}
      if (riskIndex>=9600 && riskIndex<=15600) { riskValue="medium";}
      if (riskIndex>15600) { riskValue="high, go home, please!"; }

    }
}

void getAPIinfo() {
    // get the information about covid19 local infections

    WiFi.begin(ssid, password);  // Connect to the network
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.println(" ...");
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i < 50) {  // Wait for the Wi-Fi to connect
        delay(1000);
        Serial.print(++i);
        Serial.print(' ');
    }

    if (i < 50) {  // connection OK
        Serial.println('\n');
        Serial.println("Connection established!");
        Serial.print("IP address:\t");
        Serial.println(WiFi.localIP());
        HTTPClient client;
        client.begin("http://api.ferranfabregas.me/covid19");  // not the definitive API, for testing only
        int httpCode = client.GET();
        if (httpCode > 0) {
            // only for testing
            String payload = client.getString();  // get the data
            //Serial.println(payload);
            int inf_pos = payload.indexOf("infected");
            String infected = payload.substring(inf_pos + 11, payload.indexOf(",", inf_pos));
            int inf_inh = payload.indexOf("inhabitants");
            String inhabitants = payload.substring(inf_inh + 14, payload.indexOf(",", inf_inh));
            //Serial.println(inhabitants);
            weight = (infected.toFloat() / inhabitants.toFloat()) * 100;
            Serial.println(weight);
        } else {
            Serial.println("Error on HTTP request");
        }
    } else {
        Serial.println("failed!");
        delay(2000);
    }
    WiFi.disconnect();
    WiFi.enableSTA(false);
    WiFi.softAPdisconnect(true);
}

void drawProgressBar(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h, uint8_t percentage, uint16_t frameColor, uint16_t barColor) {
#ifdef PICO32
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.textWidth(" 888% "));
    tft.drawString(String(percentage) + "%", 145, 35);
    if (percentage == 0) {
        tft.fillRoundRect(x0, y0, w, h, 3, TFT_BLACK);
    }
    uint8_t margin = 2;
    uint16_t barHeight = h - 2 * margin;
    uint16_t barWidth = w - 2 * margin;
    tft.drawRoundRect(x0, y0, w, h, 3, frameColor);
    tft.fillRect(x0 + margin, y0 + margin, barWidth * percentage / 100.0, barHeight, barColor);
#endif
}

void displayWelcome() {
#ifdef PICO32
    tft.setCursor(0, 0, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    // We can now plot text on screen using the "print" class
    tft.setTextFont(2);
    tft.println("Covid-19");
    tft.println("risk index");
    tft.println("");
    tft.println("Scanning..");
#endif
}

void displayInfo() {
#ifdef PICO32
    //if (millis() - lastMillis > 60000) { // promiscuous mode is enable 1 time every 5 min (to save battery)
    float raw = analogRead(35);
    float voltage = ((raw / 1023) * ARef * 3.3) - 4.8;  // battery control
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.println("Covid-19");
    tft.println("daily");
    tft.println("accumulated");
    tft.println("risk index:");
    tft.println("");
    tft.setTextFont(4);
    tft.println(riskValue);
    tft.println((String)riskIndex);
    tft.setTextFont(1);
    tft.println("");
    tft.setTextDatum(BR_DATUM);
    tft.println((String)voltage+"v");
#endif

#ifdef HELTEC
    Heltec.display->clear();
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->drawString(0, 0, "Covid-19 accumulated daily");
    Heltec.display->drawString(0, 10, "personal risk index:");
    Heltec.display->setFont(ArialMT_Plain_16);
    Heltec.display->drawString(0, 24, riskValue);
    Heltec.display->drawString(0, 44, (String)riskIndex);
    Heltec.display->display();
#endif
}

void displayOff() {
#ifdef PICO32
    Serial.println("Turn off screen!");
    //Serial.println(millis());
    digitalWrite(27, LOW);
#endif
}

void snifferLoop(){
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    //Serial.println("Enable WiFi");
    // set WiFi in promiscuous mode
    //esp_wifi_set_mode(WIFI_MODE_STA);            // Promiscuous works only with station mode
    esp_wifi_set_mode(WIFI_MODE_NULL);
    // power save options
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_start();
    esp_wifi_set_max_tx_power(-4);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&sniffer);  // Set up promiscuous callback
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int loops = 0; loops < 10; loops++) {
        drawProgressBar(0,TFT_HEIGHT/2, TFT_WIDTH, 10, (loops+1)*10, TFT_WHITE, TFT_BLUE);
        for (channel = 0; channel < 12; channel++) {
            esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            delay(50);
        }
    }
}

void tftInit() {
    tft.init();
    // tft.setRotation(1);
    // tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, 185);
}

void tftSleep() {
    tft.fillScreen(TFT_BLACK);
    tft.writecommand(ST7735_SWRESET);
    delay(100);
    tft.writecommand(ST7735_SLPIN);
    delay(150);
    tft.writecommand(ST7735_DISPOFF);
}

void deactivateWifi() {
    WiFi.mode(WIFI_OFF);
}

void deepSleep() {
    tftSleep();
    deactivateWifi();
    pinMode(39, GPIO_MODE_INPUT);
    esp_sleep_enable_ext1_wakeup(GPIO_SEL_33, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_disable_rom_logging();
    esp_deep_sleep_start();
}

void setup(void) {
    Serial.begin(115200);

#ifdef HELTEC

    Heltec.begin(true /*DisplayEnable Enable*/, false /*Heltec.LoRa Disable*/, false /*Serial Enable*/);
    Heltec.display->clear();
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
    Heltec.display->setFont(ArialMT_Plain_16);
    Heltec.display->drawString(0, 0, "Starting...");
    Heltec.display->display();

#endif

#ifdef PICO32

    pinMode(25, PULLUP);  // button power
    pinMode(33, PULLUP);  // button
    tftInit();

#endif

    //getAPIinfo();
}

void loop() {
    Serial.println("System awakes");
    displayWelcome();
    snifferLoop();
    displayInfo();
#ifdef PICO32
    Serial.println("System sleeps");
    delay(8000);
    deepSleep();
#endif
}
