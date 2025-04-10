#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define CVKM_NO
#define CVKM_ENABLE_FLECS
#define CVKM_FLECS_IMPLEMENTATION
#include <cvkm.h>
#include <flecs.h>
#include <funomenal.h>
#include <glitch.h>

#define TARGET_FPS 60

typedef float Lifetime;
typedef float Lifespan;
typedef float Size;

typedef struct Orbiter {
  float height, distance, speed;
} Orbiter;

typedef struct firework_phase_t {
  Color* colors;
  vkm_vec3 min_velocity, max_velocity;
  float min_lifespan, max_lifespan, min_size, max_size;
  uint16_t colors_count, min_particles, max_particles;
} firework_phase_t;

typedef struct Firework {
  firework_phase_t* phases;
  uint32_t phases_count;
} Firework;

typedef struct FireworkParticle {
  uint16_t phase;
} FireworkParticle;

typedef int32_t ShouldFadeAway;

static ECS_COMPONENT_DECLARE(Lifetime);
static ECS_COMPONENT_DECLARE(Lifespan);
static ECS_COMPONENT_DECLARE(Size);
static ECS_COMPONENT_DECLARE(Orbiter);
static ECS_TAG_DECLARE(LookingAt);
static ECS_COMPONENT_DECLARE(Firework);
static ECS_COMPONENT_DECLARE(FireworkParticle);
static ECS_COMPONENT_DECLARE(ShouldFadeAway);

ECS_CTOR(Lifetime, ptr, {
  *ptr = 0.0f;
})

ECS_CTOR(Lifespan, ptr, {
  *ptr = INFINITY;
})

ECS_CTOR(Size, ptr, {
  *ptr = 1.0f;
})

ECS_CTOR(Orbiter, ptr, {
  *ptr = (Orbiter){
    .height = 3.0f,
    .distance = 20.0f,
    .speed = 0.25f,
  };
})

ECS_CTOR(Firework, ptr, {
  *ptr = (Firework){ 0 };
})

ECS_DTOR(Firework, ptr, {
  if (ptr->phases) {
    for (unsigned j = 0; j < ptr->phases_count; j++) {
      free(ptr->phases[j].colors);
    }
  }
  free(ptr->phases);
})

ECS_CTOR(FireworkParticle, ptr, {
  *ptr = (FireworkParticle){ 0 };
})

ECS_CTOR(ShouldFadeAway, ptr, {
  *ptr = 1;
})

static float random_float(const float min, const float max) {
  return (float)rand() / (float)RAND_MAX * (max - min) + min;
}

static int random_int(const int min, const int max) {
  return rand() % (max - min) + min;
}

static void Age(ecs_iter_t* it) {
  Lifetime* lifetimes = ecs_field(it, Lifetime, 0);
  const Lifespan* lifespans = ecs_field(it, Lifespan, 1);

  for (int i = 0; i < it->count; i++) {
    Lifetime* lifetime = lifetimes + i;

    *lifetime += it->delta_time;
    if (lifespans && *lifetime >= lifespans[i]) {
      ecs_delete(it->world, it->entities[i]);
    }
  }
}

static void Orbit(ecs_iter_t* it) {
  Position3D* positions = ecs_field(it, Position3D, 0);
  Rotation3D* rotations = ecs_field(it, Rotation3D, 1);
  const Orbiter* orbiters = ecs_field(it, Orbiter, 2);

  const bool target_is_self = ecs_field_id(it, 3) == ecs_pair(LookingAt, ecs_id(Position3D));
  const Position3D* targets = ecs_field(it, Position3D, target_is_self ? 3 : 4);

  assert(ecs_field_is_self(it, 3));
  assert(!ecs_field_is_self(it, 4));

  for (int i = 0; i < it->count; i++) {
    const Orbiter* orbiter = orbiters + i;
    const Position3D* target = targets + (target_is_self ? i : 0);

    const float time = (float)ecs_get_world_info(it->world)->world_time_total * orbiter->speed;
    positions[i] = (Position3D){ {
      vkm_sin(time) * orbiter->distance,
      target->y + orbiter->height,
      vkm_cos(time) * orbiter->distance,
    } };

    vkm_vec3 direction;
    vkm_sub(target, positions + i, &direction);
    vkm_normalize(&direction, &direction);
    vkm_quat_look_at(&direction, &CVKM_VEC3_UP, rotations + i);
  }
}

