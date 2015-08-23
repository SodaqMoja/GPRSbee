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

Due to continuous development and improvement of the GPRSbee device we have
today already four different methods to switch on the GPRSbee.  They all
have their specific usage.  Normally you select the init method that fits
your environment.  And in one situation you need to call an extra function
to select a specific on-off method.

1. the "old" init method, which needs the CTS pin and the DTR pin
2. the "old" init method as 1), plus enable the Mbili JP2 switch
3. the Ndogo init method, which needs pins PWRKEY, VBAT and STATUS
4. the Autonomo init method, which needs pins PWRKEY, VBAT and STATUS

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

### The Ndogo On-Off Method

The Ndogo method needs three pins, PWRKEY, VBAT and STATUS.  To switch on
it first sets VBAT HIGH and then toggles PWRKEY (starts with LOW, then HIGH
and finally LOW again).

### The Autonomo On-Off Method

This method is for the GPRSbee (rev5 and higher) connected to the
Autonomo.  To switch it on we first have to enable BEE_3V3.  After that we
can simply set DTR HIGH and the SIM800 should be switched on.  There is no
toggling needed of the PWRKEY.  In fact, if we switch DTR LOW while Serial1
is active, it will hang up the CPU.

To switch off, we make DTR LOW and after that we make BEE_3V3 LOW.  The
GPRSbee now consumes no power at all.
