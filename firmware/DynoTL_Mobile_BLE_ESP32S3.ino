#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char *DEVICE_NAME  = "DynoTL Mobile Hardware";
static const char *SERVICE_UUID = "d7a10001-7c35-4a6d-9f0e-2ea3117f1000";
static const char *LIVE_UUID    = "d7a10002-7c35-4a6d-9f0e-2ea3117f1000";
static const char *COMMAND_UUID = "d7a10003-7c35-4a6d-9f0e-2ea3117f1000";
static const char *STATUS_UUID  = "d7a10005-7c35-4a6d-9f0e-2ea3117f1000";

BLECharacteristic *liveChar=nullptr,*commandChar=nullptr,*statusChar=nullptr;
bool bleConnected=false;

static void notifyChunks(BLECharacteristic *ch,const char *s){
  if(!bleConnected||!ch||!s)return;
  size_t len=strlen(s);
  for(size_t off=0;off<len;off+=18){
    size_t n=min((size_t)18,len-off);
    ch->setValue((uint8_t*)s+off,n);
    ch->notify();
    delay(3);
  }
}
void dynoPublishLine(const char *line){
  if(!line||line[0]!='D')return;
  char out[128];
  snprintf(out,sizeof(out),"%s\n",line);
  notifyChunks(liveChar,out);
}
void dynoPublishSample(uint32_t timeMs,float rollerRpm,float engineRpm,float afrVoltage){
  char line[112];
  snprintf(line,sizeof(line),"D,%lu,%.2f,%.2f,%.3f",(unsigned long)timeMs,rollerRpm,engineRpm,afrVoltage);
  dynoPublishLine(line);
}
class ServerCB:public BLEServerCallbacks{
  void onConnect(BLEServer*)override{bleConnected=true;delay(50);notifyChunks(statusChar,"BLE Connected");}
  void onDisconnect(BLEServer*)override{bleConnected=false;delay(80);BLEDevice::startAdvertising();}
};
class CmdCB:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic *c)override{
    String cmd=c->getValue().c_str();cmd.trim();
    if(cmd.equalsIgnoreCase("PING"))notifyChunks(statusChar,"PONG");
    else if(cmd.equalsIgnoreCase("STATUS"))notifyChunks(statusChar,"DYNOTL READY");
  }
};
void dynoBleBegin(){
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(185);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);

  BLEServer *srv=BLEDevice::createServer();srv->setCallbacks(new ServerCB());
  BLEService *svc=srv->createService(SERVICE_UUID);
  liveChar=svc->createCharacteristic(LIVE_UUID,BLECharacteristic::PROPERTY_NOTIFY|BLECharacteristic::PROPERTY_READ);liveChar->addDescriptor(new BLE2902());
  commandChar=svc->createCharacteristic(COMMAND_UUID,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR);commandChar->setCallbacks(new CmdCB());
  statusChar=svc->createCharacteristic(STATUS_UUID,BLECharacteristic::PROPERTY_NOTIFY|BLECharacteristic::PROPERTY_READ);statusChar->addDescriptor(new BLE2902());
  svc->start();

  // ESP32-S3: keep the 128-bit service UUID in the primary advertisement.
  // Put the longer device name in scan response so both fit legacy BLE payloads.
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->reset();

  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setCompleteServices(BLEUUID(SERVICE_UUID));
  adv->setAdvertisementData(advData);

  BLEAdvertisementData scanData;
  scanData.setName(DEVICE_NAME);
  scanData.addTxPower();
  adv->setScanResponseData(scanData);

  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println();
  Serial.println("========================================");
  Serial.println("[DynoTL] ESP32-S3 BLE advertising STARTED");
  Serial.printf("[DynoTL] Name    : %s\n", DEVICE_NAME);
  Serial.printf("[DynoTL] Address : %s\n", BLEDevice::getAddress().toString().c_str());
  Serial.printf("[DynoTL] Service : %s\n", SERVICE_UUID);
  Serial.println("========================================");
}
void setup(){
  Serial.begin(115200);
  delay(300);
  dynoBleBegin();
  // KEEP / INSERT YOUR EXISTING HALL, ENGINE RPM AND AFR SENSOR INITIALIZATION HERE.
}
void loop(){
  // KEEP YOUR EXISTING SENSOR LOOP HERE.
  // Whenever a fresh sample is ready call:
  // dynoPublishSample(millis(), rollerRPM, engineRPM, afrVoltage);
  delay(1);
}