static ecs_entity_t particle_shader_program, particle_mesh;

static void spawn_firework_particles(
  ecs_world_t* world,
  const ecs_entity_t parent,
  const Position3D* position,
  const firework_phase_t* phase,
  const uint16_t phase_index,
  const bool last_phase
) {
  const int count = random_int(phase->min_particles, phase->max_particles + 1);
  for (int i = 0; i < count; i++) {
    Position3D position_copy = *position;
    ecs_entity(world, {
      .parent = parent,
      .add = ecs_ids(
        ecs_id(Force3D),
        ecs_pair(ecs_id(Uses), particle_shader_program),
        ecs_pair(ecs_id(Uses), particle_mesh)
      ),
      .set = ecs_values(
        { .type = ecs_id(FireworkParticle), .ptr = &(FireworkParticle){ .phase = phase_index } },
        { .type = ecs_id(Position3D), .ptr = &position_copy },
        { .type = ecs_id(Velocity3D), .ptr = &(Velocity3D){ {
          random_float(phase->min_velocity.x, phase->max_velocity.x),
          random_float(phase->min_velocity.y, phase->max_velocity.y),
          random_float(phase->min_velocity.z, phase->max_velocity.z),
        } } },
        { .type = ecs_id(Mass), .ptr = &(Mass){ 0.01f } },
        { .type = ecs_id(Color), .ptr = phase->colors + rand() % phase->colors_count },
        { .type = ecs_id(Lifespan), .ptr = &(Lifespan){ random_float(phase->min_lifespan, phase->max_lifespan) } },
        { .type = ecs_id(Size), .ptr = &(Size){ random_float(phase->min_size, phase->max_size) } },
        { .type = ecs_id(ShouldFadeAway), .ptr = &(ShouldFadeAway){ last_phase } }
      ),
    });
  }
}

static void OnAddFirework(ecs_iter_t* it) {
  const Firework* fireworks = ecs_field(it, Firework, 0);
  const Position3D* positions = ecs_field(it, Position3D, 1);

  for (int i = 0; i < it->count; i++) {
    const Firework* firework = fireworks + i;
    if (!firework->phases) {
      continue;
    }

    spawn_firework_particles(
      it->world,
      it->entities[i],
      positions + i,
      firework->phases,
      0,
      firework->phases_count == 1
    );
  }
}

static void OnRemoveFireworkParticle(ecs_iter_t* it) {
  const FireworkParticle* firework_particles = ecs_field(it, FireworkParticle, 0);
  const Position3D* positions = ecs_field(it, Position3D, 1);
  const Firework* firework = ecs_field(it, Firework, 2);

  for (int i = 0; i < it->count; i++) {
    const FireworkParticle* firework_particle = firework_particles + i;

    assert(firework_particle->phase < firework->phases_count);
    if (firework_particle->phase < firework->phases_count - 1) {
      const uint16_t new_phase_index = firework_particle->phase + 1;
      spawn_firework_particles(
        it->world,
        it->sources[2],
        positions + i,
        firework->phases + new_phase_index,
        new_phase_index,
        new_phase_index == firework->phases_count - 1
      );
    }
  }
}

