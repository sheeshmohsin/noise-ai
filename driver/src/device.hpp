#pragma once
#include <aspl/Driver.hpp>
#include <aspl/Plugin.hpp>
#include <aspl/Device.hpp>
#include <memory>

struct DriverComponents {
    std::shared_ptr<aspl::Context> context;
    std::shared_ptr<aspl::Plugin> plugin;
    std::shared_ptr<aspl::Device> device;
    std::shared_ptr<aspl::Driver> driver;
};

DriverComponents CreateNoiseAIDriver();
