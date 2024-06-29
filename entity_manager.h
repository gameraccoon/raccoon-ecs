#pragma once

#include <algorithm>
#include <ranges>
#include <tuple>
#include <string>

#include "component_factory.h"
#include "component_indexes.h"
#include "component_map.h"
#include "delegates.h"
#include "entity.h"
#include "error_handling.h"
#include "typed_component.h"

namespace RaccoonEcs
{
	template <typename ComponentTypeId, typename ComponentFactory = ComponentFactoryImpl<ComponentTypeId>>
	class EntityManagerImpl
	{
	public:
		using EntityManager = EntityManagerImpl<ComponentTypeId>;
		using TypedComponent = TypedComponentImpl<ComponentTypeId>;
		using ConstTypedComponent = ConstTypedComponentImpl<ComponentTypeId>;
		using ComponentMap = ComponentMapImpl<ComponentTypeId>;

	public:
		/**
		 * @param componentFactory  Should be a reference to a ComponentFactory object that has longer lifetime than this EntityManager
		 */
		explicit EntityManagerImpl(const ComponentFactory& componentFactory)
			: mComponentFactory(componentFactory)
		{}

		~EntityManagerImpl()
		{
			clear();
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		explicit EntityManagerImpl(const EntityManagerImpl& other)
			: EntityManagerImpl(other.mComponentFactory)
		{
			copyEntitiesFrom(other);
		}
#else
		EntityManagerImpl(const EntityManagerImpl&) = delete;
#endif // RACCOON_ECS_COPYABLE_COMPONENTS
		EntityManagerImpl& operator=(const EntityManagerImpl&) = delete;
		EntityManagerImpl(EntityManagerImpl&&) noexcept = default;
		EntityManagerImpl& operator=(EntityManagerImpl&&) noexcept = default;

		/**
		 * @brief Generates a new unique entity and adds it to this manager
		 * @return The newly created entity
		 */
		Entity addEntity()
		{
			Entity::RawId rawEntityId;
			if (mFreeEntityIds.empty())
			{
				RACCOON_ECS_ASSERT(mEntityVersions.size() == mEntityExistanceFlags.size(), "Inconsistent entity vectors");
				mEntityVersions.push_back(0);
				mEntityExistanceFlags.push_back(true);
				rawEntityId = static_cast<Entity::RawId>(mEntityVersions.size() - 1);
			}
			else
			{
				const size_t freeEntityId = mFreeEntityIds.back();
				mFreeEntityIds.pop_back();
				mEntityExistanceFlags[freeEntityId] = true;
				rawEntityId = static_cast<Entity::RawId>(freeEntityId);
			}

			onEntityAdded.broadcast();
			return Entity{rawEntityId, mEntityVersions[rawEntityId]};
		}

		/**
		 * @brief Removes the given entity from the manager, and unregisters its ID, so it can be reused
		 * again in any manager in future
		 * @param entityToRemove  The entity that should be removed, should be bound to this manager
		 */
		void removeEntity(const Entity entityToRemove)
		{
			const size_t entityToRemoveIdx = static_cast<size_t>(entityToRemove.getRawId());
			if (entityToRemoveIdx >= mEntityExistanceFlags.size() || !mEntityExistanceFlags[entityToRemoveIdx])
			{
				RACCOON_ECS_ERROR(std::string("Trying to remove non-existent entity: ") + std::to_string(entityToRemoveIdx));
				return;
			}

			if (mEntityVersions[entityToRemoveIdx] != entityToRemove.getVersion())
			{
				RACCOON_ECS_ERROR(std::string("Trying to remove entity that was already removed. id:") + std::to_string(entityToRemoveIdx)
					+ " recorded version:" + std::to_string(mEntityVersions[entityToRemoveIdx])
					+ " removed version " + std::to_string(entityToRemove.getVersion()));
				return;
			}

			for (auto& componentVector : mComponents)
			{
				// if the vector contains deleted entity
				if (entityToRemoveIdx < componentVector.second.size())
				{
					// if the entity contains the component
					if (void*& componentPtrRef = componentVector.second[entityToRemoveIdx])
					{
						// remove the component
						auto deleterFn = mComponentFactory.get().getDeletionFn(componentVector.first);
						deleterFn(componentPtrRef);
						componentPtrRef = nullptr;
					}
				}
			}

			mIndexes.onEntityRemoved(entityToRemoveIdx);

			onEntityRemoved.broadcast();

			mEntityExistanceFlags[entityToRemoveIdx] = false;
			const Entity::Version newVersion = ++mEntityVersions[entityToRemoveIdx];
			// if we hit zero, we used up all the versions for this entity id, skip it
			if (newVersion != 0)
			{
				mFreeEntityIds.push_back(entityToRemoveIdx);
			}
		}

		/**
		 * @brief Checks if the entity is exists in this manager
		 */
		bool hasEntity(const Entity entity)
		{
			const size_t rawEntityId = static_cast<size_t>(entity.getRawId());
			return rawEntityId < mEntityVersions.size()
				&& mEntityVersions[rawEntityId] == entity.getVersion()
				&& mEntityExistanceFlags[rawEntityId];
		}

		/**
		 * @brief Returns true if this manager has at least one entity
		 * If it doesn't have any entities it's pretty much doesn't have any valuable data
		 * since EntityManager can't contain components not bound to entities
		 */
		[[nodiscard]] bool hasAnyEntity() const
		{
			return mEntityVersions.size() != mFreeEntityIds.size();
		}

		/**
		 * @brief Collect the list of all entities
		 *
		 * Can be useful for serialization
		 */
		std::vector<Entity> collectAllEntities() const
		{
			std::vector<Entity> entities;
			entities.reserve(mEntityExistanceFlags.size());
			for (size_t i = 0; i < mEntityExistanceFlags.size(); ++i)
			{
				if (mEntityExistanceFlags[i])
				{
					entities.emplace_back(static_cast<Entity::RawId>(i), mEntityVersions[i]);
				}
			}
			return entities;
		}

		/**
		 * @brief Collects components that belongs to the given entity and returns them together with their types
		 * @param entity  The entity whose components are collected
		 * @param outComponents  The list of components belonging to the entity with their types
		 */
		void getAllEntityComponents(const Entity entity, std::vector<TypedComponent>& outComponents)
		{
			const Entity::RawId entityIdx = entity.getRawId();
			if (entityIdx < mEntityExistanceFlags.size() && mEntityExistanceFlags[entityIdx])
			{
				for (auto& componentVector : mComponents)
				{
					if (componentVector.second.size() > entityIdx && componentVector.second[entityIdx] != nullptr)
					{
						outComponents.emplace_back(componentVector.first, componentVector.second[entityIdx]);
					}
				}
			}
		}

		/**
		 * @brief Collects components that belongs to the given entity and returns them together with their types
		 * @param entity  The entity whose components are collected
		 * @param outComponents  The list of constant components belonging to the entity with their types
		 */
		void getAllEntityComponents(const Entity entity, std::vector<ConstTypedComponent>& outComponents) const
		{
			const Entity::RawId entityIdx = entity.getRawId();
			if (entityIdx < mEntityExistanceFlags.size() && mEntityExistanceFlags[entityIdx])
			{
				for (auto& componentVector : mComponents)
				{
					if (componentVector.second.size() > entityIdx && componentVector.second[entityIdx] != nullptr)
					{
						outComponents.emplace_back(componentVector.first, componentVector.second[entityIdx]);
					}
				}
			}
		}

		/**
		 * @brief Checks if the given entity has the given component
		 */
		[[nodiscard]] bool doesEntityHaveComponent(const Entity entity, ComponentTypeId typeId) const
		{
			const Entity::RawId entityIdx = entity.getRawId();
			if (entityIdx < mEntityExistanceFlags.size() && mEntityExistanceFlags[entityIdx])
			{
				const std::vector<void*>& componentVector = mComponents.getComponentVectorById(typeId);
				return (componentVector.size() > entityIdx && componentVector[entityIdx] != nullptr);
			}

			RACCOON_ECS_ERROR(std::string("Trying to check component ") + toString(typeId)
				+ " of non-existing entity: " + std::to_string(entity.getRawId()));
			return false;
		}

		/**
		 * @brief Checks if the given entity has the given component
		 */
		template<typename ComponentType>
		[[nodiscard]] bool doesEntityHaveComponent(Entity entity)
		{
			return doesEntityHaveComponent(entity, ComponentType::GetTypeId());
		}

		/**
		 * @brief Adds a new default-initialized component to the given entity and returns pointer to it
		 * @param entity  The entity that will own the component
		 * @return A pointer to the newly created default-initialized component
		 *
		 * Beware that the entity should not have the component of the given type prior calling the function
		 * otherwise the call can result in memory leak or UB
		 */
		template<typename ComponentType>
		ComponentType* addComponent(Entity entity)
		{
			return static_cast<ComponentType*>(addComponentByType(entity, ComponentType::GetTypeId()));
		}

		/**
		 * @brief Adds a new default-initialized component to the given entity and returns pointer to it
		 * @param entity  The entity that will own the component
		 * @param typeId  The type of the component
		 * @return A pointer to the newly created default-initialized component
		 *
		 * Beware that the entity should not have the component of the given type prior calling the function
		 * otherwise the call can result with memory leak or UB
		 */
		void* addComponentByType(const Entity entity, ComponentTypeId typeId)
		{
			const auto createFn = mComponentFactory.get().getCreationFn(typeId);
			void* component = createFn();
			addComponent(entity, component, typeId);
			return component;
		}

		/**
		 * @brief Adds the given component with the given type to the entity
		 * @param entity  The entity that will own the component
		 * @param component  The pointer to the existent component. should not belong to any other entity
		 * @param typeId  The type of the given component
		 *
		 * Beware that the entity should not have the component of the given type prior calling the function
		 * otherwise the entity won't be added and no indication of it will be provided, effectively producing
		 * a memory leak or UB
		 */
		void addComponent(const Entity entity, void* component, ComponentTypeId typeId)
		{
			const Entity::RawId entityIdx = entity.getRawId();
			if (entityIdx >= mEntityExistanceFlags.size() || !mEntityExistanceFlags[entityIdx])
			{
				RACCOON_ECS_ERROR(std::string("Trying to add component ") + toString(typeId)
					+ " to a non-existent entity " + std::to_string(entityIdx));
				// memory leak here
				return;
			}

			addComponentToEntity(entityIdx, component, typeId);
		}

		/**
		 * @brief Removes the component of the given type that the entity owns and destroys it
		 * @param entity  The entity owning the component
		 */
		template<typename ComponentType>
		void removeComponent(Entity entity)
		{
			removeComponent(entity, ComponentType::GetTypeId());
		}

		void removeComponent(const Entity entity, ComponentTypeId typeId)
		{
			const Entity::RawId entityIdx = entity.getRawId();
			if (entityIdx >= mEntityExistanceFlags.size() || !mEntityExistanceFlags[entityIdx])
			{
				RACCOON_ECS_ERROR(std::string("Trying to remove component ") + toString(typeId)
						+ " from a non-existent entity " + std::to_string(entityIdx));
				return;
			}

			auto& componentsVector = mComponents.getComponentVectorById(typeId);

			if (entityIdx < componentsVector.size())
			{
				auto deleterFn = mComponentFactory.get().getDeletionFn(typeId);
				deleterFn(componentsVector[entityIdx]);
				componentsVector[entityIdx] = nullptr;
			}

			mIndexes.onComponentRemoved(typeId, entityIdx);
		}

		/**
		 * @brief Creates a component of the given type and schedules its addition to the given entity
		 * @param entity  The entity that will own the component
		 * @return  A pointer to the newly created default-initialized component
		 *
		 * You can use the component right away, but it won't be queried before `executeScheduledActions` called.
		 *
		 * Beware that the entity should not have the component of the given type prior `executeScheduledActions`
		 * called otherwise the call can result with memory leak or UB
		 */
		template<typename ComponentType>
		ComponentType* scheduleAddComponent(Entity entity)
		{
			const ComponentTypeId componentTypeId = ComponentType::GetTypeId();
			const auto createFn = mComponentFactory.get().getCreationFn(componentTypeId);
			auto component = static_cast<ComponentType*>(createFn());
			scheduleAddComponent(entity, component, componentTypeId);
			return component;
		}

		/**
		 * @brief Creates a component of the given type and schedules its addition to the given entity
		 * @param entity  The entity that will own the component
		 * @param component  The pointer to the existent component (can't be nullptr or be owned by another entity)
		 * @param typeId  The type of the given component
		 * @return  A pointer to the newly created default-initialized component
		 *
		 * You can use the component right away, but it won't be queried before `executeScheduledActions` called.
		 *
		 * Beware that the entity should not have the component of the given type prior `executeScheduledActions`
		 * called otherwise the call can result with memory leak or UB
		 */
		void scheduleAddComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			mScheduledComponentAdditions.emplace_back(entity, component, typeId);
		}

