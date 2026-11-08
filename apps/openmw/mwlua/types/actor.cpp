#include "types.hpp"

#include <algorithm>
#include <array>

#include <components/detournavigator/agentbounds.hpp>
#include <components/lua/luastate.hpp>
#include <components/misc/finitevalues.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/mechanicsmanager.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwmechanics/actorutil.hpp"
#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/drawstate.hpp"
#include "apps/openmw/mwrender/dismember.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include "../localscripts.hpp"
#include "../luamanagerimp.hpp"
#include "../magicbindings.hpp"
#include "../stats.hpp"

namespace MWLua
{
    using EquipmentItem = std::variant<std::string, ObjectId>;
    using Equipment = std::map<int, EquipmentItem>;
    static constexpr int sAnySlot = -1;

    static std::pair<MWWorld::ContainerStoreIterator, bool> findInInventory(
        MWWorld::ContainerStore& store, const EquipmentItem& item, int slot, bool isInventoryStore)
    {
        auto oldIt = slot != sAnySlot && isInventoryStore ? static_cast<MWWorld::InventoryStore&>(store).getSlot(slot)
                                                          : store.end();
        MWWorld::Ptr itemPtr;

        if (std::holds_alternative<ObjectId>(item))
        {
            itemPtr = MWBase::Environment::get().getWorldModel()->getPtr(std::get<ObjectId>(item));
            if (oldIt != store.end() && *oldIt == itemPtr)
                return { oldIt, true }; // already equipped
            if (itemPtr.isEmpty() || itemPtr.getCellRef().getCount() == 0 || itemPtr.getContainerStore() != &store)
            {
                Log(Debug::Warning) << "Object" << std::get<ObjectId>(item).toString() << " is not in inventory";
                return { store.end(), false };
            }
        }
        else
        {
            const auto& stringId = std::get<std::string>(item);
            ESM::RefId recordId = ESM::RefId::deserializeText(stringId);
            if (oldIt != store.end() && oldIt->getCellRef().getRefId() == recordId)
                return { oldIt, true }; // already equipped
            itemPtr = store.search(recordId);
            if (itemPtr.isEmpty() || itemPtr.getCellRef().getCount() == 0)
            {
                Log(Debug::Warning) << "There is no object with recordId='" << stringId << "' in inventory";
                return { store.end(), false };
            }
        }

        // TODO: Refactor InventoryStore to accept Ptr and get rid of this linear search.
        MWWorld::ContainerStoreIterator it = std::find(store.begin(), store.end(), itemPtr);
        if (it == store.end()) // should never happen
            throw std::logic_error("Item not found in container");

        return { it, false };
    }

