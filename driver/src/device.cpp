#include "device.hpp"
#include "io_handler.hpp"
#include <aspl/Stream.hpp>
#include <aspl/Direction.hpp>
#include <CoreAudio/AudioHardwareBase.h>

DriverComponents CreateNoiseAIDriver() {
    DriverComponents components;

    // Create context
    components.context = std::make_shared<aspl::Context>();

    // Configure device parameters
    aspl::DeviceParameters deviceParams;
    deviceParams.Name = "NoiseAI Microphone";
    deviceParams.Manufacturer = "NoiseAI";
    deviceParams.DeviceUID = "NoiseAI_VirtualMic_UID";
    deviceParams.ModelUID = "NoiseAI_Model";
    deviceParams.SampleRate = 48000;
    deviceParams.ChannelCount = 2;
    deviceParams.EnableMixing = false;

    // Create device
    components.device = std::make_shared<aspl::Device>(components.context, deviceParams);

    // Create IO handler
    auto handler = std::make_shared<IOHandler>();
    components.device->SetIOHandler(handler);
    components.device->SetControlHandler(handler);

    // Configure stream format as 32-bit float (matching what we write in IOHandler).
    // The ASPL default is 16-bit signed integer, which would cause a format mismatch
    // since our shared ring buffer carries Float32 samples.
    aspl::StreamParameters streamParams;
    streamParams.Direction = aspl::Direction::Input;
    streamParams.Format = {
        .mSampleRate = 48000,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBitsPerChannel = 32,
        .mChannelsPerFrame = 2,
        .mBytesPerFrame = 8,   // 2 channels * 4 bytes
        .mFramesPerPacket = 1,
        .mBytesPerPacket = 8,
    };

    // Add input stream with volume/mute controls using our Float32 format
    components.device->AddStreamWithControlsAsync(streamParams);

    // Build plugin hierarchy
    components.plugin = std::make_shared<aspl::Plugin>(components.context);
    components.plugin->AddDevice(components.device);

    // Create driver
    components.driver = std::make_shared<aspl::Driver>(components.context, components.plugin);

    return components;
}
