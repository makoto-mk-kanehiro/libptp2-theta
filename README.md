# libptp2 and ptpcam for RICOH THETA camera control with a USB cable

More information is [here](https://theta360.guide/special/linuxstreaming/).

This is based on ptpcam.

[Original project page](http://libptp.sourceforge.net/).

## Modifications

* support for USB 3.0 SuperSpeed USB (SS), though it's not
clear if the THETA Z1 USB3.0 implementation supports this speed - by 
[nickel110](https://github.com/nickel110/libptp2)
* support for video file transfer greater than 4GB - by 
[nickel110](https://github.com/nickel110/libptp2)
* autoconf version and configuration files updated to 
make it easier to modify code on Ubuntu 20.04 and 18.04
* some unused code for non-THETA cameras deleted to make the
code easier to edit
* PTP_VENDOR_THETA added with `0x00000006`, which was previously
assigned to PTP_VENDOR_MICROSOFT
* --reset-delay option to ptpcam to help THETA V wake from sleep - by [nickel110]


## Installation 

On Ubuntu 20.04 x86 and JetPack 4.4 NVIDIA Jetson.

```
$ sudo apt install libusb-dev
$ sudo apt install automake-1.15  # on ubuntu 20.04
or
$ sudo apt install automake   # JetPack 4.4 or Ubuntu 18.04
$ cd theta_libptp2
$ ./configure
$ make
$ sudo make install
$ sudo ldconfig -v 

``` 

If the build fails, try the following.

```
$ autoreconf -i
$ ./configure
$ automake
$ make

# if you don't have a build system installed, you may need some basic 
# tools

$ sudo apt install build-essential
$ sudo apt install libtool
$ sudo apt install automake
...
```

## Test with RICOH THETA

Connect camera to computer with USB cable.  For most commands, the camera needs to be on,
not in sleep mode.

#### get camera info 

```
$ theta --info

THETA Device Info
==================
Model: RICOH THETA Z1
  manufacturer: Ricoh Company, Ltd.
  serial number: '10010104'
  device version: 1.50.1
  extension ID: 0x00000006
  image formats supported: 0x00000005
  extension version: 0x006e
```

#### Wake RICOH THETA from sleep

```
 theta --set-property=0xD80E --val=0x00
```

You may need a delay.

```
theta --reset-delay=2000  --set-property=0xD80E --val=0
```

See [this](https://community.theta360.guide/t/ricoh-theta-api-over-usb-cable-z1-v-s-sc-models/65/127?u=craig) post if you are having problems putting the camera to sleep or waking it up from sleep.

See [this](https://youtu.be/z-mYQY3zYt0) YouTube video on the modifications
and testing.


#### Put Camera in Still Image Mode

```
$ theta --set-property=0x5013 --val=0x0001

Camera: RICOH THETA V
'Still Capture Mode' is set to: 0x8005 (-32763)
Changing property value to 0x0001 [(null)] succeeded.
```

#### Take Picture

```
$ theta --capture

Initiating capture...
Object added 0x00000017
Capture completed successfully!
```

#### List Devices 

Only one device attached at the moment.   You can use this to get the
device ID of a particular camera. 

```
$ theta --list-devices

Listing devices...
bus/dev	vendorID/prodID	device model
002/008	0x05CA/0x2714	RICOH THETA V
```

#### Put into live streaming mode

```
$ theta --set-property=0x5013 --val=0x8005

Camera: RICOH THETA V
'Still Capture Mode' is set to: [Normal]
Changing property value to 0x8005 [(null)] succeeded.
``` 

#### Put camera to sleep

```
theta --set-property=0xd80e --val=0x01
```

#### List Files

```
$ theta --list-files

Listing files...
Camera: RICOH THETA V
Handler:           Size: 	Captured:      	name:
0x00000142:      4152882	2020-06-17 20:59	R0010273.JPG
0x00000143:      3979605	2020-06-17 21:03	R0010274.JPG
```


## Credit

* libptp2 (c)2001-2006 Mariusz Woloszyn <emsi@ipartners.pl>
* ptpcam  (c)2001-2006 Mariusz Woloszyn <emsi@ipartners.pl>

