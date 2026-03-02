# USBDevice Plugin Architecture

## Overview

The USBDevice plugin is a WPEFramework/Thunder plugin that provides USB device monitoring and management capabilities for RDK platforms. It enables discovery, enumeration, and hotplug event notification for USB devices connected to the system.

## System Architecture

### Component Structure

```
┌──────────────────────────────────────────┐
│         Thunder Framework                │
│  ┌────────────────────────────────────┐  │
│  │      JSON-RPC Interface            │  │
│  └────────────────┬───────────────────┘  │
│                   │                      │
│  ┌────────────────▼───────────────────┐  │
│  │     USBDevice Plugin (JSONRPC)     │  │
│  │  - getDeviceList()                 │  │
│  │  - onUSBArrival (event)            │  │
│  │  - onUSBRemoval (event)            │  │
│  └────────────────┬───────────────────┘  │
│                   │                      │
│  ┌────────────────▼───────────────────┐  │
│  │  USBDeviceImplementation           │  │
│  │  - Device enumeration logic        │  │
│  │  - Hotplug event handling          │  │
│  │  - Thread management               │  │
│  └────────────────┬───────────────────┘  │
└───────────────────┼──────────────────────┘
                    │
         ┌──────────▼──────────┐
         │     libusb-1.0      │
         │  - USB enumeration  │
         │  - Hotplug callback │
         └──────────┬──────────┘
                    │
         ┌──────────▼──────────┐
         │   Linux USB Stack   │
         └─────────────────────┘
```

### Core Components

#### 1. USBDevice Plugin Layer
- **Purpose**: Exposes JSON-RPC interface to Thunder clients
- **Responsibilities**:
  - Register/deregister JSON-RPC methods and notifications
  - Validate incoming API requests
  - Marshal/unmarshal JSON data structures
  - Forward requests to implementation layer
- **Key Files**: `USBDevice.h`, `USBDevice.cpp`, `Module.h`

#### 2. USBDeviceImplementation
- **Purpose**: Core business logic for USB device management
- **Responsibilities**:
  - Initialize and manage libusb context
  - Enumerate connected USB devices
  - Register hotplug callbacks with libusb
  - Manage background thread for hotplug events
  - Notify plugin layer of USB events
- **Key Files**: `USBDeviceImplementation.h`, `USBDeviceImplementation.cpp`
- **Threading Model**: 
  - Background thread runs `libusb_handle_events_timeout()` in a loop
  - Hotplug callbacks execute in libusb's context
  - Event notifications are posted to Thunder's event dispatcher

#### 3. libusb Integration
- **Library Version**: libusb-1.0
- **Key Operations**:
  - `libusb_init()`: Initialize USB context
  - `libusb_get_device_list()`: Enumerate all connected USB devices
  - `libusb_hotplug_register_callback()`: Register for device arrival/removal
  - `libusb_handle_events_timeout()`: Process hotplug events (blocking)
  - `libusb_exit()`: Clean up USB context

### Data Flow

#### Device Enumeration Flow
```
Client → Thunder → USBDevice::getDeviceList() 
       → USBDeviceImplementation::GetDevices()
       → libusb_get_device_list()
       → Parse device descriptors
       → Build JSON device list
       → Return to client
```

#### Hotplug Event Flow (Arrival)
```
USB Device Connected → Linux Kernel
       → libusb hotplug callback
       → USBDeviceImplementation::OnUSBArrival()
       → USBDevice::NotifyUSBArrival() [event notification]
       → Thunder Event Dispatcher
       → All subscribed clients
```

## Plugin Configuration

The plugin uses a standard Thunder plugin configuration file (`.config`):

```json
{
  "locator": "libusbdevice.so",
  "classname": "USBDevice",
  "startmode": "Activated",
  "configuration": {
    "root": {
      "mode": "Local"
    }
  }
}
```

- **startmode**: Controls when the plugin initializes (Activated/Deactivated)
- **mode**: Security mode for the plugin (Local/Off)

## API Interface

### Methods

**getDeviceList()**
- Returns array of USB devices with properties:
  - `idVendor`: Vendor ID (hex)
  - `idProduct`: Product ID (hex)
  - `busNumber`: USB bus number
  - `deviceAddress`: Device address on bus
  - `manufacturer`: Manufacturer string (if available)
  - `product`: Product description string (if available)

### Events

**onUSBArrival**
- Triggered when USB device is connected
- Payload: Single device object with properties listed above

**onUSBRemoval**
- Triggered when USB device is disconnected
- Payload: Device identification (vendor ID, product ID, bus number)

## Dependencies

### Build Dependencies
- **Thunder Framework (R4.4.x)**: Plugin framework and JSON-RPC infrastructure
- **libusb-1.0**: USB device access and hotplug detection
- **Boost (system, filesystem)**: Utility libraries for file operations
- **pthread**: Threading support for hotplug event loop

### Runtime Dependencies
- Linux kernel USB subsystem (usbfs, sysfs)
- Appropriate permissions to access `/dev/bus/usb/*` devices
- Thunder/WPEFramework running and configured

## Thread Safety

- **Main Thread**: Handles JSON-RPC requests, initializes plugin
- **Hotplug Thread**: Dedicated thread running `libusb_handle_events_timeout()` loop
- **Synchronization**: libusb handles internal thread safety for hotplug callbacks
- **Event Notifications**: Posted asynchronously to Thunder's event dispatcher

## Error Handling

- **libusb errors**: Mapped to Thunder error codes (ERROR_GENERAL)
- **Device access failures**: Gracefully handled, partial device info returned
- **Initialization failures**: Plugin activation fails, error logged
- **Thread failures**: Plugin deactivation, cleanup performed

## Testing Support

### L1 Tests (Unit Tests)
- Mock Thunder framework interfaces
- Test device enumeration logic
- Validate JSON-RPC request/response handling
- Test error conditions

### L2 Tests (Integration Tests)
- Test with actual Thunder framework
- Verify hotplug event delivery
- Test concurrent client subscriptions
- Validate end-to-end device discovery

## Performance Characteristics

- **Device Enumeration**: O(n) where n = number of USB devices
- **Hotplug Detection**: Event-driven, <100ms notification latency
- **Memory Footprint**: ~2-5MB (includes libusb context)
- **CPU Usage**: Minimal when idle, <1% during hotplug events
