// PowerMate.cpp
//This is garbage... But it works!!!
// This is a terrible machup of
// https://github.com/microsoft/Windows-universal-samples/tree/master/Samples/BluetoothLE
// and 
// https://github.com/stefansundin/powermate-linux
// @stefansundin you laid some baller groundwork, thank you!
//Pull Requests Please!!

#include "pch.h"

#include "VolumeController.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Web::Syndication;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;


enum class STATE_MACHINE
{
    INIT,
    DEVICE_SEARCHING,
    DEVICE_FOUND,
    DEVICE_SELECTING,
    DEVICE_SELECTED,
    SERVICE_SEARCHING,
    SERVICE_FOUND,
    CHARACTERISTICS_SEARCHING,
    CHARACTERISTICS_FOUND
};

STATE_MACHINE myStatus{ STATE_MACHINE::INIT };

Windows::Devices::Enumeration::DeviceWatcher deviceWatcher{ nullptr };
Windows::Devices::Bluetooth::BluetoothLEDevice bluetoothLeDevice{ nullptr };

event_token deviceWatcherAddedToken;
event_token deviceWatcherUpdatedToken;
//event_token deviceWatcherRemovedToken;
event_token deviceWatcherEnumerationCompletedToken;

std::wstring powerMateDeviceId;

std::map<GattDeviceService, std::set<GattCharacteristic>> powerMateServices;

std::wstring uuid_read{ L"{9cf53570-ddd9-47f3-ba63-09acefc60415}" };
std::wstring uuid_led{ L"847d189e-86ee-4bd2-966f-800832b1259d" };

VolumeController masterVolume;

void deviceAdded(DeviceWatcher sender, DeviceInformation deviceInfo)
{
    std::wstring desiredDevice{ L"PowerMate Bluetooth" };
    std::wstring deviceName{ deviceInfo.Name().c_str() };
    std::wstring deviceId{ deviceInfo.Id().c_str() };
    std::wcout << "Added\n" << deviceId << ": " << deviceName << "\n";
    if (deviceName.compare(desiredDevice) == 0)
    {
        powerMateDeviceId = deviceId;
        myStatus = STATE_MACHINE::DEVICE_FOUND;
    }
}

void deviceUpdated(DeviceWatcher sender, DeviceInformationUpdate deviceInfo)
{
    std::wcout << "Updated\n" << deviceInfo.Id().c_str() << "\n";
}

void enumComplete(DeviceWatcher sender, IInspectable args)
{
    std::wcout << "Enumeration Complet\n";
}

/// <summary>
/// Starts a device watcher that looks for all nearby Bluetooth devices (paired or unpaired). 
/// Attaches event handlers to populate the device collection.
/// </summary>
void StartBleDeviceWatcher()
{
    // Additional properties we would like about the device.
    // Property strings are documented here https://msdn.microsoft.com/en-us/library/windows/desktop/ff521659(v=vs.85).aspx
    auto requestedProperties = single_threaded_vector<hstring>({ L"System.Devices.Aep.DeviceAddress", L"System.Devices.Aep.IsConnected", L"System.Devices.Aep.Bluetooth.Le.IsConnectable" });

    // BT_Code: Example showing paired and non-paired in a single query.
    hstring aqsAllBluetoothLEDevices = L"(System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\")";

    deviceWatcher =
        Windows::Devices::Enumeration::DeviceInformation::CreateWatcher(
            aqsAllBluetoothLEDevices,
            requestedProperties,
            DeviceInformationKind::AssociationEndpoint);

    // Register event handlers before starting the watcher.
    //deviceWatcherAddedToken = deviceWatcher.Added({ get_weak(), &Scenario1_Discovery::DeviceWatcher_Added });
    deviceWatcherAddedToken = deviceWatcher.Added(deviceAdded);
    deviceWatcherUpdatedToken = deviceWatcher.Updated(deviceUpdated);
    //deviceWatcherRemovedToken = deviceWatcher.Removed({ get_weak(), &Scenario1_Discovery::DeviceWatcher_Removed });
    //deviceWatcherEnumerationCompletedToken = deviceWatcher.EnumerationCompleted({ get_weak(), &Scenario1_Discovery::DeviceWatcher_EnumerationCompleted });
    deviceWatcherEnumerationCompletedToken = deviceWatcher.EnumerationCompleted(enumComplete);
    //deviceWatcherStoppedToken = deviceWatcher.Stopped({ get_weak(), &Scenario1_Discovery::DeviceWatcher_Stopped });

    // Start over with an empty collection.
    //m_knownDevices.Clear();

    // Start the watcher. Active enumeration is limited to approximately 30 seconds.
    // This limits power usage and reduces interference with other Bluetooth activities.
    // To monitor for the presence of Bluetooth LE devices for an extended period,
    // use the BluetoothLEAdvertisementWatcher runtime class. See the BluetoothAdvertisement
    // sample for an example.
    deviceWatcher.Start();
    myStatus = STATE_MACHINE::DEVICE_SEARCHING;
}

