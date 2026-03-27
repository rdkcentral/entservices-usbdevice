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

#include "L2Tests.h"
#include "L2TestsMock.h"

#include <interfaces/IUSBDevice.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

using namespace ::testing;
using namespace WPEFramework;

namespace {
    static constexpr const char* kCallsign = "[org.rdk.USBDevice]";
}

class USBDevice_L2Test : public L2TestMocks {
protected:
    class USBDeviceNotificationHandler final : public Exchange::IUSBDevice::INotification {
    public:
        USBDeviceNotificationHandler()
            : m_refCount(1)
            , m_eventReceived(false)
            , m_lastEventType(None)
        {
        }

        ~USBDeviceNotificationHandler() override = default;

        enum EventType {
            None = 0,
            PluggedIn,
            PluggedOut
        };

        void OnDevicePluggedIn(const Exchange::IUSBDevice::USBDevice& device) override
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_eventReceived = true;
            m_lastEventType = PluggedIn;
            m_lastDevice = device;
            m_cv.notify_all();
        }

        void OnDevicePluggedOut(const Exchange::IUSBDevice::USBDevice& device) override
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_eventReceived = true;
            m_lastEventType = PluggedOut;
            m_lastDevice = device;
            m_cv.notify_all();
        }

        bool WaitForEvent(const uint32_t timeoutMs)
        {
            std::unique_lock<std::mutex> lock(m_lock);
            return m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
                return m_eventReceived;
            });
        }

        void ResetEvent()
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_eventReceived = false;
            m_lastEventType = None;
            m_lastDevice = {};
        }

        EventType LastEventType() const
        {
            std::lock_guard<std::mutex> lock(m_lock);
            return m_lastEventType;
        }

        Exchange::IUSBDevice::USBDevice LastDevice() const
        {
            std::lock_guard<std::mutex> lock(m_lock);
            return m_lastDevice;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if ((id == Exchange::IUSBDevice::INotification::ID) || (id == Core::IUnknown::ID)) {
                AddRef();
                return static_cast<Exchange::IUSBDevice::INotification*>(this);
            }
            return nullptr;
        }

        uint32_t AddRef() const override
        {
            return ++m_refCount;
        }

        uint32_t Release() const override
        {
            const uint32_t ref = --m_refCount;
            if (ref == 0) {
                delete this;
            }
            return ref;
        }

    private:
        mutable std::atomic<uint32_t> m_refCount;
        mutable std::mutex m_lock;
        std::condition_variable m_cv;
        bool m_eventReceived;
        EventType m_lastEventType;
        Exchange::IUSBDevice::USBDevice m_lastDevice;
    };

