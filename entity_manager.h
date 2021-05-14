#pragma once

#include <algorithm>
#include <ranges>
#include <tuple>
#include <unordered_map>

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
	template <typename ComponentTypeId>
	class EntityManagerImpl
	{
	public:
		using EntityManager = EntityManagerImpl<ComponentTypeId>;
		using TypedComponent = TypedComponentImpl<ComponentTypeId>;
		using ConstTypedComponent = ConstTypedComponentImpl<ComponentTypeId>;
		using ComponentMap = ComponentMapImpl<ComponentTypeId>;
		using ComponentFactory = ComponentFactoryImpl<ComponentTypeId>;

		using EntityIndex = size_t;

	public:
		/**
		 * @param componentFactory should be a reference to a ComponentFactory object that has longer lifetime than this EntityManager
		 * @param entityGenerator should be a reference to an EntityGenerator object that has longer lifetime than this EntityManager
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

		Entity addEntity()
		{
			const Entity::EntityId id = mEntityGenerator.generateAndRegisterEntityId();
			EntityIndex newEntityIndex = mEntities.size();
			mEntities.emplace_back(id);
			mEntityIndexMap.emplace(id, newEntityIndex);
			onEntityAdded.broadcast();
			return Entity(id);
		}

		void removeEntity(Entity entityToRemove)
		{
			const auto entityToRemoveIdxItr = mEntityIndexMap.find(entityToRemove.getId());
			if (entityToRemoveIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying to remove an entity that doesn't exist: ") + std::to_string(entityToRemove.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
				return;
			}
			mEntityGenerator.unregisterEntityId(entityToRemove.getId());

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
						mIndexes.invalidateForComponent(componentVector.first);
					}

					// if the vector contains the last entity
					if (entityIndexToRemove < componentVector.second.size() && oldEntityIdx != entityIndexToRemove)
					{
						// move it to the freed space
						std::swap(componentVector.second[oldEntityIdx], componentVector.second[entityIndexToRemove]);
					}
				}
			}

			onEntityRemoved.broadcast();
		}

		bool hasEntity(Entity entity)
		{
			return mEntityIndexMap.find(entity.getId()) != mEntityIndexMap.end();
		}

		[[nodiscard]] bool hasAnyEntities() const
		{
			return !mEntities.empty();
		}

		[[nodiscard]] const std::vector<Entity>& getEntities() const { return mEntities; }

		/**
		 * @brief Generates yet unused Entity. Should be used together with tryInsertEntity
		 * @return Not yet used Entity
		 *
		 * Can produce collisions if between call to this function and call to tryInsertEntity
		 * any work with entity happened that could produce new Entity registration
		 */
		[[nodiscard]] Entity getNonExistentEntity()
		{
			return Entity(mEntityGenerator.generateEntityId());
		}

		/**
		 * @brief Try to insert an entity to then reconstruct its previous state (e.g. during deserialization)
		 * @param entity  an entity that need to be inserted
		 * @return true if entity was added, false if it collided with some existent entity
		 *
		 * This function should succeed if no other entities were created between removing this
		 * entity and calling this function with it, or all the new entities were removed before calling it.
		 * The function checks not only for collisions inside this EntityManager but with all that share the
		 * same EntityGenerator instance.
		 *
		 * Use cases: re-adding deleted entity by "undo" editor command, loading the game from save.
		 */
		bool tryInsertEntity(Entity entity)
		{
			bool successfullyRegistered = mEntityGenerator.registerEntityId(entity.getId());
			if (successfullyRegistered)
			{
				const EntityIndex newEntityId = mEntities.size();
				mEntities.push_back(entity);
				mEntityIndexMap.emplace(entity.getId(), newEntityId);

				onEntityAdded.broadcast();
			}
			return successfullyRegistered;
		}

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

		[[nodiscard]] bool doesEntityHaveComponent(Entity entity, ComponentTypeId typeId) const
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr != mEntityIndexMap.end())
			{
				const std::vector<void*>& componentVector = mComponents.getComponentVectorById(typeId);
				const EntityIndex index = entityIdxItr->second;
				return (componentVector.size() > index && componentVector[index] != nullptr);
			}

