#pragma once

#include <vector>
#include <tuple>
#include <functional>
#include <unordered_map>

namespace RaccoonEcs
{
	template <typename ComponentTypeId>
	class ComponentIndexes
	{
	public:
		template<typename... Components>
		void updateIndex(const std::vector<std::tuple<Components*...>>& newData)
		{
			Index& index = getOrCreateIndex<Components...>();
			auto data = *static_cast<std::vector<std::tuple<Components*...>>*>(index.data);
			data = newData;
			index.isValid = true;
		}

		template<typename... Components>
		void appendFromIndex(std::vector<std::tuple<Components*...>>& outData)
		{
			Index& index = getOrCreateIndex<Components...>();
			auto data = *static_cast<std::vector<std::tuple<Components*...>>*>(index.data);
			outData.insert(std::end(outData), std::begin(data), std::end(data));
		}

		template<typename... Components>
		bool isIndexValid()
		{
			const Index& index = getOrCreateIndex<Components...>();
			return index.isValid;
		}

		void invalidateAll()
		{
			for (auto& pair : mIndexes)
			{
				pair.second.isValid = false;
			}
		}

		void invalidateForComponent(ComponentTypeId typeId)
		{
			for (Index* index : mIndexesBoundToComponent[typeId])
			{
				index->isValid = false;
			}
		}

	private:
		struct Key
		{
			Key(std::vector<ComponentTypeId>&& components)
				: components(std::move(components))
			{}

			bool operator==(const Key& anotherKey) const
			{
				return components == anotherKey.components;
			}

			size_t hash = 0;
			std::vector<ComponentTypeId> components;
		};

		struct Index
		{
			~Index()
			{
				deleter(data);
			}

			bool isValid = false;
			void* data = nullptr;
			std::function<void(void*)> deleter;
		};

		struct KeyHasher
		{
			std::size_t operator()(const Key& key) const
			{
				return key.hash;
			}
		};

	private:
		template<typename... Components>
		constexpr size_t calculateKeyHash()
		{
			std::size_t hash = 1u << 8;
			((hash = std::hash<ComponentTypeId>()(Components::GetTypeId()) ^ std::rotl(hash, 3)), ...);
			return hash;
		}

		template<typename... Components>
		Key constructKey()
		{
			Key result{{ Components::GetTypeId()... }};
			result.hash = calculateKeyHash<Components...>();
			return result;
		}

		template<typename... Components>
		Index& getOrCreateIndex()
		{
			static const Key key = constructKey<Components...>();
			auto it = mIndexes.find(key);
			if (it == mIndexes.end())
			{
				std::tie(it, std::ignore) = mIndexes.try_emplace(key);
				Index& newIndex = it->second;
				newIndex.data = static_cast<void*>(new std::vector<std::tuple<Components*...>>);
				newIndex.deleter = [](void* indexData)
				{
					delete static_cast<std::vector<std::tuple<Components*...>>*>(indexData);
				};

				for (ComponentTypeId typeId : key.components)
				{
					mIndexesBoundToComponent[typeId].push_back(&it->second);
				}
			}
			return it->second;
		}

	private:
		std::unordered_map<Key, Index, KeyHasher> mIndexes;
		std::unordered_map<ComponentTypeId, std::vector<Index*>> mIndexesBoundToComponent;
	};

} // namespace RaccoonEcs