static void SpawnFirework(ecs_iter_t* it) {
  Firework firework = {
    .phases = malloc(3 * sizeof(firework.phases[0])),
    .phases_count = 3,
  };

  firework.phases[0] = (firework_phase_t){
    .colors = malloc(sizeof(firework.phases->colors[0])),
    .min_velocity = (vkm_vec3){ { -1.0f, 15.0f, -1.0f } },
    .max_velocity = (vkm_vec3){ {  1.0f, 25.0f,  1.0f } },
    .min_lifespan = 0.6f,
    .max_lifespan = 1.2f,
    .min_size = 0.25f,
    .max_size = 0.5f,
    .colors_count = 1,
    .min_particles = 1,
    .max_particles = 1,
  };
  firework.phases[0].colors[0] = (Color){ { 1.0f, 0.0f, 0.0f, 1.0f } };

  firework.phases[1] = (firework_phase_t){
    .colors = malloc(3 * sizeof(firework.phases->colors[0])),
    .min_velocity = (vkm_vec3){ { -10.0f,   1.0f, -10.0f } },
    .max_velocity = (vkm_vec3){ {  10.0f,  10.0f,  10.0f } },
    .min_lifespan = 0.5f,
    .max_lifespan = 1.0f,
    .min_size = 0.15f,
    .max_size = 0.30f,
    .colors_count = 2,
    .min_particles = 5,
    .max_particles = 10,
  };
  firework.phases[1].colors[0] = (Color){ { 0.0f, 1.0f, 0.0f, 1.0f } };
  firework.phases[1].colors[1] = (Color){ { 0.0f, 1.0f, 1.0f, 1.0f } };
  firework.phases[1].colors[2] = (Color){ { 1.0f, 1.0f, 0.0f, 1.0f } };

  firework.phases[2] = (firework_phase_t){
    .colors = malloc(5 * sizeof(firework.phases->colors[0])),
    .min_velocity = (vkm_vec3){ { -5.0f, -2.0f, -5.0f } },
    .max_velocity = (vkm_vec3){ {  5.0f,  5.0f,  5.0f } },
    .min_lifespan = 0.5f,
    .max_lifespan = 1.0f,
    .min_size = 0.1f,
    .max_size = 0.2f,
    .colors_count = 5,
    .min_particles = 15,
    .max_particles = 20,
  };
  firework.phases[2].colors[0] = (Color){ { 1.0f, 0.0f, 0.0f, 1.0f } };
  firework.phases[2].colors[1] = (Color){ { 0.0f, 1.0f, 0.0f, 1.0f } };
  firework.phases[2].colors[2] = (Color){ { 0.0f, 0.0f, 1.0f, 1.0f } };
  firework.phases[2].colors[3] = (Color){ { 1.0f, 0.0f, 1.0f, 1.0f } };
  firework.phases[2].colors[4] = (Color){ { 1.0f, 1.0f, 1.0f, 1.0f } };

  float lifespan_sum = 0.0f;
  for (unsigned i = 0; i < firework.phases_count; i++) {
    lifespan_sum += firework.phases[i].max_lifespan;
  }

  static int count = 0;
  char buffer[22];
  sprintf(buffer, "Firework #%d", ++count);

  ecs_entity(it->world, {
    .name = buffer,
    .set = ecs_values(
      { .type = ecs_id(Firework), .ptr = &firework },
      { .type = ecs_id(Position3D), .ptr = &(Position3D){ {
        random_float(-5.0f, 5.0f),
        -5.0f,
        random_float(-5.0f, 5.0f),
      } } },
      { .type = ecs_id(Lifespan), .ptr = &lifespan_sum }
    ),
  });
}

#ifdef GLI_EMSCRIPTEN
static bool emscripten_main_loop(const double time, void* world) {
  static double last_time = 0.0;
  const double delta_time = last_time <= 0.0 ? 1.0 / TARGET_FPS : (time - last_time) / 1000.0;
  last_time = time;

  const bool quit = !ecs_progress(world, (ecs_ftime_t)delta_time);
  if (quit) {
    ecs_fini(world);
    return false;
  }

  return true;
}
#endif

#ifndef _MSC_VER
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
#endif
#endif

