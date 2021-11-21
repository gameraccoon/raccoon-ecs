#pragma once

#include <numeric>
#include <unordered_set>
#include <vector>
#include <string>

#include "error_handling.h"

namespace RaccoonEcs
{
	struct SystemDependencies
	{
		explicit SystemDependencies(int customOrder = -1)
			: customOrder(customOrder)
		{}

		template<typename... Systems>
		SystemDependencies&& goesAfter() &&
		{
			(systemsBefore.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& goesBefore() &&
		{
			(systemsAfter.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& canNotBeRunTogetherWith() &&
		{
			(incompatibleSystems.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& limitConcurrentlyRunSystemsTo(int systemsCount) &&
		{
			allowConcurrentSystemsCount = systemsCount;
			return std::move(*this);
		}

		std::vector<std::string> systemsBefore;
		std::vector<std::string> systemsAfter;
		std::vector<std::string> incompatibleSystems;
		int allowConcurrentSystemsCount = -1;
		int customOrder;
	};

	template<typename V, typename T>
	void pushBackUnique(V& inOutVector, T&& value)
	{
		if (std::find(inOutVector.begin(), inOutVector.end(), value) == inOutVector.end())
		{
			inOutVector.push_back(std::forward<T>(value));
		}
	}

	class DependencyGraph
	{
		friend class SystemDependencyTracer;

	public:
		void initNodes(size_t count)
		{
			mNodes.resize(count);
		}

		void addDependency(size_t beforeIdx, size_t afterIdx)
		{
			pushBackUnique(mNodes[afterIdx].nodesBefore, beforeIdx);
			pushBackUnique(mNodes[beforeIdx].nodesAfter, afterIdx);
		}

		void addIncompatibility(size_t firstSystemIdx, size_t secondSystemIdx)
		{
			if (firstSystemIdx < secondSystemIdx)
			{
				mIncompatibilities.insert(std::make_pair(firstSystemIdx, secondSystemIdx));
			}
			else
			{
				mIncompatibilities.insert(std::make_pair(secondSystemIdx, firstSystemIdx));
			}
		}

		void finalize()
		{
			for (size_t nodeIdx = 0; nodeIdx < mNodes.size(); ++nodeIdx)
			{
				Node& node = mNodes[nodeIdx];
				// calculate distanceToTheLast
				if (node.nodesAfter.empty())
				{
					std::vector<size_t> nextNodes;
					node.distanceToTheLast = 1;
					nextNodes.push_back(nodeIdx);

					while (!nextNodes.empty())
					{
						RACCOON_ECS_ASSERT(nextNodes.size() <= mNodes.size(), "Too deep dependency tree, probably circular system dependency");

						Node& currentNode = mNodes[nextNodes.back()];
						nextNodes.pop_back();

						for (size_t nodeBeforeIdx : currentNode.nodesBefore)
						{
							Node& nodeBefore = mNodes[nodeBeforeIdx];
							nodeBefore.distanceToTheLast = std::min(currentNode.distanceToTheLast + 1, nodeBefore.distanceToTheLast);
							nextNodes.push_back(nodeBeforeIdx);
						}
					}
				}

				// populate mFirstNodes
				if (mNodes[nodeIdx].nodesBefore.empty())
				{
					mFirstNodes.push_back(nodeIdx);
				}
			}
		}

		bool areSystemsCompatible(size_t firstSystemIdx, size_t secondSystemIdx) const
		{
			if (firstSystemIdx < secondSystemIdx)
			{
				return mIncompatibilities.find(std::make_pair(firstSystemIdx, secondSystemIdx)) == mIncompatibilities.end();
			}
			else
			{
				return mIncompatibilities.find(std::make_pair(secondSystemIdx, firstSystemIdx)) == mIncompatibilities.end();
			}
		}

	private:
		struct Node
		{
			std::vector<size_t> nodesBefore;
			// to faster trace the graph
			std::vector<size_t> nodesAfter;
			// how many other systems depend on it (including inderect dependencies)
			size_t distanceToTheLast = std::numeric_limits<size_t>::max();
		};

		struct IndexPairHash
		{
			std::size_t operator() (std::pair<size_t, size_t> const &pair) const
			{
				return pair.first ^ std::rotl(pair.second, 7);
			}
		};

		std::vector<Node> mNodes;
		std::vector<size_t> mFirstNodes;
		std::unordered_set<std::pair<size_t, size_t>, IndexPairHash> mIncompatibilities;
	};

	class SystemDependencyTracer
	{
	public:
		SystemDependencyTracer(const DependencyGraph& dependencyGraph)
			: mDependencyGraph(&dependencyGraph)
			, mResolvedDependencies(mDependencyGraph->mNodes.size(), false)
			, mNextSystems(mDependencyGraph->mFirstNodes)
		{
		}

		void finishSystem(size_t finishedSystem)
		{
			mActiveSystems.erase(
				std::remove(
					mActiveSystems.begin(),
					mActiveSystems.end(),
					finishedSystem),
				mActiveSystems.end());

			mResolvedDependencies[finishedSystem] = true;

			const DependencyGraph::Node& systemNode = mDependencyGraph->mNodes[finishedSystem];
			for (size_t nodeAfter : systemNode.nodesAfter)
			{
				pushBackUnique(mNextSystems, nodeAfter);
			}
		}

		std::vector<size_t> getNextSystemsToRun() const
		{
			std::vector<size_t> systemsToRun;

			if (!mNextSystems.empty())
			{
				// O(n*m)
				for (size_t nextSystem : mNextSystems)
				{
					if (canRunSystem(nextSystem))
					{
						systemsToRun.push_back(nextSystem);
					}
				}
			}

			filterIncompatibleSystems(systemsToRun);

			return systemsToRun;
		}

		void runSystem(size_t systemIdx)
		{
			mNextSystems.erase(
				std::remove(
					mNextSystems.begin(),
					mNextSystems.end(),
					systemIdx),
				mNextSystems.end()
			);

			mActiveSystems.push_back(systemIdx);
		}

		bool canRunSystem(size_t systemIdx) const
		{
			const std::vector<size_t>& nodesBefore = mDependencyGraph->mNodes[systemIdx].nodesBefore;

			for (size_t nodeBeforeIdx : nodesBefore)
			{
				if (mResolvedDependencies[nodeBeforeIdx] == false)
				{
					return false;
				}
			}

			for (size_t activeSystem : mActiveSystems)
			{
				if (!mDependencyGraph->areSystemsCompatible(systemIdx, activeSystem))
				{
					return false;
				}
			}

			return true;
		}

		void filterIncompatibleSystems(std::vector<size_t>& systems) const
		{
			// O(n^2)
			for (size_t i = 0; (i + 1) < systems.size(); ++i)
			{
				for (size_t j = i + 1; j < systems.size(); ++j)
				{
					if (!mDependencyGraph->areSystemsCompatible(systems[i], systems[j]))
					{
						// we prefer to keep systems that are more distant from the last
						if (mDependencyGraph->mNodes[systems[i]].distanceToTheLast
							<
							mDependencyGraph->mNodes[systems[j]].distanceToTheLast)
						{
							systems.erase(systems.begin() + i);
							--i;
							break;
						}
						else
						{
							std::swap(systems[j], systems[systems.size() - 1]);
							systems.resize(systems.size() - 1);
							--j;
						}
					}
				}
			}
		}

		bool hasNotRunSystems() const
		{
			return std::any_of(
				mResolvedDependencies.begin(),
				mResolvedDependencies.end(),
				[](bool isResolved){ return !isResolved; }
			);
		}

	private:
		const DependencyGraph* mDependencyGraph;
		std::vector<bool> mResolvedDependencies;
		std::vector<size_t> mActiveSystems;
		std::vector<size_t> mNextSystems;
	};
} // namespace RaccoonEcs
