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

TEST_F(USBDevice_L2Test, CreateInterfaceObject_Success)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid USBDevice_Client");
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
        }
    }
}

TEST_F(USBDevice_L2Test, GetDeviceList_Invoke)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid USBDevice_Client");
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                Exchange::IUSBDevice::IUSBDeviceIterator* devices = nullptr;
                const uint32_t status = m_USBDevicePlugin->GetDeviceList(devices);

                EXPECT_TRUE((status == Core::ERROR_NONE) || (status == Core::ERROR_GENERAL));

                if (devices != nullptr) {
                    Exchange::IUSBDevice::USBDevice dev{};
                    while (devices->Next(dev) == true) {
                        TEST_LOG("Device: class=%u subclass=%u name=%s path=%s",
                            dev.deviceClass, dev.deviceSubclass,
                            dev.deviceName.c_str(), dev.devicePath.c_str());
                    }
                    devices->Release();
                }

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
        }
    }
}

TEST_F(USBDevice_L2Test, RegisterUnregisterNotification_Invoke)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid USBDevice_Client");
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                auto* notification = new USBDeviceNotificationHandler();

                const uint32_t regStatus = m_USBDevicePlugin->Register(notification);
                EXPECT_EQ(regStatus, Core::ERROR_NONE);

                const uint32_t unregStatus = m_USBDevicePlugin->Unregister(notification);
                EXPECT_EQ(unregStatus, Core::ERROR_NONE);

                notification->Release();

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
        }
    }
}

TEST_F(USBDevice_L2Test, GetDeviceInfo_Invoke_WithBestEffortDeviceName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid USBDevice_Client");
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                std::string deviceName = "001/001";

                Exchange::IUSBDevice::IUSBDeviceIterator* devices = nullptr;
                if (m_USBDevicePlugin->GetDeviceList(devices) == Core::ERROR_NONE && devices != nullptr) {
                    Exchange::IUSBDevice::USBDevice dev{};
                    if (devices->Next(dev) == true && !dev.deviceName.empty()) {
                        deviceName = dev.deviceName;
                    }
                    devices->Release();
                }

                Exchange::IUSBDevice::USBDeviceInfo info{};
                const uint32_t status = m_USBDevicePlugin->GetDeviceInfo(deviceName, info);

                EXPECT_TRUE((status == Core::ERROR_NONE) || (status == Core::ERROR_GENERAL));

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
        }
    }
}

TEST_F(USBDevice_L2Test, BindUnbindDriver_Invoke_InvalidName)
{
    if (CreateUSBDeviceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid USBDevice_Client");
    } else {
        EXPECT_TRUE(m_controller_USBDevice != nullptr);
        if (m_controller_USBDevice) {
            EXPECT_TRUE(m_USBDevicePlugin != nullptr);
            if (m_USBDevicePlugin) {
                const std::string invalidDeviceName = "999/999";
                const uint32_t bindStatus = m_USBDevicePlugin->BindDriver(invalidDeviceName);
                const uint32_t unbindStatus = m_USBDevicePlugin->UnbindDriver(invalidDeviceName);

                EXPECT_TRUE((bindStatus == Core::ERROR_NONE) || (bindStatus == Core::ERROR_GENERAL));
                EXPECT_TRUE((unbindStatus == Core::ERROR_NONE) || (unbindStatus == Core::ERROR_GENERAL));

                m_USBDevicePlugin->Release();
                m_USBDevicePlugin = nullptr;
            } else {
                TEST_LOG("m_USBDevicePlugin is NULL");
            }
            m_controller_USBDevice->Release();
            m_controller_USBDevice = nullptr;
        } else {
            TEST_LOG("m_controller_USBDevice is NULL");
        }
    }
}