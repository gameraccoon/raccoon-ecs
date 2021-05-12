#pragma once

#include <algorithm>
#include <ranges>
#include <tuple>
#include <unordered_map>

#include "component_factory.h"
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
			Entity::EntityId id = mEntityGenerator.generateAndRegisterEntityId();
			mEntityIndexMap.emplace(id, mNextEntityIndex);
			mIndexEntityMap.emplace(mNextEntityIndex, id);
			++mNextEntityIndex;
			onEntityAdded.broadcast();
			return Entity(id);
		}

		void removeEntity(Entity entity)
		{
			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying to remove an entity that doesn't exist: ") + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
				return;
			}
			mEntityGenerator.unregisterEntityId(entity.getId());

			EntityIndex oldEntityIdx = entityIdxItr->second;

			--mNextEntityIndex; // now it points to the element that is going to be removed

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
					if (mNextEntityIndex < componentVector.second.size() && oldEntityIdx != mNextEntityIndex)
					{
						// move it to the freed space
						std::swap(componentVector.second[oldEntityIdx], componentVector.second[mNextEntityIndex]);
					}
				}
			}

			mEntityIndexMap.erase(entity.getId());

			if (oldEntityIdx != mNextEntityIndex)
			{
				// relink maps
				Entity::EntityId entityID = mIndexEntityMap[mNextEntityIndex];
				mEntityIndexMap[entityID] = oldEntityIdx;
				mIndexEntityMap[oldEntityIdx] = entityID;
			}
			mIndexEntityMap.erase(mNextEntityIndex);

			onEntityRemoved.broadcast();
		}

		bool hasEntity(Entity entity)
		{
			return mEntityIndexMap.find(entity.getId()) != mEntityIndexMap.end();
		}

		[[nodiscard]] bool hasAnyEntities() const
		{
			return !mEntityIndexMap.empty();
		}

		[[nodiscard]] const std::unordered_map<Entity::EntityId, EntityIndex>& getEntities() const { return mEntityIndexMap; }

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
		 * Use cases: readding deleted entity by "undo" editor command, loading the game from save.
		 */
		bool tryInsertEntity(Entity entity)
		{
			bool successfullyRegistered = mEntityGenerator.registerEntityId(entity.getId());
			if (successfullyRegistered)
			{
				mEntityIndexMap.emplace(entity.getId(), mNextEntityIndex);
				mIndexEntityMap.emplace(mNextEntityIndex, entity.getId());
				++mNextEntityIndex;

				onEntityAdded.broadcast();
			}
			return successfullyRegistered;
		}

		void getAllEntityComponents(Entity entity, std::vector<TypedComponent>& outComponents)
		{
			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr != mEntityIndexMap.end())
			{
				EntityIndex index = entityIdxItr->second;
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
			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr != mEntityIndexMap.end())
			{
				EntityIndex index = entityIdxItr->second;
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
			auto createFn = mComponentFactory.getCreationFn(typeId);
			void* component = createFn();
			addComponent(entity, component, typeId);
			return component;
		}

		void addComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying to add component ") + std::to_string(typeId)
					+ " to a non-exsistent entity" + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
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
			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying to remove component ") + std::to_string(typeId)
					+ " from a non-exsistent entity" + std::to_string(entity.getId()));
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
			}
			mScheduledComponentAdditions.clear();

			for (const auto& removement : mScheduledComponentRemovements)
			{
				removeComponent(removement.entity, removement.typeId);
			}
			mScheduledComponentRemovements.clear();
		}

		template<typename... Components>
		std::tuple<Components*...> getEntityComponents(Entity entity)
		{
			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
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
					inOutComponents.push_back(std::tuple_cat(std::make_tuple(data...), std::move(components)));
				}
			}
		}

		template<typename FirstComponent, typename... Components, typename... AdditionalData>
		void getComponentsWithEntities(std::vector<std::tuple<Entity, AdditionalData..., FirstComponent*, Components*...>>& inOutComponents, AdditionalData... data)
		{
			auto componentVectors = mComponents.template getComponentVectors<FirstComponent, Components...>();
			auto& firstComponentVector = std::get<0>(componentVectors);
			size_t shortestVectorSize = GetShortestVector(componentVectors);

			constexpr unsigned componentsSize = sizeof...(Components);

			for (auto& [entityId, entityIndex] : mEntityIndexMap)
			{
				if (entityIndex >= shortestVectorSize)
				{
					continue;
				}

				auto& firstComponent = firstComponentVector[entityIndex];
				if (firstComponent == nullptr)
				{
					continue;
				}

				auto components = getEntityComponentSet<FirstComponent, Components...>(entityIndex, componentVectors);

				if (std::get<componentsSize>(components) != nullptr)
				{
					inOutComponents.push_back(std::tuple_cat(std::make_tuple(Entity(entityId)), std::make_tuple(data...), std::move(components)));
				}
			}
		}

		template<typename FirstComponent, typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(FunctionType processor, AdditionalData... data)
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

				if (std::get<componentsSize>(components) == nullptr)
				{
					continue;
				}

				std::apply(processor, std::tuple_cat(std::make_tuple(data...), std::move(components)));
			}
		}

		template<typename FirstComponent, typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(FunctionType processor, AdditionalData... data)
		{
			auto componentVectors = mComponents.template getComponentVectors<FirstComponent, Components...>();
			auto& firstComponentVector = std::get<0>(componentVectors);
			size_t shortestVectorSize = GetShortestVector(componentVectors);

			constexpr unsigned componentsSize = sizeof...(Components);

			for (auto& [entityId, entityIndex] : mEntityIndexMap)
			{
				if (entityIndex >= shortestVectorSize)
				{
					continue;
				}

				auto& firstComponent = firstComponentVector[entityIndex];
				if (firstComponent == nullptr)
				{
					continue;
				}

				auto components = getEntityComponentSet<FirstComponent, Components...>(entityIndex, componentVectors);

				if (std::get<componentsSize>(components) == nullptr)
				{
					continue;
				}

				std::apply(processor, std::tuple_cat(std::make_tuple(Entity(entityId)), std::make_tuple(data...), std::move(components)));
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
				bool hasAllComponents = std::all_of(
					componentVectors.cbegin(),
					componentVectors.cend(),
					[idx](const std::vector<void*>* componentVector){ return (*componentVector)[idx] != nullptr; }
				);

				if (hasAllComponents)
				{
					inOutEntities.emplace_back(mIndexEntityMap.find(idx)->second);
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

			auto entityIdxItr = mEntityIndexMap.find(entity.getId());
			if (entityIdxItr == mEntityIndexMap.end())
			{
#ifdef ECS_DEBUG_CHECKS_ENABLED
				gErrorHandler(std::string("Trying transfer non-existent entity: ") + std::to_string(entity.getId()));
#endif // ECS_DEBUG_CHECKS_ENABLED
				return;
			}

			[[maybe_unused]] auto insertionResult = otherManager.mEntityIndexMap.try_emplace(entity.getId(), otherManager.mNextEntityIndex);
#ifdef ECS_DEBUG_CHECKS_ENABLED
			if (!insertionResult.second)
			{
				gErrorHandler("EntityId is not unique, two entities have just collided");
			}
#endif // ECS_DEBUG_CHECKS_ENABLED
			otherManager.mIndexEntityMap.emplace(otherManager.mNextEntityIndex, entity.getId());
			++otherManager.mNextEntityIndex;

			--mNextEntityIndex; // now it points to the element that going to be removed
			EntityIndex oldEntityIdx = entityIdxItr->second;

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
					if (mNextEntityIndex < componentVector.second.size() && oldEntityIdx != mNextEntityIndex)
					{
						// move it to the freed space
						std::swap(componentVector.second[oldEntityIdx], componentVector.second[mNextEntityIndex]);
					}
				}
			}

			mEntityIndexMap.erase(entity.getId());

			if (oldEntityIdx != mNextEntityIndex)
			{
				// relink maps
				Entity::EntityId entityID = mIndexEntityMap[mNextEntityIndex];
				mEntityIndexMap[entityID] = oldEntityIdx;
				mIndexEntityMap[oldEntityIdx] = entityID;
			}
			mIndexEntityMap.erase(mNextEntityIndex);
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

			for (auto& pair : mEntityIndexMap)
			{
				mEntityGenerator.unregisterEntityId(pair.first);
			}

			mEntityIndexMap.clear();
			mIndexEntityMap.clear();

			mScheduledComponentAdditions.clear();
			mScheduledComponentRemovements.clear();
			mNextEntityIndex = 0;
		}

		const ComponentMap& getComponentsData() const { return mComponents; }

		auto getSortableData() { return std::make_tuple(std::ref(mComponents), std::ref(mEntityIndexMap), std::ref(mIndexEntityMap)); }

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
		}

	private:
		ComponentMap mComponents;
		std::unordered_map<Entity::EntityId, EntityIndex> mEntityIndexMap;
		std::unordered_map<EntityIndex, Entity::EntityId> mIndexEntityMap;

		std::vector<ComponentToAdd> mScheduledComponentAdditions;
		std::vector<ComponentToRemove> mScheduledComponentRemovements;

		EntityIndex mNextEntityIndex = 0;

		const ComponentFactory& mComponentFactory;
		EntityGenerator& mEntityGenerator;
	};

} // namespace RaccoonEcs
