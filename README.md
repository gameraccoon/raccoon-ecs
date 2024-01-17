
## Overview

RaccoonEcs is a simple ECS implementation that supports basic entity/component manipulation and aimed to be extendable by the user.

This implementation doesn't commit into achieving exceptional performance, though some work is done not to make it too bad (object pool to reduce allocations, indexes to iterate over only relevant entities).

Benchmarks: https://github.com/gameraccoon/raccoon-ecs-bench/

Tests: https://github.com/gameraccoon/raccoon-ecs-tests/

## Features

- **Header-only**: Add to your project, include, and start using it. No additional set-up steps are required.
- **Minimal requirements to components**: In the base implementation they need to be default-constructible and have a static `GetTypeId` method.
- **Support for separation of storages for entities**: Useful for world partition, time rewinding, level streaming, 'singleton' components, etc.
- **Opt-in copyable storages for entities**: In case you want to dynamically copy your worlds, e.g. for time rewinding.
- **No requirements for systems**: You can use `SystemsManager` or can run systems from your code directly, useful if you want to run your systems in parallel.
- **Custom types for IDs**: Want to store component IDs as enum values? int? string? You are covered!

## Example usage

Here's a simple example to get you started:

```cpp
#include "raccoon-ecs/entity_manager.h"
#include "raccoon-ecs/systems_manager.h"

#include <iostream>

// for this example we use an enum to identify components
enum ComponentType {
	PositionComponentType
};

// some useful type aliases
using ComponentFactory = RaccoonEcs::ComponentFactoryImpl<ComponentType>;
using EntityManager = RaccoonEcs::EntityManagerImpl<ComponentType>;
using Entity = RaccoonEcs::Entity;

// define a component
struct Position {
	float x, y;

	// this method is required to use the component with the entity manager
	static ComponentType GetTypeId() { return PositionComponentType; };
};

// define a system that processes entities with the Position component
class MovementSystem : public RaccoonEcs::System {
public:
	explicit MovementSystem(EntityManager& entityManager)
		: entityManager(entityManager) {
	}

	void update() override {
		// iterate over entities with the Position component
		entityManager.forEachComponentSetWithEntity<Position>([](Entity entity, Position* position) {
			// update the position (for demonstration purposes)
			position->x += 1.0f;
			position->y += 0.5f;

			// print the updated position
			std::cout << "Entity " << entity.getId() << " - Position: (" << position->x << ", " << position->y << ")\n";
		});
	}

private:
	EntityManager& entityManager;
};

int main() {
	// component factory allocate space for components and creates them
	ComponentFactory componentFactory;
	// entity manager manages entities and components, there can be multiple entity managers
	EntityManager entityManager{componentFactory};

	// register components
	componentFactory.registerComponent<Position>();

	// create an entity and add the Position component
	const Entity newEntity = entityManager.addEntity();
	Position* position = entityManager.addComponent<Position>(newEntity);
	position->x = 100.0f;
	position->y = 200.0f;

	// create a system manager and register the MovementSystem
	// you can skip this and run your systems from your code
	// (e.g. if you want to pass some arguments to the update method)
	RaccoonEcs::SystemsManager systemsManager;
	systemsManager.registerSystem<MovementSystem>(entityManager);

	// update the systems
	systemsManager.update();
}

```
