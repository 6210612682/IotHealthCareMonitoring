#include <Arduino.h>
#include "BLEDevice.h"
#include <BLEScan.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "PubSubClient.h"
#include <HTTPClient.h>
#include "AutoConnect.h"
#include <BLEUtils.h>
#include "BLEAdvertisedDevice.h"


/**For Device**/

//BloodPress//
static BLEAddress BloodPressAddress("e3:f0:01:00:1c:c0");
static BLEUUID serviceBloodPressUUID("FFF0");

//Oximeter//
static BLEAddress OximeterAddress("ce:7d:06:bb:21:00");
static BLEUUID serviceOximeterUUID("CDEACB80-5235-4C07-8846-93A37EE6B86D");

//Characteristic//
static BLEUUID charBloodPressUUID("FFF4");
static BLEUUID charOximeterUUID("CDEACB81-5235-4C07-8846-93A37EE6B86D");
static BLERemoteCharacteristic* pRemoteCharacteristicBloodPress;
static BLERemoteCharacteristic* pRemoteCharacteristicOximetorData;

static BLEAdvertisedDevice* BloodPressMonitor;
static BLEAdvertisedDevice* Oximetor;

static boolean BloodPressFinished = false;
static boolean OximetorFinished = false;


/*For Scan.*/
static BLEScan* pBLEScan = BLEDevice::getScan();
static boolean connected = false;
static boolean doScan = false;
static unsigned int connectionTimeMs = 0;
static BLEAdvertisedDevice deviceCheck;

/*For Pull Data*/
static unsigned int count = 0;
static BLEClient* pClient = BLEDevice::createClient();

/*MQTT*/
static PubSubClient client(wfClient);
const char * broker = "mqtt.eclipseprojects.io";

static void setupMQTT() {
  Serial.println(" + MQTT Connecting.");

  client.setServer(broker, 1883);

  while (client.connected() != MQTT_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println(" + Complete.");
}

/*For WiFi*/
static WiFiClient wfClient;
WebServer Server;
AutoConnect Portal(Server);

void rootPage() {
  char content[] = "Hello, world";
  Server.send(200, "text/plain", content);
}

static void setupWifi() {
  Portal.begin();
  Server.on("/", rootPage);

  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Web server started:" + WiFi.localIP().toString());
  Serial.println("");
  Serial.println(" + WiFi Connected.");
  Serial.print(" + IP address: ");
  Serial.println(WiFi.localIP());
}


static void notifyBloodPress( BLERemoteCharacteristic* pBLERemoteCharacteristic,uint8_t* pData,size_t length,bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    Serial.print("data (HEX): ");
    for (int i = 0; i < length; i++) {
      Serial.print(pData[i],HEX);
      Serial.print(" ");
    }
    Serial.println();
    
    char output[45]; 
    char mqttoutput[45];
    uint8_t sys = pData[2];
    uint8_t dia = pData[4];
    uint8_t pul = pData[8];
    uint8_t mea_status = pData[12];
    if(mea_status == 0) {
      sprintf(output, "SYS:%3u|DIA:%2u|PUL:%3u", sys, dia, pul);
      Serial.println(output);
      BloodPressFinished = true;
    } 
    
    if(length>5) {
      //const uint8_t turnoff[] = {0xFD,0xFD,0xFA,0x06,0x0D,0x0A};
      client.publish("panIot/BloodPressResult", output);
      connected = false;
      //pRemoteCharacteristicBloodPress->getDescriptor(charBloodPressUUID)->writeValue((uint8_t*)turnoff, 6, false);
      pClient->disconnect();
      Serial.println("onDisconnected");
  
    }
  } 

static void notifyOximeter(BLERemoteCharacteristic* pBLERemoteCharacteristic,uint8_t* pData,size_t length,bool isNotify) {
    if(length < 5) {
      Serial.print("Notify Oximetor for characteristic ");
      Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
      Serial.print(" of data length ");
      Serial.println(length);
      Serial.print("data (HEX): ");

      for (int i = 0; i < length; i++) {
      Serial.print(pData[i],HEX);
      Serial.print(" ");
      }
      Serial.println();
      char output[45];
      char temp[3];
      uint8_t pul = pData[1];
      uint8_t perf = pData[3];
      uint8_t sat = pData[2];
      uint8_t sat_check = 0x127;
      uint8_t pul_check = 0x255;
      uint8_t perf_check = 0x0;

      if(sat!=sat_check&&pul!=pul_check&&perf!=perf_check&&count<5) {
        sprintf(output, "SAT:%3u|PUL:%3u|PI%%:%3u", sat, pul, perf);
        count++;
        Serial.println(output);
        if(count==5) {
          client.publish("panIot/Oximetor", output); //publish to MQTT
          pClient->disconnect();
          connected = false;
          OximetorFinished = true;
        }
      } else {
        sprintf(output, "Please wait...");
        Serial.println(output);
        count=0;
      }
    }
}


class MyClientCallback:public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {

  }

  void onDisconnect(BLEClient* pclient) {
    connected=false;
    Serial.println(" - onDisconnect");
  }
};

