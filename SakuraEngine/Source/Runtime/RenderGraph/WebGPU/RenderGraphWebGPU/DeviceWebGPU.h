﻿#pragma once
#include "RenderGraph/IRenderDevice.h"
#include "system/Log.h"
#ifdef SAKURA_TARGET_PLATFORM_EMSCRIPTEN
#include <webgpu/webgpu.h>
#else
#include <dawn/webgpu.h>
#include <dawn_native/DawnNative.h>
#endif

namespace sakura {
	namespace graphics {
		struct RenderPass;
	}
	
}

namespace sakura::graphics::webgpu
{
	class RenderPipeline;
	
	class RenderGraphWebGPUAPI RenderDevice final : public IRenderDevice
	{
	public:
		RenderDevice(const DeviceConfiguration& config);
		~RenderDevice();
		static WGPUBackendType get_backend();
		
		virtual EBackend backend() const override;
		bool valid(const RenderShaderHandle shader) const override;
		sakura::string_view get_name() const override;
		bool execute(const RenderPass&, const RenderPassHandle) override;
		bool execute(const RenderGraph& graph_to_execute) override;
		bool present(const SwapChainHandle handle) override;
		void terminate() override;

		void destroy_resource(const RenderShaderHandle to_destroy) override;
		RenderShaderHandle create_shader(const RenderShaderHandle handle, const ShaderDesc& config) override;
		RenderBufferHandle create_buffer(const RenderBufferHandle handle, const BufferDesc& config) override;
		RenderAttachmentHandle create_render_attachment(const RenderAttachmentHandle handle, const Attachment& config) override;

		FenceHandle create_fence(const FenceHandle handle, const FenceDesc& desc) override;
		SwapChainHandle create_swap_chain(const SwapChainHandle handle, const SwapChainDesc& desc) override;
		RenderPipelineHandle create_render_pipeline(const RenderPipelineHandle handle, const RenderPipelineDesc& desc) override;

		RenderBufferHandle update_buffer(const RenderBufferHandle handle, size_t offset, void* data, size_t size) override;

		// TODO: Share these implementations between backend devices.
		IGPUBuffer* get(const RenderBufferHandle handle) const override;
		IGPUShader* get(const RenderShaderHandle handle) const override;
		IRenderPipeline* get(const RenderPipelineHandle handle) const override;
		ISwapChain* get(const SwapChainHandle handle) const override;
		IFence* get(const FenceHandle handle) const override;
		
		IGPUBuffer* optional(const RenderBufferHandle handle) const override;
		IGPUShader* optional(const RenderShaderHandle handle) const override;
		IRenderPipeline* optional(const RenderPipelineHandle handle) const override;
		ISwapChain* optional(const SwapChainHandle handle) const override;
		IFence* optional(const FenceHandle handle) const override;
		
		sakura::vector<sakura::pair<IGPUMemoryResource*, RenderGraphId::uhalf_t>> created_resources;
		sakura::vector<sakura::pair<IGPUObject*, RenderGraphId::uhalf_t>> created_objects;
		
	#ifndef SAKURA_TARGET_PLATFORM_EMSCRIPTEN
		dawn_native::Adapter adapter;
	#endif
		// devices
		WGPUDevice device = nullptr;
		WGPUQueue defaultQueue = nullptr;
		sakura::unordered_map<sakura::string, WGPUShaderModule> shaderModules;
		sakura::string name;

		struct PassCacheFrame
		{
			PassCacheFrame(const PassCacheFrame&) = delete;
			PassCacheFrame& operator=(const PassCacheFrame&) = delete;
			PassCacheFrame() = default;

			void destroy();
			
			sakura::vector<WGPUTextureView> texture_views = sakura::vector<WGPUTextureView>(0); // [NOT FINISHED] Clear Every Frame.
			sakura::vector< sakura::pair<sakura::vector<WGPUBindGroupEntry>, bool> > entries
				= sakura::vector< sakura::pair<sakura::vector<WGPUBindGroupEntry>, bool> >(0); // Finished
			sakura::vector<WGPUBindGroup> bindGroups = sakura::vector<WGPUBindGroup>(0); // Finished
			RenderPipeline* pipeline = nullptr; // 

			WGPUCommandBuffer commands = nullptr;
			WGPUCommandEncoder encoder = nullptr;
			WGPURenderPassEncoder pass_encoder = nullptr;
			WGPUQueue queue = nullptr;
			
			WGPUFence committed_fence;
			uint64 last_commited = 0;
		};
		struct PassCache
		{
			PassCache(uint8 frameCount);
			~PassCache();
			PassCache() = default;
			PassCacheFrame& frame(uint8 currentFrame);
			
			void destroy();
		protected:
			PassCacheFrame* frames = nullptr;
			uint8 frame_count = 0;
		};
		sakura::vector<PassCache> passCache;
	protected:
		template <bool optional, typename ResourceType, typename Handle>
		ResourceType* _get_resouce_impl(const Handle handle) const noexcept;
		template <typename ResourceType, typename Handle, typename... Args>
		Handle _create_resouce_impl(const Handle handle, Args&&... args) noexcept;