		/**
		 * @brief Schedules removing the component of the given type from the given entity
		 */
		template<typename ComponentType>
		void scheduleRemoveComponent(Entity entity)
		{
			scheduleRemoveComponent(entity, ComponentType::GetTypeId());
		}

		/**
		 * @brief Schedules removing the component of the given type from the given entity
		 */
		void scheduleRemoveComponent(Entity entity, ComponentTypeId typeId)
		{
			mScheduledComponentRemovements.emplace_back(entity, typeId);
		}

		/**
		 * @brief Executes scheduled action, such as component additions and removements
		 */
		void executeScheduledActions()
		{
			for (const auto& addition : mScheduledComponentAdditions)
			{
				addComponent(addition.entity, addition.component, addition.typeId);
			}
			mScheduledComponentAdditions.clear();

			for (const auto& removement : mScheduledComponentRemovements)
			{
				removeComponent(removement.entity, removement.typeId);
			}
			mScheduledComponentRemovements.clear();
		}

		/**
		 * @brief Get specific component set belonging to the given entity
		 * @return Returns the component set the the entity owns all the components,
		 * otherwise can return components partially (part of them can be nullptr)
		 */
		template<typename... Components>
		std::tuple<Components*...> getEntityComponents(const Entity entity)
		{
			const Entity::RawId entityIdx = entity.getRawId();
			if (entityIdx >= mEntityExistanceFlags.size() || !mEntityExistanceFlags[entityIdx])
			{
				return getEmptyComponents<Components...>();
			}

			auto componentVectors = mComponents.template getComponentVectors<Components...>();
			return getEntityComponentSet<Components...>(entityIdx, componentVectors);
		}

