#define CVKM_NO
#define CVKM_ENABLE_FLECS
#include <cvkm.h>
#include <flecs.h>
#include <funomenal.h>

static void Integrate3D(ecs_iter_t* it) {
  Position3D* positions = ecs_field(it, Position3D, 0);
  Velocity3D* velocities = ecs_field(it, Velocity3D, 1);
  Force3D* forces = ecs_field(it, Force3D, 2);
  const Mass* masses = ecs_field(it, Mass, 3);
  const Damping* dampings = ecs_field(it, Mass, 4);
  const GravityScale* gravity_scales = ecs_field(it, GravityScale, 5);
  const Gravity3D* gravity_ptr = ecs_field(it, Gravity3D, 6);

  const Gravity3D gravity = gravity_ptr ? *gravity_ptr : CVKM_VEC3_ZERO;

  for (int i = 0; i < it->count; i++) {
    Velocity3D* velocity = velocities + i;
    Force3D* accumulated_force = forces + i;

    vkm_muladd(velocity, it->delta_system_time, positions + i);

    vkm_vec3 resulting_acceleration = gravity;
    if (gravity_scales) {
      vkm_mul(&resulting_acceleration, gravity_scales[i], &resulting_acceleration);
    }
    vkm_muladd(accumulated_force, 1.0f / masses[i], &resulting_acceleration);

    vkm_muladd(&resulting_acceleration, it->delta_system_time, velocity);
        
    const float drag = dampings ? dampings[i] : 0.999f;
    vkm_mul(velocity, vkm_pow(drag, it->delta_system_time), velocity);

    *accumulated_force = CVKM_VEC3_ZERO;
  }
}

#ifndef _MSC_VER
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
#endif
#endif

void funomenalImport(ecs_world_t* world) {
  ECS_MODULE(world, funomenal);

  ECS_IMPORT(world, cvkm);

  ECS_SYSTEM(world, Integrate3D, EcsPreUpdate,
    [inout] cvkm.Position3D,
    [inout] cvkm.Velocity3D,
    [inout] cvkm.Force3D,
    [in] cvkm.Mass,
    [in] ?cvkm.Damping,
    [in] ?cvkm.GravityScale,
    [in] ?cvkm.Gravity3D($),
  );

  ecs_singleton_add(world, Gravity2D);
  ecs_singleton_add(world, Gravity3D);
  ecs_singleton_add(world, Gravity4D);
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
