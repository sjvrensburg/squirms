// Weapon firing, projectile behaviour and explosions.
#include "game.h"
#include "../physics/world.h"
#include "../terrain/grid.h"
#include "../terrain/terrain.h"
#include "../audio/sfx.h"
#include <algorithm>
#include <cmath>

using Entities::Worm;
using Entities::Projectile;
using Entities::ProjectileType;
using Entities::Prop;
using Entities::PropType;

namespace {

float frand() { return (float)GetRandomValue(0, 10000) / 10000.0f; }

struct ShellSpec {
    float radius, damage;
    bool impact, wind;
    float maxSpeed;   // px/s at full power
};

ShellSpec specFor(ProjectileType t) {
    switch (t) {
        case ProjectileType::Bazooka:    return {50, 50, true, true, 950};
        case ProjectileType::Grenade:    return {50, 50, false, false, 720};
        case ProjectileType::Cluster:    return {32, 25, false, false, 720};
        case ProjectileType::Clusterlet: return {26, 20, true, false, 0};
        case ProjectileType::Banana:     return {62, 70, false, false, 720};
        case ProjectileType::Bananalet:  return {55, 55, true, false, 0};
        case ProjectileType::Holy:       return {115, 100, false, false, 620};
        case ProjectileType::Dynamite:   return {82, 75, false, false, 0};
        case ProjectileType::AirMissile: return {42, 30, true, true, 0};
        case ProjectileType::Sheep:      return {78, 75, false, false, 0};
    }
    return {40, 40, true, false, 600};
}

// Box2D's accumulated contact list, filtered to things a shell can hit.
bool touchingSolid(World* world, void* body) {
    std::vector<World::BodyContact> contacts;
    world->getContacts(body, contacts);
    for (auto& c : contacts) {
        auto k = world->getBodyKind(c.body);
        if (k == World::BodyKind::Terrain || k == World::BodyKind::Worm || k == World::BodyKind::Prop) return true;
    }
    return false;
}

} // namespace

void Game::launch(ProjectileType type, Vector2 from, Vector2 vel, float fuseSeconds) {
    ShellSpec s = specFor(type);
    Projectile p;
    p.radius = s.radius;
    p.damage = s.damage;
    p.impact = s.impact;
    p.windAffected = s.wind;
    p.fuse = fuseSeconds;
    p.ownerBody = current ? current->body : nullptr;
    p.facing = current ? current->facing : 1;
    p.launch(world, type, from.x, from.y, vel.x, vel.y);
    if (p.alive) projectiles.push_back(p);
}

