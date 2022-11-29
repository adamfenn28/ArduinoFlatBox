#include <WiFi.h>
#include <string.h>
#include <time.h>
#include <TM1637Display.h>
#include "BluetoothSerial.h"

/*
 * What: LEDLightBoxAlnitak - PC controlled lightbox implmented using the
 * Alnitak (Flip-Flat/Flat-Man) command set found here:
 * https://www.optecinc.com/astronomy/catalog/alnitak/resources/Alnitak_GenericCommandsR4.pdf
 * Who:
 *  Created By: Jared Wellman - jared@mainsequencesoftware.com
 *  Modified By: Robert Pascale - implemented the PWM code for 31kHz - reverted from the V4 protocol as it was flaky.
 *  Modified By: Adam Fenn - made it work with the ESP32
 *  Modified By: Adam Fenn - added support for webserver control, physical button control and a 7 segment display.
 *
 * When:
 *   Last modified:  2020/Oct/04
 * 
 * 
 * Typical usage on the command prompt as NINA would useuse.
 * Send     : >SOPOO\r      //request state
 * Recieve  : *S19OOO\n    //returned state
 * 
 * Send     : >B128\r      //set brightness 128
 * Recieve  : *B19128\n    //confirming brightness set to 128
 *
 * Send     : >JOOO\r      //get brightness
 * Recieve  : *B19128\n    //brightness value of 128 (assuming as set from above)
 * 
 * Send     : >LOOO\r      //turn light on (uses set brightness value)
 * Recieie  : *L19OOO\n    //confirms light turned on
 * 
 * Send     : >DOOO\r      //turn light off (brightness value should not be changed)
 * Receive  : *D19OOO\n    //confirms light turned off.
 * 
 * When accessing via the webserver just hit the IP address of the device as configured below. 
 */

IPAddress local_IP(192, 168, 1, 212);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);
IPAddress secondaryDNS(8, 8, 8, 8);

/*
 * You will also need to adjust the wifi network name and password here
 */

#define WIFI_NAME "ZWO"
#define WIFI_PASSWORD "simplepassword"

/*
 * Set the Bluetooth name 
 */

#define BT_NAME "FLAT2"

/*
 * Once configured, the interface is quite simple and needs no explanation.
 * 
 *
 * For the physical interface, there are only increase and decrease buttons.  To turn the
 * panel off, decrease to zero and then decrease once more.  The display will show OFF.  
 * Pressing the increase button will toggle the panel from OFF to ON status.
 * 
 * 
 * Configure your pins below
 */

// TODO #define all of these
#define LED 27   // the pin that the LED is attached to, needs to be a PWM pin.
#define CLK 26   // the clock pin for the 7 segment display
#define DIO 25   // the output pin the 7 segment display
#define UP 18    // the input pin for the up button
#define DOWN 19  // the input pin for the down button


/*
 * Set your default brightness here
 */

int brightness = 50;  // Ranges from 0-255 brigtness levels.


/* 
 * End of typical configuration
 /*/


// setting PWM properties
const int freq = 10000;     // for no noticable flicker in my testing
const int ledChannel = 0;   // the PWM channel the pin will be mapped to
const int resolution = 14;  // using more htan 8 bits to get fine granularity on the low end to make very bright panels very dim.

// create the Bluetooth object
BluetoothSerial BT;


// Setup DIY Alnitak Emulation
enum devices {
  FLAT_MAN_L = 10,
  FLAT_MAN_XL = 15,
  FLAT_MAN = 19,
  FLIP_FLAT = 99
};

enum motorStatuses {
  STOPPED = 0,
  RUNNING
};

enum lightStatuses {
  OFF = 0,
  ON
};

enum shutterStatuses {
  UNKNOWN = 0,  // ie not open or closed...could be moving
  CLOSED,
  OPEN
};


int deviceId = FLAT_MAN;
int motorStatus = STOPPED;
int lightStatus = OFF;
int coverStatus = UNKNOWN;


// Setup Webserver
WiFiServer server(80);
char BRIGHTNESS[15];


