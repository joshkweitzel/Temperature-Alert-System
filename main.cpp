//Temperature Alert and Monitor System
//Public Repo
//Joshua Weitzel 2025-07-24
//Version 1.5

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <esp_system.h>

//Definitions
#define BOT_TOKEN "token"

//Variable declarations
String SSID = "";                                   //wifi name
String PASS = "";                                   //wifi password
const int TEMP_PIN = 4;                             //Temperature sensor GPIO pin
const int CHECK_INTERVAL = 1000;                    //Message request check interval in milliseconds
const int ALERT_INTERVAL = 60000;                   //Alert check interval in milliseconds
const unsigned long RESTART_INTERVAL = 86400000UL;  //System resets every 24 hours
bool alertActive = false;                           //Tracks whether an alert is currently being issued
bool criticalAlertActive = false;                   //Tracks whether a critical alert is currently being issued
unsigned long checkTime = 0;                        //Keep track of when the bot runs a check
unsigned long alertTime = 0;                        //Keep track of when alert check/send is ran
float currentTemp = 15.0;                           //Tracks current temperature, default value gets updated by sensor
float minTemp;                                      //Default minimum temperature
float maxTemp;                                      //Default maximum temperature
float criticalTemp = 1.0;                           //Critical low temperature, nearly freezing
char* sepPtr = nullptr;                             //Used to index messages during separation
bool notifs;                                        //Used to enable/disable alarm notifications

WiFiClientSecure client;                        //wifi client for bot communication
UniversalTelegramBot bot(BOT_TOKEN, client);    //Telegram bot addressed by the token
OneWire oneWire(TEMP_PIN);                      //Used to extract data from sensor
DallasTemperature tempSensor(&oneWire);         //Interprets data from sensor into temperature data
Preferences tempSettings;                       //Saves data between power cycles


String idWhiteList[] = {"userid1", "userid2"};                      //Stores allowed chat id's
short int numUsers = sizeof(idWhiteList) / sizeof(idWhiteList[0]);  //Number of allowed users
String alertUsers[] = {"userid1"};                                  //chat id's that will be sent alerts
short int numAlerts = sizeof(alertUsers) / sizeof(alertUsers[0]);   //Number of users being sent alerts

//Function declarations
void wifiConnect();

void processMessages(int numMessages);

bool validateID(String id);

void alert();

void readTemp();

void loadData();

void saveData();

String getFirstWord(String& mess);

String getSecondWord();

//Program start and loop
void setup() {
    Serial.begin(115200);
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    tempSensor.begin();
    loadData();
    readTemp(); //Runs once to prime the sensor
    wifiConnect();
}

void loop() {
    //Maintain internet connection before continuing
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(5000);
    }

    //Keep track of time for running check and alert intervals
    unsigned long currentTime = millis();

    //Check for messages to process
    if(currentTime - checkTime >= CHECK_INTERVAL){
        int newMessages = bot.getUpdates(bot.last_message_received + 1);
        while(newMessages){
            processMessages(newMessages);
            newMessages = bot.getUpdates(bot.last_message_received + 1);
        }
        checkTime = millis();
    }

    //Check for alert and read temperature
    if(currentTime - alertTime >= ALERT_INTERVAL){
        readTemp();
        alert();
        alertTime = millis();
    }

    //Check to see if the system must be restarted
    if(currentTime >= RESTART_INTERVAL){
        Serial.println("Restarting...");
        esp_restart();
    }
}

//Function definitions

//Connect to a wifi network
void wifiConnect(){
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASS);
    Serial.print("Connecting to WiFi");
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }

    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