/// <summary>
/// Stops watching for all nearby Bluetooth devices.
/// </summary>
void StopBleDeviceWatcher()
{
    if (deviceWatcher != nullptr)
    {
        // Unregister the event handlers.
        deviceWatcher.Added(deviceWatcherAddedToken);
        deviceWatcher.Updated(deviceWatcherUpdatedToken);
        //deviceWatcher.Removed(deviceWatcherRemovedToken);
        deviceWatcher.EnumerationCompleted(deviceWatcherEnumerationCompletedToken);
        //deviceWatcher.Stopped(deviceWatcherStoppedToken);

        // Stop the watcher.
        deviceWatcher.Stop();
        deviceWatcher = nullptr;
    }
}

fire_and_forget selectDevice(std::wstring deviceId)
{
    try
    {
        // BT_Code: BluetoothLEDevice.FromIdAsync must be called from a UI thread because it may prompt for consent.
        bluetoothLeDevice = co_await BluetoothLEDevice::FromIdAsync(deviceId);

        if (bluetoothLeDevice == nullptr)
        {
            std::cerr << "Failed to connect to device.\n";
            co_return;
        }
        else
        {
            myStatus = STATE_MACHINE::DEVICE_SELECTED;
        }
    }
    catch (hresult_error& ex)
    {
        if (ex.to_abi() == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE))
        {
            std::cerr << "Bluetooth radio is not on.\n";
        }
        else
        {
            throw;
        }
    }


}

/// <summary>
///  Determines whether the UUID was assigned by the Bluetooth SIG.
///  If so, extracts the assigned number.
/// </summary>

bool TryParseSigDefinedUuid(guid const& uuid, uint16_t& shortId)
{
    // UUIDs defined by the Bluetooth SIG are of the form
    // 0000xxxx-0000-1000-8000-00805F9B34FB.
    constexpr guid BluetoothGuid = { 0x00000000, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };

    shortId = static_cast<uint16_t>(uuid.Data1);
    guid possibleBluetoothGuid = uuid;
    possibleBluetoothGuid.Data1 &= 0xFFFF0000;
    return possibleBluetoothGuid == BluetoothGuid;
}

std::wstring GetServiceName(GattDeviceService const& service)
{
    uint16_t shortId;

    guid uuid = service.Uuid();
    if (TryParseSigDefinedUuid(uuid, shortId))
    {
        // Reference: https://developer.bluetooth.org/gatt/services/Pages/ServicesHome.aspx
        const static std::map<uint32_t, const wchar_t*> knownServiceIds =
        {
            { 0x0000, L"None" },
            { 0x1811, L"AlertNotification" },
            { 0x180F, L"Battery" },
            { 0x1810, L"BloodPressure" },
            { 0x1805, L"CurrentTimeService" },
            { 0x1816, L"CyclingSpeedandCadence" },
            { 0x180A, L"DeviceInformation" },
            { 0x1800, L"GenericAccess" },
            { 0x1801, L"GenericAttribute" },
            { 0x1808, L"Glucose" },
            { 0x1809, L"HealthThermometer" },
            { 0x180D, L"HeartRate" },
            { 0x1812, L"HumanInterfaceDevice" },
            { 0x1802, L"ImmediateAlert" },
            { 0x1803, L"LinkLoss" },
            { 0x1807, L"NextDSTChange" },
            { 0x180E, L"PhoneAlertStatus" },
            { 0x1806, L"ReferenceTimeUpdateService" },
            { 0x1814, L"RunningSpeedandCadence" },
            { 0x1813, L"ScanParameters" },
            { 0x1804, L"TxPower" },
            { 0xFFE0, L"SimpleKeyService" },
        };
        auto it = knownServiceIds.find(shortId);
        if (it != knownServiceIds.end())
        {
            return it->second;
        }
    }
    return L"Custom service: " + std::wstring(to_hstring(uuid));
}

