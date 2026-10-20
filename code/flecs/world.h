/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "flecs/id.hh"
#include "flecs/lifecycle.hh"

// The EC layer as one class with one global object, the way LogicClass and
// MapClass are held. No flecs type appears in this header, so files that use
// the layer do not include flecs headers. The world registers no system,
// pipeline, phase, or observer; nothing advances it implicitly.
class FlecsWorldClass
{
	public:
		// Constant-initialized for globals so sentinels constructing before
		// _flecs.cpp keep their state, while stack and heap instances still
		// start cleanly zeroed.
		constexpr FlecsWorldClass(void) noexcept :
			World(nullptr),
			State(nullptr),
			LiveCount(0)
		{
		}

		~FlecsWorldClass(void);

		// Creates the entity for one game object and returns it, or zero when
		// the EC build is disabled.
		EntityId Attach_Entity(void * object, char const * classname);

		// Creates the child entity for one locomotor. Link_To_Object parents
		// it to its owner's entity; an unparented locomotor entity is valid.
		EntityId Attach_Locomotor_Entity(void * locomotion, char const * classname);

		// Destroys the entity the object already holds, if any, then attaches
		// a fresh one and returns it. Leaf constructors assign the result to
		// their Entity member, so placement reconstruction of a cell replaces
		// the old entity rather than leaking it.
		EntityId Replace_Entity(EntityId current, void * object, char const * classname);
		EntityId Replace_Locomotor_Entity(EntityId current, void * locomotion, char const * classname);

		// Destroys the entity, removing all of its components and pairs. Zero
		// and already-destroyed entities are accepted.
		void Destroy_Entity(EntityId entity);

		// Parents a child entity to a parent entity, or removes its parent
		// relationship when parent is zero. Zero or dead entities are accepted
		// and do nothing, so an owner without an entity is not an error.
		void Set_Entity_Parent(EntityId child, EntityId parent);
		EntityId Get_Entity_Parent(EntityId child) const;

		bool Entity_Is_Alive(EntityId entity) const;
		bool Entity_Has_Tag(EntityId entity, char const * classname) const;
		void * Get_Entity_Object(EntityId entity) const;

		// The number of live entities this world has created and not yet
		// destroyed.
		int Live_Entity_Count(void) const;

		// Connects the bridge to engine RTTI, ID, IsActive, and IsInLimbo
		// queries without coupling the standalone bridge target to game files.
		void Set_Object_Inspector(ObjectInspectorFn inspector);
		ObjectInspectorFn Get_Object_Inspector(void) const;

		// Registers a gameplay component type and its compile-time ComponentOps
		// hooks under a stable serialization key. Idempotent across repeats.
		template<typename T>
		void Register_Component(char const * name)
		{
			Register_Component_Raw(Component_Type_Key<T>(), name, Make_Component_Ops<T>());
		}

		bool Is_Component_Registered(char const * name) const;

		// Adds or overwrites component T on entity and returns its storage
		// pointer, or nullptr when the entity is dead or EC is disabled.
		template<typename T>
		T * Set_Component(EntityId entity, T value)
		{
			using CleanT = std::remove_cvref_t<T>;
			bool is_new = false;
			void * slot = Ensure_Component_Raw(
				entity,
				Component_Type_Key<CleanT>(),
				Make_Component_Ops<CleanT>(),
				is_new);
			if (slot == nullptr) {
				return(nullptr);
			}
			CleanT * typed = static_cast<CleanT *>(slot);
			*typed = std::move(value);
			Finalize_Component_Write_Raw(entity, Component_Type_Key<CleanT>(), slot, is_new);
			return(typed);
		}

		// Constructs component T in place from arguments and moves it into the
		// entity's slot. Supports non-copyable move-only components.
		template<typename T, typename... Args>
		T * Emplace_Component(EntityId entity, Args &&... args)
		{
			using CleanT = std::remove_cvref_t<T>;
			return(Set_Component<CleanT>(entity, CleanT(std::forward<Args>(args)...)));
		}

		template<typename T>
		T * Get_Component(EntityId entity)
		{
			using CleanT = std::remove_cvref_t<T>;
			return(static_cast<CleanT *>(Get_Component_Raw(entity, Component_Type_Key<CleanT>())));
		}

