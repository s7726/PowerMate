#include "pch.h"
#include "VolumeController.h"

// Pulled from:
// https://www.codeproject.com/Tips/233484/Change-Master-Volume-in-Visual-Cplusplus
// A little fixing up and nudging around, but mostly whole hog a copy

VolumeController::VolumeController():
    endpointVolume(NULL)
{
    HRESULT hr = NULL;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* deviceEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
    IMMDevice* defaultDevice = NULL;

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    deviceEnumerator->Release();
    deviceEnumerator = NULL;

    hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
        CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume);
    defaultDevice->Release();
    defaultDevice = NULL;
}

VolumeController::~VolumeController()
{
    endpointVolume->Release();

    CoUninitialize();
}

void VolumeController::volumeUp()
{
    endpointVolume->VolumeStepUp(NULL);
}

void VolumeController::volumeDown()
{
    endpointVolume->VolumeStepDown(NULL);
}

void VolumeController::mute()
{
    HRESULT hr{ NULL };

    BOOL muteState{ false };
    hr = endpointVolume->GetMute(&muteState);
    hr = endpointVolume->SetMute(!muteState, NULL);
}

double VolumeController::getVolume(const bool scalar)
{
    HRESULT hr{ NULL };

    float currentVolume{ 0 };
    if(scalar)
    {
        hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    }
    else
    {
        hr = endpointVolume->GetMasterVolumeLevel(&currentVolume);
    }
       
    return currentVolume;
}

void VolumeController::setVolume(const double value, const bool scalar)
{
    HRESULT hr{ NULL };
    if (scalar)
    {
        hr = endpointVolume->SetMasterVolumeLevelScalar((float)value, NULL);
    }
    else
    {
        hr = endpointVolume->SetMasterVolumeLevel((float)value, NULL);
    }
}
