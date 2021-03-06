#pragma once
#include "ShaderCompiler/ShaderCompiler.h"

#include "RenderGraph/RenderGraph.h"
#include "RenderGraphWebGPU/RenderGraphWebGPU.h"

#include "ECS/ECS.h"

namespace render_system
{
	using namespace sakura::graphics;

	sakura::vector<std::byte> vertexShaderSpirv;
	sakura::vector<std::byte> pixelShaderSpirv;
	sakura::string vertexShaderHLSL = 
		u8"struct VertexIn\
		{\
			float2 aPos : SV_Position;\
			float3 aCol : COLOR;\
		};\
		struct VertexOut\
		{\
			float4 position : SV_Position;\
			float3 vCol : COLOR;\
		};\
		[[vk::binding(0, 0)]] cbuffer ub\
		{\
			float uRot;\
		};\
		VertexOut main(VertexIn vin)\
		{\
			VertexOut vout;\
			float cosA = cos(radians(uRot));\
			float sinA = sin(radians(uRot));\
			float3x3 rot = float3x3(cosA, sinA, 0.0,\
								   -sinA, cosA, 0.0,\
									0.0, 0.0, 1.0);\
			vout.position = float4(mul(float3(vin.aPos, 1.0), rot), 1.0);\
			vout.vCol = vin.aCol;\
			return vout;\
		}";
	sakura::string pixelShaderHLSL =
		u8"struct VertexOut\
		{\
			float4 position : SV_Position;\
			float3 vCol : COLOR;\
		};\
		float4 main(VertexOut pin) : SV_TARGET\
		{\
			return float4(pin.vCol.xyz, 1.f);\
		}";

	sakura::Window mainWindow;
	RenderGraph render_graph;
	RenderDeviceGroupProxy deviceGroup(render_graph);
	SwapChainHandle swapChain = render_graph.SwapChain("DefaultSwapChain");
	RenderShaderHandle vertexShader = render_graph.RenderShaderUnsafe("VertexShader");
	RenderShaderHandle pixelShader = render_graph.RenderShaderUnsafe("PixelShader");
	RenderPipelineHandle renderPipeline = render_graph.RenderPipeline("TrianglePipeline");
	RenderBufferHandle uniformBuffer = render_graph.RenderBuffer("UniformBuffer");
	RenderBufferHandle vertexBuffer = render_graph.RenderBuffer("VertexBuffer");
	RenderBufferHandle indexBuffer = render_graph.RenderBuffer("IndexBuffer");
	float rotDeg = 0.f;

	class RenderPassSimple : public RenderPass
	{
	public:
		RenderPassSimple(const RenderPassHandle handle)
			:RenderPass(handle) {}
		bool execute(const RenderGraph& rg, const RenderGraph::Builder& builder, IRenderDevice& device) noexcept override
		{
			command_buffer().enqueue<RenderCommandBeginRenderPass>(renderPipeline, attachment);
			command_buffer().enqueue<RenderCommandUpdateBinding>(binding);
			//command_buffer().enqueue<RenderCommandSetScissorRect>(
			//	0, device.backend() == EBackend::WebGPU ? mainWindow.extent().width / 2 + 10 : 0,
			//	mainWindow.extent().width / 2, mainWindow.extent().height
			//);
			command_buffer().enqueue<RenderCommandDraw>(
				RenderCommandDraw::VB(rg.blackboard<RenderBufferHandle>("VertexBuffer")),
				RenderCommandDraw::IB(rg.blackboard<RenderBufferHandle>("IndexBuffer"), 3, EIndexFormat::UINT16)
				);
			command_buffer().enqueue<RenderCommandEndRenderPass>();
			return device.execute(*this, handle()) && this->reset();
		}
		bool construct(RenderGraph::Builder& rg) noexcept override
		{
			rotDeg += 0.1f;
			deviceGroup.update_buffer(uniformBuffer, 0, &rotDeg, sizeof(rotDeg));

			attachment = Attachment({
				Attachment::Slot(swapChain, sakura::double4(), ELoadOp::Clear, EStoreOp::Store)
				});
			binding = Binding({
				Binding::Set({
					Binding::Slot(uniformBuffer, 0, sizeof(float), 0)
				})
				});
			return true;
		}
		Binding binding;
		Attachment attachment;
	};
	RenderPassHandle pass = render_graph.create_render_pass<RenderPassSimple>();

