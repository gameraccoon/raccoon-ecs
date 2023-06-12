#pragma once

#include <bit>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <memory>
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
		~ComponentIndexes()
		{
			for (auto& callback : mIndexRemovalCallbacks)
			{
				callback.second(*this);
			}
		}

		void onComponentAdded(ComponentTypeId typeId, size_t entityIndex, const ComponentMap& componentMap)
		{
			if (auto it = mIndexesHavingComponent.find(typeId); it != mIndexesHavingComponent.end())
			{
				for (BaseIndex* index : it->second)
				{
					index->tryAddEntity(entityIndex, componentMap);
				}
			}
		}

		void onComponentRemoved(ComponentTypeId typeId, size_t entityIndex)
		{
			if (auto it = mIndexesHavingComponent.find(typeId); it != mIndexesHavingComponent.end())
			{
				for (BaseIndex* index : it->second)
				{
					index->tryRemoveEntity(entityIndex);
				}
			}
		}

		void onEntityRemoved(size_t removedEntityIndex, size_t swappedEntityIndex)
		{
			for (BaseIndex* index : mIndexes)
			{
				index->removeEntityWithSwap(removedEntityIndex, swappedEntityIndex);
			}
		}

		template<typename... Components>
		const std::vector<size_t>& getIndex(const ComponentMap& componentMap)
		{
			const Index<std::remove_const_t<Components>...>& index = getOrCreateIndex<std::remove_const_t<Components>...>(componentMap);
			return index.getMatchingEntities();
		}

		void clear()
		{
			for (BaseIndex* index : mIndexes) {
				index->clear();
			}
			mIndexes.clear();
			mIndexesHavingComponent.clear();
		}

		void rebuild(const ComponentMap& componentMap)
		{
			for (BaseIndex* index : mIndexes)
			{
				index->repopulate(componentMap);
			}
		}

	private:
		template<typename... Components>
		struct ComponentCache {
			std::vector<std::tuple<Components*...>> components;

			ComponentCache() = default;
			~ComponentCache() = default;
			ComponentCache(const ComponentCache&) = delete;
			ComponentCache& operator=(const ComponentCache&) = delete;
			ComponentCache(ComponentCache&&) = delete;
			ComponentCache& operator=(ComponentCache&&) = delete;

			size_t addRecord([[maybe_unused]]const ComponentMap& componentMap)
			{
				//components.emplace_back(componentMap.template getComponentVectorById<Components>(Components::GetTypeId()static_cast<>[cache->matchingEntities.size()])...);
				return components.size() - 1;
			}

			size_t removeRecord(size_t idx)
			{
				std::swap(components.back(), components[idx]);
				components.pop_back();
				return components.size();
			}
		};

		class BaseIndex
		{
		public:
			BaseIndex() = default;
			virtual ~BaseIndex() = default;
			BaseIndex(const BaseIndex&) = delete;
			BaseIndex& operator=(const BaseIndex&) = delete;
			BaseIndex(BaseIndex&&) = delete;
			BaseIndex& operator=(BaseIndex&&) = delete;

			virtual void tryAddEntity(size_t entityIndex, const ComponentMap& componentMap) = 0;
			virtual void tryRemoveEntity(size_t entityIndex) = 0;
			virtual void removeEntityWithSwap(size_t removedEntityIndex, size_t swappedEntityIndex) = 0;
			virtual void populate(const ComponentMap& /*componentMap*/) = 0;
			virtual void repopulate(const ComponentMap& componentMap) = 0;
			virtual void clear() = 0;
			[[nodiscard]] bool isPopulated() const { return mIsPopulated; }

		protected:
			void setPopulated(bool isPopulated) { mIsPopulated = isPopulated; }

		protected:
			constexpr static size_t InvalidIndex = std::numeric_limits<size_t>::max();

		private:
			bool mIsPopulated = false;
		};

		template <typename... Components>
		class Index final : public BaseIndex
		{
		public:
			explicit Index()
				: mComponentTypes({Components::GetTypeId()...})
			{}

			void tryAddEntity(size_t entityIndex, const ComponentMap& componentMap) final
			{
				for (ComponentTypeId typeId : mComponentTypes)
				{
					const std::vector<void*>& componentVector = componentMap.getComponentVectorById(typeId);
					if (entityIndex >= componentVector.size() || componentVector[entityIndex] == nullptr)
					{
						return;
					}
				}

				if (mSparseArray.size() <= entityIndex)
				{
					if (mSparseArray.capacity() < entityIndex + 1)
					{
						mSparseArray.reserve(std::max(static_cast<size_t>(16), (entityIndex + 1) * 2));
					}
					mSparseArray.resize(entityIndex + 1, BaseIndex::InvalidIndex);
				}

				mSparseArray[entityIndex] = mMatchingEntities.size();
				mMatchingEntities.push_back(entityIndex);
//				[[maybe_unused]] const size_t index = mCache.addRecord(componentMap);
//				RACCOON_ECS_ASSERT(index == matchingEntities.size() - 1, "Indexes are out of sync");
			}

			void tryRemoveEntity(size_t entityIndex) final
			{
				if (entityIndex < mSparseArray.size())
				{
					const size_t idx = mSparseArray[entityIndex];
					if (idx != BaseIndex::InvalidIndex)
					{
						if (idx != mMatchingEntities.size() - 1)
						{
							mSparseArray[mMatchingEntities.back()] = idx;
							mMatchingEntities[idx] = mMatchingEntities.back();
						}
						mSparseArray[entityIndex] = BaseIndex::InvalidIndex;
						mMatchingEntities.pop_back();
//						[[maybe_unused]] const size_t newSize = mCache.removeRecord(idx);
//						RACCOON_ECS_ASSERT(newSize == matchingEntities.size(), "Vector sizes are out of sync");
					}
				}
			}

			void removeEntityWithSwap(size_t removedEntityIndex, size_t swappedEntityIndex) final
			{
				// if the swapped entity was in the index
				if (swappedEntityIndex < mSparseArray.size() && mSparseArray[swappedEntityIndex] != BaseIndex::InvalidIndex)
				{
					// if the removed entity was in the index as well
					if (removedEntityIndex < mSparseArray.size() && mSparseArray[removedEntityIndex] != BaseIndex::InvalidIndex)
					{
						// remove the swapped entity from the index because the removed entity was swapped into its place
						tryRemoveEntity(swappedEntityIndex);
					}
					else
					{
						// the removed entity was not in the index
						// update the index of the removed entity to point to the swapped entity
						const size_t newSwappedIdx = mSparseArray[swappedEntityIndex];
						mSparseArray[removedEntityIndex] = newSwappedIdx;
						mMatchingEntities[newSwappedIdx] = removedEntityIndex;
						mSparseArray[swappedEntityIndex] = BaseIndex::InvalidIndex;
					}
				}
				else
				{
					// swapped entity was not in the index, just remove the entity
					tryRemoveEntity(removedEntityIndex);
				}
			}

			[[nodiscard]] const std::vector<size_t>& getMatchingEntities() const
			{
				return mMatchingEntities;
			}

			[[nodiscard]] const std::vector<std::tuple<Components*...>>& getComponents() const
			{
				return mCache.components;
			}

			[[nodiscard]] const std::vector<ComponentTypeId>& getComponentTypes() const
			{
				return mComponentTypes;
			}

			void populate(const ComponentMap& componentMap) final
			{
				BaseIndex::setPopulated(true);
				size_t shortestVectorSize = std::numeric_limits<size_t>::max();
				std::vector<const std::vector<void*>*> componentVectors;
				componentVectors.reserve(mComponentTypes.size());
				for (ComponentTypeId typeId : mComponentTypes)
				{
					componentVectors.push_back(&componentMap.getComponentVectorById(typeId));
					shortestVectorSize = std::min(shortestVectorSize, componentVectors.back()->size());
				}

				if (shortestVectorSize == std::numeric_limits<size_t>::max() || shortestVectorSize == 0)
				{
					return;
				}

				mSparseArray.resize(shortestVectorSize, BaseIndex::InvalidIndex);
				for (size_t i = 0u; i < shortestVectorSize; ++i)
				{
					if (doesEntityHaveAllComponents(componentVectors, i))
					{
						mMatchingEntities.push_back(i);
						mSparseArray[i] = mMatchingEntities.size() - 1;
					}
				}
			}

			void repopulate(const ComponentMap& componentMap) final
			{
				clear();
				populate(componentMap);
			}

			void clear() final
			{
				BaseIndex::setPopulated(false);
				mMatchingEntities.clear();
				mSparseArray.clear();
				mCache.components.clear();
			}

		private:
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
			ComponentCache<Components...> mCache;
			std::vector<ComponentTypeId> mComponentTypes;
			std::vector<size_t> mMatchingEntities;
			std::vector<size_t> mSparseArray;
		};

	private:
		template<typename... Components>
		static std::unordered_map<ComponentIndexes<ComponentTypeId>*, Index<Components...>>& getIndexMap()
		{
			static std::unordered_map<ComponentIndexes<ComponentTypeId>*, Index<Components...>> sIndexes;
			return sIndexes;
		}

		template<typename... Components>
		const Index<Components...>& getOrCreateIndex(const ComponentMap& componentMap)
		{
			Index<Components...>& index = getIndexMap<Components...>()[this];
			if (!index.isPopulated())
			{
				index.populate(componentMap);
				mIndexes.push_back(&index);
				for (ComponentTypeId typeId : index.getComponentTypes())
				{
					mIndexesHavingComponent[typeId].push_back(&index);
				}
				// the callback may already exist if we cleaned up the index before
				mIndexRemovalCallbacks.try_emplace(&index, [](ComponentIndexes<ComponentTypeId>& self)
				{
					getIndexMap<Components...>().erase(&self);
				});
			}
			return index;
		}

	private:
		std::vector<BaseIndex*> mIndexes;
		std::unordered_map<ComponentTypeId, std::vector<BaseIndex*>> mIndexesHavingComponent;
		std::unordered_map<BaseIndex*, std::function<void(ComponentIndexes<ComponentTypeId>& self)>> mIndexRemovalCallbacks;
	};
} // namespace RaccoonEcs
