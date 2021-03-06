#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define TRACY_ENABLE
#include "tracy/Tracy.hpp"

#include "RuntimeCore/RuntimeCore.h"
#include "spdlog/spdlog.h"
#include "marl/finally.h"
#include "SakuraSTL.hpp"

#include "System/VirtualFileSystem.h"
#include "System/Log.h"

#include "ECS/ECS.h"

#include "TransformComponents.h"
#include "RenderSystem.h"
#include "Boids.h"
#include "TaskSystem/TaskSystem.h"
#include "RuntimeCore/RuntimeCore.h"
#include "kdtree.h"
#include <iostream>
#include <random>
#include <cmath>


#define forloop(i, z, n) for(auto i = std::decay_t<decltype(n)>(z); i<(n); ++i)
#define def static constexpr auto

namespace task_system = sakura::task_system;
namespace math = sakura::math;
using Vector3f = sakura::Vector3f;
using Quaternion = sakura::Quaternion;
using Rotator = sakura::Rotator;
using float4x4 = sakura::float4x4;
using IModule = sakura::IModule;

sakura::ecs::world ctx;

struct Timer
{
	void start_up()
	{
		tmpt = std::chrono::system_clock::now();
	}

	double end()
	{
		auto dur = std::chrono::system_clock::now() - tmpt;
		auto delta_time =
			static_cast<double>(dur.count()) / static_cast<double>(decltype(dur)::period::den);
		tmpt = std::chrono::system_clock::now();
		return delta_time;
	}
	std::chrono::system_clock::time_point tmpt;
};


std::size_t calc_align(std::size_t n, std::size_t align)
{
	return ((n + align - 1) / align)* align;
}

template<class T, class... Ts, class F>
task_system::Event ConvertSystem(task_system::ecs::pipeline& ppl, ecs::filters& filter, F&& f)
{
	using namespace ecs;
	static_assert(std::is_invocable<F, value_type_t<T>, const value_type_t<Ts>...>(), "wrong signature of convert function");
	static size_t timestamp = 0;
	filter.chunkFilter =
	{
		complist<Ts...>,
		timestamp
	};
	def paramList = hana::tuple{
		param<T>,
		param<const Ts>...
	};
	timestamp = ppl.get_timestamp();
	return task_system::ecs::schedule(ppl, *ppl.create_pass(filter, paramList),
		[f](const task_system::ecs::pipeline& pipeline, const pass& pass, const task& tk)
		{
			ZoneScopedN("ConvertSystem");
			auto o = operation{ paramList, pass, tk };
			hana::tuple arrays = { o.get_parameter<T>(), o.get_parameter<const Ts>()... };
			forloop(i, 0, o.get_count())
			{
				auto params = hana::transform(arrays, [i](auto v) { return v?v + i:nullptr; });
				hana::unpack(params, f);
			}
		}, 500);
}

template<class T>
task_system::Event Local2XSystem(task_system::ecs::pipeline& ppl, ecs::filters& filter)
{
	return ConvertSystem<T, Translation, Rotation, Scale>(ppl, filter,
		[](typename T::value_type* dst, const sakura::Vector3f* inTranslation, const sakura::Quaternion* inQuaternion, const sakura::Vector3f* inScale)
		{
			const Vector3f scale = inScale ? *inScale : Vector3f::vector_one();
			const Vector3f translation = inTranslation ? *inTranslation : Vector3f::vector_zero();
			const Quaternion quaternion = inQuaternion ? *inQuaternion : Quaternion::identity();

			*dst = math::make_transform(translation, scale, quaternion);
		});
}

task_system::Event RotationEulerSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<RotationEuler, Rotation>}
	};
	return ConvertSystem<Rotation, RotationEuler>(ppl, filter,
		[](sakura::Quaternion* dst, const sakura::Rotator* inRotator)
		{
			*dst = math::quaternion_from_rotator(*inRotator);
		});
}

task_system::Event HeadingSystem(task_system::ecs::pipeline& ppl)
{

	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<Heading, Rotation>}
	};
	return ConvertSystem<Rotation, Heading>(ppl, filter,
		[](sakura::Quaternion* dst, const sakura::Vector3f* inHeading)
		{
			*dst = math::look_at_quaternion(*inHeading);
		});
}