// Setup physical interface
time_t darkTime;


// Create timers to continue operating the physical interface while stuck in other loops
hw_timer_t* darkTimer = NULL;
hw_timer_t* buttonTimer = NULL;

// create one more for checking for input over Bluetooth
hw_timer_t* bluetoothTimer = NULL;


// create muxes for locking while writing to variables shared by timers
portMUX_TYPE darkTimerMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE buttonTimerMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE bluetoothTimerMux = portMUX_INITIALIZER_UNLOCKED;


// Create a display object of type TM1637Display
TM1637Display display = TM1637Display(CLK, DIO);


// Create an array that sets individual segments per digit to display the word "OFF"
const uint8_t off[] = {
  0x00,                                           // _
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,  // O
  SEG_A | SEG_E | SEG_F | SEG_G,                  // F
  SEG_A | SEG_E | SEG_F | SEG_G                   // F
};

// the standard setup function we run before the loops starts
void setup() {
  Serial.begin(115200);

  // give a few seconds after setting tup the seerial port so we don't miss anything
  delay(5000);

  initDisplay();

  initPWM();

  initButtons();

  createDarkTimer();    // TODO: are we already ready?
  createButtonTimer();  // TODO: are we already ready?

  initBluetooth();
  createBluetoothTimer();

  //bring up wifi on static IP address
  initNetwork();

  // actually starts the webserver
  server.begin();
}

void loop() {

  // check for input from the Webserver
  handleWebServer();
}

// This is the one place where the brightness is changed
void setBrightness(int b) {
  if (b > 255)
    b = 255;

  if (b < 0)
    b = 0;

  display.clear();

  brightness = b;

  display.showNumberDec(b);
  setDarkTime();
  Serial.print("Setting to: ");
  Serial.println(b);

  // change the PWM pin
  //int dutyCycle = .0945 + .055 * pow(b + 3, 2.269846108);  // Stretching the values so we have finer granularity at the dim levels
  //if (dutyCycle > 16383)
  //  dutyCycle = 16383;
  if (lightStatus == ON)
    ledcWrite(ledChannel, brightnessToDutyCycle(b));
}

int brightnessToDutyCycle(int i)
{
  int dc;

  if (i > 255)
    i = 255;

  if (i < 0)
    i = 0;

  
  if (i < 30 )
    dc = i + 1;
  else 
    dc = 29.9 + 1.1 *  pow(i - 29, 1.62413);

  if (dc > 7354)
    dc = 7354;

  return dc;
}
void increase() {
  if (brightness < 255) {
    setBrightness(++brightness);
  }
}

void decrease() {

  if (brightness > 0) {
    setBrightness(--brightness);
  } else {
    display.clear();
    display.setSegments(off);
    Serial.println("Resetting to 0");
    brightness = 0;
    setDarkTime();
  }
}

void enable() {
  Serial.print("Toggling on to: ");
  Serial.print(brightness);
  Serial.println(" ON");
  display.clear();
  display.showNumberDec(brightness);
  lightStatus = ON;
  setBrightness(brightness);
  setDarkTime();
}

void disable() {
  Serial.print("Toggling off from: ");
  Serial.print(brightness);
  Serial.println(" OFF");
  display.clear();
  display.setSegments(off);
  ledcWrite(ledChannel, 0);
  lightStatus = OFF;
  setDarkTime();
}

void checkButtons() {
  portENTER_CRITICAL_ISR(&buttonTimerMux);

  int upButtonState = digitalRead(UP);
  // only toggle on if up came from buttons and is now 0

  // when we press the up/increase button
  if (upButtonState == HIGH) {
    if (lightStatus == ON) {
      increase();
    } else {
      enable();
    }
  }

  // when we press the down/decrease button
  int downButtonState = digitalRead(DOWN);
  if (downButtonState == HIGH) {
    if ((lightStatus == ON) && (brightness == 0)) {
      disable();
    } else {
      decrease();
    }
  }
  portEXIT_CRITICAL_ISR(&buttonTimerMux);
}

