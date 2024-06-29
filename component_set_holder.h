#pragma once

#include <tuple>
#include <vector>
#include <string>

#include "component_factory.h"
#include "error_handling.h"
#include "typed_component.h"

namespace RaccoonEcs
{
	/**
	 * @brief This class can be used to store components specific for some non-entity object (e.g. for a World)
	 */
	template <typename ComponentTypeId, typename ComponentFactory = ComponentFactoryImpl<ComponentTypeId>>
	class ComponentSetHolderImpl
	{
	public:
		using ComponentSetHolder = ComponentSetHolderImpl<ComponentTypeId, ComponentFactory>;
		using TypedComponent = TypedComponentImpl<ComponentTypeId>;
		using ConstTypedComponent = ConstTypedComponentImpl<ComponentTypeId>;

		/**
		 * @brief Constructs component set holder.
		 * @param componentFactory  reference to an existent component factory that
		 * should outlive this component set holder
		 */
		explicit ComponentSetHolderImpl(const ComponentFactory& componentFactory)
			: mComponentFactory(componentFactory)
		{}

		~ComponentSetHolderImpl()
		{
			removeAllComponents();
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		ComponentSetHolderImpl(const ComponentSetHolderImpl& other)
			: mComponentFactory(other.mComponentFactory)
		{
			copyComponentsFrom(other);
		}
#else
		ComponentSetHolderImpl(const ComponentSetHolderImpl&) = delete;
#endif // RACCOON_ECS_COPYABLE_COMPONENTS
		ComponentSetHolderImpl& operator=(const ComponentSetHolderImpl&) = delete;
		ComponentSetHolderImpl(ComponentSetHolderImpl&&) noexcept = default;
		ComponentSetHolderImpl& operator=(ComponentSetHolderImpl&&) noexcept = default;

		/**
		 * @brief Gets all components stored in this component set holder
		 * @return vector of non-null pointers to components together with their types
		 */
		std::vector<TypedComponent> getAllComponents()
		{
			std::vector<TypedComponent> components;

			for (auto& componentData : mComponents)
			{
				components.emplace_back(componentData.first, componentData.second);
			}

			return components;
		}

		/**
		 * @brief Gets all components stored in this component set holder
		 * @return vector of non-null constant pointers to components together with their types
		 */
		std::vector<ConstTypedComponent> getAllComponents() const
		{
			std::vector<ConstTypedComponent> components;

			for (auto& componentData : mComponents)
			{
				components.emplace_back(componentData.first, componentData.second);
			}

			return components;
		}

		/**
		 * @return true if the given components exists in this component set holder
		 */
		template<typename ComponentType>
		bool doesComponentExists()
		{
			return mComponents[ComponentType::GetTypeId()] != nullptr;
		}

		/**
		 * @brief Creates and adds component to this component set holder
		 * @return pointer to a newly constructed component
		 */
		template<typename ComponentType>
		ComponentType* addComponent() noexcept
		{
			return static_cast<ComponentType*>(addComponentByType(ComponentType::GetTypeId()));
		}

		/**
		 * @brief Creates and adds component to this component set holder
		 * @param typeId  type of component that needs to be created
		 * @return pointer to a newly constructed component
		 */
		void* addComponentByType(ComponentTypeId typeId) noexcept
		{
			auto createFn = mComponentFactory.get().getCreationFn(typeId);
			void* component = createFn();
			addComponent(component, typeId);
			return component;
		}

		/**
		 * @brief Adds an existent component to this component set
		 * @param component  pointer to the component that needs to be added to this component set holder
		 * @param typeId  type on the component that is being added
		 *
		 * Note: the component shouldn't be owned by any other component set holder or entity manager and
		 * should be created from the same component factory that was used to construct this component set holder
		 */
		void addComponent(void* component, ComponentTypeId typeId)
		{
			if (component == nullptr)
			{
				return;
			}

			if (!mComponents.contains(typeId))
			{
				addComponentUnsafe(component, typeId);
			}
			else
			{
				RACCOON_ECS_ERROR("Trying to add component of type " + toString(typeId) + " that already exists, this will result in a memory leak");
			}
		}

		/**
		 * @brief Creates and adds the new component if it doesn't exist or
		 * just returns the pointer to the existent component
		 * @return non-null pointer to the component of the given type
		 */
		template<typename ComponentType>
		ComponentType* getOrAddComponent()
		{
			auto it = mComponents.find(ComponentType::GetTypeId());
			if (it == mComponents.end())
			{
				return static_cast<ComponentType*>(addComponentByTypeUnsafe(ComponentType::GetTypeId()));
			}
			return static_cast<ComponentType*>(it->second);
		}

		/**
		 * @brief Removes and destroys the component with the given type
		 * @param typeId  type of the component that needs to be removed
		 */
		void removeComponent(ComponentTypeId typeId)
		{
			if (auto it = mComponents.find(typeId); it != mComponents.end())
			{
				auto deleterFn = mComponentFactory.get().getDeletionFn(it->first);
				deleterFn(it->second);
				mComponents.erase(it);
			}
		}

		/**
		 * @return tuple of component pointers, either nullptr (if there's no such component)
		 * or pointing to the existent component
		 */
		template<typename... Components>
		std::tuple<Components*...> getComponents()
		{
			return std::make_tuple(getSingleComponent<Components>()...);
		}

		template<typename... Components>

		/**
		 * @return tuple of const component pointers, either nullptr (if there's no such component)
		 * or pointing to the existent component
		 */
		std::tuple<const Components*...> getComponents() const
		{
			return std::make_tuple(getSingleComponent<Components>()...);
		}

		/**
		 * @return true if the component set holder contain at least one component
		 */
		[[nodiscard]] bool hasAnyComponents() const
		{
			return !mComponents.empty();
		}

		/**
		 * @brief Removes and destroys all the components stored in this component set holder
		 */
		void removeAllComponents()
		{
			for (auto& component : mComponents)
			{
				auto deleterFn = mComponentFactory.get().getDeletionFn(component.first);
				deleterFn(component.second);
			}
			mComponents.clear();
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		/**
		 * @brief Override the storage with copies of all components from
		 */
		void overrideBy(const ComponentSetHolder& originalInstance)
		{
			removeAllComponents();
			copyComponentsFrom(originalInstance);
		}
#endif // RACCOON_ECS_COPYABLE_COMPONENTS

	private:
		void* addComponentByTypeUnsafe(ComponentTypeId typeId) noexcept
		{
			auto createFn = mComponentFactory.get().getCreationFn(typeId);
			void* component = createFn();
			addComponentUnsafe(component, typeId);
			return component;
		}

		void addComponentUnsafe(void* component, ComponentTypeId typeId)
		{
			mComponents[typeId] = component;
		}

		template<typename Component>
		Component* getSingleComponent()
		{
			auto it = mComponents.find(Component::GetTypeId());
			if (it != mComponents.end())
			{
				return static_cast<Component*>(it->second);
			}
			else
			{
				return nullptr;
			}
		}

		template<typename Component>
		const Component* getSingleComponent() const
		{
			auto it = mComponents.find(Component::GetTypeId());
			if (it != mComponents.end())
			{
				return static_cast<Component*>(it->second);
			}
			else
			{
				return nullptr;
			}
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		void copyComponentsFrom(const ComponentSetHolder& originalInstance)
		{
			for (auto& [type, component] : originalInstance.mComponents)
			{
				const auto& cloneFn = mComponentFactory.get().getCloneFn(type);
				mComponents.emplace(type, cloneFn(component));
			}
		}
#endif // RACCOON_ECS_COPYABLE_COMPONENTS

	private:
		std::unordered_map<ComponentTypeId, void*> mComponents;

		std::reference_wrapper<const ComponentFactory> mComponentFactory;
	};

} // namespace RaccoonEcs