		/**
		 * @brief Collects component sets from entities that has all the given components,
		 * appends the result to the in-out argument
		 * @param inOutComponents  The vector of tuples of component pointers, matched data will
		 * be appended to the vector
		 * @param data  Additional data, will be added to each matched record. Can be useful to
		 * identify a specific manager
		 */
		template<typename... Components, typename... AdditionalData>
		void getComponents(std::vector<std::tuple<AdditionalData..., Components* ...>>& inOutComponents, AdditionalData... data)
		{
			const auto& components = mIndexes.template getComponents<Components...>(mComponents);

			if (!components.empty())
			{
				for (const auto& componentSet : components)
				{
					inOutComponents.push_back(std::tuple_cat(
						std::make_tuple(data...),
						componentSet
					));
				}
			}
		}

		/**
		 * @brief Collects entities that has all the given components together with components,
		 * appends the result to the in-out argument
		 * @param inOutComponents  The vector of tuples of component pointers, matched data will
		 * be appended to the vector
		 * @param data  Additional data, will be added to each matched record. Can be useful to
		 * identify a specific manager
		 */
		template<typename... Components, typename... AdditionalData>
		void getComponentsWithEntities(std::vector<std::tuple<AdditionalData..., Entity, Components* ...>>& inOutComponents, AdditionalData... data)
		{
			const std::vector<size_t>& componentIndexes = mIndexes.template getIndex<Components...>(mComponents);

			if (!componentIndexes.empty())
			{
				if (inOutComponents.size() + componentIndexes.size() > inOutComponents.capacity())
				{
					inOutComponents.reserve(std::max(inOutComponents.size() + componentIndexes.size(), inOutComponents.size() * 2));
				}

				auto components = mIndexes.template getComponents<Components...>(mComponents);

				for (size_t i = 0; i < componentIndexes.size(); ++i)
				{
					const size_t entityIdx = componentIndexes[i];
					inOutComponents.push_back(std::tuple_cat(
						std::make_tuple(data...),
						std::make_tuple(Entity{static_cast<Entity::RawId>(entityIdx), mEntityVersions[entityIdx]}),
						components[i]
					));
				}
			}
		}