void Game::fire() {
    if (!current) return;
    Team& team = teams[currentTeam];
    const WeaponInfo& info = weaponInfo(weapon);
    Vector2 pos = current->pixelPos();
    Vector2 dir = aimDir(current);
    Vector2 muzzle{pos.x + dir.x * 16, pos.y + dir.y * 16};

    switch (weapon) {
        case WeaponId::Bazooka:
        case WeaponId::Grenade:
        case WeaponId::ClusterBomb:
        case WeaponId::BananaBomb:
        case WeaponId::HolyGrenade: {
            ProjectileType t = weapon == WeaponId::Bazooka ? ProjectileType::Bazooka
                             : weapon == WeaponId::Grenade ? ProjectileType::Grenade
                             : weapon == WeaponId::ClusterBomb ? ProjectileType::Cluster
                             : weapon == WeaponId::BananaBomb ? ProjectileType::Banana
                                                              : ProjectileType::Holy;
            float speed = 60.0f + power * specFor(t).maxSpeed;
            float f = (t == ProjectileType::Bazooka) ? 0 : (t == ProjectileType::Holy ? 3.0f : (float)fuse);
            launch(t, muzzle, {dir.x * speed, dir.y * speed}, f);
            Audio::playSound(t == ProjectileType::Bazooka ? "fire" : "throw");
            break;
        }
        case WeaponId::Dynamite:
            launch(ProjectileType::Dynamite, {pos.x + current->facing * 8.0f, pos.y}, {current->facing * 30.0f, -40.0f}, 5.0f);
            Audio::playSound("fuse", 0.6f);
            break;
        case WeaponId::Sheep:
            launch(ProjectileType::Sheep, {pos.x + current->facing * 12.0f, pos.y - 4}, {current->facing * 60.0f, -120.0f}, 12.0f);
            Audio::playSound("baa");
            break;
        case WeaponId::Mine: {
            Prop m;
            m.spawn(world, PropType::Mine, pos.x + current->facing * 14.0f, pos.y);
            m.armTimer = 2.0f;
            props.push_back(m);
            Audio::playSound("beep", 0.6f);
            break;
        }
        case WeaponId::Shotgun:
            shotgunBlast();
            break;
        case WeaponId::BaseballBat:
            batSwing();
            break;
        case WeaponId::SkipGo:
            say(current->name + " skips a go", WHITE, 1.5f);
            fired = true;
            shotsLeft = 0;
            endTurn();
            return;
        default:
            return;
    }

    if (team.ammo[(int)weapon] > 0) team.ammo[(int)weapon]--;
    fired = true;
    shotsLeft--;
    power = 0;
    camera.manual = false;
    if (shotsLeft > 0) say(TextFormat("%d shot%s left", shotsLeft, shotsLeft == 1 ? "" : "s"), WHITE, 1.2f);
    (void)info;
}

void Game::fireAt(Vector2 target) {
    if (!current) return;
    Team& team = teams[currentTeam];
    if (weapon == WeaponId::AirStrike) {
        // Five missiles swoop in from the side the worm is facing away from.
        planeDir = current->facing;
        planeX = target.x - planeDir * 900.0f;
        planeY = std::max(-300.0f, camera.screenToWorld({0, 60}).y);
        for (int i = 0; i < 5; i++) {
            float x = target.x - planeDir * (170.0f - i * 34.0f) + (i - 2) * 6.0f;
            launch(ProjectileType::AirMissile, {x, -60.0f - i * 14.0f}, {planeDir * 150.0f, 160.0f}, 0);
        }
        Audio::playSound("airstrike");
    } else if (weapon == WeaponId::Teleport) {
        if (!teleport(target)) {
            say("Can't teleport there!", WHITE, 1.2f);
            Audio::playSound("select", 0.6f, 0.5f);
            return;
        }
        if (team.ammo[(int)weapon] > 0) team.ammo[(int)weapon]--;
        fired = true;
        shotsLeft = 0;
        endTurn();  // teleporting uses up the turn
        return;
    } else {
        return;
    }
    if (team.ammo[(int)weapon] > 0) team.ammo[(int)weapon]--;
    fired = true;
    shotsLeft--;
    camera.manual = false;
}

bool Game::teleport(Vector2 target) {
    // Needs open air for the whole worm, above the sea and inside the map.
    if (target.x < 20 || target.x > Terrain::WORLD_PX_W - 20 || target.y < -200 || target.y > Terrain::WATER_Y - 30)
        return false;
    // No standing terrain chunk (in place or landed rubble) may overlap the
    // worm's 19x29 px capsule.
    for (auto& row : terrain->chunks) {
        for (auto* c : row) {
            if (c->state != Terrain::ChunkState::SOLID) continue;
            if (fabsf(c->centroidX - target.x) < 16 && fabsf(c->centroidY - target.y) < 22) return false;
        }
    }
    Vector2 from = current->pixelPos();
    fx.sparkle(from.x, from.y, Color{140, 220, 255, 255}, 26);
    current->teleportTo(target.x, target.y + 14);
    fx.sparkle(target.x, target.y, Color{140, 220, 255, 255}, 26);
    Audio::playSound("teleport");
    return true;
}