		template <bool optional, typename ObjectType, typename Handle>
		ObjectType* _get_object_impl(const Handle handle) const noexcept;
		template <typename ObjectType, typename Handle, typename... Args>
		Handle _create_object_impl(const Handle handle, Args&&... args) noexcept;

		
		void initPlatformSpecific(const DeviceConfiguration& config);
		void processCommand(PassCacheFrame& cache, const RenderCommand* command, WGPUCommandEncoder* encoder, WGPURenderPassEncoder* pass) const;
		void processCommandUpdateBinding(PassCacheFrame& cache,
			const RenderCommandUpdateBinding& command, WGPUCommandEncoder* encoder,
			WGPURenderPassEncoder* pass, RenderPipeline* ppl) const;
		void processCommandDraw(const RenderCommandDraw& command,
			WGPUCommandEncoder* encoder, WGPURenderPassEncoder* pass) const;
		RenderPipeline* processCommandBeginRenderPass(PassCacheFrame& cache,
			const RenderCommandBeginRenderPass& command, WGPUCommandEncoder* encoder, WGPURenderPassEncoder* pass) const;
		void processCommandEndRenderPass(const RenderCommandEndRenderPass& command, WGPUCommandEncoder* encoder, WGPURenderPassEncoder* pass) const;
		void processCommandSetScissorRect(const RenderCommandSetScissorRect& command, WGPUCommandEncoder* encoder, WGPURenderPassEncoder* pass) const;
		void processCommandFence(const RenderCommandFence& command, WGPUCommandEncoder* encoder, WGPURenderPassEncoder* pass) const;
	};
}

namespace sakura::graphics::webgpu
{	
	template<typename Handle>
	struct RenderGraphWebGPUAPI handle_error
	{
		static void not_find(const Handle handle)
		{
			sakura::error("RenderResource/RenderObject With Handle {} Not found in WebGPU Resources!", handle);
		}
		static void generation_dismatch(const Handle handle)
		{
			sakura::error("Generation Error : RenderResourceHandle {} has a different generation with WebGPU Resource!", handle);
		}
		static void create_on_existed(const Handle handle)
		{
			sakura::error(
				"[RenderDeviceWeb]: Resource with handle {} alreay created! hash code: {}", handle, size_t(handle.id()));
		}
	};
	template<> struct handle_error<RenderBufferHandle>
	{
		static void not_find(const RenderBufferHandle handle)
		{
			sakura::error("RenderBuffer With Handle {} Not found in WebGPU Resources!", handle);
		}
		static void generation_dismatch(const RenderBufferHandle handle)
		{
			sakura::error("Generation Error : RenderBufferHandle{} has a different generation with WebGPU Resource!", handle);
		}
		static void create_on_existed(const RenderBufferHandle handle)
		{
			sakura::error(
				"[RenderDeviceWeb]: RenderBuffer with handle {} alreay created! hash code: {}", handle, size_t(handle.id()));
		}
	};
	template<> struct handle_error<RenderShaderHandle>
	{
		static void not_find(const RenderShaderHandle handle)
		{
			sakura::error("RenderShader With Handle {} Not found in WebGPU Resources!", handle);
		}
		static void generation_dismatch(const RenderShaderHandle handle)
		{
			sakura::error("Generation Error : RenderShaderHandle{} has a different generation with WebGPU Resource!", handle);
		}
		static void create_on_existed(const RenderShaderHandle handle)
		{
			sakura::error(
				"[RenderDeviceWeb]: RenderShader with handle {} alreay created! hash code: {}", handle, size_t(handle.id()));
		}
	};
	template<> struct handle_error<SwapChainHandle>
	{
		static void not_find(const SwapChainHandle handle)
		{
			sakura::error("RenderSwapChain With Handle {} Not found in WebGPU Objects!", handle);
		}
		static void generation_dismatch(const SwapChainHandle handle)
		{
			sakura::error("Generation Error : RenderSwapChainHandle{} has a different generation with WebGPU Objects!", handle);
		}
		static void create_on_existed(const SwapChainHandle handle)
		{
			sakura::error(
				"[RenderDeviceWeb]: RenderSwapChain with handle {} alreay created! hash code: {}", handle, size_t(handle.id()));
		}
	};
	template<> struct handle_error<RenderPipelineHandle>
	{
		static void not_find(const RenderPipelineHandle handle)
		{
			sakura::error("RenderPipeline With Handle {} Not found in WebGPU Objects!", handle);
		}
		static void generation_dismatch(const RenderPipelineHandle handle)
		{
			sakura::error("Generation Error : RenderPipelineHandle{} has a different generation with WebGPU Objects!", handle);
		}
		static void create_on_existed(const RenderPipelineHandle handle)
		{
			sakura::error(
				"[RenderDeviceWeb]: RenderPipeline with handle {} alreay created! hash code: {}", handle, size_t(handle.id()));
		}
	};

