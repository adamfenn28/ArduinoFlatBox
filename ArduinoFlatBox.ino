#include <dummy.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

/*
  What: LEDLightBoxAlnitak - PC controlled lightbox implmented using the
    Alnitak (Flip-Flat/Flat-Man) command set found here:
    https://www.optecinc.com/astronomy/catalog/alnitak/resources/Alnitak_GenericCommandsR4.pdf
  Who:
    Created By: Jared Wellman - jared@mainsequencesoftware.com
    Modified By: Robert Pascale - implemented the PWM code for 31kHz - reverted from the V4 protocol as it was flaky.
    Modified By: Adam Fenn - made it work with the ESP32

  When:
    Last modified:  2020/Jul/28


  Typical usage on the command prompt:
  Send     : >SOOO\r      //request state
  Recieve  : *S19OOO\n    //returned state

  Send     : >B128\r      //set brightness 128
  Recieve  : *B19128\n    //confirming brightness set to 128

  Send     : >JOOO\r      //get brightness
  Recieve  : *B19128\n    //brightness value of 128 (assuming as set from above)

  Send     : >LOOO\r      //turn light on (uses set brightness value)
  Recieve  : *L19OOO\n    //confirms light turned on

  Send     : >DOOO\r      //turn light off (brightness value should not be changed)
  Recieve  : *D19OOO\n    //confirms light turned off.
*/

volatile int ledPin = 13;      // the pin that the LED is attached to, needs to be a PWM pin.
int brightness = 0;

// setting PWM properties
const int freq = 5000; // need to test this to see if it's high enough
const int ledChannel = 0;
const int resolution = 8;
BluetoothSerial BT; // Bluetooth Object


enum devices
{
  FLAT_MAN_L = 10,
  FLAT_MAN_XL = 15,
  FLAT_MAN = 19,
  FLIP_FLAT = 99
};

enum motorStatuses
{
  STOPPED = 0,
  RUNNING
};

enum lightStatuses
{
  OFF = 0,
  ON
};

enum shutterStatuses
{
  UNKNOWN = 0, // ie not open or closed...could be moving
  CLOSED,
  OPEN
};


int deviceId = FLAT_MAN;
int motorStatus = STOPPED;
int lightStatus = OFF;
int coverStatus = UNKNOWN;

void setup()
{

  // initialize the serial communication:
  Serial.begin(9600); // Not sure this is needed but it's not hurting anything
  BT.begin("FLAT");
  Serial.println("The device started, now you can pair it with bluetooth!");
  
  // configure LED PWM functionalitites
  ledcSetup(ledChannel, freq, resolution);
  
  // attach the channel to the GPIO to be controlled
  ledcAttachPin(ledPin, ledChannel);
}

void loop()
{
  handleSerial();
}


void handleSerial()
{
  if ( BT.available() >= 6 ) // all incoming communications are fixed length at 6 bytes including the \n
  
  {
    char* cmd;
    char* data;
    char temp[10];

    int len = 0;

    char str[20];
    memset(str, 0, 20);

    BT.readBytesUntil('\r', str, 20);

    cmd = str + 1;
    data = str + 2;

    switch ( *cmd )
    {
      /*
        Ping device
        Request: >POOO\r
        Return : *PidOOO\n
          id = deviceId
      */
      case 'P':
        sprintf(temp, "*P%dOOO\n", deviceId); // MADE CHANGE FROM PRINTLN TO PRINT HERE
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
        sprintf(temp, "*O%dOOO\n", deviceId); // MADE CHANGE FROM PRINTLN TO PRINT HERE
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
        sprintf(temp, "*C%dOOO\n", deviceId); // MADE CHANGE FROM PRINTLN TO PRINT HERE
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
        sprintf(temp, "*L%dOOO\n", deviceId);     // MADE CHANGE FROM PRINTLN TO PRINT HERE
        BT.print(temp);
        lightStatus = ON;
        ledcWrite(ledChannel, brightness);
        break;

      /*
        Turn light off
        Request: >DOOO\r
        Return : *DidOOO\n
          id = deviceId
      */
      case 'D':
        sprintf(temp, "*D%dOOO\n", deviceId); // MAKING TEST CHANGE HERE WITH THE REMOVAL OF PRINTLN AND REPLACING WITH JUST THE NEW LINE CHARACTER
        BT.print(temp);
        lightStatus = OFF;
        ledcWrite(ledChannel, 0);
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
        brightness = atoi(data);
        if ( lightStatus == ON ) 
          ledcWrite(ledChannel, brightness);
        sprintf( temp, "*B%d%03d\n", deviceId, brightness ); // MADE CHANGE FROM PRINTLN TO PRINT HERE
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
        sprintf(temp, "*J%d%03d\n", deviceId, brightness); // MADE CHANGE FROM PRINTLN TO PRINT HERE
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
        sprintf( temp, "*S%d%d%d%d\n", deviceId, motorStatus, lightStatus, coverStatus); // MADE CHANGE FROM PRINTLN TO PRINT HERE
        BT.print(temp);
        break;

      /*
        Get firmware version
        Request: >VOOO\r
        Return : *Vii001\n
          id = deviceId
      */
      case 'V': // get firmware version
        sprintf(temp, "*V%d001\n", deviceId);   // MADE CHANGE FROM PRINTLN TO PRINT HERE
        BT.print(temp);
        break;
    }

    while ( BT.available() > 0 )
      BT.read();

  }
}

void SetShutter(int val)
{
  if ( val == OPEN && coverStatus != OPEN )
  {
    coverStatus = OPEN;
    // TODO: Implement code to OPEN the shutter.
  }
  else if ( val == CLOSED && coverStatus != CLOSED )
  {
    coverStatus = CLOSED;
    // TODO: Implement code to CLOSE the shutter
  }
  else
  {
    // TODO: Actually handle this case
    coverStatus = val;
  }

}