void Game::shotgunBlast() {
    Vector2 pos = current->pixelPos();
    Vector2 dir = aimDir(current);
    const float range = 900.0f;
    b2Vec2 a(pos.x / Terrain::PPM, Terrain::WORLD_H - pos.y / Terrain::PPM);
    b2Vec2 b((pos.x + dir.x * range) / Terrain::PPM, Terrain::WORLD_H - (pos.y + dir.y * range) / Terrain::PPM);
    World::RayCastResult hit;
    bool any = world->RayCast(a, b, hit, current->body,
                              World::CAT_TERRAIN | World::CAT_WORM | World::CAT_PROP);
    Vector2 end = any ? Vector2{hit.point.x * Terrain::PPM, (Terrain::WORLD_H - hit.point.y) * Terrain::PPM}
                      : Vector2{pos.x + dir.x * range, pos.y + dir.y * range};
    // Tracer.
    float len = sqrtf((end.x - pos.x) * (end.x - pos.x) + (end.y - pos.y) * (end.y - pos.y));
    for (float d = 14; d < len; d += 18) fx.trail(pos.x + dir.x * d, pos.y + dir.y * d, false);
    fx.sparkle(pos.x + dir.x * 16, pos.y + dir.y * 16, Color{255, 230, 150, 255}, 6);
    Audio::playSound("shotgun");
    if (!any) return;

    // A direct hit on a worm hurts more and shoves harder than the blast.
    if (world->getBodyKind(hit.body) == World::BodyKind::Worm) {
        for (auto* w : worms) {
            if (w->body == hit.body) w->hit(25, dir.x * 5.0f, -dir.y * 5.0f + 2.0f);
        }
        Audio::playSound("hurt");
    }
    explode(end.x, end.y, 20, 8);
}

void Game::batSwing() {
    Vector2 pos = current->pixelPos();
    Vector2 dir = aimDir(current);
    Vector2 sweet{pos.x + dir.x * 20, pos.y + dir.y * 20};
    bool connected = false;
    for (auto* w : worms) {
        if (w == current || !w->alive || w->drowned) continue;
        Vector2 p = w->pixelPos();
        float dx = p.x - sweet.x, dy = p.y - sweet.y;
        if (dx * dx + dy * dy < 30 * 30) {
            w->hit(30, dir.x * 16.0f, -dir.y * 16.0f + 3.0f);
            connected = true;
        }
    }
    for (auto& p : props) {
        if (!p.alive || !p.body) continue;
        float dx = p.pos.x - sweet.x, dy = p.pos.y - sweet.y;
        if (dx * dx + dy * dy < 30 * 30) {
            world->setBodyVelocity(p.body, dir.x * 14.0f, -dir.y * 14.0f + 3.0f);
            connected = true;
        }
    }
    Audio::playSound(connected ? "bat" : "throw");
    if (connected) fx.banner(sweet.x, sweet.y - 30, "THWACK!", Color{255, 230, 60, 255}, 30, 1.2f);
}

// ---------------------------------------------------------------------------