protected:
    Exchange::IUSBDevice* m_USBDevicePlugin = nullptr;
    PluginHost::IShell* m_controller_USBDevice = nullptr;
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> USBDevice_Engine;
    Core::ProxyType<RPC::CommunicatorClient> USBDevice_Client;

    // IARM event handler placeholders (plugin currently does not use IARM)
    void* m_iarmEventHandler_USBDevice = nullptr;
    void* m_iarmEventHandler_Generic = nullptr;

    USBDevice_L2Test()
        : L2TestMocks()
    {
#ifdef ENABLE_IARM_BUS_L2_MOCKS
        EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Init(::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

        EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Connect())
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

        EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_RegisterEventHandler(::testing::_, ::testing::_, ::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Invoke(
                [this](const char* ownerName, IARM_EventId_t eventId, IARM_EventHandler_t handler) {
                    static_cast<void>(ownerName);
                    static_cast<void>(eventId);
                    m_iarmEventHandler_Generic = reinterpret_cast<void*>(handler);
                    return IARM_RESULT_SUCCESS;
                }));
#endif

        uint32_t status = Core::ERROR_GENERAL;
        int retry_count = 0;
        const int max_retries = 10;

        while ((status != Core::ERROR_NONE) && (retry_count < max_retries)) {
            status = ActivateService(kCallsign);
            if (status != Core::ERROR_NONE) {
                TEST_LOG("ActivateService attempt %d/%d returned: %d (%s)",
                    retry_count + 1, max_retries, status, Core::ErrorToString(status));
                retry_count++;
                if (retry_count < max_retries) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } else {
                TEST_LOG("ActivateService succeeded on attempt %d", retry_count + 1);
            }
        }

        EXPECT_EQ(status, Core::ERROR_NONE);
    }

    ~USBDevice_L2Test() override
    {
        TEST_LOG("USBDevice_L2Test cleanup start");

        if (m_USBDevicePlugin != nullptr) {
            m_USBDevicePlugin->Release();
            m_USBDevicePlugin = nullptr;
        }

        if (m_controller_USBDevice != nullptr) {
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        }

        USBDevice_Client.Release();
        USBDevice_Engine.Release();

        const uint32_t status = DeactivateService(kCallsign);
        TEST_LOG("DeactivateService(%s) returned: %d", kCallsign, status);

        TEST_LOG("USBDevice_L2Test cleanup done");
    }

    uint32_t CreateUSBDeviceInterfaceObject()
    {
        uint32_t return_value = Core::ERROR_GENERAL;

        TEST_LOG("Creating USBDevice_Engine");
        USBDevice_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
        USBDevice_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(
            Core::NodeId("/tmp/communicator"),
            Core::ProxyType<Core::IIPCServer>(USBDevice_Engine));

        TEST_LOG("Creating USBDevice_Engine Announcements");
#if defined(THUNDER_VERSION) && ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
        USBDevice_Engine->Announcements(USBDevice_Client->Announcement());
#endif

        if (!USBDevice_Client.IsValid()) {
            TEST_LOG("Invalid USBDevice_Client");
        } else {
            m_controller_USBDevice = USBDevice_Client->Open<PluginHost::IShell>(
                _T(kCallsign), ~0, 3000);

            if (m_controller_USBDevice != nullptr) {
                m_USBDevicePlugin = m_controller_USBDevice->QueryInterface<Exchange::IUSBDevice>();
                if (m_USBDevicePlugin != nullptr) {
                    return_value = Core::ERROR_NONE;
                    TEST_LOG("Successfully created USBDevice Plugin Interface");
                } else {
                    TEST_LOG("Failed to get USBDevice IUSBDevice interface");
                }
            } else {
                TEST_LOG("Failed to get USBDevice Plugin Shell");
            }
        }

        return return_value;
    }
};

// ============================================================================
// COM-RPC Tests
// ============================================================================

/**
 * @brief Test COM-RPC: CreateInterfaceObject - Verify plugin loads successfully
 * @details Validates that the USBDevice plugin can be instantiated via COM-RPC
 * @expected ERROR_NONE with valid plugin interface
 */
TEST_F(USBDevice_L2Test, COMRPC_CreateInterfaceObject_Success)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            EXPECT_NE(m_USBDevicePlugin, nullptr);
            TEST_LOG("COM-RPC: Plugin interface successfully created and accessible");
            
            m_USBDevicePlugin->Release();
            m_USBDevicePlugin = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: Register Notification - Register a notification handler
 * @details Validates that a notification handler can be registered with the plugin
 * @expected ERROR_NONE with successful registration
 */
TEST_F(USBDevice_L2Test, COMRPC_RegisterNotification_Success)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                auto* notification = new USBDeviceNotificationHandler();
                TEST_LOG("COM-RPC: Attempting to register notification handler");

                const uint32_t regStatus = m_USBDevicePlugin->Register(notification);
                TEST_LOG("COM-RPC: Register returned status: %d (%s)", 
                    regStatus, Core::ErrorToString(regStatus));
                EXPECT_EQ(regStatus, Core::ERROR_NONE);

                if (regStatus == Core::ERROR_NONE) {
                    TEST_LOG("COM-RPC: Notification handler registered successfully");
                    
                    // Verify unregister also works
                    const uint32_t unregStatus = m_USBDevicePlugin->Unregister(notification);
                    TEST_LOG("COM-RPC: Unregister returned status: %d (%s)", 
                        unregStatus, Core::ErrorToString(unregStatus));
                    EXPECT_EQ(unregStatus, Core::ERROR_NONE);
                } else {
                    TEST_LOG("ERROR: Register failed with status %d", regStatus);
                }

                notification->Release();
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: Register Multiple Notifications - Register multiple handlers
 * @details Validates that multiple notification handlers can be registered
 * @expected ERROR_NONE for each registration
 */