		/**
		 * @brief Applies the given callable to all the component sets from matched entities
		 * @param processor  The callable that will be applied to the matched component sets
		 * @param data  Additional data, will be added to each matched record. Can be useful
		 * to identify a specific manager
		 */
		template<typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(FunctionType processor, AdditionalData... data)
		{
			const auto& components = mIndexes.template getComponents<Components...>(mComponents);

			if (!components.empty())
			{
				for (const auto& componentSet : components)
				{
					std::apply(processor, std::tuple_cat(
						std::make_tuple(data...),
						componentSet
					));
				}
			}
		}

		/**
		 * @brief Applies the given callable to all the entities together with component sets
		 * that have all the given components
		 * @param processor  The callable that will be applied to the matched component sets
		 * @param data  Additional data, will be added to each matched record. Can be useful
		 * to identify a specific manager
		 */
		template<typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(FunctionType processor, AdditionalData... data)
		{
			const std::vector<size_t>& componentIndexes = mIndexes.template getIndex<Components...>(mComponents);

			if (!componentIndexes.empty())
			{
				auto components = mIndexes.template getComponents<Components...>(mComponents);

				for (size_t i = 0; i < componentIndexes.size(); ++i)
				{
					const size_t entityIdx = componentIndexes[i];
					std::apply(processor, std::tuple_cat(
						std::make_tuple(data...),
						std::make_tuple(Entity{static_cast<Entity::RawId>(entityIdx), mEntityVersions[entityIdx]}),
						components[i]
					));
				}
			}
		}