    static void setEquipment(const MWWorld::Ptr& actor, const Equipment& equipment)
    {
        bool isPlayer = actor == MWBase::Environment::get().getWorld()->getPlayerPtr();
        MWWorld::InventoryStore& store = actor.getClass().getInventoryStore(actor);
        std::array<bool, MWWorld::InventoryStore::Slots> usedSlots;
        std::fill(usedSlots.begin(), usedSlots.end(), false);

        auto tryEquipToSlot = [&store, &usedSlots, isPlayer](int slot, const EquipmentItem& item) -> bool {
            auto [it, alreadyEquipped] = findInInventory(store, item, slot, true);
            if (alreadyEquipped)
                return true;
            if (it == store.end())
                return false;
            MWWorld::Ptr itemPtr = *it;

            auto [allowedSlots, _] = itemPtr.getClass().getEquipmentSlots(itemPtr);
            bool requestedSlotIsAllowed
                = std::find(allowedSlots.begin(), allowedSlots.end(), slot) != allowedSlots.end();
            if (!requestedSlotIsAllowed)
            {
                auto firstAllowed
                    = std::find_if(allowedSlots.begin(), allowedSlots.end(), [&](int s) { return !usedSlots[s]; });
                if (firstAllowed == allowedSlots.end())
                {
                    Log(Debug::Warning) << "No suitable slot for " << itemPtr.toString();
                    return false;
                }
                slot = *firstAllowed;
            }

            bool skipEquip = false;

            if (isPlayer)
            {
                const ESM::RefId& script = itemPtr.getClass().getScript(itemPtr);
                if (!script.empty())
                {
                    MWScript::Locals& locals = itemPtr.getRefData().getLocals();
                    locals.setVarByInt(script, "onpcequip", 1);
                    skipEquip = locals.getIntVar(script, "pcskipequip") == 1;
                }
            }

            if (!skipEquip)
                store.equip(slot, it);

            return requestedSlotIsAllowed; // return true if equipped to requested slot and false if slot was changed
        };

        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            auto oldIt = store.getSlot(slot);
            auto newIt = equipment.find(slot);
            if (newIt == equipment.end())
            {
                if (oldIt != store.end())
                    store.unequipSlot(slot);
                continue;
            }
            if (tryEquipToSlot(slot, newIt->second))
                usedSlots[slot] = true;
        }
        for (const auto& [slot, item] : equipment)
            if (slot >= MWWorld::InventoryStore::Slots)
                tryEquipToSlot(sAnySlot, item);
    }

    static void setSelectedEnchantedItem(const MWWorld::Ptr& actor, const EquipmentItem& item)
    {
        MWWorld::ContainerStore& contStore = actor.getClass().getContainerStore(actor);
        // We're not passing in a specific slot, so ignore the already equipped return value
        auto [it, _] = findInInventory(contStore, item, sAnySlot, actor.getClass().hasInventoryStore(actor));
        if (it == contStore.end())
            return;
        if (!actor.getClass().hasInventoryStore(actor))
        {
            contStore.setSelectedEnchantItem(it);
            return;
        }

        MWWorld::InventoryStore& store = actor.getClass().getInventoryStore(actor);
        MWWorld::Ptr itemPtr = *it;

        // Equip the item if applicable
        auto slots = itemPtr.getClass().getEquipmentSlots(itemPtr);
        if (!slots.first.empty())
        {
            bool alreadyEquipped = false;
            for (auto slot : slots.first)
            {
                if (store.getSlot(slot) == it)
                    alreadyEquipped = true;
            }

            if (!alreadyEquipped)
            {
                MWBase::Environment::get().getWindowManager()->useItem(itemPtr);
                // make sure that item was successfully equipped
                if (!store.isEquipped(itemPtr))
                    return;
            }
        }
        store.setSelectedEnchantItem(it);
        // to reset WindowManager::mSelectedSpell immediately
        MWBase::Environment::get().getWindowManager()->setSelectedEnchantItem(*it);
    }

    void addActorBindings(sol::table actor, const Context& context)
    {
        sol::state_view lua = context.sol();
        actor["STANCE"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, MWMechanics::DrawState>(lua,
                {
                    { "Nothing", MWMechanics::DrawState::Nothing },
                    { "Weapon", MWMechanics::DrawState::Weapon },
                    { "Spell", MWMechanics::DrawState::Spell },
                }));
        actor["EQUIPMENT_SLOT"] = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, int>(lua,
            { { "Helmet", MWWorld::InventoryStore::Slot_Helmet }, { "Cuirass", MWWorld::InventoryStore::Slot_Cuirass },
                { "Greaves", MWWorld::InventoryStore::Slot_Greaves },
                { "LeftPauldron", MWWorld::InventoryStore::Slot_LeftPauldron },
                { "RightPauldron", MWWorld::InventoryStore::Slot_RightPauldron },
                { "LeftGauntlet", MWWorld::InventoryStore::Slot_LeftGauntlet },
                { "RightGauntlet", MWWorld::InventoryStore::Slot_RightGauntlet },
                { "Boots", MWWorld::InventoryStore::Slot_Boots }, { "Shirt", MWWorld::InventoryStore::Slot_Shirt },
                { "Pants", MWWorld::InventoryStore::Slot_Pants }, { "Skirt", MWWorld::InventoryStore::Slot_Skirt },
                { "Robe", MWWorld::InventoryStore::Slot_Robe }, { "LeftRing", MWWorld::InventoryStore::Slot_LeftRing },
                { "RightRing", MWWorld::InventoryStore::Slot_RightRing },
                { "Amulet", MWWorld::InventoryStore::Slot_Amulet }, { "Belt", MWWorld::InventoryStore::Slot_Belt },
                { "CarriedRight", MWWorld::InventoryStore::Slot_CarriedRight },
                { "CarriedLeft", MWWorld::InventoryStore::Slot_CarriedLeft },
                { "Ammunition", MWWorld::InventoryStore::Slot_Ammunition } }));

        actor["getStance"] = [](const Object& o) {
            const MWWorld::Class& cls = o.ptr().getClass();
            if (cls.isActor())
                return cls.getCreatureStats(o.ptr()).getDrawState();
            else
                throw std::runtime_error("Actor expected");
        };
        actor["stance"] = actor["getStance"]; // for compatibility; should be removed later
        actor["setStance"] = [](const SelfObject& self, int stance) {
            const MWWorld::Class& cls = self.ptr().getClass();
            if (!cls.isActor())
                throw std::runtime_error("Actor expected");
            auto& stats = cls.getCreatureStats(self.ptr());
            if (stance != static_cast<int>(MWMechanics::DrawState::Nothing)
                && stance != static_cast<int>(MWMechanics::DrawState::Weapon)
                && stance != static_cast<int>(MWMechanics::DrawState::Spell))
            {
                throw std::runtime_error("Incorrect stance");
            }
            MWMechanics::DrawState newDrawState = static_cast<MWMechanics::DrawState>(stance);
            if (stats.getDrawState() == newDrawState)
                return;
            if (newDrawState == MWMechanics::DrawState::Spell)
            {
                bool hasSelectedSpell;
                if (self.ptr() == MWBase::Environment::get().getWorld()->getPlayerPtr())
                    // For the player selecting spell in UI doesn't change selected spell in CreatureStats (was
                    // implemented this way to prevent changing spell during casting, probably should be refactored), so
                    // we have to handle the player separately.
                    hasSelectedSpell = !MWBase::Environment::get().getWindowManager()->getSelectedSpell().empty();
                else
                    hasSelectedSpell = !stats.getSpells().getSelectedSpell().empty();
                if (!hasSelectedSpell)
                {
                    const MWWorld::ContainerStore& store = cls.getContainerStore(self.ptr());
                    if (store.getSelectedEnchantItem() == store.end())
                        return; // No selected spell and no selected enchanted item; can't use magic stance.
                }
            }
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            // We want to interrupt animation only if attack is preparing, but still is not triggered.
            // Otherwise we will get a "speedshooting" exploit, when player can skip reload animation by hitting "Toggle
            // Weapon" key twice.
            if (mechanics->isAttackPreparing(self.ptr()))
                stats.setAttackingOrSpell(false); // interrupt attack
            else if (mechanics->isAttackingOrSpell(self.ptr()))
                return; // can't be interrupted; ignore setStance
            stats.setDrawState(newDrawState);
        };

        actor["getSelectedEnchantedItem"] = [](sol::this_state thisState, const Object& o) -> sol::object {
            const MWWorld::Ptr& ptr = o.ptr();
            if (!ptr.getClass().isActor())
                return sol::nil;
            MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
            auto it = store.getSelectedEnchantItem();
            if (it == store.end())
                return sol::nil;
            MWBase::Environment::get().getWorldModel()->registerPtr(*it);
            if (o.isGObject())
                return sol::make_object(thisState, GObject(*it));
            else
                return sol::make_object(thisState, LObject(*it));
        };
        actor["setSelectedEnchantedItem"] = [context](const SelfObject& obj, const sol::object& item) {
            const MWWorld::Ptr& ptr = obj.ptr();
            if (!ptr.getClass().isActor())
                return;

            EquipmentItem ei;
            if (item.is<Object>())
            {
                ei = LuaUtil::cast<Object>(item).id();
            }
            else
            {
                ei = LuaUtil::cast<std::string>(item);
            }
            context.mLuaManager->addAction(
                [obj = Object(ptr), ei = std::move(ei)] { setSelectedEnchantedItem(obj.ptr(), ei); },
                "setSelectedEnchantedItemAction");
        };

        actor["canMove"] = [](const Object& o) {
            const MWWorld::Class& cls = o.ptr().getClass();
            return cls.getMaxSpeed(o.ptr()) > 0;
        };
        actor["getRunSpeed"] = [](const Object& o) {
            const MWWorld::Class& cls = o.ptr().getClass();
            return cls.getRunSpeed(o.ptr());
        };
        actor["getWalkSpeed"] = [](const Object& o) {
            const MWWorld::Class& cls = o.ptr().getClass();
            return cls.getWalkSpeed(o.ptr());
        };
        actor["getCurrentSpeed"] = [](const Object& o) {
            const MWWorld::Class& cls = o.ptr().getClass();
            return cls.getCurrentSpeed(o.ptr());
        };

        // for compatibility; should be removed later
        actor["runSpeed"] = actor["getRunSpeed"];
        actor["walkSpeed"] = actor["getWalkSpeed"];
        actor["currentSpeed"] = actor["getCurrentSpeed"];

        actor["isOnGround"]
            = [](const LObject& o) { return MWBase::Environment::get().getWorld()->isOnGround(o.ptr()); };
        actor["isSwimming"]
            = [](const LObject& o) { return MWBase::Environment::get().getWorld()->isSwimming(o.ptr()); };

        actor["inventory"] = sol::overload([](const LObject& o) { return Inventory<LObject>{ o }; },
            [](const GObject& o) { return Inventory<GObject>{ o }; });
        auto getAllEquipment = [](sol::this_state thisState, const Object& o) {
            const MWWorld::Ptr& ptr = o.ptr();
            sol::table equipment(thisState, sol::create);
            if (!ptr.getClass().hasInventoryStore(ptr))
                return equipment;

            MWWorld::InventoryStore& store = ptr.getClass().getInventoryStore(ptr);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                auto it = store.getSlot(slot);
                if (it == store.end())
                    continue;
                MWBase::Environment::get().getWorldModel()->registerPtr(*it);
                if (o.isGObject())
                    equipment[slot] = sol::make_object(thisState, GObject(*it));
                else
                    equipment[slot] = sol::make_object(thisState, LObject(*it));
            }
            return equipment;
        };
        auto getEquipmentFromSlot = [](sol::this_state thisState, const Object& o, int slot) -> sol::object {
            const MWWorld::Ptr& ptr = o.ptr();
            if (!ptr.getClass().hasInventoryStore(ptr))
                return sol::nil;
            MWWorld::InventoryStore& store = ptr.getClass().getInventoryStore(ptr);
            auto it = store.getSlot(slot);
            if (it == store.end())
                return sol::nil;
            MWBase::Environment::get().getWorldModel()->registerPtr(*it);
            if (o.isGObject())
                return sol::make_object(thisState, GObject(*it));
            else
                return sol::make_object(thisState, LObject(*it));
        };
        actor["getEquipment"] = sol::overload(getAllEquipment, getEquipmentFromSlot);
        actor["equipment"] = actor["getEquipment"]; // for compatibility; should be removed later
        actor["hasEquipped"] = [](const Object& o, const Object& item) {
            const MWWorld::Ptr& ptr = o.ptr();
            if (!ptr.getClass().hasInventoryStore(ptr))
                return false;
            MWWorld::InventoryStore& store = ptr.getClass().getInventoryStore(ptr);
            return store.isEquipped(item.ptr());
        };
        actor["setEquipment"] = [context](const SelfObject& obj, const sol::table& equipment) {
            const MWWorld::Ptr& ptr = obj.ptr();
            if (!ptr.getClass().hasInventoryStore(ptr))
            {
                if (!equipment.empty())
                    throw std::runtime_error(obj.toString() + " has no equipment slots");
                return;
            }
            Equipment eqp;
            for (auto& [key, value] : equipment)
            {
                int slot = LuaUtil::cast<int>(key);
                if (value.is<Object>())
                    eqp[slot] = LuaUtil::cast<Object>(value).id();
                else
                    eqp[slot] = LuaUtil::cast<std::string>(value);
            }
            context.mLuaManager->addAction(
                [obj = Object(ptr), eqp = std::move(eqp)] { setEquipment(obj.ptr(), eqp); }, "SetEquipmentAction");
        };
        actor["getPathfindingAgentBounds"] = [](sol::this_state thisState, const LObject& o) {
            const DetourNavigator::AgentBounds agentBounds
                = MWBase::Environment::get().getWorld()->getPathfindingAgentBounds(o.ptr());
            sol::table result(thisState, sol::create);
            result["shapeType"] = agentBounds.mShapeType;
            result["halfExtents"] = agentBounds.mHalfExtents;
            return result;
        };
        actor["isInActorsProcessingRange"] = [](const Object& o) {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            const auto& target = o.ptr();
            if (target == player)
                return true;

            if (!target.getClass().isActor())
                throw std::runtime_error("Actor expected");

            if (target.getCell()->getCell()->getWorldSpace() != player.getCell()->getCell()->getWorldSpace())
                return false;

            const int actorsProcessingRange = Settings::game().mActorsProcessingRange;
            const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

            const float dist = (playerPos - target.getRefData().getPosition().asVec3()).length2();
            return dist <= (actorsProcessingRange * actorsProcessingRange);
        };

        actor["isDead"] = [](const Object& o) {
            const auto& target = o.ptr();
            return target.getClass().getCreatureStats(target).isDead();
        };

        actor["isDeathFinished"] = [](const Object& o) {
            const auto& target = o.ptr();
            return target.getClass().getCreatureStats(target).isDeathAnimationFinished();
        };

        actor["getEncumbrance"] = [](const Object& object) -> float {
            const MWWorld::Ptr ptr = object.ptr();
            return ptr.getClass().getEncumbrance(ptr);
        };

        actor["getCapacity"] = [](const Object& object) -> float {
            const MWWorld::Ptr ptr = object.ptr();
            return ptr.getClass().getCapacity(ptr);
        };

        actor["getBarterGold"] = [](const Object& object) -> int {
            const MWWorld::Ptr ptr = object.ptr();
            return ptr.getClass().getCreatureStats(ptr).getGoldPool();
        };

        actor["setBarterGold"] = [context](const Object& object, int gold) {
            if (gold < 0)
                throw std::runtime_error("Barter gold must be positive");
            if (!object.isGObject() && !object.isSelfObject())
                throw std::runtime_error("Can only be used in global scripts or in local scripts on self.");
            context.mLuaManager->addAction(
                [obj = Object(object), gold] {
                    const MWWorld::Ptr ptr = obj.ptr();
                    ptr.getClass().getCreatureStats(ptr).setGoldPool(gold);
                },
                "SetBarterGoldAction");
        };

        actor["_onHit"] = [context](const SelfObject& self, const sol::table& options) {
            sol::optional<sol::table> damageLua = options.get<sol::optional<sol::table>>("damage");
            std::map<std::string, float> damageCpp;
            if (damageLua)
            {
                for (auto& [key, value] : damageLua.value())
                {
                    damageCpp[key.as<std::string>()] = value.as<Misc::FiniteFloat>();
                }
            }
            std::string sourceTypeStr = options.get_or<std::string>("sourceType", "unspecified");
            MWMechanics::DamageSourceType sourceType = MWMechanics::DamageSourceType::Unspecified;
            if (sourceTypeStr == "melee")
                sourceType = MWMechanics::DamageSourceType::Melee;
            else if (sourceTypeStr == "ranged")
                sourceType = MWMechanics::DamageSourceType::Ranged;
            else if (sourceTypeStr == "magic")
                sourceType = MWMechanics::DamageSourceType::Magical;
            std::string_view ammoId = options.get_or<std::string_view>("ammo", {});
            ESM::RefId weaponId = ESM::RefId::deserializeText(ammoId);
            if (weaponId.empty())
            {
                if (sol::optional<Object> weapon = options.get<sol::optional<Object>>("weapon"))
                {
                    MWWorld::Ptr weaponPtr = weapon->ptrOrEmpty();
                    if (!weaponPtr.isEmpty())
                        weaponId = weaponPtr.getCellRef().getRefId();
                }
            }

            // NerevarMoon: critical and blocked come from Npc::hit / Creature::hit through the Hit event
            const bool critical = options.get_or("critical", false);
            const bool blocked = options.get_or("blocked", false);
            context.mLuaManager->addAction(
                [context, self = Object(self), damages = std::move(damageCpp),
                    attacker = options.get<sol::optional<Object>>("attacker"), weaponId,
                    successful = options.get<bool>("successful"), sourceType = sourceType,
                    sourceTypeStr = std::move(sourceTypeStr), critical, blocked] {
                    MWWorld::Ptr attackerPtr;
                    MWWorld::Ptr weaponPtr;
                    if (attacker)
                        attackerPtr = attacker->ptrOrEmpty();
                    const MWWorld::Ptr victim = self.ptr();
                    MWMechanics::CreatureStats& stats = victim.getClass().getCreatureStats(victim);
                    const float healthBefore = stats.getHealth().getCurrent();
                    const bool wasDead = stats.isDead();
                    const bool wasDown = stats.getKnockedDown();
                    victim.getClass().onHit(victim, damages, weaponId, attackerPtr, successful, sourceType);

                    // NerevarMoon: NMHitResult, the outcome after the engine applied the hit (same frame as the
                    // health change), to the victim and to the attacker
                    const float damage = std::max(healthBefore - stats.getHealth().getCurrent(), 0.f);
                    const bool killed = !wasDead && stats.isDead();
                    const bool knockdown = !wasDown && stats.getKnockedDown();
                    context.mLua->protectedCall([&](LuaUtil::LuaView& view) {
                        sol::table result = view.newTable();
                        result["victim"] = LObject(victim);
                        if (!attackerPtr.isEmpty())
                            result["attacker"] = LObject(attackerPtr);
                        result["damage"] = damage;
                        result["killed"] = killed;
                        result["blocked"] = blocked;
                        result["critical"] = critical;
                        result["knockdown"] = knockdown;
                        result["successful"] = successful;
                        result["sourceType"] = sourceTypeStr;
                        if (!weaponId.empty())
                            result["weapon"] = weaponId.serializeText();
                        context.mLuaManager->sendLocalEvent(victim, "NMHitResult", result);
                        if (!attackerPtr.isEmpty() && attackerPtr != victim
                            && attackerPtr.getRefData().getLuaScripts())
                            context.mLuaManager->sendLocalEvent(attackerPtr, "NMHitResult", result);
                    });
                },
                "HitAction");
        };
        actor["hitResult"] = true; // NerevarMoon marker: the NMHitResult event exists
        actor["spellHit"] = true; // NerevarMoon marker: the NMSpellHit event exists

        // NerevarMoon: knockdown state and trigger
        actor["isKnockedDown"] = [](const Object& o) {
            const MWWorld::Ptr& ptr = o.ptr();
            return ptr.getClass().getCreatureStats(ptr).getKnockedDown();
        };
        actor["isKnockedOut"] = [](const Object& o) {
            const MWMechanics::CreatureStats& stats = o.ptr().getClass().getCreatureStats(o.ptr());
            // same rule as CharacterController: fatigue below zero, or a zero fatigue base
            return stats.getKnockedDown()
                && (stats.getFatigue().getCurrent() < 0 || stats.getFatigue().getBase() == 0);
        };
        // NerevarMoon: the body is a ragdoll (dead, or knocked down and not yet getting up)
        // NerevarMoon dismemberment
        actor["sever"] = [context](const Object& o, std::string_view piece, sol::optional<osg::Vec3f> impulse,
                             sol::optional<sol::table> options) {
            if (!MWRender::parsePiece(piece))
                throw std::runtime_error("Unknown piece: " + std::string(piece));
            const MWWorld::Ptr& ptr = o.ptr();
            if (!ptr.getClass().isActor() || ptr == MWBase::Environment::get().getWorld()->getPlayerPtr())
                return false;
            const bool crushed = options && options->get_or("crushed", false);
            const bool silent = options && options->get_or("silent", false);
            context.mLuaManager->addAction(
                [obj = Object(o), name = std::string(piece), push = impulse.value_or(osg::Vec3f()), crushed, silent] {
                    const MWWorld::Ptr target = obj.ptrOrEmpty();
                    if (!target.isEmpty())
                        MWBase::Environment::get().getWorld()->sever(target, name, push, crushed, silent);
                },
                "SeverAction");
            return true;
        };
        actor["isSevered"] = [](const Object& o, std::string_view piece) {
            return MWBase::Environment::get().getWorld()->isSevered(o.ptr(), piece);
        };
        actor["setLowerBodyLocked"] = [context](const Object& o, bool locked) {
            context.mLuaManager->addAction(
                [obj = Object(o), locked] {
                    const MWWorld::Ptr target = obj.ptrOrEmpty();
                    if (!target.isEmpty())
                        MWBase::Environment::get().getWorld()->setLowerBodyLocked(target, locked);
                },
                "LowerBodyLockAction");
        };
        actor["isRagdoll"] = [](const Object& o) {
            const MWWorld::Ptr& ptr = o.ptr();
            return ptr.getClass().isActor() && MWBase::Environment::get().getWorld()->isRagdoll(ptr);
        };
        actor["knockDown"] = [context](const Object& o) {
            context.mLuaManager->addAction(
                [obj = Object(o)] {
                    const MWWorld::Ptr ptr = obj.ptrOrEmpty();
                    if (ptr.isEmpty())
                        return;
                    MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                    if (!stats.isDead() && !stats.getKnockedDown())
                        stats.setKnockedDown(true);
                },
                "KnockDownAction");
        };

        // NerevarMoon: per-actor time scale in [0, 2] for animation, turning and movement speed; not saved
        actor["getTimeScale"] = [](const Object& o) {
            return o.ptr().getClass().getCreatureStats(o.ptr()).getTimeScale();
        };
        actor["setTimeScale"] = [context](const Object& o, Misc::FiniteFloat scale) {
            const float value = std::clamp(static_cast<float>(scale), 0.f, 2.f);
            context.mLuaManager->addAction(
                [obj = Object(o), value] {
                    const MWWorld::Ptr ptr = obj.ptrOrEmpty();
                    if (!ptr.isEmpty())
                        ptr.getClass().getCreatureStats(ptr).setTimeScale(value);
                },
                "SetTimeScaleAction");
        };

        addActorStatsBindings(actor, context);
        addActorMagicBindings(actor, context);
    }
}