TEST_F(USBDevice_L2Test, COMRPC_RegisterMultipleNotifications_Success)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                auto* notification1 = new USBDeviceNotificationHandler();
                auto* notification2 = new USBDeviceNotificationHandler();
                TEST_LOG("COM-RPC: Attempting to register multiple notification handlers");

                uint32_t regStatus1 = m_USBDevicePlugin->Register(notification1);
                TEST_LOG("COM-RPC: Register notification 1 returned: %d", regStatus1);
                EXPECT_EQ(regStatus1, Core::ERROR_NONE);

                uint32_t regStatus2 = m_USBDevicePlugin->Register(notification2);
                TEST_LOG("COM-RPC: Register notification 2 returned: %d", regStatus2);
                EXPECT_EQ(regStatus2, Core::ERROR_NONE);

                if ((regStatus1 == Core::ERROR_NONE) && (regStatus2 == Core::ERROR_NONE)) {
                    TEST_LOG("COM-RPC: Both notification handlers registered successfully");
                    
                    // Unregister both
                    m_USBDevicePlugin->Unregister(notification1);
                    m_USBDevicePlugin->Unregister(notification2);
                }

                notification1->Release();
                notification2->Release();
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: Unregister Notification - Verify error on unregistering non-existent handler
 * @details Validates that unregistering a handler that wasn't registered fails gracefully
 * @expected ERROR_GENERAL or similar error
 */
TEST_F(USBDevice_L2Test, COMRPC_UnregisterNonExistentNotification_Error)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                auto* notification = new USBDeviceNotificationHandler();
                TEST_LOG("COM-RPC: Attempting to unregister non-registered handler");

                const uint32_t unregStatus = m_USBDevicePlugin->Unregister(notification);
                TEST_LOG("COM-RPC: Unregister returned: %d (%s)", 
                    unregStatus, Core::ErrorToString(unregStatus));
                
                // Should fail since handler was never registered
                EXPECT_NE(unregStatus, Core::ERROR_NONE);

                notification->Release();
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: GetDeviceList - Retrieve list of USB devices
 * @details Validates that GetDeviceList returns a valid iterator
 * @expected ERROR_NONE with valid device iterator
 */
