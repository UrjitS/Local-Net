# Local-Net
This project proposes the design and implementation of a decentralized, peer-to-peer communications system for Internet of Things (IoT) devices leveraging Bluetooth-based mesh networking. This system consists of a communications protocol, routing algorithm, and encryption standard that will enable devices to share data without relying on centralized infrastructure or internet connectivity. The goal of this system is to address dependency on centralized cloud infrastructure and single gateways which create points of failures rendering devices helpless when connectivity is lost. By creating a self-healing, scalable mesh network of IoT devices, the project aims to support IoT applications in remote operations, disaster recovery, industrial automation, and other situations where secure, resilient, and autonomous device-to-device communication is critical.

## Resources 
https://people.csail.mit.edu/albert/bluez-intro/index.html

https://www.circuitstate.com/tutorials/getting-started-with-espressif-esp32-wifi-bluetooth-soc-using-doit-esp32-devkit-v1-development-board/#ESP32_Programming


## File Structure
The ```linux_bluez_prototype.c``` is an implementation of the prototype for the BlueZ stack made primarily through https://people.csail.mit.edu/albert/bluez-intro/index.html

The ```esp-idf``` is a fork of the https://github.com/espressif/esp-idf which is a IoT Development Framework for building, flashing, and monitoring ESP32 devices.

The ```esp_proto``` folder contains the implemenation for ESP32 devices. Its folder structure is unique and made to work for the esp-idf framework.

## Setup

### Building Linux BlueZ Implementation
The following prerequisite is required
```
sudo apt-get install libbluetooth-dev
```

To build the ```linux_bluez_prototype.c``` use the following command 
```
gcc -o lproto linux_bluez_prototype.c -lbluetooth -lpthread
```
### Building ESP Prototype Implementation
To build the ESP prototype
1. CD into the ```esp-idf``` directory and run ```. ./export.sh```
2. CD into the ```esp_proto``` directory and run ```idf.py set-target esp32```
3. To build run ```idf.py build```
4. To flash run ```idf.py -p /dev/ttyUSB0 flash```
5. To monitor the program run ```idf.py -p /dev/ttyUSB0 monitor```

Replace ```/dev/ttyUSB0``` with the appropriate path for the ESP device

## How to Run
### Running Linux BlueZ Implementation
To run the program use the command ```sudo ./lproto <message to send>``` for example ```sudo ./lproto message123```

### Running ESP Prototype Implementation
Once it is flashed to the ESP32 device the device is reset and automatically runs. If you want to monitor what it's doing you can use the monitor command ```idf.py -p /dev/ttyUSB0 monitor```