void Game::updateProjectiles(float dt) {
    for (auto& p : projectiles) {
        if (!p.alive || !p.body) continue;
        p.age += dt;
        Vector2 bp = world->getBodyPosition(p.body);
        Vector2 bv = world->getBodyVelocity(p.body);
        Vector2 newVel{bv.x * Terrain::PPM, -bv.y * Terrain::PPM};
        p.pos = {bp.x * Terrain::PPM, (Terrain::WORLD_H - bp.y) * Terrain::PPM};

        // Bounce clack: a sharp change in velocity means we hit something.
        float dvx = newVel.x - p.vel.x, dvy = newVel.y - p.vel.y;
        if (!p.impact && p.type != ProjectileType::Sheep && dvx * dvx + dvy * dvy > 90 * 90 &&
            p.age - p.lastBounceSound > 0.08f) {
            p.lastBounceSound = p.age;
            Audio::playSound("bounce", std::min(1.0f, sqrtf(dvx * dvx + dvy * dvy) / 400.0f), 0.9f + frand() * 0.2f);
        }
        p.vel = newVel;

        switch (p.type) {
            case ProjectileType::Bazooka:
            case ProjectileType::AirMissile:
                p.angle = atan2f(p.vel.y, p.vel.x);
                if (GetRandomValue(0, 1) == 0) fx.trail(p.pos.x - cosf(p.angle) * 8, p.pos.y - sinf(p.angle) * 8, true);
                break;
            case ProjectileType::Sheep:
                break;
            default:
                p.angle = -world->getBodyAngle(p.body);
                break;
        }

        if (p.windAffected && wind != 0) {
            // m/s^2 of sideways push at full wind.
            const float windAccel = 4.0f;
            world->applyLinearImpulse(p.body, wind * windAccel * world->getBodyMass(p.body) * dt, 0);
        }

        if (p.type == ProjectileType::Sheep) updateSheep(p, dt);
        if (p.type == ProjectileType::Dynamite && GetRandomValue(0, 2) == 0) {
            fx.sparkle(p.pos.x + 4, p.pos.y - 10, Color{255, 220, 120, 255}, 1);
        }

        // Into the sea: fizzle out with a splash.
        if (p.pos.y > Terrain::WATER_Y + 4) {
            fx.splash(p.pos.x, Terrain::WATER_Y, 0.6f);
            Audio::playSound("splash", 0.6f, 1.2f);
            if (p.type == ProjectileType::Sheep) Audio::playSound("baa", 0.6f, 0.8f);
            p.destroy(world);
            continue;
        }
        if (p.pos.x < -400 || p.pos.x > Terrain::WORLD_PX_W + 400) {
            p.destroy(world);
            continue;
        }

        if (p.fuse > 0) {
            float before = p.fuse;
            p.fuse -= dt;
            if (p.type == ProjectileType::Holy && before > 1.9f && p.fuse <= 1.9f) Audio::playSound("holy");
            if (p.fuse <= 0) p.detonateNow = true;
        }
        if (p.impact && p.age > 0.04f && touchingSolid(world, p.body)) p.detonateNow = true;

        if (p.detonateNow) detonate(p);
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
                                     [](const Projectile& p) { return !p.alive; }),
                      projectiles.end());
    for (auto& k : newShells) projectiles.push_back(k);
    newShells.clear();
}

void Game::updateSheep(Projectile& p, float dt) {
    // Waddle along in its facing direction, hopping now and then and over
    // obstacles, turning round when well and truly stuck.
    b2Vec2 pos(p.pos.x / Terrain::PPM, Terrain::WORLD_H - p.pos.y / Terrain::PPM);
    World::RayCastResult hit;
    bool grounded = world->RayCast(pos, pos + b2Vec2(0, -0.42f), hit, static_cast<b2Body*>(p.body), World::CAT_TERRAIN);
    world->setAngularVelocity(p.body, 0);
    world->setBodyTransform(p.body, pos.x, pos.y, 0);
    p.hopTimer -= dt;
    if (grounded) {
        Vector2 v = world->getBodyVelocity(p.body);
        bool blocked = world->RayCast(pos, pos + b2Vec2(p.facing * 0.45f, 0), hit, static_cast<b2Body*>(p.body), World::CAT_TERRAIN);
        if (blocked && p.hopTimer < 0.6f) {
            world->setBodyVelocity(p.body, p.facing * 2.0f, 6.2f);
            p.hopTimer = 0.9f;
            // Hopped at this wall already and still stuck: turn round.
            if (fabsf(v.x) < 0.2f && p.age > 1.0f && GetRandomValue(0, 2) == 0) p.facing = -p.facing;
        } else if (p.hopTimer <= 0) {
            world->setBodyVelocity(p.body, p.facing * 3.0f, 4.6f);
            p.hopTimer = 0.8f + frand() * 0.9f;
            if (GetRandomValue(0, 3) == 0) Audio::playSound("baa", 0.5f, 0.9f + frand() * 0.3f);
        } else {
            world->setBodyVelocity(p.body, p.facing * 3.0f, v.y);
        }
    }
}

