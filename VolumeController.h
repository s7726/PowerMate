#pragma once
#include <mmdeviceapi.h>
#include <endpointvolume.h>

class VolumeController
{
public:
    VolumeController();
    ~VolumeController();
    void volumeUp();
    void volumeDown();
    void mute();

    double getVolume(const bool scalar = false);
    void setVolume(const double value, const bool scalar = false);
private:
    IAudioEndpointVolume* endpointVolume;
};

