#pragma once

#include <vector>
#include <span>

#include "../msvc_fix.h"

namespace RaccoonEcs
{
	// A container that allows to perform operations over entities from multiple entity managers in a single call
	template<typename EntityManager, typename ExtraData = nullptr_t>
	class CombinedEntityManagerView
	{
	public:
		using EntityManagerRef = std::reference_wrapper<EntityManager>;

		struct Record
		{
			EntityManagerRef entityManager;
			ExtraData extraData;
		};

		using RecordsVector = std::vector<Record>;

	public:
		CombinedEntityManagerView(std::span<Record> entityManagers)
			: mRecords(entityManagers.begin(), entityManagers.end())
		{
		}

		CombinedEntityManagerView(const std::vector<Record>& entityManagers)
			: mRecords(entityManagers)
		{
		}

		CombinedEntityManagerView(std::vector<Record>&& entityManagers)
			: mRecords(std::move(entityManagers))
		{
		}

		CombinedEntityManagerView(const std::vector<EntityManagerRef>& entityManagers)
		{
			mRecords.reserve(entityManagers.size());
			for (auto& entityManager : entityManagers)
			{
				mRecords.push_back({entityManager, nullptr});
			}
		}

		template<typename... Components, typename DataVector>
		void getComponents(DataVector& inOutComponents)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX getComponents<Components...>(inOutComponents);
			}
		}

		template<typename... Components, typename DataVector>
		void getComponentsWithEntities(DataVector& inOutComponents)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX getComponentsWithEntities<Components...>(inOutComponents);
			}
		}

		template<typename... Components, typename DataVector>
		void getComponentsWithExtraData(DataVector& inOutComponents)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX getComponents<Components...>(inOutComponents, record.extraData);
			}
		}

		template<typename... Components, typename DataVector>
		void getComponentsWithEntitiesAndExtraData(DataVector& inOutComponents)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX getComponentsWithEntities<Components...>(inOutComponents, record.extraData);
			}
		}

		template<typename... Components, typename FunctionType>
		void forEachComponentSet(FunctionType processor)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX forEachComponentSet<Components...>(processor);
			}
		}

		template<typename... Components, typename FunctionType>
		void forEachComponentSetWithEntity(FunctionType processor)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX forEachComponentSetWithEntity<Components...>(processor);
			}
		}

		template<typename... Components, typename FunctionType>
		void forEachComponentSetWithExtraData(FunctionType processor)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX forEachComponentSet<Components...>(processor, record.extraData);
			}
		}

		template<typename... Components, typename FunctionType>
		void forEachComponentSetWithEntityAndExtraData(FunctionType processor)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX forEachComponentSetWithEntity<Components...>(processor, record.extraData);
			}
		}

		void executeScheduledActions()
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX executeScheduledActions();
			}
		}

		template<typename TypedComponent>
		void getAllEntityComponents(Entity entity, std::vector<TypedComponent>& outComponents)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_FIX getAllEntityComponents(entity, outComponents);

				// if components are not empty, then we have found the entity
				if (!outComponents.empty())
				{
					return;
				}
			}
		}

	private:
		std::vector<Record> mRecords;
	};
}