void Game::detonate(Projectile& p) {
    Vector2 at = p.pos;
    ProjectileType type = p.type;
    float radius = p.radius, damage = p.damage;
    p.destroy(world);
    explode(at.x, at.y, radius, damage);

    if (type == ProjectileType::Holy) {
        fx.banner(at.x, at.y - radius - 20, "Hallelujah!", Color{255, 240, 140, 255}, 30, 2.0f);
    }
    if (type == ProjectileType::Sheep) fx.feathers(at.x, at.y);

    // Cluster weapons scatter their bomblets upwards.
    int kids = 0;
    ProjectileType kidType = ProjectileType::Clusterlet;
    if (type == ProjectileType::Cluster) { kids = 5; kidType = ProjectileType::Clusterlet; }
    if (type == ProjectileType::Banana) { kids = 5; kidType = ProjectileType::Bananalet; }
    for (int i = 0; i < kids; i++) {
        float a = -PI / 2 + (i - (kids - 1) / 2.0f) * 0.32f + (frand() - 0.5f) * 0.25f;
        float sp = (kidType == ProjectileType::Bananalet ? 330.0f : 260.0f) * (0.8f + frand() * 0.4f);
        Projectile k;
        ShellSpec s = specFor(kidType);
        k.radius = s.radius;
        k.damage = s.damage;
        k.impact = s.impact;
        k.launch(world, kidType, at.x, at.y - 6, cosf(a) * sp, sinf(a) * sp);
        k.age = -0.15f;  // brief grace so they clear the crater they came from
        if (k.alive) newShells.push_back(k);
    }
}

// ---------------------------------------------------------------------------