int main(const int argc, char** argv) {
  ecs_world_t* world = ecs_init_w_args(argc, argv);

  ECS_IMPORT(world, funomenal);
  ECS_IMPORT(world, glitch);

  ECS_COMPONENT_DEFINE(world, Lifetime);
  ecs_primitive(world, { .entity = ecs_id(Lifetime), .kind = EcsF32 });
  ecs_add_pair(world, ecs_id(Lifetime), EcsIsA, EcsSeconds);
  ecs_set_hooks(world, Lifetime, { .ctor = ecs_ctor(Lifetime) });

  ECS_COMPONENT_DEFINE(world, Lifespan);
  ecs_primitive(world, { .entity = ecs_id(Lifespan), .kind = EcsF32 });
  ecs_add_pair(world, ecs_id(Lifespan), EcsIsA, EcsSeconds);
  ecs_set_hooks(world, Lifespan, { .ctor = ecs_ctor(Lifespan) });

  ecs_add_pair(world, ecs_id(Lifespan), EcsWith, ecs_id(Lifetime));

  ECS_COMPONENT_DEFINE(world, Size);
  ecs_primitive(world, { .entity = ecs_id(Size), .kind = EcsF32 });
  ecs_add_pair(world, ecs_id(Size), EcsIsA, EcsMeters);
  ecs_set_hooks(world, Size, { .ctor = ecs_ctor(Size) });

  ECS_COMPONENT_DEFINE(world, Orbiter);
  ecs_struct(world, {
    .entity = ecs_id(Orbiter),
    .members = {
      {
        .name = "height",
        .type = ecs_id(ecs_f32_t),
        .offset = offsetof(Orbiter, height),
        .unit = EcsMeters,
      },
      {
        .name = "distance",
        .type = ecs_id(ecs_f32_t),
        .offset = offsetof(Orbiter, distance),
        .unit = EcsMeters,
      },
    },
  });
  ecs_set_hooks(world, Orbiter, { .ctor = ecs_ctor(Orbiter) });

  ECS_TAG_DEFINE(world, LookingAt);

  ECS_COMPONENT_DEFINE(world, Firework);
  ecs_set_hooks(world, Firework, {
    .ctor = ecs_ctor(Firework),
    .dtor = ecs_dtor(Firework),
  });
  ECS_OBSERVER(world, OnAddFirework, EcsOnAdd, [in] Firework, [in] cvkm.Position3D);

  ECS_COMPONENT_DEFINE(world, FireworkParticle);
  ecs_set_hooks(world, FireworkParticle, {
    .ctor = ecs_ctor(FireworkParticle),
  });
  ECS_OBSERVER(world, OnRemoveFireworkParticle, EcsOnRemove,
    [in] FireworkParticle,
    [in] cvkm.Position3D,
    [in] Firework(up),
  );

  ECS_COMPONENT_DEFINE(world, ShouldFadeAway);
  ecs_primitive(world, { .entity = ecs_id(ShouldFadeAway), .kind = EcsI32 });

  ECS_SYSTEM(world, Age, EcsOnUpdate, [inout] Lifetime, [in] ?Lifespan);
  ECS_SYSTEM(world, Orbit, EcsOnUpdate,
    [out] cvkm.Position3D,
    [out] cvkm.Rotation3D,
    [in] Orbiter,
    [in] (LookingAt, cvkm.Position3D) || (LookingAt, $target),
    [in] ?cvkm.Position3D($target),
  );

  ECS_SYSTEM(world, SpawnFirework, EcsOnUpdate, 0);
  ecs_set_interval(world, ecs_id(SpawnFirework), 1.5f);

  static const float floor_vertices[] = {
    // Position
    -1.0f, 0.0f,  1.0f,
     1.0f, 0.0f,  1.0f,
     1.0f, 0.0f, -1.0f,
    -1.0f, 0.0f, -1.0f,
  };
  float* floor_vertices_buffer = malloc(FUN_COUNTOF(floor_vertices) * sizeof(floor_vertices[0]));
  memcpy(floor_vertices_buffer, floor_vertices, sizeof(floor_vertices));

  const ecs_entity_t floor_mesh = ecs_entity(world, {
    .set = ecs_values(
      {
        .type = ecs_id(MeshData),
        .ptr = &(MeshData){
          .data = floor_vertices_buffer,
          .vertices_count = 4,
          .primitive = GLI_TRIANGLE_FAN,
          .vertex_attributes = {
            { .type = GLI_VEC3 },
          },
        }
      }
    )
  });

  particle_mesh = ecs_entity(world, {
    .set = ecs_values(
      {
        .type = ecs_id(MeshData),
        .ptr = &(MeshData){
          .vertices_count = 1,
          .primitive = GLI_POINTS,
        },
      }
    ),
  });

  static const char* particle_vertex_shader =
    "uniform float entitySize;\n"
    "uniform float entityLifetime;\n"
    "uniform float entityLifespan;\n"
    "uniform int entityShouldFadeAway;\n"
    "\n"
    "void main() {\n"
    "  vec4 view_position = view * model * vec4(0.0, 0.0, 0.0, 1.0);\n"
    "\n"
    "  float distance_to_camera = -view_position.z;\n"
    "\n"
    "  float focal_length_normalized = projection[1][1];\n"
    "  float focal_length_pixels = (resolution.y * 0.5) * focal_length_normalized;\n"
    "  float size = entitySize * (entityShouldFadeAway != 0\n"
    "    ? (entityLifespan - entityLifetime) / entityLifespan\n"
    "    : entitySize);\n"
    "  gl_PointSize = size * focal_length_pixels / distance_to_camera;\n"
    "\n"
    "  gl_Position = projection * view_position;\n"
    "}\n";

  static const char* particle_fragment_shader =
    "uniform vec4 entityColor;\n"
    "\n"
    "out vec4 fragment_color;\n"
    "\n"
    "void main() {\n"
    "  fragment_color = entityColor;\n"
    "}\n";

  static const char* floor_vertex_shader =
    "layout(location = 0) in vec3 position_attrib;\n"
    "\n"
    "out vec3 position;\n"
    "\n"
    "void main() {\n"
    "  position = position_attrib;\n"
    "  gl_Position = projection * view * model * vec4(position, 1.0);\n"
    "}\n";

  static const char* floor_fragment_shader =
    "in vec3 position;\n"
    "\n"
    "out vec4 fragment_color;\n"
    "\n"
    "void main() {\n"
    "  float distance = length(position);\n"
    "  fragment_color = vec4(vec3(distance * -0.8 + 1.0), 1.0);\n"
    "}\n";

  particle_shader_program = ecs_entity(world, {
    .name = "Particle shader program",
    .set = ecs_values(
      {
        .type = ecs_id(ShaderProgramSource),
        .ptr = &(ShaderProgramSource){
          .vertex_shader = strdup(particle_vertex_shader),
          .fragment_shader = strdup(particle_fragment_shader),
        },
      }
    ),
  });

  const ecs_entity_t floor_shader_program = ecs_entity(world, {
    .name = "Floor shader program",
    .set = ecs_values(
      {
        .type = ecs_id(ShaderProgramSource),
        .ptr = &(ShaderProgramSource){
          .vertex_shader = strdup(floor_vertex_shader),
          .fragment_shader = strdup(floor_fragment_shader),
        },
      }
    ),
  });

  ecs_entity(world, {
    .name = "Floor",
    .add = ecs_ids(
      ecs_pair(ecs_id(Uses), floor_shader_program),
      ecs_pair(ecs_id(Uses), floor_mesh)
    ),
    .set = ecs_values(
      { .type = ecs_id(Position3D), .ptr = &(Position3D){ { 0.0f, -5.0f, 0.0f } } },
      { .type = ecs_id(Scale3D), .ptr = &(Scale3D){ { 10.0f, 1.0f, 10.0f } } }
    ),
  });

  ecs_add(world, ecs_id(Camera3D), Orbiter);
  ecs_set_pair_second(world, ecs_id(Camera3D), LookingAt, Position3D, CVKM_VEC3_ZERO_INIT);

  ecs_set_id(world, ecs_id(Window), ecs_id(Window), sizeof(GLitchWindow), &(GLitchWindow){
    .name = "Funomenal tests",
    .size = { { 800, 600 } },
  });

  ecs_randomize_timers(world);

#ifdef GLI_EMSCRIPTEN
  emscripten_request_animation_frame_loop(emscripten_main_loop, world);
#else
  ecs_app_run(world, &(ecs_app_desc_t){
    .target_fps = 60,
    .frames = 0,
    .enable_rest = true,
    .enable_stats = true,
  });

  ecs_fini(world);
#endif
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