	void initialize()
	{
		sakura::development::ShaderCompilerVariables vars;
		vars.target_language = sakura::development::EShaderLanguage::Spirv;

		vars.entry = u8"main";
		vars.name = u8"BaseVS";
		vars.freq = sakura::graphics::EShaderFrequency::VertexShader;
		vertexShaderSpirv = 
			sakura::development::compile_hlsl(vertexShaderHLSL, vars);

		vars.entry = u8"main";
		vars.name = u8"BasePS";
		vars.freq = sakura::graphics::EShaderFrequency::PixelShader;
		pixelShaderSpirv = 
			sakura::development::compile_hlsl(pixelShaderHLSL, vars);

		sakura::Core::initialize(sakura::Core::Parameters());
		sakura::info("game thread id: {}", std::hash<std::thread::id>()(sakura::Core::get_main_thread_id()));
		
		sakura::Window::desc windDesc;
		windDesc.height = 450;
		windDesc.width = 800;
		windDesc.name = "Sakura Engine";
		mainWindow = sakura::Window::create(windDesc);

		// Create Devices
		DeviceConfiguration deviceConfig;
		deviceConfig.name = "DawnDevice";
		render_graph.emplace_device(new webgpu::RenderDevice(deviceConfig));
		IRenderDevice* dawnDevice = render_graph.get_device("DawnDevice");
		assert(dawnDevice != nullptr && "ERROR: Failed to create Dawn device!");
		deviceGroup.emplace(dawnDevice);

		// Create Swap Chains.
		deviceGroup.create_swap_chain(swapChain, SwapChainDesc(EPresentMode::Mailbox, mainWindow, 3));
		// Init RenderPipeline Desc
		RenderPipelineDesc pipelineDesc = RenderPipelineDesc(
			ShaderLayout({
				// Create Actual ShaderResources on Device.
				deviceGroup.create_shader(vertexShader, ShaderDesc("VertexShader", "main", EShaderFrequency::VertexShader, vertexShaderSpirv)),
				deviceGroup.create_shader(pixelShader, ShaderDesc("PiexelShader", "main", EShaderFrequency::PixelShader, pixelShaderSpirv))
				}),
			VertexLayout(
				{
					VertexLayout::Element("POSITION", EVertexFormat::FLOAT2, 0),
					VertexLayout::Element("COLOR", EVertexFormat::FLOAT3, 2 * sizeof(float))
				}, VertexLayout::Frequency::PerVertexData, sizeof(float) * 5
			),
			BindingLayout(
				{
					BindingLayout::Set(
					{
						BindingLayout::Slot(0, BindingLayout::UniformBuffer, EShaderFrequency::VertexShader) //Rotation
					})
				}),
			AttachmentLayout(
#ifndef SAKURA_TARGET_PLATFORM_EMSCRIPTEN
		{ AttachmentLayout::Slot(ETextureFormat::R8G8B8A8_UNORM) }
#else
		{ AttachmentLayout::Slot(ETextureFormat::B8G8R8A8_UNORM) }
#endif
		), EPrimitiveTopology::TriangleList, 1, 0xFFFFFFFF
		);

		// Create Render pipeline.
		deviceGroup.create_render_pipeline(renderPipeline, pipelineDesc);
		// create the buffers (x, y, r, g, b)
		float const vertData[] = {
			-0.8f, -0.8f, 0.0f, 0.0f, 1.0f, // BL
			 0.8f, -0.8f, 0.0f, 1.0f, 0.0f, // BR
			-0.0f,  0.8f, 1.0f, 0.0f, 0.0f, // top
		};
		uint16_t const indxData[] = {
			0, 1, 2,
			0 // padding (better way of doing this?)
		};
		// Create Buffers.
		deviceGroup.create_buffer(uniformBuffer,
			BufferDesc(EBufferUsage::UniformBuffer, sizeof(float), &rotDeg));
		deviceGroup.create_buffer(vertexBuffer,
			BufferDesc(EBufferUsage::VertexBuffer, sizeof(vertData), vertData));
		deviceGroup.create_buffer(indexBuffer,
			BufferDesc(EBufferUsage::IndexBuffer, sizeof(indxData), &indxData));
		sakura::info("All Tests Passed!");
	}

	namespace task_system = sakura::task_system;
	namespace math = sakura::math;
	using Vector3f = sakura::Vector3f;
	using Quaternion = sakura::Quaternion;
	using Rotator = sakura::Rotator;
	using float4x4 = sakura::float4x4;
	using IModule = sakura::IModule;
	task_system::Event RenderSystem(task_system::ecs::pipeline& ppl, float deltaTime)
	{
		using namespace sakura;
		using namespace sakura::graphics;
		using namespace ecs;

		RenderPass* pass_ptr = render_graph.render_pass(pass);
		pass_ptr->construct(render_graph.builder(pass));
		if (pass_ptr->execute(render_graph, render_graph.builder(pass), deviceGroup))
		{		
			deviceGroup.present(swapChain);
		}

		filters filter;
		filter.archetypeFilter = {
			{complist<LocalToWorld, WorldToLocal>}, //all
			{}, //any
			{} //none
		};
		static constexpr auto paramList = boost::hana::make_tuple(
			// write
			param<WorldToLocal>,
			// read.
			param<const LocalToWorld>
		);
		return task_system::ecs::schedule(ppl, *ppl.create_pass(filter, paramList),
			[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
			{
				auto o = operation{ paramList, pass, tk };
				
			});
	}
}