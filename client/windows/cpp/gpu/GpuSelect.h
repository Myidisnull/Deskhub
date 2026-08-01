#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>

enum class GpuVendor { Nvidia,
    Intel,
    Amd,
    Microsoft,
    Unknown };

const char* GpuVendorName(GpuVendor v);

struct GpuChoice {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    std::wstring description;
    GpuVendor vendor = GpuVendor::Unknown;
    bool hardware = false;
};

bool CreateBestDevice(const std::vector<GpuVendor>& preference, GpuChoice& out);