task_system::Event Child2WorldSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<Child, LocalToWorld>},
		{},
		{complist<Parent, LocalToParent>} // from root
	};
	def paramList = boost::hana::make_tuple(
		// write
		param<LocalToWorld>,
		// read.
		param<const LocalToParent>, param<const Child>
	);
	struct children2World
	{
		static void solve(const float4x4& parent_l2w, const entity e)
		{
			float4x4 l2w = float4x4();
			auto child_l2w = static_cast<float4x4*>(ctx.get_owned_rw(e, cid<LocalToWorld>));
			const auto child_l2p = static_cast<const float4x4*>(ctx.get_owned_ro(e, cid<LocalToParent>));
			if (child_l2w && child_l2p)
			{
				l2w = sakura::math::multiply(parent_l2w, *child_l2p);
				*child_l2w = l2w;
			}

			index_t childSet[] = { cid<Child> };
			if (ctx.has_component(e, childSet))
			{
				ecs::value_type_t<Child> child_children = ctx.get_owned_ro(e, cid<Child>);
				for (auto& child : child_children)
				{
					children2World::solve(l2w, child);
				}
			}
		}
	};
	return task_system::ecs::schedule(ppl,
		*ppl.create_pass(filter, paramList),
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			ZoneScopedN("Child2WorldSystem");
			auto o = operation{ paramList, pass, tk };
			const auto childrens = o.get_parameter<const Child>();
			float4x4* l2ws = o.get_parameter<LocalToWorld>();

			forloop(i, 0, o.get_count())
			{
				auto& children = childrens[i];
				for (const auto& child : children)
				{
					children2World::solve(l2ws[i], child);
				}
			}
		});
}

task_system::Event World2LocalSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<LocalToWorld, WorldToLocal>}, //all
		{}, //any
		{} //none
	};
	def paramList = boost::hana::make_tuple(
		// write
		param<WorldToLocal>,
		// read.
		param<const LocalToWorld>
	);
	return task_system::ecs::schedule(ppl,
		*ppl.create_pass(filter, paramList),
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			ZoneScopedN("World2LocalSystem");
			auto o = operation{ paramList, pass, tk };
			const float4x4* l2ws = o.get_parameter<const LocalToWorld>();
			float4x4* w2ls = o.get_parameter<WorldToLocal>();

			forloop(i, 0, o.get_count())
			{
				w2ls[i] = sakura::math::inverse(l2ws[i]);
			}
		});
}

template<class C, class T>
task_system::Event CopyComponent(task_system::ecs::pipeline& ppl, const ecs::filters& filter, ecs::shared_resource<T>& vector, int maxSlice = -1)
{
	using namespace ecs;
	def paramList = hana::tuple{ param<const C> };
	shared_entry shareList[] = { write(vector) };
	auto pass = ppl.create_pass(filter, paramList, shareList);
	vector->resize(pass->entityCount);
	return task_system::ecs::schedule(ppl, *pass,
		[vector](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk) mutable
		{
			ZoneScopedN("CopyComponent");
			auto o = operation{ paramList, pass, tk };
			auto index = o.get_index();
			auto comps = o.get_parameter<const C>();
			forloop(i, 0, o.get_count())
				(*vector)[index + i] = comps[i];
		}, maxSlice);
}

struct BoidPosition
{
	sakura::Vector3f value;
	BoidPosition() {}
	BoidPosition(sakura::Vector3f value)
		:value(value) {}
	def dim = 3;
	using value_type = float;
	float operator[](size_t i) const { return value.data_view()[i]; }
};

sakura::Vector3f nearest_position(const sakura::Vector3f& query, const std::vector<sakura::Vector3f>& searchTargets)
{
	float minDistance = std::numeric_limits<float>::max();
	sakura::Vector3f result;
	for (const auto& target : searchTargets)
	{
		float d = math::distance(query, target);
		if (d < minDistance)
		{
			minDistance = d;
			result = target;
		}
	}
	return result;
}

auto& get_random_engine()
{
	static std::random_device r;
	static std::default_random_engine el(r());
	return el;
}