		/**
		 * @brief Collects entities that have all of the given components and appends them to in-out argument
		 * @param componentIndexes  Vector of types that need to be checked
		 * @param inOutEntities  Vector of entities that matched entities will be appended to
		 */
		void getEntitiesHavingComponents(const std::vector<ComponentTypeId>& componentIndexes, std::vector<Entity>& inOutEntities) const
		{
			if (componentIndexes.empty())
			{
				return;
			}

			size_t endIdx = std::numeric_limits<Entity::RawId>::max();
			std::vector<const std::vector<void*>*> componentVectors;
			componentVectors.reserve(componentIndexes.size());
			for (ComponentTypeId typeId : componentIndexes)
			{
				auto& componentVector = mComponents.getComponentVectorById(typeId);

				endIdx = std::min(endIdx, componentVector.size());

				componentVectors.push_back(&componentVector);
			}

			for (size_t idx = 0; idx < endIdx; ++idx)
			{
				const bool hasAllComponents = std::all_of(
					componentVectors.cbegin(),
					componentVectors.cend(),
					[idx](const std::vector<void*>* componentVector){ return (*componentVector)[idx] != nullptr; }
				);

				if (hasAllComponents)
				{
					inOutEntities.push_back(Entity{static_cast<Entity::RawId>(idx), mEntityVersions[idx]});
				}
			}
		}

		/**
		 * @brief Returns amount of entities with matching components
		 *
		 * Note that this call can create an index for the requested components
		 * (if such index didn't exist)
		 */
		template<typename... Components>
		size_t getMatchingEntitiesCount()
		{
			return mIndexes.template getIndexSize<Components...>(mComponents);
		}