void IRAM_ATTR onDarkTimer() {
  // turn off the display if the last change was 5 seconds ago
  portENTER_CRITICAL_ISR(&darkTimerMux);
  if (darkTime < time(NULL)) {
    display.clear();
  }
  portEXIT_CRITICAL_ISR(&darkTimerMux);
}

// runs when the button timer is pressed
void IRAM_ATTR onButtonTimer() {
  checkButtons();
}


void IRAM_ATTR onBluetoothTimer() {
  if (BT.available() >= 6)  // all incoming communications are fixed length at 6 bytes including the \n

  {
    char* cmd;
    char* data;
    char temp[10];

    int len = 0;
    int level;

    char str[20];
    memset(str, 0, 20);
    size_t bytes = BT.readBytesUntil('\r', str, 20);

    cmd = str + 1;
    data = str + 2;

    switch (*cmd) {
      /*
         Ping device
         Request: >POOO\r
         Return : *PidOOO\n
           id = deviceId
       */
      case 'P':
        sprintf(temp, "*P%dOOO\n", deviceId);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        Serial.println(temp);
        BT.print(temp);
        break;

      /*
         Open shutter
         Request: >OOOO\r
         Return : *OidOOO\n
         id = deviceId

         This command is only supported on the Flip-Flat!
       */
      case 'O':
        sprintf(temp, "*O%dOOO\n", deviceId);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        SetShutter(OPEN);
        BT.print(temp);
        break;

      /*
         Close shutter
         Request: >COOO\r
         Return : *CidOOO\n
         id = deviceId

         This command is only supported on the Flip-Flat!
       */
      case 'C':
        sprintf(temp, "*C%dOOO\n", deviceId);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        SetShutter(CLOSED);
        BT.print(temp);
        break;

      /*
         Turn light on
         Request: >LOOO\r
         Return : *LidOOO\n
           id = deviceId
       */
      case 'L':
        sprintf(temp, "*L%dOOO\n", deviceId);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        BT.print(temp);
        enable();
        break;

      /*
         Turn light off
         Request: >DOOO\r
         Return : *DidOOO\n
           id = deviceId
       */
      case 'D':
        sprintf(temp, "*D%dOOO\n", deviceId);  // MAKING TEST CHANGE HERE WITH THE REMOVAL OF PRINTLN AND REPLACING WITH JUST THE NEW LINE CHARACTER
        BT.print(temp);
        disable();
        break;

      /*
         Set brightness
         Request: >Bxxx\r
           xxx = brightness value from 000-255
         Return : *Biiyyy\n
           id = deviceId
           yyy = value that brightness was set from 000-255
       */
      case 'B':
        level = atoi(data);
        setBrightness(level);
        sprintf(temp, "*B%d%03d\n", deviceId, level);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        BT.print(temp);
        break;

      /*
         Get brightness
         Request: >JOOO\r
         Return : *Jiiyyy\n
           id = deviceId
           yyy = current brightness value from 000-255
       */
      case 'J':
        portENTER_CRITICAL_ISR(&buttonTimerMux);
        sprintf(temp, "*J%d%03d\n", deviceId, brightness);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        portEXIT_CRITICAL_ISR(&buttonTimerMux);

        BT.print(temp);
        break;

      /*
         Get device status:
         Request: >SOOO\r
         Return : *SidMLC\n
           id = deviceId
           M  = motor status( 0 stopped, 1 running)
           L  = light status( 0 off, 1 on)
           C  = Cover Status( 0 moving, 1 closed, 2 open)
       */
      case 'S':
        portENTER_CRITICAL_ISR(&buttonTimerMux);
        sprintf(temp, "*S%d%d%d%d\n", deviceId, motorStatus, lightStatus, coverStatus);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        portEXIT_CRITICAL_ISR(&buttonTimerMux);
        BT.print(temp);
        break;

      /*
         Get firmware version
         Request: >VOOO\r
         Return : *Vii001\n
           id = deviceId
       */
      case 'V':                                // get firmware version
        sprintf(temp, "*V%d001\n", deviceId);  // MADE CHANGE FROM PRINTLN TO PRINT HERE
        BT.print(temp);
        break;
    }
    while (BT.available() > 0)
      BT.read();
  }
}

  void createButtonTimer() {
    Serial.println("Creating timer");
    // Create semaphore to inform us when the timer has fired
    //timerSemaphore = xSemaphoreCreateBinary();

    // Use 1st timer of 4 (counted from zero).
    // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
    // info).
    buttonTimer = timerBegin(1, 80, true);

    // Attach onTimer function to our timer.
    timerAttachInterrupt(buttonTimer, &onButtonTimer, true);

    // .15 seconds seem slong enoguh to pick up button presses, but not to detect 2 during a single press.
    timerAlarmWrite(buttonTimer, 150000, true);

    // Start an alarm
    timerAlarmEnable(buttonTimer);
  }


  void createDarkTimer() {
    // Use 1st timer of 4 (counted from zero).
    // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
    // info).
    darkTimer = timerBegin(0, 80, true);

    // Attach onTimer function to our timer.
    timerAttachInterrupt(darkTimer, &onDarkTimer, true);

    // so we clear the screen within a second of our 5 second target
    timerAlarmWrite(darkTimer, 1000000, true);

    // Start an alarm
    timerAlarmEnable(darkTimer);
  }

  void createBluetoothTimer() {
    // Use 1st timer of 4 (counted from zero).
    // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
    // info).
    bluetoothTimer = timerBegin(3, 80, true);

    // Attach onTimer function to our timer.
    timerAttachInterrupt(bluetoothTimer, &onBluetoothTimer, true);

    // so we clear the screen within a second of our 5 second target
    timerAlarmWrite(bluetoothTimer, 500000, true);

    // Start an alarm
    timerAlarmEnable(bluetoothTimer);
  }

  void initBluetooth() {
    Serial.println("Starting Bluetooth");

    if (BT.begin(BT_NAME))
      Serial.println("The device started, now you can pair it with bluetooth!");
    else
      Serial.println("BT failed to start");
  }

  void initDisplay() {
    Serial.println("Starting Display");
    // Set the brightness to 3 (0=dimmest 7=brightest)
    display.setBrightness(2);

    display.clear();
    display.showNumberDec(brightness);

    setDarkTime();
  }

  void setDarkTime() {
    portENTER_CRITICAL_ISR(&darkTimerMux);
    darkTime = time(NULL) + 5;
    portEXIT_CRITICAL_ISR(&darkTimerMux);
  }

  void initPWM() {
    Serial.println("Setting PWM Up");
    // configure LED PWM functionalitites
    ledcSetup(ledChannel, freq, resolution);

    // attach the channel to the GPIO to be controlled
    ledcAttachPin(LED, ledChannel);
  }

  void initButtons() {
    Serial.println("Setting Buttons Up");
    pinMode(UP, INPUT_PULLDOWN);
    pinMode(DOWN, INPUT_PULLDOWN);
  }

  void initNetwork() {
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("IP Setup Failure");
    }


    Serial.print("Connecting to ");
    Serial.println(WIFI_NAME);
    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

    Serial.println("Trying to connect to Wifi Network");

    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("Successfully connected to WiFi network");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }


  // someday it would be nice to design an automatic aperature cover component to this project
  void SetShutter(int val) {
    if (val == OPEN && coverStatus != OPEN) {
      coverStatus = OPEN;
      // TODO: Implement code to OPEN the shutter.
    } else if (val == CLOSED && coverStatus != CLOSED) {
      coverStatus = CLOSED;
      // TODO: Implement code to CLOSE the shutter
    } else {
      // TODO: Actually handle this case
      coverStatus = val;
    }
  }

  void handleWebServer() {
    // check for web input..
    WiFiClient client = server.available();

    if (client) {

      Serial.println("New Client is requesting web page");
      String header;
      String current_data_line = "";
      while (client.connected()) {
        if (client.available()) {

          char new_byte = client.read();
          Serial.write(new_byte);
          header += new_byte;
          if (new_byte == '\n') {

            if (current_data_line.length() == 0) {

              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              int start;

              start = header.indexOf("GET");
              String subHeader = header.substring(start);

              int to;
              to = subHeader.indexOf("Host");

              subHeader = subHeader.substring(start, to);

              int n = subHeader.length();

              if (subHeader.indexOf("ACTION=ON") != -1) {
                enable();
              }
              if (subHeader.indexOf("ACTION=OFF") != -1) {
                disable();
              }
              if (subHeader.indexOf("ACTION=UP") != -1) {
                increase();
              }
              if (subHeader.indexOf("ACTION=DOWN") != -1) {
                decrease();
              }
              if (subHeader.indexOf("ACTION=SET") != -1) {
                start = subHeader.indexOf("BRIGHTNESS=");

                if (start != -1) {
                  subHeader = header.substring(start);

                  to = subHeader.indexOf("&");

                  subHeader = subHeader.substring(11, to);
                  int n = subHeader.length();
                  char value[n + 1];
                  strcpy(value, subHeader.c_str());
                  Serial.println(value);
                  setBrightness(atoi(value));
                }
              }


              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              client.println("<style>html { font-family: Arial; display: inline-block; margin: 0px auto; text-align: center; color: white;}");
              client.println("h1 { padding-top: 100px; padding-bottom: 0px; }");
              client.println("h3 { padding-top: 0px; padding-bottom: 200px; }");
              client.println(".br { display: block; margin-bottom: 3em; }");
              client.println(".button { background-color: #00796b; border: 2px solid #00796b;; color: white; padding: 15px 32px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; cursor: pointer; width: 150px;}");
              client.println(".buttonDisabled { background-color: #174e4d; border: 2px solid #174e4d;; color: white; padding: 15px 32px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; cursor: pointer; width: 150px;}");
              client.println(".input { background-color: #303b41; border: 2px solid #303b41;; color: white; padding: 15px 0px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; cursor: pointer; }");
              client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
              // Web Page Heading
              client.println("</style></head>");
              client.println("<body style=\"background-color:#263238;\"><center><h1 padding-top=\"50\" padding-bottom=\"200\">ArduinoFlat+</h1><h3>Web Controller</h3></center>");

              client.println("<form><center>");
              client.println("<button class=\"button\" name=\"ACTION\" style=\"display: none\" value=\"SET\" type=\"submit\">Set</button>");

              client.println("<center><p><input class=\"input\" type=\"text\" pattern=\"\\d*\" id=\"BRIGHTNESS\" maxlength=\"3\" size=\"15\" name=\"BRIGHTNESS\" value=\"");

              portENTER_CRITICAL_ISR(&buttonTimerMux);
              int b = brightness;
              int status = lightStatus;
              portEXIT_CRITICAL_ISR(&buttonTimerMux);

              sprintf(BRIGHTNESS, "%d", b);

              client.println(BRIGHTNESS);
              client.println("\">");
              client.println("<button class=\"button\" name=\"ACTION\" value=\"SET\" type=\"submit\">Set</button><span class=\"br\"></span>");

              client.println("<button class=\"button\" name=\"ACTION\" value=\"DOWN\" type=\"submit\">Decrease</button>");
              client.println("<button class=\"button\" name=\"ACTION\" value=\"UP\" type=\"submit\">Increase</button><span class=\"br\"></span>");

              client.print("<button class=\"button");

              if (status == 1)
                client.print("Disabled");

              client.println("\" name=\"ACTION\" value=\"ON\" type=\"submit\">On</button>");

              client.print("<button class=\"button");

              if (status == 0)
                client.print("Disabled");

              client.println("\" name=\"ACTION\" value=\"OFF\" type=\"submit\">Off</button>");

              client.println("</center></form></body></html>");
              client.println();
              break;
            } else {
              current_data_line = "";
            }
          } else if (new_byte != '\r') {
            current_data_line += new_byte;
          }
        }
      }
      // Clear the header variable
      header = "";
      // Close the connection
      client.stop();
      Serial.println("Client disconnected.");
      Serial.println("");
    }
  }