fire_and_forget getServices()
{
    // Note: BluetoothLEDevice.GattServices property will return an empty list for unpaired devices. For all uses we recommend using the GetGattServicesAsync method.
    // BT_Code: GetGattServicesAsync returns a list of all the supported services of the device (even if it's not paired to the system).
    // If the services supported by the device are expected to change during BT usage, subscribe to the GattServicesChanged event.
    GattDeviceServicesResult result = co_await bluetoothLeDevice.GetGattServicesAsync(BluetoothCacheMode::Uncached);

    if (result.Status() == GattCommunicationStatus::Success)
    {
        IVectorView<GattDeviceService> services = result.Services();
        std::wcout << L"Found " << services.Size() << L" services" << "\n";
        for (auto&& service : services)
        {
            std::wcout << "\t" << GetServiceName(service) << "\n";
            powerMateServices[service] = {};
        }
        myStatus = STATE_MACHINE::SERVICE_FOUND;
    }
    else
    {
        std::wcerr << L"Device unreachable" << "\n";
        myStatus = STATE_MACHINE::SERVICE_SEARCHING;
    }
}


std::wstring GetCharacteristicName(GattCharacteristic const& characteristic)
{
    uint16_t shortId;

    guid uuid = characteristic.Uuid();
    if (TryParseSigDefinedUuid(uuid, shortId))
    {
        // Reference: https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicsHome.aspx
        const static std::map<uint32_t, const wchar_t*> knownCharacteristicIds =
        {
            { 0x0000, L"None" },
            { 0x2A43, L"AlertCategoryID" },
            { 0x2A42, L"AlertCategoryIDBitMask" },
            { 0x2A06, L"AlertLevel" },
            { 0x2A44, L"AlertNotificationControlPoint" },
            { 0x2A3F, L"AlertStatus" },
            { 0x2A01, L"Appearance" },
            { 0x2A19, L"BatteryLevel" },
            { 0x2A49, L"BloodPressureFeature" },
            { 0x2A35, L"BloodPressureMeasurement" },
            { 0x2A38, L"BodySensorLocation" },
            { 0x2A22, L"BootKeyboardInputReport" },
            { 0x2A32, L"BootKeyboardOutputReport" },
            { 0x2A33, L"BootMouseInputReport" },
            { 0x2A5C, L"CSCFeature" },
            { 0x2A5B, L"CSCMeasurement" },
            { 0x2A2B, L"CurrentTime" },
            { 0x2A08, L"DateTime" },
            { 0x2A0A, L"DayDateTime" },
            { 0x2A09, L"DayofWeek" },
            { 0x2A00, L"DeviceName" },
            { 0x2A0D, L"DSTOffset" },
            { 0x2A0C, L"ExactTime256" },
            { 0x2A26, L"FirmwareRevisionString" },
            { 0x2A51, L"GlucoseFeature" },
            { 0x2A18, L"GlucoseMeasurement" },
            { 0x2A34, L"GlucoseMeasurementContext" },
            { 0x2A27, L"HardwareRevisionString" },
            { 0x2A39, L"HeartRateControlPoint" },
            { 0x2A37, L"HeartRateMeasurement" },
            { 0x2A4C, L"HIDControlPoint" },
            { 0x2A4A, L"HIDInformation" },
            { 0x2A2A, L"IEEE11073_20601RegulatoryCertificationDataList" },
            { 0x2A36, L"IntermediateCuffPressure" },
            { 0x2A1E, L"IntermediateTemperature" },
            { 0x2A0F, L"LocalTimeInformation" },
            { 0x2A29, L"ManufacturerNameString" },
            { 0x2A21, L"MeasurementInterval" },
            { 0x2A24, L"ModelNumberString" },
            { 0x2A46, L"NewAlert" },
            { 0x2A04, L"PeripheralPreferredConnectionParameters" },
            { 0x2A02, L"PeripheralPrivacyFlag" },
            { 0x2A50, L"PnPID" },
            { 0x2A4E, L"ProtocolMode" },
            { 0x2A03, L"ReconnectionAddress" },
            { 0x2A52, L"RecordAccessControlPoint" },
            { 0x2A14, L"ReferenceTimeInformation" },
            { 0x2A4D, L"Report" },
            { 0x2A4B, L"ReportMap" },
            { 0x2A40, L"RingerControlPoint" },
            { 0x2A41, L"RingerSetting" },
            { 0x2A54, L"RSCFeature" },
            { 0x2A53, L"RSCMeasurement" },
            { 0x2A55, L"SCControlPoint" },
            { 0x2A4F, L"ScanIntervalWindow" },
            { 0x2A31, L"ScanRefresh" },
            { 0x2A5D, L"SensorLocation" },
            { 0x2A25, L"SerialNumberString" },
            { 0x2A05, L"ServiceChanged" },
            { 0x2A28, L"SoftwareRevisionString" },
            { 0x2A47, L"SupportedNewAlertCategory" },
            { 0x2A48, L"SupportedUnreadAlertCategory" },
            { 0x2A23, L"SystemID" },
            { 0x2A1C, L"TemperatureMeasurement" },
            { 0x2A1D, L"TemperatureType" },
            { 0x2A12, L"TimeAccuracy" },
            { 0x2A13, L"TimeSource" },
            { 0x2A16, L"TimeUpdateControlPoint" },
            { 0x2A17, L"TimeUpdateState" },
            { 0x2A11, L"TimewithDST" },
            { 0x2A0E, L"TimeZone" },
            { 0x2A07, L"TxPowerLevel" },
            { 0x2A45, L"UnreadAlertStatus" },
            { 0x2A5A, L"AggregateInput" },
            { 0x2A58, L"AnalogInput" },
            { 0x2A59, L"AnalogOutput" },
            { 0x2A66, L"CyclingPowerControlPoint" },
            { 0x2A65, L"CyclingPowerFeature" },
            { 0x2A63, L"CyclingPowerMeasurement" },
            { 0x2A64, L"CyclingPowerVector" },
            { 0x2A56, L"DigitalInput" },
            { 0x2A57, L"DigitalOutput" },
            { 0x2A0B, L"ExactTime100" },
            { 0x2A6B, L"LNControlPoint" },
            { 0x2A6A, L"LNFeature" },
            { 0x2A67, L"LocationandSpeed" },
            { 0x2A68, L"Navigation" },
            { 0x2A3E, L"NetworkAvailability" },
            { 0x2A69, L"PositionQuality" },
            { 0x2A3C, L"ScientificTemperatureinCelsius" },
            { 0x2A10, L"SecondaryTimeZone" },
            { 0x2A3D, L"String" },
            { 0x2A1F, L"TemperatureinCelsius" },
            { 0x2A20, L"TemperatureinFahrenheit" },
            { 0x2A15, L"TimeBroadcast" },
            { 0x2A1B, L"BatteryLevelState" },
            { 0x2A1A, L"BatteryPowerState" },
            { 0x2A5F, L"PulseOximetryContinuousMeasurement" },
            { 0x2A62, L"PulseOximetryControlPoint" },
            { 0x2A61, L"PulseOximetryFeatures" },
            { 0x2A60, L"PulseOximetryPulsatileEvent" },
            { 0xFFE1, L"SimpleKeyState" },
        };

        auto it = knownCharacteristicIds.find(shortId);
        if (it != knownCharacteristicIds.end())
        {
            return it->second;
        }
    }

    std::wstring userDescription = characteristic.UserDescription().c_str();
    if (!userDescription.empty())
    {
        return userDescription;
    }

    return L"Custom Characteristic: " + std::wstring{ to_hstring(uuid) };
}