bool connectToBloodPressServer() {

    Serial.println(" * In connectToBloodPressServer()");
    Serial.print(" * Forming a connection to ");
    Serial.println(BloodPressMonitor->getAddress().toString().c_str());

    if(!(pClient->isConnected())) {
      pClient = BLEDevice::createClient();
    }
    Serial.println(" + Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(BloodPressMonitor); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    if(!(pClient->isConnected())) {
      pClient->connect(BloodPressMonitor);
    } 
    Serial.println(" + Connected to BloodPressServer");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceBloodPressUUID);
    if (pRemoteService == nullptr) {
      Serial.print(" - Failed to find our service UUID: ");
      Serial.println(serviceBloodPressUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" + Found BloodPress service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristicBloodPress = pRemoteService->getCharacteristic(charBloodPressUUID);
    if (pRemoteCharacteristicBloodPress == nullptr) {
      Serial.print(" - Failed to find our charMeasurementUUID: ");
      Serial.println(charBloodPressUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" + Found our BloodPressDataCharacteristic");
    Serial.println(pRemoteCharacteristicBloodPress->canWrite());

    // Read the value of the characteristic.
    if(pRemoteCharacteristicBloodPress->canRead()) {
      Serial.println(" + Blood Press monitor Can read.");
      std::string value = pRemoteCharacteristicBloodPress->readValue();
      byte buf[64] = {0};
      memcpy(buf,value.c_str(),value.length());

      Serial.print("The characteristic value was: ");
      for(int i=0; i<value.length(); i++) {
        Serial.print(buf[i]);
        Serial.print(" ");
      }
      Serial.println();
    } 

    if(pRemoteCharacteristicBloodPress->canWrite()) {
      Serial.println("BloodPress can write.");
    }
    
    
    if(pRemoteCharacteristicBloodPress->canNotify()) {
      Serial.println(" - Blood Press can Notify Data.");
      pRemoteCharacteristicBloodPress->registerForNotify(notifyBloodPress);
    } else {
      Serial.println(" - Blood Press can not Notify Data.");
      return false;
    }


    //Set Noti true// 
    pRemoteCharacteristicBloodPress->readValue();
    const uint8_t notificationOn[] = {0x1, 0x0};
    pRemoteCharacteristicBloodPress->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);

    connected = true;

    return true;
}

bool connectToOximeterServer() {

    Serial.println(" * In connectToOximeterServer()");
    Serial.print(" * Forming a connection to ");
    Serial.println(Oximetor->getServiceUUID().toString().c_str());

    if(!(pClient->isConnected())) {
      pClient = BLEDevice::createClient();
    }
    Serial.println(" + Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    pClient->connect(Oximetor);
    if(!(pClient->isConnected())) {
      pClient->connect(Oximetor);
    }
    Serial.println(" + Connected to OximetorServer");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceOximeterUUID);
    
    if (pRemoteService == nullptr) {
      Serial.print(" - Failed to find our service UUID: ");
      Serial.println(serviceOximeterUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" + Found Oximeter service");
   

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristicOximetorData = pRemoteService->getCharacteristic(charOximeterUUID);
    if (pRemoteCharacteristicOximetorData == nullptr) {
      Serial.print(" - Failed to find our charOximeterUUID: ");
      Serial.println(charOximeterUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" + Found our OximeterDataCharacteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristicOximetorData->canRead()) {
      Serial.println(" + Oximeter Can read.");
      
      std::string value = pRemoteCharacteristicOximetorData->readValue();
      byte buf[64] = {0};
      memcpy(buf,value.c_str(),value.length());

      Serial.print(" + The characteristic value was: ");
      for(int i=0; i<value.length(); i++) {
        Serial.print(buf[i]);
        Serial.print(" ");
      }
      Serial.println();
    }
    
    if(pRemoteCharacteristicOximetorData->canWrite()) {
      Serial.println(" + Oximetor can Write Data.");
    }

    if(pRemoteCharacteristicOximetorData->canNotify()) {
      Serial.println(" + Oximeter can Notify Data.");
      pRemoteCharacteristicOximetorData->registerForNotify(notifyOximeter, true, true);
   
    } else {
      Serial.println(" - Oximeter can not Notify Data.");
      return false;
    }

    //Set Noti true// 
    pRemoteCharacteristicOximetorData->readValue();
    const uint8_t notificationOn[] = {0x1, 0x1};
    pRemoteCharacteristicOximetorData->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);

    connected = true;
    OximetorFinished = true;
    return true;

}


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print(" -> BLE Advertised Device found : ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if ((advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceBloodPressUUID)
    || advertisedDevice.getAddress() == BloodPressAddress) && BloodPressFinished == false) {

      BLEDevice::getScan()->stop();
      BloodPressMonitor = new BLEAdvertisedDevice(advertisedDevice);
      doScan = true;
 
    } else if((advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceOximeterUUID)
    || advertisedDevice.getAddress() == OximeterAddress) && OximetorFinished == false) {
      
      BLEDevice::getScan()->stop();
      Oximetor = new BLEAdvertisedDevice(advertisedDevice);
      doScan = true;

    } 
  
    deviceCheck = advertisedDevice;
  } // onResult
}; // MyAdvertisedDeviceCallbacks