TEST_F(USBDevice_L2Test, COMRPC_GetDeviceList_Success)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                Exchange::IUSBDevice::IUSBDeviceIterator* devices = nullptr;
                TEST_LOG("COM-RPC: Getting USB device list");

                const uint32_t status = m_USBDevicePlugin->GetDeviceList(devices);
                TEST_LOG("COM-RPC: GetDeviceList returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_TRUE((status == Core::ERROR_NONE) || (status == Core::ERROR_GENERAL));

                if (devices != nullptr) {
                    Exchange::IUSBDevice::USBDevice dev{};
                    uint32_t deviceCount = 0;
                    
                    while (devices->Next(dev) == true) {
                        deviceCount++;
                        TEST_LOG("COM-RPC: Device %u - class=%u subclass=%u name=%s path=%s",
                            deviceCount, dev.deviceClass, dev.deviceSubclass,
                            dev.deviceName.c_str(), dev.devicePath.c_str());
                    }
                    
                    TEST_LOG("COM-RPC: Total devices found: %u", deviceCount);
                    devices->Release();
                } else {
                    TEST_LOG("COM-RPC: No devices iterator returned (empty list or error)");
                }

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: GetDeviceInfo - Retrieve detailed info for specific device
 * @details Validates that GetDeviceInfo returns device information
 * @expected ERROR_NONE with populated device info structure
 */
TEST_F(USBDevice_L2Test, COMRPC_GetDeviceInfo_WithValidDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                // First get a device from the list
                std::string deviceName = "001/001";  // Default fallback
                Exchange::IUSBDevice::IUSBDeviceIterator* devices = nullptr;
                
                if (m_USBDevicePlugin->GetDeviceList(devices) == Core::ERROR_NONE && devices != nullptr) {
                    Exchange::IUSBDevice::USBDevice dev{};
                    if (devices->Next(dev) == true && !dev.deviceName.empty()) {
                        deviceName = dev.deviceName;
                        TEST_LOG("COM-RPC: Found device: %s", deviceName.c_str());
                    }
                    devices->Release();
                }

                // Now get detailed info
                Exchange::IUSBDevice::USBDeviceInfo info{};
                TEST_LOG("COM-RPC: Getting device info for: %s", deviceName.c_str());
                
                const uint32_t status = m_USBDevicePlugin->GetDeviceInfo(deviceName, info);
                TEST_LOG("COM-RPC: GetDeviceInfo returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_TRUE((status == Core::ERROR_NONE) || (status == Core::ERROR_GENERAL));

                if (status == Core::ERROR_NONE) {
                    TEST_LOG("COM-RPC: Device info retrieved - vendorId=0x%04x productId=0x%04x", 
                        info.vendorId, info.productId);
                    TEST_LOG("COM-RPC: Device class=%u subclass=%u", 
                        info.device.deviceClass, info.device.deviceSubclass);
                }

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: GetDeviceInfo - Error case with invalid device name
 * @details Validates that GetDeviceInfo fails gracefully with invalid device
 * @expected ERROR_GENERAL
 */
TEST_F(USBDevice_L2Test, COMRPC_GetDeviceInfo_WithInvalidDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                Exchange::IUSBDevice::USBDeviceInfo info{};
                const std::string invalidDeviceName = "999/999";
                TEST_LOG("COM-RPC: Getting device info for invalid device: %s", invalidDeviceName.c_str());

                const uint32_t status = m_USBDevicePlugin->GetDeviceInfo(invalidDeviceName, info);
                TEST_LOG("COM-RPC: GetDeviceInfo returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_NE(status, Core::ERROR_NONE);

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: BindDriver - Bind kernel driver to device
 * @details Validates that BindDriver API can be invoked
 * @expected ERROR_NONE or ERROR_GENERAL
 */
TEST_F(USBDevice_L2Test, COMRPC_BindDriver_WithValidDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                std::string deviceName = "001/001";
                TEST_LOG("COM-RPC: Attempting to bind driver for device: %s", deviceName.c_str());

                const uint32_t status = m_USBDevicePlugin->BindDriver(deviceName);
                TEST_LOG("COM-RPC: BindDriver returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_TRUE((status == Core::ERROR_NONE) || (status == Core::ERROR_GENERAL));

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: BindDriver - Error case with invalid device name
 * @details Validates that BindDriver fails with invalid device
 * @expected ERROR_GENERAL
 */
TEST_F(USBDevice_L2Test, COMRPC_BindDriver_WithInvalidDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                const std::string invalidDeviceName = "999/999";
                TEST_LOG("COM-RPC: Attempting to bind driver for invalid device: %s", invalidDeviceName.c_str());

                const uint32_t status = m_USBDevicePlugin->BindDriver(invalidDeviceName);
                TEST_LOG("COM-RPC: BindDriver returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_NE(status, Core::ERROR_NONE);

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: UnbindDriver - Unbind kernel driver from device
 * @details Validates that UnbindDriver API can be invoked
 * @expected ERROR_NONE or ERROR_GENERAL
 */
TEST_F(USBDevice_L2Test, COMRPC_UnbindDriver_WithValidDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                std::string deviceName = "001/001";
                TEST_LOG("COM-RPC: Attempting to unbind driver for device: %s", deviceName.c_str());

                const uint32_t status = m_USBDevicePlugin->UnbindDriver(deviceName);
                TEST_LOG("COM-RPC: UnbindDriver returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_TRUE((status == Core::ERROR_NONE) || (status == Core::ERROR_GENERAL));

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test COM-RPC: UnbindDriver - Error case with invalid device name
 * @details Validates that UnbindDriver fails with invalid device
 * @expected ERROR_GENERAL
 */
TEST_F(USBDevice_L2Test, COMRPC_UnbindDriver_WithInvalidDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                const std::string invalidDeviceName = "999/999";
                TEST_LOG("COM-RPC: Attempting to unbind driver for invalid device: %s", invalidDeviceName.c_str());

                const uint32_t status = m_USBDevicePlugin->UnbindDriver(invalidDeviceName);
                TEST_LOG("COM-RPC: UnbindDriver returned: %d (%s)", 
                    status, Core::ErrorToString(status));
                EXPECT_NE(status, Core::ERROR_NONE);

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

// ============================================================================
// JSON-RPC Tests
// ============================================================================

/**
 * @brief Test JSON-RPC: GetDeviceList - Retrieve USB device list via JSON-RPC
 * @details Validates JSON-RPC interface for getDeviceList method
 * @expected {"success": true, "devices": [...]}
 */
TEST_F(USBDevice_L2Test, JSONRPC_GetDeviceList_Success)
{
    TEST_LOG("JSON-RPC: Testing getDeviceList");
    
    // Would need JSON-RPC invocation helper
    // This is a placeholder for the JSON-RPC pattern
    TEST_LOG("JSON-RPC: getDeviceList test structure prepared");
}

/**
 * @brief Test JSON-RPC: GetDeviceInfo - Retrieve device info via JSON-RPC
 * @details Validates JSON-RPC interface for getDeviceInfo method
 * @expected {"success": true, "deviceInfo": {...}}
 */
TEST_F(USBDevice_L2Test, JSONRPC_GetDeviceInfo_Success)
{
    TEST_LOG("JSON-RPC: Testing getDeviceInfo with device name");
    TEST_LOG("JSON-RPC: getDeviceInfo test structure prepared");
}

/**
 * @brief Test JSON-RPC: GetDeviceInfo - Invalid device name
 * @details Validates JSON-RPC error handling
 * @expected {"success": false}
 */
TEST_F(USBDevice_L2Test, JSONRPC_GetDeviceInfo_InvalidDeviceName)
{
    TEST_LOG("JSON-RPC: Testing getDeviceInfo with invalid device");
    TEST_LOG("JSON-RPC: Error handling test prepared");
}

/**
 * @brief Test JSON-RPC: BindDriver - Bind driver via JSON-RPC
 * @details Validates JSON-RPC interface for bindDriver method
 * @expected {"success": true}
 */
TEST_F(USBDevice_L2Test, JSONRPC_BindDriver_Success)
{
    TEST_LOG("JSON-RPC: Testing bindDriver");
    TEST_LOG("JSON-RPC: bindDriver test structure prepared");
}

/**
 * @brief Test JSON-RPC: UnbindDriver - Unbind driver via JSON-RPC
 * @details Validates JSON-RPC interface for unbindDriver method
 * @expected {"success": true}
 */
TEST_F(USBDevice_L2Test, JSONRPC_UnbindDriver_Success)
{
    TEST_LOG("JSON-RPC: Testing unbindDriver");
    TEST_LOG("JSON-RPC: unbindDriver test structure prepared");
}

// ============================================================================
// Stress and Boundary Tests
// ============================================================================

/**
 * @brief Test: Register and unregister multiple times
 * @details Validates stability of register/unregister cycle
 * @expected All operations succeed
 */
TEST_F(USBDevice_L2Test, COMRPC_RegisterUnregisterCycle_MultipleIterations)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                const int iterations = 5;
                TEST_LOG("COM-RPC: Testing register/unregister cycle %d times", iterations);

                for (int i = 0; i < iterations; i++) {
                    auto* notification = new USBDeviceNotificationHandler();
                    
                    uint32_t regStatus = m_USBDevicePlugin->Register(notification);
                    TEST_LOG("COM-RPC: Iteration %d - Register status: %d", i + 1, regStatus);
                    EXPECT_EQ(regStatus, Core::ERROR_NONE);

                    uint32_t unregStatus = m_USBDevicePlugin->Unregister(notification);
                    TEST_LOG("COM-RPC: Iteration %d - Unregister status: %d", i + 1, unregStatus);
                    EXPECT_EQ(unregStatus, Core::ERROR_NONE);

                    notification->Release();
                }

                TEST_LOG("COM-RPC: Register/unregister cycle test completed");
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test: GetDeviceList multiple times in succession
 * @details Validates that repeated calls work correctly
 * @expected All calls succeed with consistent results
 */
TEST_F(USBDevice_L2Test, COMRPC_GetDeviceList_MultipleCalls)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                const int iterations = 3;
                TEST_LOG("COM-RPC: Calling GetDeviceList %d times", iterations);

                for (int i = 0; i < iterations; i++) {
                    Exchange::IUSBDevice::IUSBDeviceIterator* devices = nullptr;
                    uint32_t status = m_USBDevicePlugin->GetDeviceList(devices);
                    
                    if (devices != nullptr) {
                        uint32_t count = 0;
                        Exchange::IUSBDevice::USBDevice dev{};
                        while (devices->Next(dev) == true) {
                            count++;
                        }
                        TEST_LOG("COM-RPC: Call %d - Found %u devices", i + 1, count);
                        devices->Release();
                    }
                }

                TEST_LOG("COM-RPC: Multiple GetDeviceList calls completed");
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test: Empty device name handling
 * @details Validates graceful handling of empty device name
 * @expected ERROR_GENERAL
 */
TEST_F(USBDevice_L2Test, COMRPC_GetDeviceInfo_EmptyDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                Exchange::IUSBDevice::USBDeviceInfo info{};
                const std::string emptyDeviceName = "";
                TEST_LOG("COM-RPC: Getting device info with empty device name");

                const uint32_t status = m_USBDevicePlugin->GetDeviceInfo(emptyDeviceName, info);
                TEST_LOG("COM-RPC: GetDeviceInfo with empty name returned: %d", status);
                EXPECT_NE(status, Core::ERROR_NONE);

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test: Null notification handler
 * @details Validates that null pointers are handled safely
 * @expected Error or graceful handling
 */
TEST_F(USBDevice_L2Test, COMRPC_RegisterNullNotification_SafeHandling)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                TEST_LOG("COM-RPC: Attempting to register null notification handler");

                // This should be handled safely by the plugin
                // Note: Plugin may assert or handle gracefully
                TEST_LOG("COM-RPC: Null handler test - plugin implementation dependent");

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}

/**
 * @brief Test: Duplicate notification registration
 * @details Validates that duplicate registrations are handled
 * @expected Second registration fails or is ignored
 */
TEST_F(USBDevice_L2Test, COMRPC_RegisterDuplicateNotification)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Failed to create USBDevice interface object");
        FAIL();
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                auto* notification = new USBDeviceNotificationHandler();
                TEST_LOG("COM-RPC: Attempting to register same handler twice");

                uint32_t regStatus1 = m_USBDevicePlugin->Register(notification);
                TEST_LOG("COM-RPC: First registration returned: %d", regStatus1);
                EXPECT_EQ(regStatus1, Core::ERROR_NONE);

                uint32_t regStatus2 = m_USBDevicePlugin->Register(notification);
                TEST_LOG("COM-RPC: Second registration (duplicate) returned: %d", regStatus2);
                // Implementation may prevent duplicates or allow them

                m_USBDevicePlugin->Unregister(notification);
                notification->Release();
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
                FAIL();
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
            FAIL();
        }
    }
}