hstring to_hstring(GattCommunicationStatus status)
{
    switch (status)
    {
    case GattCommunicationStatus::Success: return L"Success";
    case GattCommunicationStatus::Unreachable: return L"Unreachable";
    case GattCommunicationStatus::ProtocolError: return L"ProtocolError";
    case GattCommunicationStatus::AccessDenied: return L"AccessDenied";
    }
    return to_hstring(static_cast<int>(status));
}

enum class POWERMATE_ACTIONS : uint8_t
{
    PRESS = 0x65,
    LONG_RELEASE = 0x66, // This seems to be less reliably sent
    LEFT = 0x67,
    RIGHT = 0x68,
    PRESSED_LEFT = 0x69,
    PRESSED_RIGHT = 0x70,
    HOLD_1 = 0x71,
    HOLD_2 = 0x72,
    HOLD_3 = 0x74,
    HOLD_4 = 0x75,
    HOLD_5 = 0x76,
    HOLD_6 = 0x77
};

void actOnPowerMate(const uint8_t value)
{
    switch (static_cast<POWERMATE_ACTIONS>(value))
    {
    case(POWERMATE_ACTIONS::PRESS):
        masterVolume.mute();
        break;
    case(POWERMATE_ACTIONS::LONG_RELEASE):
        break;
    case(POWERMATE_ACTIONS::LEFT):
        masterVolume.volumeDown();
        break;
    case(POWERMATE_ACTIONS::RIGHT):
        masterVolume.volumeUp();
        break;
    case(POWERMATE_ACTIONS::PRESSED_LEFT):
        break;
    case(POWERMATE_ACTIONS::PRESSED_RIGHT):
        break;
    case(POWERMATE_ACTIONS::HOLD_1):
        break;
    case(POWERMATE_ACTIONS::HOLD_2):
        break;
    case(POWERMATE_ACTIONS::HOLD_3):
        break;
    case(POWERMATE_ACTIONS::HOLD_4):
        break;
    case(POWERMATE_ACTIONS::HOLD_5):
        break;
    case(POWERMATE_ACTIONS::HOLD_6):
        break;
    default:
        std::cerr << "UNKNOWN PowerMate Action" << std::hex << value << "\n";
    }    
}
void Characteristic_ValueChanged(GattCharacteristic const& c, GattValueChangedEventArgs args)
{
    // BT_Code: An Indicate or Notify reported that the value has changed.
    // Display the new value with a timestamp.
    IBuffer value_buffer{ args.CharacteristicValue() };
    std::wcout << value_buffer.Length() << "\n";
    uint8_t value{ DataReader::FromBuffer(value_buffer).ReadByte() };

    std::wcout << GetCharacteristicName(c) << L" Value: " << std::hex << value << "\n";
    actOnPowerMate(value);
}

