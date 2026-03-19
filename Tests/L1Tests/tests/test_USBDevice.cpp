/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#include <gtest/gtest.h>
#include <mntent.h>
#include <fstream>
#include "USBDevice.h"
#include "USBDeviceImplementation.h"
#include "libUSBMock.h"
#include "ServiceMock.h"
#include "FactoriesImplementation.h"
#include <fstream> // Added for file creation
#include <string>
#include <vector>
#include <cstdio>
#include "COMLinkMock.h"
#include "WorkerPoolImplementation.h"
#include "WrapsMock.h"
#include "secure_wrappermock.h"
#include "ThunderPortability.h"

#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);

using ::testing::NiceMock;
using namespace WPEFramework;

#define MOCK_USB_DEVICE_BUS_NUMBER_1    100
#define MOCK_USB_DEVICE_ADDRESS_1       001
#define MOCK_USB_DEVICE_PORT_1          123

#define MOCK_USB_DEVICE_BUS_NUMBER_2    101
#define MOCK_USB_DEVICE_ADDRESS_2       002
#define MOCK_USB_DEVICE_PORT_2          124

#define MOCK_USB_DEVICE_SERIAL_NO "0401805e4532973503374df52a239c898397d348"
#define MOCK_USB_DEVICE_MANUFACTURER "USB"
#define MOCK_USB_DEVICE_PRODUCT "SanDisk 3.2Gen1"
#define LIBUSB_CONFIG_ATT_BUS_POWERED 0x80
namespace {
const string callSign = _T("USBDevice");
}

class USBDeviceTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::USBDevice> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    Core::JSONRPC::Message message;
    string response;
    libUSBImplMock  *p_libUSBImplMock   = nullptr;
    Core::ProxyType<Plugin::USBDeviceImplementation> USBDeviceImpl;
    NiceMock<COMLinkMock> comLinkMock;
    NiceMock<ServiceMock> service;
    PLUGINHOST_DISPATCHER* dispatcher;
    libusb_hotplug_callback_fn libUSBHotPlugCbDeviceAttached = nullptr;
    libusb_hotplug_callback_fn libUSBHotPlugCbDeviceDetached = nullptr;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    NiceMock<FactoriesImplementation> factoriesImplementation;

    USBDeviceTest()
        : plugin(Core::ProxyType<Plugin::USBDevice>::Create())
        , handler(*(plugin))
        , INIT_CONX(1, 0)
        , workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(
            2, Core::Thread::DefaultStackSize(), 16))
    {
        p_libUSBImplMock  = new NiceMock <libUSBImplMock>;
        libusbApi::setImpl(p_libUSBImplMock);

        ON_CALL(service, COMLink())
            .WillByDefault(::testing::Invoke(
                  [this]() {
                        TEST_LOG("Pass created comLinkMock: %p ", &comLinkMock);
                        return &comLinkMock;
                    }));

#ifdef USE_THUNDER_R4
        ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
			.WillByDefault(::testing::Invoke(
                  [&](const RPC::Object& object, const uint32_t waitTime, uint32_t& connectionId) {
                        USBDeviceImpl = Core::ProxyType<Plugin::USBDeviceImplementation>::Create();
                        TEST_LOG("Pass created USBDeviceImpl: %p &USBDeviceImpl: %p", USBDeviceImpl, &USBDeviceImpl);
                        return &USBDeviceImpl;
                    }));
#else
	  ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
	    .WillByDefault(::testing::Return(USBDeviceImpl));
#endif /*USE_THUNDER_R4 */

        PluginHost::IFactories::Assign(&factoriesImplementation);

        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
           plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);

        /* Set all the asynchronouse event handler with libusb to handle various events*/
        ON_CALL(*p_libUSBImplMock, libusb_hotplug_register_callback(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](libusb_context *ctx, int events, int flags, int vendor_id, int product_id, int dev_class,
                 libusb_hotplug_callback_fn cb_fn, void *user_data, libusb_hotplug_callback_handle *callback_handle) {
                if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == events) {
                    libUSBHotPlugCbDeviceAttached = cb_fn;
                    *callback_handle = 1;
                }
                if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == events) {
                    libUSBHotPlugCbDeviceDetached = cb_fn;
                    *callback_handle = 2;
                }
                return LIBUSB_SUCCESS;
            }));

        EXPECT_EQ(string(""), plugin->Initialize(&service));
    }
    virtual ~USBDeviceTest() override
    {
        TEST_LOG("USBDeviceTest Destructor");

        plugin->Deinitialize(&service);

        dispatcher->Deactivate();
        dispatcher->Release();

        Core::IWorkerPool::Assign(nullptr);
        workerPool.Release();

        PluginHost::IFactories::Assign(nullptr);

        libusbApi::setImpl(nullptr);
        if (p_libUSBImplMock != nullptr)
        {
            delete p_libUSBImplMock;
            p_libUSBImplMock = nullptr;
        }
    }

    virtual void SetUp()
    {
        ASSERT_TRUE(libUSBHotPlugCbDeviceAttached != nullptr);
    }
    void Mock_SetDeviceDesc(uint8_t bus_number, uint8_t device_address);
    void Mock_SetSerialNumberInUSBDevicePath();
};