void Game::explode(float x, float y, float radius, float damage) {
    fx.explosion(x, y, radius, &terrainGfx);
    if (radius >= 40 && (radius >= 60 || GetRandomValue(0, 2) > 0)) fx.comicWord(x, y, radius);
    Audio::playSound(radius >= 40 ? "explode" : "explode_small", std::min(1.0f, 0.5f + radius / 100.0f),
                     (radius >= 90 ? 0.8f : 1.0f) * (0.92f + frand() * 0.16f));

    // Terrain: blow out every chunk whose centre is inside the crater. Rock
    // is tougher, so it only goes near the middle.
    bool destroyed = false;
    for (auto& row : terrain->chunks) {
        for (auto* c : row) {
            if (c->state != Terrain::ChunkState::SOLID || c->material == Terrain::Material::Bedrock) continue;
            float reach = radius * (c->material == Terrain::Material::Rock ? 0.8f : 1.0f);
            float dx = c->centroidX - x, dy = c->centroidY - y;
            if (dx * dx + dy * dy < reach * reach) {
                if (terrain->damageChunk(c, 1e9f)) destroyed = true;
            }
        }
    }
    // Ejecta: some chunks just outside the crater are torn loose and flung
    // out as real tumbling pieces of terrain (they land as rubble).
    std::vector<Terrain::Chunk*> ring;
    for (auto& row : terrain->chunks) {
        for (auto* c : row) {
            if (c->state != Terrain::ChunkState::SOLID || c->material == Terrain::Material::Bedrock) continue;
            float reach = radius * (c->material == Terrain::Material::Rock ? 0.8f : 1.0f);
            float dx = c->centroidX - x, dy = c->centroidY - y, d2 = dx * dx + dy * dy;
            if (d2 >= reach * reach && d2 < reach * reach * 2.1f) ring.push_back(c);
        }
    }
    for (int i = (int)ring.size() - 1; i > 0; i--) std::swap(ring[i], ring[GetRandomValue(0, i)]);
    int maxEjecta = 3 + (int)(radius / 14), ejected = 0;
    std::vector<void*> ejecta;
    for (auto* c : ring) {
        if (ejected >= maxEjecta) break;
        if (c->state != Terrain::ChunkState::SOLID) continue;
        float chance = c->material == Terrain::Material::Rock ? 0.12f : 0.4f;
        if (frand() > chance) continue;
        std::vector<Terrain::Chunk*> group{c};
        if (!c->landed) {
            const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& d : dirs) {
                int nx = c->col + d[0], ny = c->row + d[1];
                if (nx < 0 || nx >= Terrain::COLS || ny < 0 || ny >= Terrain::ROWS || group.size() >= 3) continue;
                Terrain::Chunk* n = terrain->chunks[ny][nx];
                if (n->state == Terrain::ChunkState::SOLID && !n->landed && n->material == Terrain::Material::Dirt &&
                    GetRandomValue(0, 1)) group.push_back(n);
            }
        }
        void* b = terrain->breakOff(group);
        if (!b) continue;
        float dx = c->centroidX - x, dy = c->centroidY - y, d = std::max(1.0f, sqrtf(dx * dx + dy * dy));
        float speed = (3.5f + frand() * 4.5f) * std::clamp(damage / 50.0f, 0.6f, 1.8f);
        world->setBodyVelocity(b, dx / d * speed, -dy / d * speed + 3.0f + frand() * 2.0f);
        world->setAngularVelocity(b, (frand() - 0.5f) * 14.0f);
        ejecta.push_back(b);
        ejected++;
    }

    // Rubble nearby is shaken loose, and anything that lost its support
    // (grid terrain or rubble resting on what just vanished) starts to fall.
    terrain->shakeLoose(x, y, radius * 1.5f);
    terrain->recomputeSupportAndThaw();
    (void)destroyed;

    // Shove falling terrain caught in the blast (ejecta already have their
    // launch velocity).
    for (void* body : terrain->dynamicBodies) {
        if (std::find(ejecta.begin(), ejecta.end(), body) != ejecta.end()) continue;
        Vector2 bp = world->getBodyPosition(body);
        float px = bp.x * Terrain::PPM, py = (Terrain::WORLD_H - bp.y) * Terrain::PPM;
        float dx = px - x, dy = py - y, d = sqrtf(dx * dx + dy * dy);
        if (d < radius * 1.6f && d > 1) {
            float k = (1 - d / (radius * 1.6f)) * world->getBodyMass(body) * 3.0f;
            world->applyLinearImpulse(body, dx / d * k, -dy / d * k + k * 0.3f);
        }
    }

    // Worms: damage falls off from the centre; knockback scales with the
    // weapon's punch and flings them up and away.
    bool anyHurt = false;
    for (auto* w : worms) {
        if (!w->alive || w->drowned) continue;
        Vector2 p = w->pixelPos();
        float dx = p.x - x, dy = p.y - y;
        float d = std::max(0.0f, sqrtf(dx * dx + dy * dy) - 10.0f);
        float reach = radius * 1.1f;
        if (d >= reach) continue;
        float fall = 1.0f - d / reach;
        float dmg = std::round(std::max(1.0f, damage * fall));
        float len = sqrtf(dx * dx + dy * dy);
        float nx = len > 1 ? dx / len : 0, ny = len > 1 ? dy / len : -1;
        ny -= 0.7f;   // bias upwards
        float nl = sqrtf(nx * nx + ny * ny);
        nx /= nl; ny /= nl;
        float kick = (2.5f + damage * 0.16f) * (0.35f + 0.65f * fall);
        w->hit(w->dying ? 0 : dmg, nx * kick, -ny * kick);
        anyHurt = true;
    }
    if (anyHurt) Audio::playSound("hurt", 0.8f, 0.9f + frand() * 0.25f);

    // Props: barrels go up in chains, mines trip, crates break.
    for (auto& p : props) {
        if (!p.alive || !p.body) continue;
        float dx = p.pos.x - x, dy = p.pos.y - y, d = sqrtf(dx * dx + dy * dy);
        float reach = radius * 1.2f;
        if (d >= reach) continue;
        float fall = 1.0f - d / reach;
        float nx = d > 1 ? dx / d : 0, ny = d > 1 ? dy / d : -1;
        float kick = (3.0f + damage * 0.14f) * fall;
        Vector2 v = world->getBodyVelocity(p.body);
        world->setBodyVelocity(p.body, v.x + nx * kick, v.y - ny * kick + kick * 0.5f);
        switch (p.type) {
            case PropType::Barrel:
                p.hp -= damage * fall;
                if (p.hp <= 0 && p.fuse < 0) p.fuse = 0.15f + frand() * 0.25f;
                break;
            case PropType::Mine:
                if (p.fuse < 0) p.fuse = 0.4f + frand() * 0.6f;
                break;
            case PropType::WeaponCrate:
                if (p.fuse < 0) p.fuse = 0.05f;
                break;
            case PropType::HealthCrate:
                fx.sparkle(p.pos.x, p.pos.y, Color{255, 120, 140, 255}, 12);
                p.destroy(world);
                break;
            case PropType::Grave:
                break;
        }
    }

    // Shells in flight (grenades, dynamite, the sheep) get shoved too.
    for (auto& p : projectiles) {
        if (!p.alive || !p.body) continue;
        float dx = p.pos.x - x, dy = p.pos.y - y, d = sqrtf(dx * dx + dy * dy);
        if (d >= radius * 1.2f || d < 1) continue;
        float kick = (1 - d / (radius * 1.2f)) * 6.0f;
        Vector2 v = world->getBodyVelocity(p.body);
        world->setBodyVelocity(p.body, v.x + dx / d * kick, v.y - dy / d * kick + kick * 0.3f);
    }
}