//Process messages being sent to the bot
void processMessages(int numMessages){
    //Process all messages sent within the time interval
    for(int i = 0; i < numMessages; i++){
        //Skip over messages sent by unrecognized users
        if(!validateID(bot.messages[i].chat_id)){
            continue;
        }
        
        //Store message recieved and the username who sent it
        String message = bot.messages[i].text;
        String sender = bot.messages[i].from_name;
        String replyId = bot.messages[i].chat_id;

        Serial.println("Received, " + message + " from " + sender);

        //Execute commands and issue replies
        if(message == "/start"){
            String reply = "Hello " + sender + ", Here is a list of commands: \n";
            reply += "/start - command list\n";
            reply += "/status - show the current status of the environment\n";
            reply += "/setmin - set minimum temperature to raise alarm, ex) /setmin 12\n";
            reply += "/setmax - set maximum temperature to raise alarm\n";
            reply += "/notif - toggles whether alarm notifications are on/off\n";
            bot.sendMessage(replyId, reply);
        }

        if(message == "/status"){
            readTemp();
            String reply = "Temperature: " + String(currentTemp) + "\n";
            reply += "Alarm status: ";
            if(alertActive){
                reply += "triggered\n";
            } else {
                reply += "armed\n";
            }

            reply += "Alarm notifications: ";
            if(notifs){
                reply += "Enabled\n";
            } else {
                reply += "Disabled\n";
            }
            reply += "Minimum temperature: " + String(minTemp) + "\n";
            reply += "Maximum temperature: " + String(maxTemp) + "\n";
            bot.sendMessage(replyId, reply);
        }

        if(getFirstWord(message) == "/setmin"){
            float setPt = getSecondWord().toFloat(); //Pull the number following the command and convert to float

            //If toFloat() fails it returns 0.0. In this application the minTemp will never have to be 0 anyways
            //MinTemp cannot be greater than maxTemp
            if(setPt == 0.0 || setPt >= maxTemp){
                bot.sendMessage(replyId, "Please enter a valid number after /setmin");
            } else {
                minTemp = setPt;
                bot.sendMessage(replyId, "Minimum temperature set to " + String(minTemp));
                saveData();
            }
        }

        if(getFirstWord(message) == "/setmax"){
            float setPt = getSecondWord().toFloat();

            //maxTemp cannot be less than minTemp
            if(setPt == 0.0 || setPt <= minTemp){
                bot.sendMessage(replyId, "Please enter a valid number after /setmax");
            } else {
                maxTemp = setPt;
                bot.sendMessage(replyId, "Maximum temperature set to " + String(maxTemp));
                saveData();
            }
        }

        if(getFirstWord(message) == "/notif"){
            String reply = "Alarm notifications are ";

            if(notifs){
                notifs = false;
                reply += "disabled";
            } else {
                notifs = true;
                reply += "enabled";
            }
            bot.sendMessage(replyId, reply);
            saveData();
        }
    }
}

//Check incoming chat_ID against list of valid users
bool validateID(String id){
    for(int i = 0; i < numUsers; i++){
        if(id == idWhiteList[i]){
            return true;
        }
    }
    return false;
}

//Send alerts based on the set temperature range
void alert(){
    //Check whether to send an alert
    if(currentTemp < minTemp && !alertActive){
        alertActive = true;
        if(notifs){
            for(int i = 0; i < numAlerts; i++){
                bot.sendMessage(alertUsers[i], "TEMPERATURE IS BELOW MINIMUM THRESHOLD.");
            }
        }
    } else if(currentTemp > maxTemp && !alertActive){
        alertActive = true;
        if(notifs){
            for(int i = 0; i < numAlerts; i++){
                bot.sendMessage(alertUsers[i], "TEMPERATURE IS ABOVE MAXIMUM THRESHOLD.");
            }
        }
    } else if(currentTemp > minTemp && currentTemp < maxTemp && alertActive) {
        alertActive = false;
    }

    //Check whether to send out a critical alert
    if(currentTemp < criticalTemp){
        criticalAlertActive = true;
        if(notifs){
            for(int i = 0; i < numAlerts; i++){
                bot.sendMessage(alertUsers[i], "CRITICAL ALERT, TEMPERATURE IS NEAR FREEZING.");
            }
        }
    } else if (currentTemp > criticalTemp + 1 && criticalAlertActive){
        criticalAlertActive = false;
    }
}

//Read the the temperature from the sensor
void readTemp(){
    tempSensor.requestTemperatures();
    currentTemp = tempSensor.getTempCByIndex(0);
    Serial.println(currentTemp);
}

//Load in saved data stored regarding the temperature ranges
//The numbers next to the key name are default values
void loadData(){
    tempSettings.begin("tempSettings", true);
    minTemp = tempSettings.getFloat("minTemp", 10.0);
    maxTemp = tempSettings.getFloat("maxTemp", 35.0);
    notifs = tempSettings.getBool("notifs", true);
    tempSettings.end();
}

//Save data after updating values so that the settings persist after power cycles
void saveData(){
    tempSettings.begin("tempSettings", false);
    tempSettings.putFloat("minTemp", minTemp);
    tempSettings.putFloat("maxTemp", maxTemp);
    tempSettings.putBool("notifs", notifs);
    tempSettings.end();
}

//Used to seperate the first instruction out of a message in chained messages
String getFirstWord(String& mess){
    String word1;
    sepPtr = const_cast<char*>(mess.c_str()); //Can't believe this works, cast to fixed memory location

    //Add characters from the first word until a space or the null terminator is reached
    while(*sepPtr != ' '){
        if(*sepPtr == '\0'){
            break;
        }
        word1 += *sepPtr;
        sepPtr++;
    }
    
    return word1;
}

//Used in tandem with getFirstWord to return the second word associated with the chained message
//The sepPtr should be pointing to the space within the message if used correctly
String getSecondWord(){
    String word2;
    if (*sepPtr == '\0') return "";
    sepPtr++; //Move the pointer ahead to skip the space

    //Now the pointer will hit the end of the message for sure, and it will encounter the null terminator
    while(*sepPtr != '\0'){
        word2 += *sepPtr;
        sepPtr++;
    }

    return word2;
}
