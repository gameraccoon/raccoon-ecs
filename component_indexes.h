#pragma once

#include <bit>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "component_map.h"

namespace RaccoonEcs
{
	template <typename ComponentTypeId>
	class ComponentIndexes
	{
	public:
		using ComponentMap = ComponentMapImpl<ComponentTypeId>;

	public:
		void onComponentAdded(ComponentTypeId typeId, size_t entityIndex, const ComponentMap& componentMap)
		{
			if (auto it = mIndexesHavingComponent.find(typeId); it != mIndexesHavingComponent.end())
			{
				for (Index* index : it->second)
				{
					index->tryAddEntity(entityIndex, componentMap);
				}
			}
		}

		void onComponentRemoved(ComponentTypeId typeId, size_t entityIndex)
		{
			if (auto it = mIndexesHavingComponent.find(typeId); it != mIndexesHavingComponent.end())
			{
				for (Index* index : it->second)
				{
					index->tryRemoveEntity(entityIndex);
				}
			}
		}

		void onEntityRemoved(size_t removedEntityIndex, size_t swappedEntityIndex)
		{
			for (auto& pair : mIndexes)
			{
				pair.second.removeEntityWithSwap(removedEntityIndex, swappedEntityIndex);
			}
		}

		template<typename... Components>
		const std::vector<size_t>& getIndex(const ComponentMap& componentMap)
		{
			Index& index = getOrCreateIndex<Components...>(componentMap);
			return index.matchingEntities;
		}

		void clear()
		{
			mIndexes.clear();
			mIndexesHavingComponent.clear();
		}

		void rebuild(const ComponentMap& componentMap)
		{
			for (auto& pair : mIndexes)
			{
				Index& index = pair.second;
				index.matchingEntities.clear();
				index.sparseArray.clear();
				populateIndex(index, componentMap);
			}
		}

	private:
		struct Key
		{
			size_t hash = 0;
			std::vector<ComponentTypeId> componentTypes;

			Key(std::vector<ComponentTypeId>&& componentTypes)
				: componentTypes(std::move(componentTypes))
			{
				std::sort(this->componentTypes.begin(), this->componentTypes.end());
			}

			bool operator==(const Key& anotherKey) const
			{
				return componentTypes == anotherKey.componentTypes;
			}
		};

		struct Index
		{
			constexpr static size_t InvalidIndex = std::numeric_limits<size_t>::max();

			std::vector<size_t> matchingEntities;
			std::vector<size_t> sparseArray;
			std::vector<ComponentTypeId> componentTypes;

			void tryAddEntity(size_t entityIndex, const ComponentMap& componentMap)
			{
				for (ComponentTypeId typeId : componentTypes)
				{
					const std::vector<void*>& componentVector = componentMap.getComponentVectorById(typeId);
					if (entityIndex >= componentVector.size() || componentVector[entityIndex] == nullptr)
					{
						return;
					}
				}

				if (sparseArray.size() <= entityIndex)
				{
					if (sparseArray.capacity() < entityIndex + 1)
					{
						sparseArray.reserve(std::max(static_cast<size_t>(16), (entityIndex + 1) * 2));
					}
					sparseArray.resize(entityIndex + 1, InvalidIndex);
				}

				sparseArray[entityIndex] = matchingEntities.size();
				matchingEntities.push_back(entityIndex);
			}

			void tryRemoveEntity(size_t entityIndex)
			{
				if (entityIndex < sparseArray.size())
				{
					const size_t idx = sparseArray[entityIndex];
					if (idx != InvalidIndex)
					{
						if (idx != matchingEntities.size() - 1)
						{
							sparseArray[matchingEntities.back()] = idx;
							matchingEntities[idx] = matchingEntities.back();
						}
						sparseArray[entityIndex] = InvalidIndex;
						matchingEntities.pop_back();
					}
				}
			}

			void removeEntityWithSwap(size_t removedEntityIndex, size_t swappedEntityIndex)
			{
				if (swappedEntityIndex < sparseArray.size() && sparseArray[swappedEntityIndex] != InvalidIndex)
				{
					if (sparseArray[removedEntityIndex] != InvalidIndex)
					{
						tryRemoveEntity(swappedEntityIndex);
					}
					else
					{
						size_t newSwappedIdx = sparseArray[swappedEntityIndex];
						sparseArray[removedEntityIndex] = newSwappedIdx;
						matchingEntities[newSwappedIdx] = removedEntityIndex;
						sparseArray[swappedEntityIndex] = InvalidIndex;
					}
				}
				else
				{
					tryRemoveEntity(removedEntityIndex);
				}
			}
		};

		struct KeyHasher
		{
			std::size_t operator()(const Key& key) const
			{
				return key.hash;
			}
		};

	private:
		static constexpr size_t calculateKeyHash(const std::vector<ComponentTypeId>& types)
		{
			std::size_t hash = 1u << 8;
			for (ComponentTypeId typeId : types)
			{
				hash = std::hash<ComponentTypeId>()(typeId) ^ std::rotl(hash, 3);
			}
			return hash;
		}

		template<typename... Components>
		static Key constructKey()
		{
			Key result{{Components::GetTypeId()...}};
			result.hash = calculateKeyHash(result.componentTypes);
			return result;
		}

		template<typename... Components>
		Index& getOrCreateIndex(const ComponentMap& componentMap)
		{
			static const Key key = constructKey<Components...>();
			auto it = mIndexes.find(key);
			if (it == mIndexes.end())
			{
				std::tie(it, std::ignore) = mIndexes.try_emplace(key);

				Index& newIndex = it->second;
				newIndex.componentTypes = key.componentTypes;
				populateIndex(newIndex, componentMap);

				for (ComponentTypeId typeId : key.componentTypes)
				{
					mIndexesHavingComponent[typeId].push_back(&it->second);
				}
			}
			return it->second;
		}

		static void populateIndex(Index& inOutIndex, const ComponentMap& componentMap)
		{
			size_t shortestVectorSize = std::numeric_limits<size_t>::max();
			std::vector<const std::vector<void*>*> componentVectors;
			componentVectors.reserve(inOutIndex.componentTypes.size());
			for (ComponentTypeId typeId : inOutIndex.componentTypes)
			{
				componentVectors.push_back(&componentMap.getComponentVectorById(typeId));
				shortestVectorSize = std::min(shortestVectorSize, componentVectors.back()->size());
			}

			if (shortestVectorSize == std::numeric_limits<size_t>::max() || shortestVectorSize == 0)
			{
				return;
			}

			inOutIndex.sparseArray.resize(shortestVectorSize, Index::InvalidIndex);
			for (size_t i = 0u; i < shortestVectorSize; ++i)
			{
				if (doesEntityHaveAllComponents(componentVectors, i))
				{
					inOutIndex.matchingEntities.push_back(i);
					inOutIndex.sparseArray[i] = inOutIndex.matchingEntities.size() - 1;
				}
			}
		}

		static bool doesEntityHaveAllComponents(const std::vector<const std::vector<void*>*>& componentVectors, size_t i)
		{
			for (const std::vector<void*>* componentVector : componentVectors)
			{
				if ((*componentVector)[i] == nullptr)
				{
					return false;
				}
			}
			return true;
		}

	private:
		std::unordered_map<Key, Index, KeyHasher> mIndexes;
		std::unordered_map<ComponentTypeId, std::vector<Index*>> mIndexesHavingComponent;
	};

} // namespace RaccoonEcs