void USBDeviceTest::Mock_SetDeviceDesc(uint8_t bus_number, uint8_t device_address)
{
     ON_CALL(*p_libUSBImplMock, libusb_get_device_descriptor(::testing::_, ::testing::_))
        .WillByDefault(
            [bus_number, device_address](libusb_device *dev, struct libusb_device_descriptor *desc) {
                 if ((bus_number == dev->bus_number) &&
                     (device_address == dev->device_address))
                 {
                      desc->bDeviceSubClass = LIBUSB_CLASS_MASS_STORAGE;
                      desc->bDeviceClass = LIBUSB_CLASS_MASS_STORAGE;
                 }
                 return LIBUSB_SUCCESS;
     });

    ON_CALL(*p_libUSBImplMock, libusb_get_device_address(::testing::_))
        .WillByDefault(::testing::Return(device_address));

    ON_CALL(*p_libUSBImplMock, libusb_get_bus_number(::testing::_))
        .WillByDefault(::testing::Return(bus_number));

    ON_CALL(*p_libUSBImplMock, libusb_get_port_numbers(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault([](libusb_device *dev, uint8_t *port_numbers, int port_numbers_len) {
            if((nullptr != dev) && (nullptr != port_numbers))
            {
                port_numbers[0] = dev->port_number;
                return 1;
            }
            else
            {
                return 0;
            }
        });

    if (device_address == MOCK_USB_DEVICE_ADDRESS_1)
    {
        std::string vendorFileName = "/tmp/block/sda/device/vendor";
        std::ofstream outVendorStream(vendorFileName);

        if (!outVendorStream) {
            TEST_LOG("Error opening file for writing!");
        }
        outVendorStream << "Generic" << std::endl;
        outVendorStream.close();

        std::string modelFileName = "/tmp/block/sda/device/model";
        std::ofstream outModelStream(modelFileName);

        if (!outModelStream) {
            TEST_LOG("Error opening file for writing!");
        }
        outModelStream << "Flash Disk" << std::endl;
        outModelStream.close();
    }

    if (device_address == MOCK_USB_DEVICE_ADDRESS_2)
    {
        std::string vendorFileName = "/tmp/block/sdb/device/vendor";
        std::ofstream  outVendorStream(vendorFileName);

        if (!outVendorStream) {
            TEST_LOG("Error opening file for writing!");
        }
        outVendorStream << "JetFlash" << std::endl;
        outVendorStream.close();

        std::string modelFileName = "/tmp/block/sdb/device/model";
        std::ofstream outModelStream(modelFileName);

        if (!outModelStream) {
            TEST_LOG("Error opening file for writing!");
        }
        outModelStream << "Transcend_16GB" << std::endl;
        outModelStream.close();
    }
}

void USBDeviceTest::Mock_SetSerialNumberInUSBDevicePath()
{
    std::string serialNumFileName1 = "/tmp/bus/usb/devices/100-123/serial";
    std::ofstream serialNumOutFile1(serialNumFileName1);

    if (!serialNumOutFile1) {
        TEST_LOG("Error opening file for writing!");
    }
    serialNumOutFile1 << "B32FD507" << std::endl;
    serialNumOutFile1.close();

    std::string serialNumFileName2 = "/tmp/bus/usb/devices/101-124/serial";
    std::ofstream serialNumOutFile2(serialNumFileName2);

    if (!serialNumOutFile2) {
        TEST_LOG("Error opening file for writing!");
    }
    serialNumOutFile2<< "UEUIRCXT" << std::endl;
    serialNumOutFile2.close();

    std::string serialNumFileSda = "/dev/sda";
    std::ofstream serialNumOutFileSda(serialNumFileSda);

    if (!serialNumOutFileSda) {
        TEST_LOG("Error opening file for writing!");
    }
    serialNumOutFileSda << "B32FD507 100-123" << std::endl;
    serialNumOutFileSda.close();


    std::string serialNumFileSdb = "/dev/sdb";
    std::ofstream serialNumOutFileSdb(serialNumFileSdb);

    if (!serialNumOutFileSdb) {
        TEST_LOG("Error opening file for writing!");
    }
    serialNumOutFileSdb << "UEUIRCXT 101-124" << std::endl;
    serialNumOutFileSdb.close();
}

TEST_F(USBDeviceTest, GetDeviceList_Success_NoDevices)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceList"), _T("{}"), response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"devices\":[]"));
}

