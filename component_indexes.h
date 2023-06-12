#pragma once

#include <bit>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "component_map.h"

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
	} // namespace TemplateTrick

	template<typename ComponentTypeId>
	class ComponentIndexes
	{
	public:
		using ComponentMap = ComponentMapImpl<ComponentTypeId>;

	public:
		ComponentIndexes() = default;
		ComponentIndexes(const ComponentIndexes&)
			: ComponentIndexes()
		{}

		ComponentIndexes(ComponentIndexes&& other) noexcept
			: mIndexes(std::move(other.mIndexes))
			, mIndexesHavingComponent(std::move(other.mIndexesHavingComponent))
			, mIndexRemovalCallbacks(std::move(other.mIndexRemovalCallbacks))
			, mIndexMoveCallbacks(std::move(other.mIndexMoveCallbacks))
		{
			for (auto& callback : mIndexMoveCallbacks)
			{
				callback.second(other, *this);
			}
		}

		ComponentIndexes& operator=(const ComponentIndexes&)
		{
			clear();
			return *this;
		}

		ComponentIndexes& operator=(ComponentIndexes&& other) noexcept
		{
			for (auto& callback : mIndexRemovalCallbacks)
			{
				callback.second(*this);
			}
			mIndexes = std::move(other.mIndexes);
			mIndexesHavingComponent = std::move(other.mIndexesHavingComponent);
			mIndexRemovalCallbacks = std::move(other.mIndexRemovalCallbacks);
			mIndexMoveCallbacks = std::move(other.mIndexMoveCallbacks);
			for (auto& callback : mIndexMoveCallbacks)
			{
				callback.second(other, *this);
			}
			return *this;
		}

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
		void initializeIndex(const ComponentMap& componentMap)
		{
			getOrCreateIndex<std::remove_const_t<Components>...>(componentMap);
		}

		template<typename... Components>
		const std::vector<size_t>& getIndex(const ComponentMap& componentMap)
		{
			const auto& index = getOrCreateIndex<std::remove_const_t<Components>...>(componentMap);
			return index.getMatchingEntities();
		}

		template<typename... Components>
		size_t getIndexSize(const ComponentMap& componentMap)
		{
			const auto& index = getOrCreateIndex<std::remove_const_t<Components>...>(componentMap);
			return index.getMatchingEntities().size();
		}

		template<typename... Components>
		const std::vector<std::tuple<std::remove_const_t<Components>*...>>& getComponents(const ComponentMap& componentMap)
		{
			const auto& index = getOrCreateIndex<std::remove_const_t<Components>...>(componentMap);
			return index.getComponents();
		}

		void clear()
		{
			for (BaseIndex* index : mIndexes)
			{
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
		struct DenseArray
		{
			std::vector<std::tuple<Components*...>> cachedComponents;
			std::vector<size_t> matchingEntityIndexes;

			DenseArray() = default;
			~DenseArray() = default;
			DenseArray(const DenseArray&) = delete;
			DenseArray& operator=(const DenseArray&) = delete;
			DenseArray(DenseArray&&) noexcept = default;
			DenseArray& operator=(DenseArray&&) noexcept = default;
		};

		class BaseIndex
		{
		public:
			BaseIndex() = default;
			virtual ~BaseIndex() = default;
			BaseIndex(const BaseIndex&) = delete;
			BaseIndex& operator=(const BaseIndex&) = delete;
			BaseIndex(BaseIndex&&) noexcept = default;
			BaseIndex& operator=(BaseIndex&&) noexcept = default;

			virtual void tryAddEntity(size_t entityIndex, const ComponentMap& componentMap) = 0;
			virtual void tryRemoveEntity(size_t entityIndex) = 0;
			virtual void removeEntityWithSwap(size_t removedEntityIndex, size_t swappedEntityIndex) = 0;
			virtual void populate(const ComponentMap& componentMap) = 0;
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

		template<typename... Components>
		class Index final : public BaseIndex
		{
		public:
			explicit Index()
				: mComponentTypes({Components::GetTypeId()...})
			{}

			void tryAddEntity(size_t entityIndex, const ComponentMap& componentMap) final
			{
				using namespace TemplateTrick;
				std::array<const std::vector<void*>*, sizeof...(Components)> componentVectors;
				for (size_t i = 0; i < sizeof...(Components); ++i)
				{
					const std::vector<void*>& componentVector = componentMap.getComponentVectorById(mComponentTypes[i]);
					if (entityIndex >= componentVector.size() || componentVector[entityIndex] == nullptr)
					{
						return;
					}
					componentVectors[i] = &componentVector;
				}

				if (mSparseArray.size() <= entityIndex)
				{
					if (mSparseArray.capacity() < entityIndex + 1)
					{
						mSparseArray.reserve(std::max(static_cast<size_t>(16), (entityIndex + 1) * 2));
					}
					mSparseArray.resize(entityIndex + 1, BaseIndex::InvalidIndex);
				}

				mSparseArray[entityIndex] = mDenseArray.matchingEntityIndexes.size();
				mDenseArray.cachedComponents.emplace_back(static_cast<Components*>((*componentVectors[Idx<Components, Components...>()])[entityIndex])...);
				mDenseArray.matchingEntityIndexes.push_back(entityIndex);
			}

			void tryRemoveEntity(size_t entityIndex) final
			{
				if (entityIndex < mSparseArray.size())
				{
					const size_t idx = mSparseArray[entityIndex];
					if (idx != BaseIndex::InvalidIndex)
					{
						if (idx != mDenseArray.matchingEntityIndexes.size() - 1)
						{
							mSparseArray[mDenseArray.matchingEntityIndexes.back()] = idx;
							mDenseArray.cachedComponents[idx] = mDenseArray.cachedComponents.back();
							mDenseArray.matchingEntityIndexes[idx] = mDenseArray.matchingEntityIndexes.back();
						}
						mSparseArray[entityIndex] = BaseIndex::InvalidIndex;
						mDenseArray.cachedComponents.pop_back();
						mDenseArray.matchingEntityIndexes.pop_back();
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
						// update the index of the removed entity to point to the swapped entity
						// removedEntityIndex = 1; swappedEntityIndex = 4
						// Before:
						// Sparse array
						// [-1, >0<, -1, 2, (1), -1]
						// Dense array (indexes)
						// [>1<, 4, 3]
						// Dense array (components)
						// [>A<, B, C]
						// After:
						// Sparse array
						// [-1, (0), -1, 1, -1, -1]
						// Dense array (indexes)
						// [1, 3] -> pop 4
						// Dense array (components)
						// [B, C] -> pop A

						// find the positions in the dense array of the removed and swapped entities
						const size_t removedEntityDenseIdx = mSparseArray[removedEntityIndex]; // 0
						const size_t swappedEntityDenseIdx = mSparseArray[swappedEntityIndex]; // 1
						// find the position in the sparse array of the last entity in the dense array
						const size_t lastEntitySparseIdx = mDenseArray.matchingEntityIndexes.back(); // 3
						// swap the swapped entity with the last entity in the dense array
						std::swap(mDenseArray.matchingEntityIndexes[swappedEntityDenseIdx], mDenseArray.matchingEntityIndexes[lastEntitySparseIdx]);
						// pop the last entity from the dense array
						mDenseArray.matchingEntityIndexes.pop_back();

						// update the index of the last entity in the sparse array to point to the swapped entity
						mSparseArray[lastEntitySparseIdx] = swappedEntityDenseIdx;
						// remove the swapped entity from the sparse array
						mSparseArray[swappedEntityIndex] = BaseIndex::InvalidIndex;

						// for dense arrays of components, swap the removed entity with the last entity in the dense array
						std::swap(mDenseArray.cachedComponents[removedEntityDenseIdx], mDenseArray.cachedComponents.back());
						// pop the last entity from the dense array
						mDenseArray.cachedComponents.pop_back();
						// swap the removed entity with the swapped entity in the sparse array
						if (mDenseArray.cachedComponents.size() != swappedEntityDenseIdx)
						{
							std::swap(mDenseArray.cachedComponents[swappedEntityDenseIdx], mDenseArray.cachedComponents[removedEntityDenseIdx]);
						}
					}
					else
					{
						// removed entity was not in the index, just update the index of the swapped entity
						mSparseArray[swappedEntityIndex] = mSparseArray[removedEntityIndex];
						mSparseArray[removedEntityIndex] = BaseIndex::InvalidIndex;
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
				return mDenseArray.matchingEntityIndexes;
			}

			[[nodiscard]] size_t getMatchingEntitiesCount() const
			{
				return mDenseArray.matchingEntityIndexes.size();
			}

			[[nodiscard]] const std::vector<std::tuple<Components*...>>& getComponents() const
			{
				return mDenseArray.cachedComponents;
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
						using namespace TemplateTrick;
						mDenseArray.matchingEntityIndexes.push_back(i);
						mSparseArray[i] = mDenseArray.matchingEntityIndexes.size() - 1;
						mDenseArray.cachedComponents.emplace_back(static_cast<Components*>((*componentVectors[Idx<Components, Components...>()])[i])...);
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
				mDenseArray.matchingEntityIndexes.clear();
				mDenseArray.cachedComponents.clear();
				mSparseArray.clear();
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
			DenseArray<Components...> mDenseArray;
			std::vector<ComponentTypeId> mComponentTypes;
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
				mIndexRemovalCallbacks.try_emplace(&index, [](ComponentIndexes<ComponentTypeId>& self) {
					getIndexMap<Components...>().erase(&self);
				});
				mIndexMoveCallbacks.try_emplace(&index, [](ComponentIndexes<ComponentTypeId>& from, ComponentIndexes<ComponentTypeId>& to) {
					auto& indexMap = getIndexMap<Components...>();
					indexMap[&to] = std::move(indexMap[&from]);
					indexMap.erase(&from);
				});
			}
			return index;
		}

	private:
		std::vector<BaseIndex*> mIndexes;
		std::unordered_map<ComponentTypeId, std::vector<BaseIndex*>> mIndexesHavingComponent;
		std::unordered_map<BaseIndex*, std::function<void(ComponentIndexes<ComponentTypeId>& self)>> mIndexRemovalCallbacks;
		std::unordered_map<BaseIndex*, std::function<void(ComponentIndexes<ComponentTypeId>& from, ComponentIndexes<ComponentTypeId>& to)>> mIndexMoveCallbacks;
	};
} // namespace RaccoonEcs