// Falling terrain: dust, rumble and shake where slabs land, and damage to
// worms caught underneath.
void Game::terrainAftermath() {
    for (auto& im : terrain->impacts) {
        float s = im.strength;
        if (s < 4) continue;
        Color dust = terrainGfx.sample(im.x, im.y);
        fx.dust(im.x, im.y, dust, std::min(14, 2 + (int)(s / 5)));
        if (im.shattered) fx.crumble(im.x, im.y, dust, std::min(24, 6 + (int)(s / 4)));
        if (s > 15) fx.addShake(std::min(14.0f, s * 0.12f));
        if (s > 30) Audio::playSound("explode_small", std::min(0.8f, s / 90.0f), 0.45f + frand() * 0.1f);
        else Audio::playSound("land", std::min(1.0f, s / 20.0f), 0.7f + frand() * 0.3f);
    }
    terrain->impacts.clear();

    for (auto* w : worms) {
        if (!w->alive || w->drowned || w->crushCooldown > 0 || !w->body) continue;
        std::vector<World::BodyContact> contacts;
        world->getContacts(w->body, contacts);
        for (auto& c : contacts) {
            if (!terrain->isDynamicBody(c.body)) continue;
            Vector2 v = world->getBodyVelocity(c.body);
            Vector2 wv = w->getVelocity();
            float rx = v.x - wv.x, ry = v.y - wv.y, rel = sqrtf(rx * rx + ry * ry);
            if (rel > 4.5f && v.y < -1.0f) {
                float dmg = std::min(35.0f, std::round((rel - 4.0f) * 6.0f));
                w->hit(dmg, v.x * 0.4f, v.y * 0.3f);
                w->crushCooldown = 0.7f;
                Audio::playSound("hurt");
                Vector2 p = w->pixelPos();
                fx.banner(p.x, p.y - 30, "SQUASH!", Color{255, 220, 120, 255}, 22, 1.2f);
                break;
            }
        }
    }
}
