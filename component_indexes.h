#pragma once

#include <algorithm>
#include <bit>
#include <limits>
#include <memory>
#include <tuple>
#include <unordered_map>
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
		ComponentIndexes(ComponentIndexes&& other) noexcept = default;
		ComponentIndexes& operator=(const ComponentIndexes&)
		{
			clear();
			return *this;
		}
		ComponentIndexes& operator=(ComponentIndexes&& other) noexcept = default;
		~ComponentIndexes() = default;

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

		void onEntityRemoved(size_t removedEntityIndex)
		{
			for (auto& [key, index] : mIndexes)
			{
				index->tryRemoveEntity(removedEntityIndex);
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
			for (auto& [key, index] : mIndexes)
			{
				index->clear();
			}
			mIndexes.clear();
			mIndexesHavingComponent.clear();
		}

		void rebuild(const ComponentMap& componentMap)
		{
			for (auto& [key, index] : mIndexes)
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
				return std::all_of(componentVectors.begin(), componentVectors.end(), [i](const std::vector<void*>* componentVector) {
					return (*componentVector)[i] != nullptr;
				});
			}

		private:
			DenseArray<Components...> mDenseArray;
			std::vector<ComponentTypeId> mComponentTypes;
			std::vector<size_t> mSparseArray;
		};

	private:
		template<typename... Components>
		static const std::vector<ComponentTypeId>& getComponentTypes()
		{
			static const std::vector<ComponentTypeId> componentTypes = []{
				std::vector<ComponentTypeId> types{Components::GetTypeId()...};
				std::sort(types.begin(), types.end());
				return types;
			}();
			return componentTypes;
		}

		static size_t calculateHash(const std::vector<ComponentTypeId>& componentTypes)
		{
			size_t hash = 0;
			for (ComponentTypeId typeId : componentTypes)
			{
				hash ^= std::hash<ComponentTypeId>{}(typeId);
				hash = std::rotl(hash, 5);
			}
			return hash;
		}

		struct IndexKey
		{
			const size_t hash;
			const std::vector<ComponentTypeId>& componentTypes;

			template<typename... Components>
			IndexKey(const std::vector<ComponentTypeId>& componentTypes)
				: hash(calculateHash(componentTypes))
				, componentTypes(componentTypes)
			{}

			template<typename... Components>
			static IndexKey Create()
			{
				return IndexKey(getComponentTypes<Components...>());
			}

			bool operator==(const IndexKey& other) const
			{
				return hash == other.hash && componentTypes == other.componentTypes;
			}

			struct HashFunction
			{
				size_t operator()(const IndexKey& key) const
				{
					return key.hash;
				}
			};
		};

		template<typename... Components>
		const Index<Components...>& getOrCreateIndex(const ComponentMap& componentMap)
		{
			static const IndexKey key = IndexKey::template Create<Components...>();
			if (auto it = mIndexes.find(key); it != mIndexes.end())
			{
				return *static_cast<Index<Components...>*>(it->second.get());
			}

			std::unique_ptr<Index<Components...>> indexPtr = std::make_unique<Index<Components...>>();
			Index<Components...>& index = *indexPtr;
			index.populate(componentMap);
			for (ComponentTypeId typeId : index.getComponentTypes())
			{
				mIndexesHavingComponent[typeId].push_back(&index);
			}

			mIndexes.emplace(key, std::move(indexPtr));
			return index;
		}

	private:
		std::unordered_map<IndexKey, std::unique_ptr<BaseIndex>, typename IndexKey::HashFunction> mIndexes;
		std::unordered_map<ComponentTypeId, std::vector<BaseIndex*>> mIndexesHavingComponent;
	};
} // namespace RaccoonEcs