#ifdef ECS_DEBUG_CHECKS_ENABLED
			gErrorHandler(std::string("Trying to check component ") + std::to_string(typeId)
				+ " of non-existing entity: " + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
			return false;
		}

		template<typename ComponentType>
		[[nodiscard]] bool doesEntityHaveComponent(Entity entity)
		{
			return doesEntityHaveComponent(entity, ComponentType::GetTypeId());
		}

		template<typename ComponentType>
		ComponentType* addComponent(Entity entity)
		{
			return static_cast<ComponentType*>(addComponentByType(entity, ComponentType::GetTypeId()));
		}

		void* addComponentByType(Entity entity, ComponentTypeId typeId)
		{
			const auto createFn = mComponentFactory.getCreationFn(typeId);
			void* component = createFn();
			addComponent(entity, component, typeId);
			return component;
		}

		void addComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying to add component ") + std::to_string(typeId)
					+ " to a non-exsistent entity" + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
				// memory leak here
				return;
			}

			addComponentToEntity(entityIdxItr->second, component, typeId);
		}

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
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying to remove component ") + std::to_string(typeId)
					+ " from a non-existent entity" + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
				return;
			}

			auto& componentsVector = mComponents.getComponentVectorById(typeId);

			if (componentsVector.size() > entityIdxItr->second)
			{
				auto deleterFn = mComponentFactory.getDeletionFn(typeId);
				deleterFn(componentsVector[entityIdxItr->second]);
				componentsVector[entityIdxItr->second] = nullptr;
			}

			mIndexes.invalidateForComponent(typeId);
		}

		template<typename ComponentType>
		ComponentType* scheduleAddComponent(Entity entity)
		{
			const ComponentTypeId componentTypeId = ComponentType::GetTypeId();
			const auto createFn = mComponentFactory.getCreationFn(componentTypeId);
			ComponentType* component = static_cast<ComponentType*>(createFn());
			scheduleAddComponent(entity, component, componentTypeId);
			return component;
		}

		void scheduleAddComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			mScheduledComponentAdditions.emplace_back(entity, component, typeId);
		}

		template<typename ComponentType>
		void scheduleRemoveComponent(Entity entity)
		{
			scheduleRemoveComponent(entity, ComponentType::GetTypeId());
		}

		void scheduleRemoveComponent(Entity entity, ComponentTypeId typeId)
		{
			mScheduledComponentRemovements.emplace_back(entity, typeId);
		}

		void executeScheduledActions()
		{
			for (const auto& addition : mScheduledComponentAdditions)
			{
				addComponent(addition.entity, addition.component, addition.typeId);
				mIndexes.invalidateForComponent(addition.typeId);
			}
			mScheduledComponentAdditions.clear();

			for (const auto& removement : mScheduledComponentRemovements)
			{
				removeComponent(removement.entity, removement.typeId);
				mIndexes.invalidateForComponent(removement.typeId);
			}
			mScheduledComponentRemovements.clear();
		}

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

		template<typename FirstComponent, typename... Components, typename... AdditionalData>
		void getComponents(std::vector<std::tuple<AdditionalData..., FirstComponent*, Components*...>>& inOutComponents, AdditionalData... data)
		{
			if constexpr (sizeof...(AdditionalData) == 0)
			{
				appendComponentsIndexed<FirstComponent, Components...>(inOutComponents);
			}
			else
			{
				std::vector<std::tuple<FirstComponent*, Components*...>> components;
				appendComponentsIndexed<FirstComponent, Components...>(components);
				inOutComponents.reserve(inOutComponents.size() + components.size());
				for (std::tuple<FirstComponent*, Components*...>& componentSet : components)
				{
					inOutComponents.push_back(std::tuple_cat(std::make_tuple(data...), std::move(componentSet)));
				}
			}
		}

		template<typename FirstComponent, typename... Components, typename... AdditionalData>
		void getComponentsWithEntities(std::vector<std::tuple<AdditionalData..., Entity, FirstComponent*, Components*...>>& inOutComponents, AdditionalData... data)
		{
			if constexpr (sizeof...(AdditionalData) == 0)
			{
				appendComponentsWithEntityIndexed<FirstComponent, Components...>(inOutComponents);
			}
			else
			{
				std::vector<std::tuple<Entity, FirstComponent*, Components*...>> components;
				appendComponentsWithEntityIndexed<FirstComponent, Components...>(components);
				inOutComponents.reserve(inOutComponents.size() + components.size());
				for (std::tuple<Entity, FirstComponent*, Components*...>& componentSet : components)
				{
					inOutComponents.push_back(std::tuple_cat(std::make_tuple(data...), std::move(componentSet)));
				}
			}
		}

		template<typename FirstComponent, typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(FunctionType processor, AdditionalData... data)
		{
			std::vector<std::tuple<FirstComponent*, Components*...>> components;
			appendComponentsIndexed<FirstComponent, Components...>(components);
			for (std::tuple<FirstComponent*, Components*...>& componentSet : components)
			{
				std::apply(processor, std::tuple_cat(std::make_tuple(data...), std::move(componentSet)));
			}
		}

		template<typename FirstComponent, typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(FunctionType processor, AdditionalData... data)
		{
			std::vector<std::tuple<Entity, FirstComponent*, Components*...>> components;
			appendComponentsWithEntityIndexed<FirstComponent, Components...>(components);
			for (std::tuple<Entity, FirstComponent*, Components*...>& componentSet : components)
			{
				std::apply(processor, std::tuple_cat(std::make_tuple(data...), std::move(componentSet)));
			}
		}

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

		void transferEntityTo(EntityManager& otherManager, Entity entity)
		{
			if (this == &otherManager)
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler("Transferring entity to the same manager. This should never happen");
#endif // ECS_DEBUG_CHECKS_ENABLED
				return;
			}

			const auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying transfer non-existent entity: ") + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
				return;
			}

			[[maybe_unused]] const auto insertionResult = otherManager.mEntityIndexMap.try_emplace(entity.getId(), otherManager.mEntities.size());
