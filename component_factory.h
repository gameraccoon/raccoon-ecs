#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "error_handling.h"
#include "component_pool.h"

namespace RaccoonEcs
{
	template <typename ComponentTypeId>
	class ComponentFactoryImpl
	{
	public:
		using CreationFn = std::function<void*()>;
		using DeletionFn = std::function<void(void*)>;
		using CloneFn = std::function<void*(void*)>;

		ComponentFactoryImpl() = default;
		ComponentFactoryImpl(ComponentFactoryImpl&) = delete;
		ComponentFactoryImpl& operator=(ComponentFactoryImpl&) = delete;
		ComponentFactoryImpl(ComponentFactoryImpl&&) = delete;
		ComponentFactoryImpl& operator=(ComponentFactoryImpl&&) = delete;

		template<typename ComponentType>
		void registerComponent(
			const size_t defaultChunkSize = std::max(static_cast<size_t>(1), static_cast<size_t>(4096u / sizeof(ComponentType))),
			const bool needPreallocate = false,
			const std::function<size_t(size_t)>& poolGrowStrategyFn = nullptr)
		{
			const ComponentTypeId componentTypeId = ComponentType::GetTypeId();

			auto componentPoolRawPtr = new (std::nothrow) ComponentPool<ComponentType>(defaultChunkSize, needPreallocate, poolGrowStrategyFn);
			mComponentPools.emplace_back(componentPoolRawPtr);

			mComponentCreators[componentTypeId] = [componentPoolRawPtr]{
				return componentPoolRawPtr->acquireComponent();
			};
			mComponentDeleters[componentTypeId] = [componentPoolRawPtr](void* component){
				if (component)
				{
					componentPoolRawPtr->releaseComponent(component);
				}
			};
#ifdef RACOON_ECS_COPYABLE_COMPONENTS
			mComponentCloners[componentTypeId] = [componentPoolRawPtr](void* component) -> void* {
				if (component)
				{
					return componentPoolRawPtr->acquireComponent(std::ref(*static_cast<ComponentType*>(component)));
				}
				else
				{
					return nullptr;
				}
			};
#endif // RACOON_ECS_COPYABLE_COMPONENTS
		}

		[[nodiscard]] CreationFn getCreationFn(ComponentTypeId typeId) const
		{
			const auto& it = mComponentCreators.find(typeId);
			if (it != mComponentCreators.cend())
			{
				return it->second;
			}

			using std::to_string;
			RACCOON_ECS_ERROR(std::string("Unknown component type: '") + to_string(typeId) + "'");
			return nullptr;
		}

		[[nodiscard]] DeletionFn getDeletionFn(ComponentTypeId typeId) const
		{
			const auto& it = mComponentDeleters.find(typeId);
			if (it != mComponentDeleters.cend())
			{
				return it->second;
			}

			using std::to_string;
			RACCOON_ECS_ERROR(std::string("Unknown component type: '") + to_string(typeId) + "'");
			return nullptr;
		}

#ifdef RACOON_ECS_COPYABLE_COMPONENTS
		[[nodiscard]] CloneFn getCloneFn(ComponentTypeId typeId) const
		{
			const auto& it = mComponentCloners.find(typeId);
			if (it != mComponentCloners.cend())
			{
				return it->second;
			}

			using std::to_string;
			RACCOON_ECS_ERROR(std::string("Unknown component type: '") + to_string(typeId) + "'");
			return nullptr;
		}
#endif // RACOON_ECS_COPYABLE_COMPONENTS

		[[nodiscard]] void* createComponent(ComponentTypeId typeId) const
		{
			const auto& it = mComponentCreators.find(typeId);
			if (it != mComponentCreators.cend() && it->second)
			{
				return it->second();
			}

			return nullptr;
		}

		template<typename F>
		void forEachComponentType(F fn) const
		{
			for (auto& creator : mComponentCreators)
			{
				fn(creator.first);
			}
		}

	private:
		std::vector<std::unique_ptr<ComponentPoolBase>> mComponentPools;

		std::unordered_map<ComponentTypeId, CreationFn> mComponentCreators;
		std::unordered_map<ComponentTypeId, DeletionFn> mComponentDeleters;
#ifdef RACOON_ECS_COPYABLE_COMPONENTS
		std::unordered_map<ComponentTypeId, CloneFn> mComponentCloners;
#endif // RACOON_ECS_COPYABLE_COMPONENTS
	};

} // namespace RaccoonEcs