		/**
		 * @brief Transfers the given entity together with its components to another manager
		 * @param newManager  The manager to which the entity will be transfer to
		 * @param entity  The entity that will be transferred
		 * @return The entity in the new manager
		 *
		 * The components are guaranteed not to be moved in the memory.
		 */
		Entity transferEntityTo(EntityManager& newManager, const Entity entity)
		{
			if (this == &newManager)
			{
				RACCOON_ECS_ERROR("Transferring entity to the same manager. This should never happen");
				return entity;
			}

			RACCOON_ECS_ASSERT(&mComponentFactory.get() == &newManager.mComponentFactory.get(),
				"Trying to transfer entity between managers with different component factories, this is not supported yet");

			const size_t oldEntityIdx = static_cast<size_t>(entity.getRawId());
			if (oldEntityIdx >= mEntityExistanceFlags.size() || !mEntityExistanceFlags[oldEntityIdx])
			{
				RACCOON_ECS_ERROR(std::string("Trying transfer non-existent entity: ") + std::to_string(entity.getRawId()));
				return entity;
			}

			const Entity newEntity = newManager.addEntity();

			for (auto& componentVector : mComponents)
			{
				if (oldEntityIdx < componentVector.second.size())
				{
					// transfer the component if it exists
					if (componentVector.second[oldEntityIdx] != nullptr)
					{
						newManager.addComponent(
							newEntity,
							componentVector.second[oldEntityIdx],
							componentVector.first
						);

						// remove the component from the old manager
						componentVector.second[oldEntityIdx] = nullptr;
					}
				}
			}

			mIndexes.onEntityRemoved(oldEntityIdx);

			mEntityExistanceFlags[oldEntityIdx] = false;
			const Entity::Version newVersion = ++mEntityVersions[oldEntityIdx];
			// if we hit zero, we used up all the versions for this entity id, skip it
			if (newVersion != 0)
			{
				mFreeEntityIds.push_back(oldEntityIdx);
			}

			return newEntity;
		}

