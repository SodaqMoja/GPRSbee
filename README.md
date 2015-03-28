# GPRSbee

This is the Arduino library for GPRSbee.

## HTTP GET Methods

There is a group of functions to do a http GET. The main function is
doHTTPGET.  There are also lower level functions which you may need
when you want to do special things.  For example if you want to do
multiple GETs in a row you can use the following sequence:
```c
  on
  doHTTPprolog
  doHTTPGETmiddle
  doHTTPGETmiddle
  doHTTPGETmiddle
  doHTTPGETmiddle
  doHTTPepilog
  off
```
The function doHTTPGETmiddle does the actual GET.


Another example to use these lower level GET functions is if you want
to keep the GPRS connection up.

## On-Off Methods

There are two methods to switch on the GPRSbee.  The "old" method
is to toggle DTR.  It toggles the on-off state of the SIM900.  The
"new" method is to switch the power supply of GPRSbee.  This new
method is only supported by SODAQ Mbili.  This board has a JP2
conector which is switched on by setting D23 to HIGH.

### The On-Off Toggle Method

The power (battery) connections are as follows:
* connect battery to one of the GPRSbee power
* connect other GPRSbee power to the LiPo connector of the SODAQ board

By default the GPRSbee library supports this mode.

### The Switched Power Method

( Not supported by SODAQ Moja )
The power (battery) connections are as follows:
* connect the battery to the LiPo connector of the SODAQ board
* connect SODAQ JP2 to one of the power connectors of GPRSbee

To enable this mode to must add the following in your setup() code
```c
    gprsbee.setPowerSwitchedOnOff(true);
```
