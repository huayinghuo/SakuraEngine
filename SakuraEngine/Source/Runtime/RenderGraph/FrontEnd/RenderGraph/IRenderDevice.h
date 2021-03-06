﻿/*
 * @CopyRight: MIT License
 * Copyright (c) 2020 SaeruHikari
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THESOFTWARE.
 * 
 * 
 * @Description: 
 * @Version: 0.1.0
 * @Autor: SaeruHikari
 * @Date: 2020-08-20 15:56:21
 * @LastEditTime: 2020-08-21 20:03:41
 */
#pragma once
#include "RenderGraphCore.h"
#include "RenderGraph/RenderCommandBuffer.h"
#include "system/Window.h"

namespace sakura::graphics
{
	struct RenderPass;
	struct ISwapChain;
}

namespace sakura::graphics
{
	struct DeviceConfiguration
	{
		sakura::string name;
		sakura::Window window_handle;
	};

	// actual render device class interface
	struct RenderGraphAPI IRenderDevice
	{
		IRenderDevice() = default;
		virtual ~IRenderDevice() = default;

		virtual bool valid(const RenderShaderHandle shader) const = 0;

		virtual sakura::string_view get_name() const = 0;
		virtual EBackend backend() const = 0;
		
		// execute a command buffer
		virtual bool execute(const RenderPass&, const RenderPassHandle) = 0;
		virtual bool execute(const RenderGraph& graph_to_execute) = 0;
		virtual bool present(const SwapChainHandle handle) { return true; }
		virtual void destroy_resource(const RenderShaderHandle to_destroy) = 0;
		
		virtual RenderShaderHandle create_shader(const RenderShaderHandle handle, const ShaderDesc& desc) = 0;
		virtual RenderBufferHandle create_buffer(const RenderBufferHandle handle, const BufferDesc& desc) = 0;
		virtual RenderAttachmentHandle create_render_attachment(const RenderAttachmentHandle handle, const Attachment& desc) = 0;

		virtual FenceHandle create_fence(const FenceHandle handle, const FenceDesc& desc) = 0;
		virtual SwapChainHandle create_swap_chain(const SwapChainHandle handle, const SwapChainDesc& desc) = 0;
		virtual RenderPipelineHandle create_render_pipeline(const RenderPipelineHandle handle, const RenderPipelineDesc& desc) = 0;

		virtual RenderBufferHandle update_buffer(const RenderBufferHandle handle, size_t offset, void* data, size_t size) { return handle; };

		virtual IGPUBuffer* get(const RenderBufferHandle handle) const { return nullptr; }
		virtual IGPUShader* get(const RenderShaderHandle handle) const { return nullptr; }
		virtual IRenderPipeline* get(const RenderPipelineHandle handle) const { return nullptr; }
		virtual ISwapChain* get(const SwapChainHandle handle) const { return nullptr; }
		virtual IFence* get(const FenceHandle handle) const { return nullptr; }

		virtual IGPUBuffer* optional(const RenderBufferHandle handle) const { return nullptr; }
		virtual IGPUShader* optional(const RenderShaderHandle handle) const { return nullptr; }
		virtual IRenderPipeline* optional(const RenderPipelineHandle handle) const { return nullptr; }
		virtual ISwapChain* optional(const SwapChainHandle handle) const { return nullptr; }
		virtual IFence* optional(const FenceHandle handle) const { return nullptr; }
		
		virtual void terminate() = 0;
	};

	class RenderDeviceGroupProxy final : public IRenderDevice
	{
	public:
		FORCEINLINE RenderDeviceGroupProxy(const RenderGraph& inRenderGraph)
			:renderGraph(inRenderGraph)
		{
			
		}
		void emplace(IRenderDevice* in_device)
		{
			devices.push_back(in_device);
			name.append(" ").append(sakura::string(in_device->get_name())).append(" &");
		}
		template<typename... Args>
		void emplace(Args... args)
		{
			std::initializer_list<int32_t> {(emplace(args), 0)...};
			std::cout << std::endl;
		}
		FORCEINLINE sakura::size_t count() const
		{
			return devices.size();
		}
		FORCEINLINE bool valid(const RenderShaderHandle shader) const override
		{
			bool result = true;
			for (auto i = 0; i < count(); i++)
			{
				result &= devices[i]->valid(shader);
			}
			return result;
		}
		bool present(const SwapChainHandle handle) override
		{
			bool result = true;
			for (auto i = 0; i < count(); i++)
			{
				result = result & devices[i]->present(handle);
			}
			return result;
		}
		FORCEINLINE bool execute(const RenderPass& pass, const RenderPassHandle handle) override
		{
			bool result = true;
			for (auto i = 0; i < count(); i++)
			{
				result = result & devices[i]->execute(pass, handle);
			}
			return result;
		}
		FORCEINLINE bool execute(const RenderGraph& graph_to_execute) override
		{
			bool result = true;
			for (auto i = 0; i < count(); i++)
			{
				result = result & devices[i]->execute(graph_to_execute);
			}
			return result;
		}
		FORCEINLINE void destroy_resource(const RenderShaderHandle to_destroy) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->destroy_resource(to_destroy);
			}
		}
		FORCEINLINE RenderShaderHandle create_shader(const RenderShaderHandle handle, const ShaderDesc& config) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->create_shader(handle, config);
			}
			return handle;
		}
		FenceHandle create_fence(const FenceHandle handle, const FenceDesc& desc) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->create_fence(handle, desc);
			}
			return handle;
		}
		FORCEINLINE RenderBufferHandle create_buffer(const RenderBufferHandle handle, const BufferDesc& config) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->create_buffer(handle, config);
			}
			return handle;
		}
		FORCEINLINE RenderAttachmentHandle create_render_attachment(const RenderAttachmentHandle handle, const Attachment& config) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->create_render_attachment(handle, config);
			}
			return handle;
		}
		SwapChainHandle create_swap_chain(
			const SwapChainHandle handle, const SwapChainDesc& desc) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->create_swap_chain(handle, desc);
			}
			return handle;
		}

		RenderPipelineHandle create_render_pipeline(const RenderPipelineHandle handle, const RenderPipelineDesc& desc) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->create_render_pipeline(handle, desc);
			}
			return handle;
		}
		FORCEINLINE void terminate() override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->terminate();
			}
		}
		FORCEINLINE sakura::string_view get_name() const override
		{
			return sakura::string_view(name.c_str(), name.size() - 1);
		}

		RenderBufferHandle update_buffer(const RenderBufferHandle handle, size_t offset, void* data, size_t size) override
		{
			for (auto i = 0; i < count(); i++)
			{
				devices[i]->update_buffer(handle, offset, data, size);
			}
			return handle;
		}

		EBackend backend() const override
		{
			return EBackend::Count;
		}
		
		IRenderDevice* operator[](int index) const
		{
			return devices[index];
		}
	private:
		sakura::string name = "DeviceGroup:";
		sakura::vector<IRenderDevice*> devices;
		const RenderGraph& renderGraph;
	};
}