#ifdef ECS_DEBUG_CHECKS_ENABLED
			if (!insertionResult.second)
			{
				gErrorHandler("EntityId is not unique, two entities collided during transfer. Make sure all entity managers use one shared EntityGenerator");
			}
#endif // ECS_DEBUG_CHECKS_ENABLED
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
					mIndexes.invalidateForComponent(componentVector.first);

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
		}

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

			mIndexes.invalidateAll();
		}

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

			for (Entity entity : mEntities)
			{
				mEntityGenerator.unregisterEntityId(entity.getId());
			}

			mEntities.clear();
			mEntityIndexMap.clear();

			mScheduledComponentAdditions.clear();
			mScheduledComponentRemovements.clear();

			mIndexes.invalidateAll();
		}

		const ComponentMap& getComponentsData() const { return mComponents; }

		template<typename Func>
		void applySortingFunction(Func&& func) {
			func(mComponents, mEntities, mEntityIndexMap);
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
#ifdef ECS_DEBUG_CHECKS_ENABLED
			else
			{
				gErrorHandler("Trying to add a component when the entity already has one of the same type. This will result in memory leak");
			}
#endif // ECS_DEBUG_CHECKS_ENABLED
			mIndexes.invalidateForComponent(typeId);
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

		template<typename... Components>
		void appendComponentsIndexed(std::vector<std::tuple<Components*...>>& inOutComponents)
		{
			if (mIndexes.template isIndexValid<false, Components...>())
			{
				mIndexes.template appendFromIndex<Components...>(inOutComponents);
			}
			else
			{
				std::vector<std::tuple<Components*...>> newIndexData;
				gatherNonIndexedComponents<Components...>(newIndexData);
				mIndexes.template updateIndex(newIndexData);
				inOutComponents.insert(std::end(inOutComponents), std::begin(newIndexData), std::end(newIndexData));
			}
		}

		template<typename... Components>
		void appendComponentsWithEntityIndexed(std::vector<std::tuple<Entity, Components*...>>& inOutComponents)
		{
			if (mIndexes.template isIndexValid<true, Components...>())
			{
				mIndexes.template appendFromIndexWithData<Components...>(inOutComponents);
			}
			else
			{
				std::vector<std::tuple<Entity, Components*...>> newIndexData;
				gatherNonIndexedComponentsWithEntities<Components...>(newIndexData);
				mIndexes.template updateIndexWithData(newIndexData);
				inOutComponents.insert(std::end(inOutComponents), std::begin(newIndexData), std::end(newIndexData));
			}
		}

	private:
		ComponentMap mComponents;
		std::vector<Entity> mEntities;
		std::unordered_map<Entity::EntityId, EntityIndex> mEntityIndexMap;

		ComponentIndexes<ComponentTypeId, Entity> mIndexes;

		std::vector<ComponentToAdd> mScheduledComponentAdditions;
		std::vector<ComponentToRemove> mScheduledComponentRemovements;

		const ComponentFactory& mComponentFactory;
		EntityGenerator& mEntityGenerator;
	};

} // namespace RaccoonEcs