void AddValueChangedHandler(GattCharacteristic c)
{
    c.ValueChanged(Characteristic_ValueChanged);
}

fire_and_forget subscribeToValueChange(GattCharacteristic c)
{
    GattClientCharacteristicConfigurationDescriptorValue cccdValue = GattClientCharacteristicConfigurationDescriptorValue::None;
    if ((c.CharacteristicProperties() & GattCharacteristicProperties::Indicate) != GattCharacteristicProperties::None)
    {
        cccdValue = GattClientCharacteristicConfigurationDescriptorValue::Indicate;
    }
    else if ((c.CharacteristicProperties() & GattCharacteristicProperties::Notify) != GattCharacteristicProperties::None)
    {
        cccdValue = GattClientCharacteristicConfigurationDescriptorValue::Notify;
    }
    else
    {
        std::cerr << L"Couldn't set Characteristic Configuration Descriptor" << "\n";
        co_return;
    }

    try
    {
        // BT_Code: Must write the CCCD in order for server to send indications.
        // We receive them in the ValueChanged event handler.
        GattCommunicationStatus status = co_await c.WriteClientCharacteristicConfigurationDescriptorAsync(cccdValue);

        if (status == GattCommunicationStatus::Success)
        {
            AddValueChangedHandler(c);
            std::wcout << L"Successfully subscribed for value changes" << "\n";
        }
        else
        {
            std::wcerr << L"Error registering for value changes: Status = " << std::wstring{ to_hstring(status).c_str() } << "\n";
        }
    }
    catch (hresult_access_denied& ex)
    {
        // This usually happens when a device reports that it support indicate, but it actually doesn't.
        std::wcerr << L"Error registering for value changes: Status = " << std::wstring{ ex.message().c_str() };
    }
}