	template <bool isOptional, typename ResourceType, typename Handle>
	ResourceType* RenderDevice::_get_resouce_impl(const Handle handle) const noexcept
	{
		static_assert(std::is_base_of_v<IGPUMemoryResource, ResourceType>, "[DeviceWebGPU::_get_resource_impl]: ResourceType must be derived from IGPUMemoryResource!");
		static_assert(std::is_base_of_v<RenderResourceHandle, Handle>, "[DeviceWebGPU::_get_resource_impl]: Handle must be derived from RenderResourceHandle!");
		static_assert(std::is_base_of_v<typename Handle::ResourceType, ResourceType>, "[DeviceWebGPU::_get_resource_impl]: Handle must match to it's ResourceType!");
		if (created_resources.size() < handle.id().index() + 1)
		{
			if constexpr (isOptional)
				;
			else
				handle_error<Handle>::not_find(handle);
			return nullptr;
		}
		auto& resource = created_resources[handle.id().index()];
		if (handle.id().generation() == resource.second)
			return static_cast<ResourceType*>(resource.first);
		else
		{
			if constexpr (isOptional);
			else
				handle_error<Handle>::generation_dismatch(handle);
			return nullptr;
		}
	}

	template <typename ResourceType, typename Handle, typename ... Args>
	Handle RenderDevice::_create_resouce_impl(const Handle handle, Args&&... args) noexcept
	{
		static_assert(std::is_base_of_v<IGPUMemoryResource, ResourceType>, "[DeviceWebGPU::_create_resouce_impl]: ResourceType must be derived from IGPUMemoryResource!");
		static_assert(std::is_base_of_v<RenderResourceHandle, Handle>, "[DeviceWebGPU::_create_resouce_impl]: Handle must be derived from RenderResourceHandle!");
		static_assert(std::is_base_of_v<typename Handle::ResourceType, ResourceType>, "[DeviceWebGPU::_create_resouce_impl]: Handle must match to it's ResourceType!");
		if (this->optional(handle))
		{
			handle_error<Handle>::create_on_existed(handle);
		}
		else
		{
			if (created_resources.size() < handle.id().index() + 1)
				created_resources.resize(handle.id().index() + 1);
			auto newRes = new ResourceType(handle, std::forward<Args>(args)...);
			created_resources[handle.id().index()] = sakura::make_pair(newRes, handle.id().generation());
		}
		return handle;
	}

	template <bool isOptional, typename ObjectType, typename Handle>
	ObjectType* RenderDevice::_get_object_impl(const Handle handle) const noexcept
	{
		static_assert(std::is_base_of_v<IGPUObject, ObjectType>, "[DeviceWebGPU::_get_object_impl]: ResourceType must be derived from IGPUObject!");
		static_assert(std::is_base_of_v<RenderGraphHandle, Handle>, "[DeviceWebGPU::_get_object_impl]: Handle must be derived from RenderObjectHandle!");
		static_assert(std::is_base_of_v<typename Handle::ObjectType, ObjectType>, "[DeviceWebGPU::_get_object_impl]: Handle must match to it's ObjectType!");
		if (created_objects.size() < handle.id().index() + 1)
		{
			if constexpr (isOptional);
			else
				handle_error<Handle>::not_find(handle);
			return nullptr;
		}
		auto& object = created_objects[handle.id().index()];
		if (handle.id().generation() == object.second)
			return static_cast<ObjectType*>(object.first);
		else
		{
			if constexpr (isOptional);
			else
				handle_error<Handle>::generation_dismatch(handle);
			return nullptr;
		}
	}

	template <typename ObjectType, typename Handle, typename ... Args>
	Handle RenderDevice::_create_object_impl(const Handle handle, Args&&... args) noexcept
	{
		static_assert(std::is_base_of_v<IGPUObject, ObjectType>, "[DeviceWebGPU::_create_object_impl]: ResourceType must be derived from IGPUObject!");
		static_assert(std::is_base_of_v<RenderGraphHandle, Handle>, "[DeviceWebGPU::_create_object_impl]: Handle must be derived from RenderObjectHandle!");
		static_assert(std::is_base_of_v<typename Handle::ObjectType, ObjectType>, "[DeviceWebGPU::_create_object_impl]: Handle must match to it's ObjectType!");
		if (this->optional(handle))
		{
			handle_error<Handle>::create_on_existed(handle);
		}
		else
		{
			if (created_objects.size() < handle.id().index() + 1)
				created_objects.resize(handle.id().index() + 1);
			auto newRes = new ObjectType(handle, std::forward<Args>(args)...);
			created_objects[handle.id().index()] = sakura::make_pair(newRes, handle.id().generation());
		}
		return handle;
	}
}
