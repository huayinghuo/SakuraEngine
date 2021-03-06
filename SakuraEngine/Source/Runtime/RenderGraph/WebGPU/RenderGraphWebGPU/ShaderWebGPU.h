﻿#pragma once
#include "RenderGraph/RenderGraphCore.h"
#ifdef SAKURA_TARGET_PLATFORM_EMSCRIPTEN
#include <webgpu/webgpu.h>
#else
#include <dawn/webgpu.h>
#include <dawn_native/DawnNative.h>
#endif

namespace sakura::graphics::webgpu
{
	class RenderDevice;
	
	class RenderGraphWebGPUAPI GPUShader : public IGPUShader
	{
	public:
		GPUShader(const RenderShaderHandle handle,
			webgpu::RenderDevice& dev, const ShaderDesc& desc) noexcept;
		~GPUShader();
		RenderResourceHandle handle() const override;
		size_t size() const override;
		EShaderFrequency frequency() const override;

		sakura::string_view entry_name() const override;
		EShaderCodeFormat code_format() const override;
		
		RenderShaderHandle _handle;
		WGPUShaderModule module_ref = nullptr;
		ShaderDesc desc;
	};
}
