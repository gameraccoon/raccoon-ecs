#pragma once

#include <array>
#include <numeric>
#include <vector>
#include <functional>

#include "error_handling.h"

namespace RaccoonEcs
{
	class ComponentPoolBase
	{
	public:
		virtual ~ComponentPoolBase() = default;
	};

	template <typename ComponentType>
	class ComponentPool final : public ComponentPoolBase
	{
	public:
		using PoolGrowStrategyFn = std::function<size_t(size_t)>;
	public:
		ComponentPool(size_t defaultChunkSize, bool needPreallocate = false, const PoolGrowStrategyFn& growStategyFn = nullptr) noexcept
			: mDefaultChunkSize(defaultChunkSize)
			, mGrowStrategyFn(growStategyFn)
		{
			if (needPreallocate)
			{
				allocateNewChunk();
			}
		}

		~ComponentPool() final
		{
			// we assume that all the components were unregistered before component pool destruction
			for (ComponentSlot* chunk : mChunks)
			{
				delete[] chunk;
			}
		}

		ComponentPool(ComponentPool&) = delete;
		ComponentPool& operator=(ComponentPool&) = delete;
		ComponentPool(ComponentPool&&) = delete;
		ComponentPool& operator=(ComponentPool&&) = delete;

		template<typename... Args>
		void* acquireComponent(Args... constructorArguments)
		{
			if (mNextFreeSlot == nullptr)
			{
				allocateNewChunk();
			}

			ComponentSlot* takenSlot = mNextFreeSlot;
			mNextFreeSlot = takenSlot->nextFreeSlot;

			return new (&takenSlot->component) ComponentType(std::forward<Args>(constructorArguments)...);
		}

		void releaseComponent(void* component)
		{
			// component is a union part of CompoenentSlot, so they have the same address in memory
			ComponentSlot* slot = static_cast<ComponentSlot*>(component);
			slot->component.~ComponentType();
			slot->nextFreeSlot = mNextFreeSlot;
			mNextFreeSlot = slot;
		}

	private:
		struct ComponentSlot {
			union {
				// free list implementation
				ComponentType component;
				ComponentSlot* nextFreeSlot;
			};

			ComponentSlot()
			{
			}

			~ComponentSlot()
			{
				// do nothing. assume that the component was released before destroing the pool
			}
		};

	private:
		size_t getNewChunkSize() const
		{
			if (mAllocatedComponentsCount == 0)
			{
				return mDefaultChunkSize;
			}
			else
			{
				if (mGrowStrategyFn)
				{
					return mGrowStrategyFn(mAllocatedComponentsCount);
				}
				else
				{
					return mAllocatedComponentsCount * 2;
				}
			}
		}

		void allocateNewChunk()
		{
			const size_t newChunkSize = getNewChunkSize();

			mChunks.push_back(new (std::nothrow) ComponentSlot[newChunkSize]);

			ComponentSlot* newChunk = mChunks.back();
			for (size_t i = 0; i < newChunkSize - 1; ++i)
			{
				newChunk[i].nextFreeSlot = &newChunk[i + 1];
			}

			newChunk[newChunkSize - 1].nextFreeSlot = mNextFreeSlot;
			mNextFreeSlot = &newChunk[0];

			mAllocatedComponentsCount += newChunkSize;
		}

	private:
		ComponentSlot* mNextFreeSlot = nullptr;
		std::vector<ComponentSlot*> mChunks;
		size_t mAllocatedComponentsCount = 0;
		const size_t mDefaultChunkSize;
		std::function<size_t(size_t)> mGrowStrategyFn;
	};

} // namespace RaccoonEcs