TEST_F(USBDeviceTest, GetDeviceList_Success_SingleDevice)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceList"), _T("{}"), response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"devices\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"deviceclass\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"devicesubclass\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"devicename\""));
}

TEST_F(USBDeviceTest, GetDeviceList_Success_MultipleDevices)
{
    libusb_device mockDev1, mockDev2;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    mockDev2.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_2;
    mockDev2.device_address = MOCK_USB_DEVICE_ADDRESS_2;
    mockDev2.port_number = MOCK_USB_DEVICE_PORT_2;
    libusb_device* devs[] = { &mockDev1, &mockDev2, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_2, MOCK_USB_DEVICE_ADDRESS_2);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(2)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceList"), _T("{}"), response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"devices\""));
}

TEST_F(USBDeviceTest, GetDeviceList_Failure_NegativeDeviceCount)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(-1));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceList"), _T("{}"), response));
}

TEST_F(USBDeviceTest, GetDeviceList_Failure_GetDeviceDescriptorFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceList"), _T("{}"), response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Success)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"vendorId\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"productId\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"deviceStatus\""));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_DeviceNotFound)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    string deviceName = "999/999";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_NoDevices)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_LibUSBOpenFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_GetConfigDescriptorFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NOT_FOUND));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Success_BusPowered)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_BUS_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"deviceStatus\""));
}

TEST_F(USBDeviceTest, BindDriver_Success)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    EXPECT_CALL(*p_libUSBImplMock, libusb_attach_kernel_driver(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_SUCCESS));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, BindDriver_Success_DriverAlreadyActive)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(1));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, BindDriver_Failure_DeviceNotFound)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    string deviceName = "999/999";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, BindDriver_Failure_NoDevices)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, BindDriver_Failure_LibUSBOpenFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, BindDriver_Failure_KernelDriverActiveFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, BindDriver_Failure_AttachKernelDriverFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    EXPECT_CALL(*p_libUSBImplMock, libusb_attach_kernel_driver(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NOT_FOUND));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("bindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Success)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(1));

    EXPECT_CALL(*p_libUSBImplMock, libusb_detach_kernel_driver(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_SUCCESS));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Success_NoDriverActive)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Failure_DeviceNotFound)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    string deviceName = "999/999";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Failure_NoDevices)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Failure_LibUSBOpenFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Failure_KernelDriverActiveFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, UnbindDriver_Failure_DetachKernelDriverFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_kernel_driver_active(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(1));

    EXPECT_CALL(*p_libUSBImplMock, libusb_detach_kernel_driver(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NOT_FOUND));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("unbindDriver"), payload, response));
}