void mqttReconnect() {
  while(!client.connected()) {
    Serial.println("Attempting MQTT connection...");

    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if(client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.publish("panIot/MQTTStatus", "MQTT Connected");
    } else {
      Serial.println("failed, rc=");
      Serial.println(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void startScan() {
  Serial.println(" + Starting Arduino BLE Client application...");
  BLEDevice::init("");

  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5,false);

}//End of Setup.

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  setupWifi();
  setupMQTT();
  startScan();
  delay(3000);

}

void loop() {
  // put your main code here, to run repeatedly:

  //AutoConnect()
  Portal.handleClient();
  bool connectStatus = pClient->isConnected();
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.

  if(client.connected()) {
    if (!connectStatus) {
      if(BloodPressFinished == false && deviceCheck.getAddress() == BloodPressAddress){

        if (connectToBloodPressServer()) {
          
          Serial.println("We are now connected to the BLE Server.");

        } else {
          
          Serial.println("We have failed to connect to the server; there is nothin more we will do.");

        }

      } else if(OximetorFinished == false && (deviceCheck.getAddress() == OximeterAddress) ) {

        if (connectToOximeterServer()) {
          Serial.println("We are now connected to the BLE Server.");

        } else {
          Serial.println("We have failed to connect to the server; there is nothin more we will do.");

        }
      } 
    }
  


    // If we are connected to a peer BLE Server, update the characteristic each time we are reached
    // with the current time since boot.
    if (connected) {

      /*
      if (pRemoteCharacteristicMeasurementData->canWrite() && BloodPressFinished != true) {
        // Set the characteristic's value to be the array of bytes that is actually a string.
  
        String newValue = "Time since boot: " + String(millis()/1000);
        Serial.println("Setting new characteristic value to \"" + newValue + "\"");
        pRemoteCharacteristicMeasurementData->writeValue(newValue.c_str(), newValue.length());

      } else if(pRemoteCharacteristicOximeterData->canWrite() && OximeterFinished != true) {

        String newValue = "Time since boot: " + String(millis()/1000);
        Serial.println("Setting new characteristic value to \"" + newValue + "\"");
        pRemoteCharacteristicOximeterData->writeValue(newValue.c_str(), newValue.length());
      } 
      */
    }
    else {
      if (doScan) {
        BLEDevice::getScan()->start(0);  // this is just an example to start scan after disconnect, most likely there is better way to do it in arduino
      }
      else { // enable connects if no device was found on first boot
        if (millis() > connectionTimeMs + 6000) {
          Serial.println("Enabling scanning.");
          doScan = true;
        }
      }
    } 
  } else {
    mqttReconnect();
  }
  
    delay(1000); // Delay a second between loops.
} 