		/**
		 * @brief Initializes the index if it wasn't created
		 *
		 * This function is not necessary to call, indexes will be created automatically
		 * when some request for components made, however you can do it in advance
		 * (e.g. to reduce first frame time)
		 */
		template <typename... Components>
		void initIndex()
		{
			mIndexes.template initializeIndex<Components...>(mComponents);
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		/**
		 * @brief Rewrite this entity manager with a copy of originalInstance
		 */
		void overrideBy(const EntityManager& originalInstance)
		{
			clear();
			copyEntitiesFrom(originalInstance);
		}
#endif // RACCOON_ECS_COPYABLE_COMPONENTS

		/**
		 * @brief Shrinks the vectors of components to eliminate empty elements at the end, and
		 * remove empty vectors
		 */
		void clearCaches()
		{
			for (auto& componentVectorPair : mComponents)
			{
				auto& componentVector = componentVectorPair.second;
				auto lastFilledRIt = std::find_if(componentVector.rbegin(), componentVector.rend(),
					[](const void* component){ return component != nullptr; }
				);

				if (lastFilledRIt != componentVector.rend())
				{
					const size_t lastFilledIdx = std::distance(lastFilledRIt, componentVector.rend());
					componentVector.erase(componentVector.begin() + static_cast<ptrdiff_t>(lastFilledIdx), componentVector.end());
				}
				else
				{
					componentVector.clear();
				}
			}

			mComponents.cleanEmptyVectors();
		}

		/**
		 * @brief Delete all entities and components stored in the manager
		 */
		void clear()
		{
			for (auto& componentVector : mComponents)
			{
				auto deleterFn = mComponentFactory.get().getDeletionFn(componentVector.first);

				for (auto component : componentVector.second)
				{
					deleterFn(component);
				}
				componentVector.second.clear();
			}
			mComponents.cleanEmptyVectors();

			mEntityExistanceFlags.clear();
			mEntityVersions.clear();
			mFreeEntityIds.clear();

			mScheduledComponentAdditions.clear();
			mScheduledComponentRemovements.clear();

			mIndexes.clear();
		}

		/**
		 * @brief Get const component data
		 *
		 * Can be useful for serialization
		 */
		const ComponentMap& getComponentsData() const { return mComponents; }

	public:
		MulticastDelegate<> onEntityAdded;
		MulticastDelegate<> onEntityRemoved;

	private:
		struct ComponentToAdd
		{
			Entity entity;
			void* component;
			ComponentTypeId typeId;

			ComponentToAdd(const Entity entity, void* component, ComponentTypeId typeId)
				: entity(entity)
				, component(component)
				, typeId(typeId)
			{}
		};

		struct ComponentToRemove
		{
			Entity entity;
			ComponentTypeId typeId;

			ComponentToRemove(const Entity entity, ComponentTypeId typeId)
				: entity(entity)
				, typeId(typeId)
			{}
		};

	private:
		template<int I = 0>
		static std::tuple<> getEmptyComponents()
		{
			return {};
		}

		template<typename FirstComponent, typename... Components>
		std::tuple<FirstComponent*, Components*...> getEmptyComponents()
		{
			return std::tuple_cat(std::tuple<FirstComponent*>(nullptr), getEmptyComponents<Components...>());
		}

		template<unsigned Index, typename Datas>
		static std::tuple<> getEntityComponentSetInner(size_t /*entityIdx*/, Datas& /*componentVectors*/)
		{
			return {};
		}

		template<unsigned Index, typename Datas, typename FirstComponent, typename... Components>
		std::tuple<FirstComponent*, Components*...> getEntityComponentSetInner(size_t entityIdx, Datas& componentVectors)
		{
			if (std::get<Index>(componentVectors).size() <= entityIdx)
			{
				return getEmptyComponents<FirstComponent, Components...>();
			}

			auto& component = std::get<Index>(componentVectors)[entityIdx];
			if (component == nullptr)
			{
				return getEmptyComponents<FirstComponent, Components...>();
			}

			return std::tuple_cat(std::make_tuple(static_cast<FirstComponent*>(component)), getEntityComponentSetInner<Index + 1, Datas, Components...>(entityIdx, componentVectors));
		}

		template<typename FirstComponent, typename... Components, typename... Data>
		std::tuple<FirstComponent*, Components*...> getEntityComponentSet(const size_t entityIdx, std::tuple<std::vector<Data*>&...>& componentVectors)
		{
			using Datas = std::tuple<std::vector<Data*>&...>;
			return getEntityComponentSetInner<0, Datas, FirstComponent, Components...>(entityIdx, componentVectors);
		}

		template<typename... ComponentVector>
		static size_t GetShortestVector(const std::tuple<ComponentVector&...>& vectorTuple)
		{
			size_t minimalSize = std::numeric_limits<size_t>::max();
			std::apply(
				[&minimalSize](const ComponentVector&... componentVector)
				{
					((minimalSize = std::min(minimalSize, componentVector.size())), ...);
				},
				vectorTuple
			);
			return minimalSize;
		}

		void addComponentToEntity(size_t entityIdx, void* component, ComponentTypeId typeId)
		{
			auto& componentsVector = mComponents.getOrCreateComponentVectorById(typeId);
			if (componentsVector.size() <= entityIdx)
			{
				if (componentsVector.capacity() <= entityIdx)
				{
					componentsVector.reserve((entityIdx + 1) * 2);
				}
				componentsVector.resize(entityIdx + 1);
			}

			if (componentsVector[entityIdx] == nullptr)
			{
				componentsVector[entityIdx] = component;
			}
			else
			{
				RACCOON_ECS_ERROR(std::string("Trying to add a component when the entity already has one of the same type. This will result in UB, entity: ") + std::to_string(entityIdx) + ", component: " + toString(typeId));
			}
			mIndexes.onComponentAdded(typeId, entityIdx, mComponents);
		}

#ifdef RACCOON_ECS_COPYABLE_COMPONENTS
		void copyEntitiesFrom(const EntityManager& originalInstance)
		{
			mEntityExistanceFlags = originalInstance.mEntityExistanceFlags;
			mEntityVersions = originalInstance.mEntityVersions;
			mFreeEntityIds = originalInstance.mFreeEntityIds;

			for (auto& componentVectorPair : originalInstance.mComponents)
			{
				std::vector<void*>& newComponents = mComponents.getOrCreateComponentVectorById(componentVectorPair.first);
				const std::vector<void*>& originalComponents = componentVectorPair.second;
				const size_t componentsCount = originalComponents.size();
				newComponents.resize(componentsCount);
				const auto cloneFn = mComponentFactory.get().getCloneFn(componentVectorPair.first);
				for (size_t i = 0; i < componentsCount; ++i)
				{
					newComponents[i] = cloneFn(originalComponents[i]);
				}
			}
		}
#endif // RACCOON_ECS_COPYABLE_COMPONENTS

	private:
		ComponentMap mComponents;

		ComponentIndexes<ComponentTypeId> mIndexes;

		std::vector<bool> mEntityExistanceFlags;
		std::vector<Entity::Version> mEntityVersions;
		std::vector<size_t> mFreeEntityIds;

		std::vector<ComponentToAdd> mScheduledComponentAdditions;
		std::vector<ComponentToRemove> mScheduledComponentRemovements;

		std::reference_wrapper<const ComponentFactory> mComponentFactory;
	};

} // namespace RaccoonEcs
