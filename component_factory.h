#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "component_pool.h"
#include "error_handling.h"

namespace RaccoonEcs
{
	template<typename ComponentType>
	struct DefaultComponentChunkSize
	{
		// have to use this weird syntax because it otherwise can break on MSVC is someone
		// inludes <windows.h> before this file without NOMINMAX defined
		constexpr static size_t value = (std::max)(static_cast<size_t>(1u), static_cast<size_t>(4096u / sizeof(ComponentType)));
	};

	template<typename ComponentTypeId>
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

		// component flag
		template<typename ComponentType, std::enable_if_t<std::is_empty_v<ComponentType>, int> = 0>
		void registerComponent(
			const size_t = 0,
			const bool = false,
			std::function<size_t(size_t)>&& = nullptr
		)
		{
			const ComponentTypeId componentTypeId = ComponentType::GetTypeId();

			mComponentCreators[componentTypeId] = [] {
				// the component has no data, so we don't need to allocate it
				static ComponentType component;
				return &component;
			};
			mComponentDeleters[componentTypeId] = [](void*) {};
#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
			mComponentCloners[componentTypeId] = [](void* component) -> void* {
				// all instances are mapping to the same memory, so we can just return the pointer
				return component;
			};
#endif // RACCOON_ECS_COPYABLE_COMPONENTS
		}

		// normal component
		template<typename ComponentType, std::enable_if_t<!std::is_empty_v<ComponentType>, int> = 0>
		void registerComponent(
			const size_t defaultChunkSize = DefaultComponentChunkSize<ComponentType>::value,
			const bool needPreallocate = false,
			std::function<size_t(size_t)>&& poolGrowStrategyFn = nullptr
		)
		{
			const ComponentTypeId componentTypeId = ComponentType::GetTypeId();

			auto componentPoolRawPtr = new (std::nothrow) ComponentPool<ComponentType>(defaultChunkSize, needPreallocate, std::move(poolGrowStrategyFn));
			mComponentPools.emplace_back(componentPoolRawPtr);

			mComponentCreators[componentTypeId] = [componentPoolRawPtr] {
				return componentPoolRawPtr->acquireComponent();
			};
			mComponentDeleters[componentTypeId] = [componentPoolRawPtr](void* component) {
				if (component)
				{
					componentPoolRawPtr->releaseComponent(component);
				}
			};
#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
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
#endif // RACCOON_ECS_COPYABLE_COMPONENTS
		}

		[[nodiscard]] CreationFn getCreationFn(ComponentTypeId typeId) const
		{
			const auto& it = mComponentCreators.find(typeId);
			if (it != mComponentCreators.cend())
			{
				return it->second;
			}

			RACCOON_ECS_ERROR(std::string("Unknown component type: '") + toString(typeId) + "'");
			return nullptr;
		}

		[[nodiscard]] DeletionFn getDeletionFn(ComponentTypeId typeId) const
		{
			const auto& it = mComponentDeleters.find(typeId);
			if (it != mComponentDeleters.cend())
			{
				return it->second;
			}

			RACCOON_ECS_ERROR(std::string("Unknown component type: '") + toString(typeId) + "'");
			return nullptr;
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		[[nodiscard]] CloneFn getCloneFn(ComponentTypeId typeId) const
		{
			const auto& it = mComponentCloners.find(typeId);
			if (it != mComponentCloners.cend())
			{
				return it->second;
			}

			RACCOON_ECS_ERROR(std::string("Unknown component type: '") + toString(typeId) + "'");
			return nullptr;
		}
#endif // RACCOON_ECS_COPYABLE_COMPONENTS

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
#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		std::unordered_map<ComponentTypeId, CloneFn> mComponentCloners;
#endif // RACCOON_ECS_COPYABLE_COMPONENTS
	};

} // namespace RaccoonEcs
