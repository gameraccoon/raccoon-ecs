#pragma once

#include <algorithm>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <memory>

#include "component_factory.h"
#include "component_indexes.h"
#include "component_map.h"
#include "delegates.h"
#include "entity.h"
#include "entity_generator.h"
#include "error_handling.h"
#include "typed_component.h"

namespace RaccoonEcs
{
	namespace TemplateTrick
	{
		// a trick to get index of a parameter pack argument by its type
		template<typename Type, typename... Types>
		struct Idx;

		template<typename Type, typename... Types>
		struct Idx<Type, Type, Types...> : std::integral_constant<std::size_t, 0>
		{
		};

		template<typename Type, typename FirstType, typename... Types>
		struct Idx<Type, FirstType, Types...> : std::integral_constant<std::size_t, 1 + Idx<Type, Types...>::value>
		{
		};

		template<typename Type, typename... Types>
		static constexpr std::size_t PackIdx = Idx<Type, Types...>::value;
	}

	template <typename ComponentTypeId, typename ComponentFactory = ComponentFactoryImpl<ComponentTypeId>>
	class EntityManagerImpl
	{
	public:
		using EntityManager = EntityManagerImpl<ComponentTypeId>;
		using TypedComponent = TypedComponentImpl<ComponentTypeId>;
		using ConstTypedComponent = ConstTypedComponentImpl<ComponentTypeId>;
		using ComponentMap = ComponentMapImpl<ComponentTypeId>;

		using EntityIndex = size_t;

	public:
		/**
		 * @param componentFactory  Should be a reference to a ComponentFactory object that has longer lifetime than this EntityManager
		 * @param entityGenerator  Should be a reference to an EntityGenerator object that has longer lifetime than this EntityManager
		 */
		EntityManagerImpl(const ComponentFactory& componentFactory, EntityGenerator& entityGenerator)
			: mComponentFactory(componentFactory)
			, mEntityGenerator(entityGenerator)
		{}

		~EntityManagerImpl()
		{
			clear();
		}

		EntityManagerImpl(const EntityManagerImpl&) = delete;
		EntityManagerImpl& operator=(const EntityManagerImpl&) = delete;
		EntityManagerImpl(EntityManagerImpl&&) = delete;
		EntityManagerImpl& operator=(EntityManagerImpl&&) = delete;

		/**
		 * @brief Generates a new unique entity and adds it to this manager
		 * @return The newly created entity
		 */
		Entity addEntity()
		{
			const Entity::EntityId id = mEntityGenerator.generateNewEntityId();
			EntityIndex newEntityIndex = mEntities.size();
			mEntities.emplace_back(id);
			mEntityIndexMap.emplace(id, newEntityIndex);
			onEntityAdded.broadcast();
			return Entity(id);
		}

		/**
		 * @brief Removes the given entity from the manager, and unregisters its ID, so it can be reused
		 * again in any manager in future
		 * @param entityToRemove  The entity that should be removed, should be bound to this manager
		 */
		void removeEntity(Entity entityToRemove)
		{
			const auto entityToRemoveIdxItr = mEntityIndexMap.find(entityToRemove.getId());
			if (entityToRemoveIdxItr == mEntityIndexMap.end())
			{
				RACCOON_ECS_ERROR(std::string("Trying to remove an entity that doesn't exist: ") + std::to_string(entityToRemove.getId()));
				return;
			}

			const EntityIndex oldEntityIdx = entityToRemoveIdxItr->second;

			// we need to swap the removed entity with the latest
			const EntityIndex entityIndexToRemove = mEntities.size() - 1;

			mEntityIndexMap.erase(entityToRemove.getId());

			if (oldEntityIdx != entityIndexToRemove)
			{
				// relink maps
				const Entity swappedEntity = mEntities[entityIndexToRemove];
				std::swap(mEntities[oldEntityIdx], mEntities[entityIndexToRemove]);
				mEntityIndexMap[swappedEntity.getId()] = oldEntityIdx;
			}
			mEntities.pop_back();

			for (auto& componentVector : mComponents)
			{
				// if the vector contains deleted entity
				if (oldEntityIdx < componentVector.second.size())
				{
					// if the entity contains the component
					if (void*& componentPtrRef = componentVector.second[oldEntityIdx])
					{
						// remove the element
						auto deleterFn = mComponentFactory.getDeletionFn(componentVector.first);
						deleterFn(componentPtrRef);
						componentPtrRef = nullptr;
					}

					// if the vector contains the last entity
					if (entityIndexToRemove < componentVector.second.size() && oldEntityIdx != entityIndexToRemove)
					{
						// move it to the freed space
						std::swap(componentVector.second[oldEntityIdx], componentVector.second[entityIndexToRemove]);
					}
				}
			}

			mIndexes.onEntityRemoved(oldEntityIdx, entityIndexToRemove);

			onEntityRemoved.broadcast();
		}

