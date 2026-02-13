# USBDevice Plugin - Product Documentation

## Product Overview

The USBDevice plugin is a Thunder/WPEFramework service that provides comprehensive USB device management capabilities for RDK-based platforms. It enables applications to discover, monitor, and respond to USB device connections and disconnections in real-time.

## Key Features

### 1. USB Device Discovery
- **Comprehensive Enumeration**: Retrieve complete list of all connected USB devices
- **Device Attributes**: Access vendor ID, product ID, manufacturer, product name, bus number, and device address
- **Real-time Query**: On-demand device list retrieval via JSON-RPC API
- **Descriptor Parsing**: Automatic extraction of device metadata from USB descriptors

### 2. Hotplug Event Notification
- **Device Arrival Events**: Instant notification when USB devices are connected
- **Device Removal Events**: Immediate notification when USB devices are disconnected
- **Event Subscription**: Multiple clients can subscribe to USB events simultaneously
- **Low Latency**: Sub-100ms event delivery from kernel to application layer

### 3. Multi-Client Support
- **Concurrent Access**: Multiple applications can query and monitor USB devices
- **Event Broadcasting**: All subscribed clients receive hotplug notifications
- **Resource Sharing**: Efficient shared access to USB device information

### 4. Standards Compliance
- **USB 1.1/2.0/3.0 Support**: Compatible with all USB specifications
- **libusb-1.0**: Built on industry-standard USB access library
- **Linux USB Stack**: Leverages kernel USB subsystem for reliability

## Use Cases

### Consumer Electronics Devices
- **USB Storage Detection**: Automatically detect and mount USB flash drives
- **Peripheral Management**: Identify connected keyboards, mice, game controllers
- **Media Playback**: Trigger media scanning when USB drives are connected
- **Accessory Support**: Enable USB webcams, microphones, audio devices

### Set-Top Boxes (STBs)
- **PVR Functionality**: Detect USB storage for recording TV content
- **Software Updates**: Identify USB drives containing firmware updates
- **Content Transfer**: Enable media transfer from USB devices
- **Diagnostic Tools**: Support USB-based diagnostic and debugging tools

### Smart Home Devices
- **Dongle Detection**: Identify Zigbee, Z-Wave, Thread USB dongles
- **Backup Solutions**: Detect USB backup devices
- **Configuration Import**: Auto-detect USB drives with configuration files

### Development & Testing
- **Device Debugging**: Monitor USB device connections during development
- **Automated Testing**: Programmatically verify USB device detection
- **Quality Assurance**: Validate USB hotplug behavior in test suites

## API Capabilities

### JSON-RPC Methods

#### getDeviceList
Retrieve all connected USB devices.

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "USBDevice.1.getDeviceList"
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "devices": [
      {
        "idVendor": "0x1234",
        "idProduct": "0x5678",
        "busNumber": 1,
        "deviceAddress": 3,
        "manufacturer": "Example Corp",
        "product": "USB Storage Device"
      }
    ]
  }
}
```

### Event Notifications

#### onUSBArrival
Notification when a USB device is connected.

**Event:**
```json
{
  "jsonrpc": "2.0",
  "method": "client.events.1.onUSBArrival",
  "params": {
    "idVendor": "0x1234",
    "idProduct": "0x5678",
    "busNumber": 1,
    "deviceAddress": 4,
    "manufacturer": "Example Corp",
    "product": "USB Keyboard"
  }
}
```

#### onUSBRemoval
Notification when a USB device is disconnected.

**Event:**
```json
{
  "jsonrpc": "2.0",
  "method": "client.events.1.onUSBRemoval",
  "params": {
    "idVendor": "0x1234",
    "idProduct": "0x5678",
    "busNumber": 1,
    "deviceAddress": 4
  }
}
```

## Integration Benefits

### For Application Developers
- **Simple API**: JSON-RPC interface requires no USB-specific knowledge
- **Event-Driven**: Asynchronous notifications enable responsive UIs
- **Language Agnostic**: JSON-RPC accessible from any programming language
- **No Permissions Required**: Plugin handles USB access permissions

### For System Integrators
- **Thunder Integration**: Seamlessly integrates with existing Thunder services
- **Security**: Centralized USB access control through Thunder framework
- **Resource Efficiency**: Shared USB monitoring reduces system overhead
- **Logging**: Integrated with Thunder logging infrastructure

### For Product Managers
- **Feature Enablement**: Unlocks USB-dependent product features
- **User Experience**: Enables automatic device recognition and configuration
- **Reliability**: Built on proven libusb and Linux USB stack
- **Maintainability**: Standard Thunder plugin architecture

## Performance & Reliability

### Performance Metrics
- **Device Enumeration**: <50ms for typical device count (1-10 devices)
- **Event Latency**: <100ms from physical connection to application notification
- **CPU Usage**: <1% during normal operation
- **Memory Footprint**: ~2-5MB including libusb context
- **Concurrent Clients**: Supports 10+ simultaneous client connections

### Reliability Features
- **Automatic Recovery**: Handles transient USB errors gracefully
- **Thread Safety**: Internal synchronization for concurrent access
- **Resource Cleanup**: Proper cleanup on plugin shutdown/crashes
- **Error Reporting**: Detailed error codes and logging
- **Stress Tested**: Validated with rapid connect/disconnect cycles

## Deployment Requirements

### System Requirements
- Linux kernel with USB support (usbfs enabled)
- libusb-1.0.0 or higher
- Thunder Framework R4.4.x or compatible
- Read/write access to `/dev/bus/usb/*` devices

### Configuration
- Standard Thunder plugin configuration
- Optional security mode configuration
- Configurable plugin activation mode

### Security Considerations
- USB device access controlled by Linux permissions
- Thunder security framework integration
- Event subscription requires valid Thunder client credentials

## Future Enhancements
- USB device class filtering (HID, storage, audio, etc.)
- Device-specific metadata extraction
- USB power management integration
- Enhanced security policies per device class