task_system::Event RandomTargetSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter =
	{
		{complist<Translation, MoveToward, RandomMoveTarget>}
	};
	def paramList = hana::tuple{
		param<MoveToward>, param<const RandomMoveTarget>, param<const Translation>
	};
	return task_system::ecs::schedule(ppl, *ppl.create_pass(filter, paramList), 
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			ZoneScopedN("RandomTargetSystem");
			auto o = operation{ paramList, pass, tk };
			auto mts = o.get_parameter<MoveToward>();
			auto trs = o.get_parameter<const Translation>();
			auto rmts = o.get_parameter<const RandomMoveTarget>();
			forloop(i, 0, o.get_count())
			{
				if (math::subtract(mts[i].Target, trs[i]).is_nearly_zero())
				{
					auto sphere = rmts[i];
					mts[i].Target = sphere.random_point(get_random_engine());
				}
			}
		});
}

task_system::Event MoveTowardSystem(task_system::ecs::pipeline& ppl, float deltaTime)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter =
	{
		{complist<Translation, MoveToward>}
	};
	def paramList = hana::tuple{
		param<Translation>, param<const MoveToward>
	};
	static std::random_device r;
	static std::default_random_engine el(r());
	return task_system::ecs::schedule(ppl, *ppl.create_pass(filter, paramList),
		[deltaTime](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			ZoneScopedN("MoveTowardSystem");
			auto o = operation{ paramList, pass, tk };
			auto mts = o.get_parameter<const MoveToward>();
			auto trs = o.get_parameter<Translation>();
			forloop(i, 0, o.get_count())
				trs[i] = trs[i] + math::normalize(mts[i].Target - trs[i]) * mts[i].MoveSpeed;
		});
}
std::atomic<size_t> averageNeighberCount = 0;
std::atomic<size_t> maxNeighberCount = 0;