		template<typename T>
		T const * Try_Get_Component(EntityId entity) const
		{
			using CleanT = std::remove_cvref_t<T>;
			return(static_cast<CleanT const *>(Try_Get_Component_Raw(entity, Component_Type_Key<CleanT>())));
		}

		template<typename T>
		bool Has_Component(EntityId entity) const
		{
			using CleanT = std::remove_cvref_t<T>;
			return(Has_Component_Raw(entity, Component_Type_Key<CleanT>()));
		}

		template<typename T>
		void Remove_Component(EntityId entity)
		{
			using CleanT = std::remove_cvref_t<T>;
			Remove_Component_Raw(entity, Component_Type_Key<CleanT>());
		}

		// Stages On_Ready calls during scenario loading so components run only
		// after rules, houses, cells, and initial objects exist together.
		void Begin_Scenario_Load(void);
		bool Is_Scenario_Loading(void) const;
		void Flush_Scenario_Ready(void);

		// Dispatches map enter and exit notifications for components on entity.
		// Exit runs in reverse component registration order.
		void Notify_Enter_World(EntityId entity);
		void Notify_Exit_World(EntityId entity);

		// Executes one deterministic simulation phase inside a deferred scope,
		// ordered by owner (Kind, FetchId, AttachSequence).
		void Run_Sim_Phase(SimPhase phase);

		// Executes the read-only foreground presentation pass. Never mutates
		// component state or touches simulation RNG.
		void Render_Foreground(void * surface = nullptr) const;

		// Notifies components carrying target pointers that target is leaving
		// play or being destroyed.
		void Detach_Target(AbstractClass const * target, bool all = true);

		// Folds deterministic component state into the lockstep CRC engine in
		// canonical (registration, Kind, FetchId, AttachSequence) order.
		void Fold_CRCs(CRCEngine & crc) const;

		// Saves or loads all serializable components on entity using a
		// length-framed directory record compatible with SaveStreamClass.
		template<ByteSaveStream S>
		bool Serialize_Entity(EntityId entity, S & stream)
		{
			return(Serialize_Entity_Raw(entity, Make_Stream_Adapter(stream)));
		}

		// Runs Post_Load_Resolve across all restored components once object
		// loading and pointer swizzling have completed.
		void Post_Load_Resolve_All(void);

		// Destroys every entity this world created. Scenario teardown already
		// deletes every game object, which destroys its entity; this is for
		// tests and for checking that teardown left nothing behind.
		void Reset(void);

		// Destroys the flecs world itself. Called from the game's exit path;
		// attach recreates the world on demand, so a global destroyed after
		// shutdown cannot crash on it.
		void Shutdown(void);

	private:
		struct BridgeState;
		struct BridgeInternals;

		// Creates internal state and the flecs world on demand because global
		// sentinels such as CellClass BlubCell attach before main starts.
		BridgeState & Ensure_State(void);
		void Ensure_World(void);

		EntityId Attach(void * object, char const * classname, bool locomotion);
		void Register_Tag(char const * classname);

		void Register_Component_Raw(ComponentTypeKey key, char const * name, ComponentOps const & ops);
		void * Ensure_Component_Raw(EntityId entity, ComponentTypeKey key, ComponentOps const & ops, bool & is_new);
		void Finalize_Component_Write_Raw(EntityId entity, ComponentTypeKey key, void * slot, bool is_new);
		void * Get_Component_Raw(EntityId entity, ComponentTypeKey key);
		void const * Try_Get_Component_Raw(EntityId entity, ComponentTypeKey key) const;
		bool Has_Component_Raw(EntityId entity, ComponentTypeKey key) const;
		void Remove_Component_Raw(EntityId entity, ComponentTypeKey key);
		bool Serialize_Entity_Raw(EntityId entity, ComponentStreamAdapter const & io);
		void Drain_Pending_Ready(void);

		// Defined in register.cpp, which owns the class list, so this list
		// and the rollout table in the adoption plan are checked against
		// each other by the entityinventory test.
		void Register_Tags(void);

	private:
		// Held as opaque pointers initialized to zero in static storage so a
		// global constructor calling Attach_Entity before FlecsWorld's own
		// dynamic constructor runs never loses state.
		void * World;
		BridgeState * State;

		int LiveCount;
};

extern FlecsWorldClass FlecsWorld;
