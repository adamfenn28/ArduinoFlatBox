## What: LEDLightBoxAlnitak - PC controlled lightbox implmented using the Alnitak (Flip-Flat/Flat-Man) command set found here:  https://www.optecinc.com/astronomy/catalog/alnitak/resources/Alnitak_GenericCommandsR4.pdf

## Who:
 Created By: Jared Wellman - jared@mainsequencesoftware.com
 Modified By: Robert Pascale - implemented the PWM code for 31kHz - reverted from the V4 protocol as it was flaky.
 Modified By: Adam Fenn - made it work with the ESP32
 Modified By: Adam Fenn - added support for webserver control, physical button control and a 7 segment display.

## When:
 Last modified:  2020/Oct/04
 

This is modified version of the ArduinoFlat project, which includes a bluetooth version and another version with a pushbuttons and a 7 segment display as well as a web interface.  

It's intended for use along with a tracing pad as a high quality flat panel for astrophotography.  See full details here:

https://adamfenn.me/f/automating-flats-with-the-asiair