TEST_F(USBDeviceTest, OnDevicePluggedIn_Event)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    ASSERT_TRUE(libUSBHotPlugCbDeviceAttached != nullptr);

    bool eventReceived = false;
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(callSign, nullptr);
    jsonrpc.Subscribe<JsonObject>(1000, _T("onDevicePluggedIn"),
        [&](const JsonObject& params) {
            eventReceived = true;
        });

    libUSBHotPlugCbDeviceAttached(nullptr, &mockDev1, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, nullptr);

    auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (!eventReceived && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(eventReceived);
}

TEST_F(USBDeviceTest, OnDevicePluggedOut_Event)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    ASSERT_TRUE(libUSBHotPlugCbDeviceDetached != nullptr);

    bool eventReceived = false;
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(callSign, nullptr);
    jsonrpc.Subscribe<JsonObject>(1000, _T("onDevicePluggedOut"),
        [&](const JsonObject& params) {
            eventReceived = true;
        });

    libUSBHotPlugCbDeviceDetached(nullptr, &mockDev1, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, nullptr);

    auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (!eventReceived && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(eventReceived);
}

TEST_F(USBDeviceTest, OnDevicePluggedIn_Failure_GetDeviceDescriptorFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_IO));

    ASSERT_TRUE(libUSBHotPlugCbDeviceAttached != nullptr);

    bool eventReceived = false;
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(callSign, nullptr);
    jsonrpc.Subscribe<JsonObject>(1000, _T("onDevicePluggedIn"),
        [&](const JsonObject& params) {
            eventReceived = true;
        });

    libUSBHotPlugCbDeviceAttached(nullptr, &mockDev1, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, nullptr);

    auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!eventReceived && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(eventReceived);
}

TEST_F(USBDeviceTest, OnDevicePluggedOut_Failure_GetDeviceDescriptorFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_IO));

    ASSERT_TRUE(libUSBHotPlugCbDeviceDetached != nullptr);

    bool eventReceived = false;
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(callSign, nullptr);
    jsonrpc.Subscribe<JsonObject>(1000, _T("onDevicePluggedOut"),
        [&](const JsonObject& params) {
            eventReceived = true;
        });

    libUSBHotPlugCbDeviceDetached(nullptr, &mockDev1, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, nullptr);

    auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!eventReceived && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(eventReceived);
}