template<typename T>
void update_maximum(std::atomic<T>& maximum_value, T const& value) noexcept
{
	T prev_value = maximum_value;
	while (prev_value < value &&
		!maximum_value.compare_exchange_weak(prev_value, value))
	{
	}
}
task_system::Event BoidsSystem(task_system::ecs::pipeline& ppl, float deltaTime)
{
	using namespace ecs;
	filters boidFilter;
	boidFilter.archetypeFilter =
	{
		{complist<Boid, Translation, Heading>}, //all
		{}, //any
		{}, //none
		complist<Boid> //shared
	};
	//构造 kdtree, 提取 headings
	auto positions = make_resource<std::vector<BoidPosition>>();
	auto headings = make_resource<std::vector<sakura::Vector3f>>();
	auto kdtree = make_resource<core::algo::kdtree<BoidPosition>>();
	{
		auto copyPositionJob = CopyComponent<Translation>(ppl, boidFilter, positions);
		CopyComponent<Heading>(ppl, boidFilter, headings);
		shared_entry shareList[] = { read(positions), write(kdtree) };
		task_system::ecs::schedule_custom(ppl, *ppl.create_custom_pass(shareList), [positions, kdtree]() mutable
			{
				ZoneScopedN("Build Boid KDTree");
				kdtree->initialize(std::move(*positions));
			});
	}

	//收集目标和障碍物
	auto targets = make_resource<std::vector<BoidPosition>>();
	auto targetTree = make_resource<core::algo::kdtree<BoidPosition>>();
	{
		filters targetFilter;
		targetFilter.archetypeFilter =
		{
			{complist<BoidTarget, Translation>}
		};
		CopyComponent<Translation>(ppl, targetFilter, targets);
		shared_entry shareList[] = { read(targets), write(targetTree) };
		task_system::ecs::schedule_custom(ppl, *ppl.create_custom_pass(shareList), [targets, targetTree]() mutable
			{
				ZoneScopedN("Build Target KDTree");
				targetTree->initialize(std::move(*targets));
			});
	}
	//计算新的朝向
	auto newHeadings = make_resource<chunk_vector<sakura::Vector3f>>();
	{
		shared_entry shareList[] = { read(kdtree), read(headings), read(targetTree), write(newHeadings) };
		def paramList = hana::tuple{ param<const Heading>, param<const Translation>, param<const Boid> };
		auto pass = ppl.create_pass(boidFilter, paramList, shareList);
		newHeadings->resize(pass->entityCount);
		task_system::ecs::schedule(ppl, *pass,
			[headings, kdtree, targetTree, newHeadings, deltaTime](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk) mutable
			{
				ZoneScopedN("Boid Main");
				auto o = operation{ paramList, pass, tk };
				auto index = o.get_index();
				auto hds = o.get_parameter_owned<const Heading>();
				auto trs = o.get_parameter_owned<const Translation>();
				auto boid = o.get_parameter<const Boid>(); //这玩意是 shared
				std::vector<std::pair<float, int>> neighbers;
				neighbers.reserve(10);
				chunk_vector<sakura::Vector3f> alignments;
				chunk_vector<sakura::Vector3f> separations;
				chunk_vector<sakura::Vector3f> targetings;
				alignments.resize(o.get_count());
				separations.resize(o.get_count());
				targetings.resize(o.get_count());
				{
					ZoneScopedN("Collect Neighbors");
					forloop(i, 0, o.get_count())
					{
						//收集附近单位的位置和朝向信息
						neighbers.clear();
						kdtree->search_k_radius(trs[i], boid->SightRadius, 10, neighbers);
						alignments[i] = sakura::Vector3f::vector_zero();
						separations[i] = sakura::Vector3f::vector_zero();
						for (auto ng : neighbers)
						{
							alignments[i] = alignments[i] + (*headings)[ng.second];
							separations[i] = separations[i] + (*kdtree)[ng.second].value;
						}
						averageNeighberCount += neighbers.size();
						update_maximum(maxNeighberCount, neighbers.size());
					}
				}

				{
					ZoneScopedN("Collect Targets");
					forloop(i, 0, o.get_count())
					{
						//寻找一个目标
						targetings[i] = (*targetTree)[targetTree->search_nearest(trs[i])].value;
					}
				}
				
				{
					ZoneScopedN("Calculate Boids");
					forloop(i, 0, o.get_count())
					{
						//Boid 算法
						sakura::Vector3f alignment = math::normalize(alignments[i] / (float)neighbers.size() - hds[i]);
						sakura::Vector3f separation = math::normalize((float)neighbers.size() * trs[i] - separations[i]);
						sakura::Vector3f targeting = math::normalize(targetings[i] - trs[i]);
						sakura::Vector3f newHeading = math::normalize(alignment * boid->AlignmentWeight + separation * boid->SeparationWeight + targeting * boid->TargetWeight);
						(*newHeadings)[index + i] = math::normalize((hds[i] + (newHeading - hds[i]) * deltaTime));
					}
				}
			}, 100);
	}
	//结果转换
	{
		shared_entry shareList[] = { read(newHeadings) };
		def paramList = hana::tuple{ param<Heading>, param<Translation>, param<const Boid> };
		return task_system::ecs::schedule(ppl, *ppl.create_pass(boidFilter, paramList, shareList),
			[newHeadings, deltaTime](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
			{
				ZoneScopedN("Apply Boid");
				auto o = operation{ paramList, pass, tk };
				auto index = o.get_index();
				auto hds = o.get_parameter<Heading>();
				auto trs = o.get_parameter_owned<Translation>();
				auto boid = o.get_parameter<const Boid>(); //这玩意是 shared
				forloop(i, 0, o.get_count())
				{
					hds[i] = (*newHeadings)[i + index];
					trs[i] = trs[i] + hds[i] * deltaTime * boid->MoveSpeed;
				}
			}, 500);
	}
}

int main()
{
	if (!IModule::Registry::regist("ECS", &ECSModule::create) || !sakura::IModule::StartUp("ECS"))
	{
		sakura::error("Failed to StartUp ECSModule!");
		return -1;
	}
	render_system::initialize();

	using namespace sakura::ecs;

	register_components<Translation, Rotation, RotationEuler, Scale, LocalToWorld, LocalToParent, 
		WorldToLocal, Child, Parent, Boid, BoidTarget, MoveToward, RandomMoveTarget, Heading>();
	
	{	
		//创建 Boid 目标
		entity_type type
		{
			complist<BoidTarget, Translation, LocalToWorld, MoveToward, RandomMoveTarget>
		};
		for (auto slice : ctx.allocate(type, 500))
		{
			auto trs = init_component<Translation>(ctx, slice);
			auto mts = init_component<MoveToward>(ctx, slice);
			auto rmts = init_component<RandomMoveTarget>(ctx, slice);
			forloop(i, 0, slice.count)
			{
				std::uniform_real_distribution<float> speedDst(15.f, 25.f);
				rmts[i].center = Vector3f::vector_zero();
				rmts[i].radius = 1000.f;
				mts[i].Target = rmts[i].random_point(get_random_engine());
				mts[i].MoveSpeed = speedDst(get_random_engine());
				trs[i] = rmts[i].random_point(get_random_engine());
			}
		}
	}
	entity e;
	{
		//创建 Boid 设置
		entity_type type
		{
			complist<Boid>
		};
		for (auto slice : ctx.allocate(type, 1))
		{
			auto bs = init_component<Boid>(ctx, slice);
			bs->AlignmentWeight = bs->SeparationWeight = bs->TargetWeight = 1.f;
			bs->MoveSpeed = 15.f;
			bs->SightRadius = 5.f;
			e = ctx.get_entities(slice.c)[slice.start];
		}
	}

	{
		//创建 Boid
		entity_type type
		{
			complist<Translation, Heading, Rotation>,
			{&e, 1}
		};
		sphere s;
		s.center = Vector3f::vector_zero();
		s.radius = 1000.f;
		for (auto slice : ctx.allocate(type, 10000))
		{
			auto trs = init_component<Translation>(ctx, slice);
			auto hds = init_component<Heading>(ctx, slice);
			forloop(i, 0, slice.count)
			{
				std::uniform_real_distribution<float> uniform_dist(0, 1);
				auto& el = get_random_engine();
				sakura::Vector3f vector{ uniform_dist(el), uniform_dist(el), uniform_dist(el) };
				hds[i] = math::normalize(vector);
				trs[i] = s.random_point(el);
			}
		}
	}
	
	task_system::Scheduler scheduler(task_system::Scheduler::Config::allCores());
	scheduler.bind();
	defer(scheduler.unbind());  // Automatically unbind before returning.3
	Timer timer; 
	double deltaTime = 0;
	while(sakura::Core::yield())
	{
		ZoneScoped;

		timer.start_up();
		task_system::ecs::pipeline ppl(ctx);
		ppl.inc_timestamp();
		ppl.on_sync = [&](gsl::span<custom_pass*> dependencies)
		{
			for(auto dp : dependencies)
				ppl.pass_events[dp->passIndex].wait();
		};
		{
			ZoneScopedN("Schedule Systems")
			RotationEulerSystem(ppl);

			RandomTargetSystem(ppl);
			MoveTowardSystem(ppl, deltaTime);
			BoidsSystem(ppl, deltaTime);
			HeadingSystem(ppl);

			filters wrd_filter;
			wrd_filter.archetypeFilter = {
				{complist<LocalToWorld>},
				{complist<Translation, Scale, Rotation>},
				{complist<LocalToParent, Parent>}
			};
			Local2XSystem<LocalToWorld>(ppl, wrd_filter);

			filters c2p_filter;
			c2p_filter.archetypeFilter = {
				{complist<LocalToParent, Parent>},
				{complist<Translation, Scale, Rotation>},
				{}
			};
			Local2XSystem<LocalToParent>(ppl, c2p_filter);
			Child2WorldSystem(ppl);
			World2LocalSystem(ppl);
		}
		
		{
			ZoneScopedN("Pipeline Sync")
			// 等待pipeline
			ppl.wait();
		}

		//std::cout << "delta time: " << deltaTime * 1000 << std::endl;
		//std::cout << "average neighbor count: " << averageNeighberCount / 50000 << std::endl;
		//std::cout << "maximum neighbor count: " << maxNeighberCount << std::endl;
		//averageNeighberCount.store(0);
		deltaTime = timer.end();

		FrameMark;
	}
}