		/**
		 * @brief Checks if the entity is exists in this manager
		 */
		bool hasEntity(Entity entity)
		{
			return mEntityIndexMap.find(entity.getId()) != mEntityIndexMap.end();
		}

		/**
		 * @brief Returns true if this manager has any entities
		 * If it doesn't have any entities it's pretty much doesn't have any valuable data
		 * since EntityManager can't contain components not bound to entities
		 */
		[[nodiscard]] bool hasAnyEntities() const
		{
			return !mEntities.empty();
		}

		/**
		 * @brief Returns list of entities that are stored in this manager
		 */
		[[nodiscard]] const std::vector<Entity>& getEntities() const { return mEntities; }

		/**
		 * @brief Collects components that belongs to the given entity and returns them together with their types
		 * @param entity  The entity whose components are collected
		 * @param outComponents  The list of components belonging to the entity with their types
		 */
		void getAllEntityComponents(Entity entity, std::vector<TypedComponent>& outComponents)
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr != mEntityIndexMap.end())
			{
				const EntityIndex index = entityIdxItr->second;
				for (auto& componentVector : mComponents)
				{
					if (componentVector.second.size() > index && componentVector.second[index] != nullptr)
					{
						outComponents.emplace_back(componentVector.first, componentVector.second[index]);
					}
				}
			}
		}

		/**
		 * @brief Collects components that belongs to the given entity and returns them together with their types
		 * @param entity  The entity whose components are collected
		 * @param outComponents  The list of constant components belonging to the entity with their types
		 */
		void getAllEntityComponents(Entity entity, std::vector<ConstTypedComponent>& outComponents) const
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr != mEntityIndexMap.end())
			{
				const EntityIndex index = entityIdxItr->second;
				for (auto& componentVector : mComponents)
				{
					if (componentVector.second.size() > index && componentVector.second[index] != nullptr)
					{
						outComponents.emplace_back(componentVector.first, componentVector.second[index]);
					}
				}
			}
		}

		/**
		 * @brief Checks if the given entity has the given component
		 */
		[[nodiscard]] bool doesEntityHaveComponent(Entity entity, ComponentTypeId typeId) const
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr != mEntityIndexMap.end())
			{
				const std::vector<void*>& componentVector = mComponents.getComponentVectorById(typeId);
				const EntityIndex index = entityIdxItr->second;
				return (componentVector.size() > index && componentVector[index] != nullptr);
			}

			using std::to_string;
			RACCOON_ECS_ERROR(std::string("Trying to check component ") + to_string(typeId)
				+ " of non-existing entity: " + std::to_string(entity.getId()));
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
		 * @return A pointer to the newly created default-initialized component
		 *
		 * Beware that the entity should not have the component of the given type prior calling the function
		 * otherwise the call can result with memory leak or UB
		 */
		void* addComponentByType(Entity entity, ComponentTypeId typeId)
		{
			const auto createFn = mComponentFactory.getCreationFn(typeId);
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
		void addComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
				using std::to_string;
				RACCOON_ECS_ERROR(std::string("Trying to add component ") + to_string(typeId)
					+ " to a non-exsistent entity" + std::to_string(entity.getId()));
				// memory leak here
				return;
			}

			addComponentToEntity(entityIdxItr->second, component, typeId);
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

		void removeComponent(Entity entity, ComponentTypeId typeId)
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
				using std::to_string;
				RACCOON_ECS_ERROR(std::string("Trying to remove component ") + to_string(typeId)
						+ " from a non-existent entity" + std::to_string(entity.getId()));
				return;
			}

			auto& componentsVector = mComponents.getComponentVectorById(typeId);

			EntityIndex entityIdx = entityIdxItr->second;

			if (entityIdx < componentsVector.size())
			{
				auto deleterFn = mComponentFactory.getDeletionFn(typeId);
				deleterFn(componentsVector[entityIdx]);
				componentsVector[entityIdx] = nullptr;
			}

			mIndexes.onComponentRemoved(typeId, entityIdx);
		}

		/**
		 * @brief Creates a component of the given type and chedules its addition to the given entity
		 * @param entity  The entity that will own the component
		 * @return  A pointer to the newly created default-initialized component
		 *
		 * You can use the component right away, but it won't be querried before `executeScheduledActions` called.
		 *
		 * Beware that the entity should not have the component of the given type prior `executeScheduledActions`
		 * called otherwise the call can result with memory leak or UB
		 */
		template<typename ComponentType>
		ComponentType* scheduleAddComponent(Entity entity)
		{
			const ComponentTypeId componentTypeId = ComponentType::GetTypeId();
			const auto createFn = mComponentFactory.getCreationFn(componentTypeId);
			ComponentType* component = static_cast<ComponentType*>(createFn());
			scheduleAddComponent(entity, component, componentTypeId);
			return component;
		}

		/**
		 * @brief Creates a component of the given type and chedules its addition to the given entity
		 * @param entity  The entity that will own the component
		 * @return  A pointer to the newly created default-initialized component
		 *
		 * You can use the component right away, but it won't be querried before `executeScheduledActions` called.
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
		std::tuple<Components*...> getEntityComponents(Entity entity)
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
				return getEmptyComponents<Components...>();
			}
			EntityIndex entityIdx = entityIdxItr->second;

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
			const std::vector<size_t>& componentIndexes = mIndexes.template getIndex<Components...>(mComponents);

			if (!componentIndexes.empty())
			{
				using namespace TemplateTrick;

				if (inOutComponents.size() + componentIndexes.size() > inOutComponents.capacity())
				{
					inOutComponents.reserve(std::max(inOutComponents.size() + componentIndexes.size(), inOutComponents.size() * 2));
				}

				auto vectors = mComponents.template getComponentVectors<Components...>();

				for (size_t index : componentIndexes)
				{
					inOutComponents.push_back(std::tuple_cat(
						std::make_tuple(data...),
						std::make_tuple(
							static_cast<Components*>(std::get<PackIdx<Components, Components...>>(vectors)[index])...)
						)
					);
				}
			}
		}

		/**
		 * @brief Collects enrities that has all the given components together with components,
		 * appends the result to the in-out argument
		 * @param inOutComponents  The vector of tuples of component pointers, matched data will
		 * be appended to the vector
		 * @param data  Additional data, will be added to each matched record. Can be useful to
		 * identify a specific manager
		 */
		template<typename... Components, typename... AdditionalData>
		void getComponentsWithEntities(std::vector<std::tuple<AdditionalData..., Entity, Components* ...>>& inOutComponents, AdditionalData... data)
		{
			using namespace TemplateTrick;

			const std::vector<size_t>& componentIndexes = mIndexes.template getIndex<Components...>(mComponents);

			if (!componentIndexes.empty())
			{
				if (inOutComponents.size() + componentIndexes.size() > inOutComponents.capacity())
				{
					inOutComponents.reserve(std::max(inOutComponents.size() + componentIndexes.size(), inOutComponents.size() * 2));
				}

				auto vectors = mComponents.template getComponentVectors<Components...>();

				for (size_t index : componentIndexes)
				{
					inOutComponents.push_back(std::tuple_cat(
						std::make_tuple(data...),
						std::make_tuple(mEntities[index]),
						std::make_tuple(
							static_cast<Components*>(std::get<PackIdx<Components, Components...>>(vectors)[index])...)
						)
					);
				}
			}
		}

		/**
		 * @brief Applies the given callable to all the component sets from matched entities
		 * @param processor  The callable that will be applied to the mathced component sets
		 * @param data  Additional data, will be added to each matched record. Can be useful
		 * to identify a specific manager
		 */
		template<typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(FunctionType processor, AdditionalData... data)
		{
			using TemplateTrick::PackIdx;

			const std::vector<size_t>& componentIndexes = mIndexes.template getIndex<Components...>(mComponents);

			if (!componentIndexes.empty())
			{
				auto vectors = mComponents.template getComponentVectors<Components...>();

				for (size_t index : componentIndexes)
				{
					std::apply(processor, std::tuple_cat(
						std::make_tuple(data...),
						std::make_tuple(
							static_cast<Components*>(std::get<PackIdx<Components, Components...>>(vectors)[index])...)
						)
					);
				}
			}
		}

		/**
		 * @brief Applies the given callable to all the entities together with component sets
		 * that have all the given components
		 * @param processor  The callable that will be applied to the mathced component sets
		 * @param data  Additional data, will be added to each matched record. Can be useful
		 * to identify a specific manager
		 */
		template<typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(FunctionType processor, AdditionalData... data)
		{
			using TemplateTrick::PackIdx;

			const std::vector<size_t>& componentIndexes = mIndexes.template getIndex<Components...>(mComponents);

			if (!componentIndexes.empty())
			{
				auto vectors = mComponents.template getComponentVectors<Components...>();

				for (size_t index : componentIndexes)
				{
					std::apply(processor, std::tuple_cat(
						std::make_tuple(data...),
						std::make_tuple(mEntities[index]),
						std::make_tuple(
							static_cast<Components*>(std::get<PackIdx<Components, Components...>>(vectors)[index])...)
						)
					);
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

			EntityIndex endIdx = std::numeric_limits<EntityIndex>::max();
			std::vector<const std::vector<void*>*> componentVectors;
			componentVectors.reserve(componentIndexes.size());
			for (ComponentTypeId typeId : componentIndexes)
			{
				auto& componentVector = mComponents.getComponentVectorById(typeId);

				endIdx = std::min(endIdx, componentVector.size());

				componentVectors.push_back(&componentVector);
			}

			for (EntityIndex idx = 0; idx < endIdx; ++idx)
			{
				const bool hasAllComponents = std::all_of(
					componentVectors.cbegin(),
					componentVectors.cend(),
					[idx](const std::vector<void*>* componentVector){ return (*componentVector)[idx] != nullptr; }
				);

				if (hasAllComponents)
				{
					inOutEntities.emplace_back(mEntities[idx]);
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
			return mIndexes.template getIndex<Components...>(mComponents).size();
		}

		/**
		 * @brief Transfers the given entity together with its components to another manager
		 * @param otherManager  The manager to which the entity will be transfer to
		 * @param entity  The enitity that will be transfered
		 *
		 * The components are guaranteed not to be moved in the memory.
		 */
		void transferEntityTo(EntityManager& otherManager, Entity entity)
		{
			if (this == &otherManager)
			{
				RACCOON_ECS_ERROR("Transferring entity to the same manager. This should never happen");
				return;
			}

			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
				RACCOON_ECS_ERROR(std::string("Trying transfer non-existent entity: ") + std::to_string(entity.getId()));
				return;
			}

			[[maybe_unused]] const auto insertionResult = otherManager.mEntityIndexMap.try_emplace(entity.getId(), otherManager.mEntities.size());
			RACCOON_ECS_ASSERT(insertionResult.second, "EntityId is not unique, two entities collided during transfer. Make sure all entity managers use one shared EntityGenerator");
			otherManager.mEntities.push_back(entity);

			const EntityIndex entityToRemoveIdx = mEntities.size() - 1;

			const EntityIndex oldEntityIdx = entityIdxItr->second;

			for (auto& componentVector : mComponents)
			{
				if (oldEntityIdx < componentVector.second.size())
				{
					// add the element to the new manager
					if (componentVector.second[oldEntityIdx] != nullptr)
					{
						otherManager.addComponent(
							entity,
							componentVector.second[oldEntityIdx],
							componentVector.first
						);
					}

					// remove the element from the old manager
					componentVector.second[oldEntityIdx] = nullptr;

					// if the vector contains the last entity
					if (entityToRemoveIdx < componentVector.second.size() && oldEntityIdx != entityToRemoveIdx)
					{
						// move it to the freed space
						std::swap(componentVector.second[oldEntityIdx], componentVector.second[entityToRemoveIdx]);
					}
				}
			}

			mEntityIndexMap.erase(entity.getId());

			if (oldEntityIdx != entityToRemoveIdx)
			{
				// relink maps
				const Entity entityToSwap = mEntities[entityToRemoveIdx];
				mEntityIndexMap[entityToSwap.getId()] = oldEntityIdx;
				std::swap(mEntities[entityToRemoveIdx], mEntities[oldEntityIdx]);
			}
			mEntities.pop_back();
			mIndexes.onEntityRemoved(oldEntityIdx, entityToRemoveIdx);
		}

		/**
		 * @brief Inits the index if it wasn't created
		 *
		 * This function is not necessary to call, indexes will be created automatically
		 * when some request for components made, however you can do it in advance
		 * (e.g. to reduce first frame time)
		 */
		template <typename... Components>
		void initIndex()
		{
			mIndexes.template getIndex<Components...>(mComponents);
		}

		/**
		 * @brief Generates yet unused Entity. Should be used together with addExistingEntityUnsafe
		 * @return Not yet used Entity
		 *
		 * Can produce collisions if between call to this function and call to tryInsertEntity
		 * any work with entity happened that could produce new Entity registration
		 */
		[[nodiscard]] Entity generateNewEntityUnsafe()
		{
			return Entity(mEntityGenerator.generateNewEntityId());
		}

		/**
		 * @brief Inserts an entity that was removed from an entity manager or created with generateNewEntityUnsafe
		 * @param entity  The entity that need to be inserted
		 *
		 * Use cases: re-adding deleted entity by "undo" editor command, loading the game from save.
		 */
		void addExistingEntityUnsafe(Entity entity)
		{
			mEntityGenerator.registerEntityId(entity.getId());

			const EntityIndex newEntityId = mEntities.size();
			mEntities.push_back(entity);
			mEntityIndexMap.emplace(entity.getId(), newEntityId);

			onEntityAdded.broadcast();
		}

		std::unique_ptr<EntityManager> clone() const
		{
			std::unique_ptr<EntityManager> result = std::make_unique<EntityManager>(mComponentFactory, mEntityGenerator);
			result->mEntities = mEntities;
			result->mEntityIndexMap = mEntityIndexMap;

			for (auto& componentVectorPair : mComponents)
			{
				std::vector<void*>& newComponents = result->mComponents.getOrCreateComponentVectorById(componentVectorPair.first);
				const std::vector<void*>& originalComponents = componentVectorPair.second;
				const size_t componentsCount = originalComponents.size();
				newComponents.resize(componentsCount);
				const auto cloneFn = mComponentFactory.getCloneFn(componentVectorPair.first);
				for (size_t i = 0; i < componentsCount; ++i)
				{
					newComponents[i] = cloneFn(originalComponents[i]);
				}
			}

			return result;
		}

		/**
		 * @brief Shrinks the vectors of components to elliminate empty elements at the end, and
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
					size_t lastFilledIdx = std::distance(lastFilledRIt, componentVector.rend());
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
				auto deleterFn = mComponentFactory.getDeletionFn(componentVector.first);

				for (auto component : componentVector.second)
				{
					deleterFn(component);
				}
				componentVector.second.clear();
			}
			mComponents.cleanEmptyVectors();

			mEntities.clear();
			mEntityIndexMap.clear();

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

		/**
		 * @brief Applies a function that suppose to sort the data before serializing
		 * @param func  A callable that will accept references to inner data (can change in future)
		 */
		template<typename Func>
		void applySortingFunction(Func&& func) {
			func(mComponents, mEntities, mEntityIndexMap);
			mIndexes.rebuild(mComponents);
		}

	public:
		MulticastDelegate<> onEntityAdded;
		MulticastDelegate<> onEntityRemoved;

	private:
		struct ComponentToAdd
		{
			Entity entity;
			void* component;
			ComponentTypeId typeId;

			ComponentToAdd(Entity entity, void* component, ComponentTypeId typeId)
				: entity(entity)
				, component(component)
				, typeId(typeId)
			{}
		};

		struct ComponentToRemove
		{
			Entity entity;
			ComponentTypeId typeId;

			ComponentToRemove(Entity entity, ComponentTypeId typeId)
				: entity(entity)
				, typeId(typeId)
			{}
		};

	private:
		template<int I = 0>
		std::tuple<> getEmptyComponents()
		{
			return std::tuple<>();
		}

		template<typename FirstComponent, typename... Components>
		std::tuple<FirstComponent*, Components*...> getEmptyComponents()
		{
			return std::tuple_cat(std::tuple<FirstComponent*>(nullptr), getEmptyComponents<Components...>());
		}

		template<unsigned Index, typename Datas>
		std::tuple<> getEntityComponentSetInner(EntityIndex /*entityIdx*/, Datas& /*componentVectors*/)
		{
			return std::tuple<>();
		}

		template<unsigned Index, typename Datas, typename FirstComponent, typename... Components>
		std::tuple<FirstComponent*, Components*...> getEntityComponentSetInner(EntityIndex entityIdx, Datas& componentVectors)
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
		std::tuple<FirstComponent*, Components*...> getEntityComponentSet(EntityIndex entityIdx, std::tuple<std::vector<Data*>&...>& componentVectors)
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

		void addComponentToEntity(EntityIndex entityIdx, void* component, ComponentTypeId typeId)
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
				RACCOON_ECS_ERROR("Trying to add a component when the entity already has one of the same type. This will result in UB");
			}
			mIndexes.onComponentAdded(typeId, entityIdx, mComponents);
		}

		template<typename FirstComponent, typename... Components>
		void gatherNonIndexedComponents(std::vector<std::tuple<FirstComponent*, Components*...>>& outComponents)
		{
			auto componentVectors = mComponents.template getComponentVectors<FirstComponent, Components...>();
			auto& firstComponentVector = std::get<0>(componentVectors);
			size_t shortestVectorSize = GetShortestVector(componentVectors);

			constexpr unsigned componentsSize = sizeof...(Components);

			for (EntityIndex entityIndex = 0, iSize = shortestVectorSize; entityIndex < iSize; ++entityIndex)
			{
				auto& firstComponent = firstComponentVector[entityIndex];
				if (firstComponent == nullptr)
				{
					continue;
				}

				auto components = getEntityComponentSet<FirstComponent, Components...>(entityIndex, componentVectors);

				if (std::get<componentsSize>(components) != nullptr)
				{
					outComponents.push_back(std::move(components));
				}
			}
		}

		template<typename FirstComponent, typename... Components>
		void gatherNonIndexedComponentsWithEntities(std::vector<std::tuple<Entity, FirstComponent*, Components*...>>& outComponents)
		{
			auto componentVectors = mComponents.template getComponentVectors<FirstComponent, Components...>();
			auto& firstComponentVector = std::get<0>(componentVectors);
			size_t shortestVectorSize = GetShortestVector(componentVectors);

			constexpr unsigned componentsSize = sizeof...(Components);

			for (EntityIndex entityIndex = 0, iSize = shortestVectorSize; entityIndex < iSize; ++entityIndex)
			{
				auto& firstComponent = firstComponentVector[entityIndex];
				if (firstComponent == nullptr)
				{
					continue;
				}

				auto components = getEntityComponentSet<FirstComponent, Components...>(entityIndex, componentVectors);

				if (std::get<componentsSize>(components) != nullptr)
				{
					outComponents.push_back(
							std::tuple_cat(std::make_tuple(mEntities[entityIndex]), std::move(components)));
				}
			}
		}

	private:
		ComponentMap mComponents;
		std::vector<Entity> mEntities;
		std::unordered_map<Entity::EntityId, EntityIndex> mEntityIndexMap;

		ComponentIndexes<ComponentTypeId> mIndexes;

		std::vector<ComponentToAdd> mScheduledComponentAdditions;
		std::vector<ComponentToRemove> mScheduledComponentRemovements;

		const ComponentFactory& mComponentFactory;
		EntityGenerator& mEntityGenerator;
	};

} // namespace RaccoonEcs
