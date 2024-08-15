#pragma once

#include <span>
#include <vector>

#include "../msvc_fix.h"
#include "entity_view.h"

namespace RaccoonEcs
{
	// A container that allows to perform operations over entities from multiple entity managers in a single call
	template<typename EntityManager, typename ExtraData = std::nullptr_t>
	class CombinedEntityManagerView
	{
	public:
		using EntityManagerRef = std::reference_wrapper<EntityManager>;
		using EntityView = EntityViewImpl<EntityManager>;

		struct Record
		{
			EntityManagerRef entityManager;
			ExtraData extraData;
		};

		using RecordsVector = std::vector<Record>;

	public:
		explicit CombinedEntityManagerView(std::span<Record> entityManagers)
			: mRecords(entityManagers.begin(), entityManagers.end())
		{
		}

		explicit CombinedEntityManagerView(const std::vector<Record>& entityManagers)
			: mRecords(entityManagers)
		{
		}

		explicit CombinedEntityManagerView(std::vector<Record>&& entityManagers)
			: mRecords(std::move(entityManagers))
		{
		}

		explicit CombinedEntityManagerView(const std::vector<EntityManagerRef>& entityManagers)
		{
			mRecords.reserve(entityManagers.size());
			for (auto& entityManager : entityManagers)
			{
				mRecords.push_back({ entityManager, nullptr });
			}
		}

		CombinedEntityManagerView(const CombinedEntityManagerView&) = default;
		CombinedEntityManagerView(CombinedEntityManagerView&&) = default;
		CombinedEntityManagerView& operator=(const CombinedEntityManagerView&) = default;
		CombinedEntityManagerView& operator=(CombinedEntityManagerView&&) = default;
		~CombinedEntityManagerView() = default;

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
				record.entityManager.get().TEMPLATE_MSVC_FIX forEachComponentSetWithEntity<Components...>(
					[&processor, &entityManager = record.entityManager.get()](Entity entity, Components*... components) mutable -> void {
						processor(EntityView{ entity, entityManager }, components...);
					}
				);
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
				record.entityManager.get().TEMPLATE_MSVC_FIX forEachComponentSetWithEntity<Components...>(
					[&processor, &entityManager = record.entityManager.get()](ExtraData data, Entity entity, Components*... components) mutable -> void {
						processor(data, EntityView{ entity, entityManager }, components...);
					},
					record.extraData
				);
			}
		}

		void executeScheduledActions()
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_CLANG_FIX executeScheduledActions();
			}
		}

		template<typename TypedComponent>
		void getAllEntityComponents(Entity entity, std::vector<TypedComponent>& outComponents)
		{
			for (Record& record : mRecords)
			{
				record.entityManager.get().TEMPLATE_MSVC_EMSCRIPTEN_FIX getAllEntityComponents(entity, outComponents);

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
} // namespace RaccoonEcs