fire_and_forget readCharacteristic(GattCharacteristic c)
{
    // BT_Code: Read the actual value from the device by using Uncached.
    GattReadResult result = co_await c.ReadValueAsync(BluetoothCacheMode::Uncached);
    if (result.Status() == GattCommunicationStatus::Success)
    {
        //std::wcout << L"Read result: " + FormatValueByPresentation(result.Value(), presentationFormat), NotifyType::StatusMessage);
        uint32_t value{ DataReader::FromBuffer(result.Value()).ReadUInt32() };
        std::wcout << GetCharacteristicName(c) << L" value: " << value << "\n";
    }
    else
    {
        std::wcerr << L"Read failed: Status = " << std::wstring { to_hstring(result.Status()) } << "\n";
    }
}

int uuid_equal(std::wstring left, guid right)
{
    return (std::wstring(to_hstring(right)).compare(left) == 0);
}

fire_and_forget getCharacteristics(GattDeviceService service)
{
    IVectorView<GattCharacteristic> characteristics{ nullptr };
    try
    {
        // Ensure we have access to the device.
        auto accessStatus = co_await service.RequestAccessAsync();
        if (accessStatus == DeviceAccessStatus::Allowed)
        {
            // BT_Code: Get all the child characteristics of a service. Use the cache mode to specify uncached characterstics only 
            // and the new Async functions to get the characteristics of unpaired devices as well. 
            GattCharacteristicsResult result = co_await service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached);
            if (result.Status() == GattCommunicationStatus::Success)
            {
                characteristics = result.Characteristics();
            }
            else
            {
                std::wcout << L"Error accessing service." << "\n";
            }
        }
        else
        {
            // Not granted access
            std::wcout << L"Error accessing service." << "\n";

        }
    }
    catch (hresult_error& ex)
    {
        std::wcout << L"Restricted service. Can't read characteristics: " << ex.message().c_str();
    }

    if (characteristics)
    {
        //std::wcout << L"Found " << characteristics.Size() << L" characteristics for " << GetServiceName(service) << "\n";
        for (GattCharacteristic&& c : characteristics)
        {
            if (powerMateServices[service].find(c) == powerMateServices[service].end())
            {
                std::wcout << GetServiceName(service) << "\t" << GetCharacteristicName(c) << "\t" << std::wstring{ to_hstring(c.Uuid()).c_str() } << "\n";
                if (uuid_equal(uuid_read, c.Uuid()))
                {
                    //readCharacteristic(c);
                    subscribeToValueChange(c);
                }
                powerMateServices[service].insert(c);
            }
            else
            {
                myStatus = STATE_MACHINE::CHARACTERISTICS_FOUND;
            }
        }
    }
}


int main()
{
    StartBleDeviceWatcher();
    bool waiting{ true };
    std::cout << "0 <ENTER> to stop/quit\n";
    
    while (waiting)
    {
        switch (myStatus)
        {
        case(STATE_MACHINE::INIT):
            break;
        case(STATE_MACHINE::DEVICE_SEARCHING):
            break;
        case(STATE_MACHINE::DEVICE_FOUND):
            myStatus = STATE_MACHINE::DEVICE_SELECTING;
            selectDevice(powerMateDeviceId);
            break;
        case(STATE_MACHINE::DEVICE_SELECTING):
            break;
        case(STATE_MACHINE::DEVICE_SELECTED):
            if (bluetoothLeDevice != nullptr)
            {
                myStatus = STATE_MACHINE::SERVICE_SEARCHING;
                getServices();
            }
            break;
        case(STATE_MACHINE::SERVICE_SEARCHING):
            break;
        case(STATE_MACHINE::SERVICE_FOUND):
            for (auto&& service : powerMateServices)
            {
                getCharacteristics(service.first);
            }
            break;
        case(STATE_MACHINE::CHARACTERISTICS_SEARCHING):
            break;
        case(STATE_MACHINE::CHARACTERISTICS_FOUND):
            //waiting = false;
            break;
        default:
            std::cerr << "Unknown State" << "\n;";
        }        
    }
    StopBleDeviceWatcher();
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