TEST_F(USBDeviceTest, GetDeviceInfo_Success_DetailedInfo)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"vendorId\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"productId\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"deviceStatus\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"serialNumber\""));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Success_SecondDevice)
{
    libusb_device mockDev1, mockDev2;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    mockDev2.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_2;
    mockDev2.device_address = MOCK_USB_DEVICE_ADDRESS_2;
    mockDev2.port_number = MOCK_USB_DEVICE_PORT_2;
    libusb_device* devs[] = { &mockDev1, &mockDev2, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_2, MOCK_USB_DEVICE_ADDRESS_2);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(2)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "101/002";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"vendorId\""));
    EXPECT_THAT(response, ::testing::HasSubstr("\"productId\""));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_EmptyDeviceList)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(nullptr),
            ::testing::Return(0)));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_NegativeDeviceCount)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(-1));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_FirstDeviceNameMismatch)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    string deviceName = "200/002";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_MiddleDeviceNameMismatch)
{
    libusb_device mockDev1, mockDev2, mockDev3;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    mockDev2.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_2;
    mockDev2.device_address = MOCK_USB_DEVICE_ADDRESS_2;
    mockDev2.port_number = MOCK_USB_DEVICE_PORT_2;
    mockDev3.bus_number = 102;
    mockDev3.device_address = 003;
    mockDev3.port_number = 125;
    libusb_device* devs[] = { &mockDev1, &mockDev2, &mockDev3, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(3)));

    string deviceName = "200/005";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_GetDeviceDescriptorFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_LibUSBOpenFailsAfterDescriptor)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_GetStringDescriptorFails)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_InsufficientBufferForStringDescriptor)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char shortBuff[2] = { 0x02, 0x03 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&shortBuff](unsigned char* data) {
                memcpy(data, shortBuff, 2);
            }),
            ::testing::Return(2)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_GetConfigDescriptorFailsAfterStringRetrieval)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_InvalidDeviceNameFormat)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    string deviceName = "invalid";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_EmptyDeviceName)
{
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(0));

    string deviceName = "";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_LargeDeviceCountWithNoMatch)
{
    libusb_device mockDevices[10];
    libusb_device* devs[11];

    for (int i = 0; i < 10; i++) {
        mockDevices[i].bus_number = 100 + i;
        mockDevices[i].device_address = i + 1;
        mockDevices[i].port_number = 120 + i;
        devs[i] = &mockDevices[i];
    }
    devs[10] = nullptr;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(10)));

    string deviceName = "200/200";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_LibUSBExitErrorHandling)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_BUSY));

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_MassStorageDeviceWithNoPath)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Boundary_MinBusNumber)
{
    libusb_device mockDev1;
    mockDev1.bus_number = 0;
    mockDev1.device_address = 1;
    mockDev1.port_number = 1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(0, 1);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "000/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"vendorId\""));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Boundary_MaxBusNumber)
{
    libusb_device mockDev1;
    mockDev1.bus_number = 255;
    mockDev1.device_address = 127;
    mockDev1.port_number = 255;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(255, 127);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));

    string deviceName = "255/127";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"vendorId\""));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Success_WithResourceCleanup)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);
    Mock_SetSerialNumberInUSBDevicePath();

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    unsigned char langBuff[256] = { 0x04, 0x03, 0x09, 0x04 };
    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::WithArg<3>([&langBuff](unsigned char* data) {
                memcpy(data, langBuff, 4);
            }),
            ::testing::Return(4)))
        .WillRepeatedly(::testing::Return(LIBUSB_ERROR_IO));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor_ascii(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArg<2>([](unsigned char* data) {
                strcpy((char*)data, MOCK_USB_DEVICE_SERIAL_NO);
            }),
            ::testing::Return(strlen(MOCK_USB_DEVICE_SERIAL_NO))));

    libusb_config_descriptor mockConfigDesc;
    mockConfigDesc.bmAttributes = LIBUSB_CONFIG_ATT_SELF_POWERED;
    libusb_config_descriptor* pConfigDesc = &mockConfigDesc;

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_active_config_descriptor(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(pConfigDesc),
            ::testing::Return(LIBUSB_SUCCESS)));

    ::testing::InSequence seq;
    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
    EXPECT_THAT(response, ::testing::HasSubstr("\"vendorId\""));
}

TEST_F(USBDeviceTest, GetDeviceInfo_Failure_ResourceCleanupOnError)
{
    libusb_device mockDev1;
    mockDev1.bus_number = MOCK_USB_DEVICE_BUS_NUMBER_1;
    mockDev1.device_address = MOCK_USB_DEVICE_ADDRESS_1;
    mockDev1.port_number = MOCK_USB_DEVICE_PORT_1;
    libusb_device* devs[] = { &mockDev1, nullptr };

    Mock_SetDeviceDesc(MOCK_USB_DEVICE_BUS_NUMBER_1, MOCK_USB_DEVICE_ADDRESS_1);

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_device_list(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(devs),
            ::testing::Return(1)));

    libusb_device_handle mockHandle;
    EXPECT_CALL(*p_libUSBImplMock, libusb_open(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<1>(&mockHandle),
            ::testing::Return(LIBUSB_SUCCESS)));

    EXPECT_CALL(*p_libUSBImplMock, libusb_get_string_descriptor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(LIBUSB_ERROR_NO_DEVICE));

    ::testing::InSequence seq;
    EXPECT_CALL(*p_libUSBImplMock, libusb_close(::testing::_))
        .Times(1);
    EXPECT_CALL(*p_libUSBImplMock, libusb_free_device_list(::testing::_, ::testing::_))
        .Times(1);

    string deviceName = "100/001";
    string payload = "{\"deviceName\":\"" + deviceName + "\"}";
    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDeviceInfo"), payload, response));
}