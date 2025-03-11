#ifdef WIN32
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#include <stddef.h>
#include <stdlib.h>

#define CVKM_NO
#define CVKM_ENABLE_FLECS
#define CVKM_FLECS_IMPLEMENTATION
#include <cvkm.h>
#include <flecs.h>
#include <funomenal.h>
#include <glitch.h>

typedef float Lifetime;
typedef float Lifespan;
typedef float Size;

typedef struct Orbiter {
  float height, distance, speed;
} Orbiter;

static ECS_COMPONENT_DECLARE(Lifetime);
static ECS_COMPONENT_DECLARE(Lifespan);
static ECS_COMPONENT_DECLARE(Size);
static ECS_COMPONENT_DECLARE(Orbiter);
static ECS_TAG_DECLARE(LookingAt);

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
    .distance = 15.0f,
    .speed = 0.25f,
  };
})

static float random() {
  return (float)rand() / (float)RAND_MAX;
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

static void SpawnParticle(ecs_iter_t* it) {
  Velocity3D velocity = { random(), random(), random() };
  vkm_mul(&velocity, 30.0f, &velocity);
  vkm_sub(&velocity, 15.0f, &velocity);

  ecs_entity(it->world, {
    .add = ecs_ids(
      ecs_id(Position3D),
      ecs_id(Force3D),
      ecs_pair(ecs_id(Uses), particle_shader_program),
      ecs_pair(ecs_id(Uses), particle_mesh),
      ecs_id(Lifetime)
    ),
    .set = ecs_values(
      { .type = ecs_id(Velocity3D), .ptr = &velocity },
      { .type = ecs_id(Mass), .ptr = &(Mass){ 0.01f }},
      { .type = ecs_id(Color), .ptr = &(Color){ { random(), random(), random(), 1.0f } }},
      { .type = ecs_id(Lifespan), .ptr = &(Lifespan){ random() * 5.0f }},
      { .type = ecs_id(Size), .ptr = &(Size){ 1.0f } }
    ),
  });
}

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

  ECS_SYSTEM(world, Age, EcsOnUpdate, [inout] Lifetime, [in] ?Lifespan);
  ECS_SYSTEM(world, Orbit, EcsOnUpdate,
    [out] cvkm.Position3D,
    [out] cvkm.Rotation3D,
    [in] Orbiter,
    [in] (LookingAt, cvkm.Position3D) || (LookingAt, $target),
    [in] ?cvkm.Position3D($target),
  );

  ECS_SYSTEM(world, SpawnParticle, EcsOnUpdate, 0);
  ecs_set_interval(world, ecs_id(SpawnParticle), 1.0f / 20.0f);

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
    "\n"
    "void main() {\n"
    "  vec4 view_position = view * model * vec4(0.0, 0.0, 0.0, 1.0);\n"
    "\n"
    "  float distance_to_camera = -view_position.z;\n"
    "\n"
    "  float focal_length_normalized = projection[1][1];\n"
    "  float focal_length_pixels = (resolution.y * 0.5) * focal_length_normalized;\n"
    "  gl_PointSize = (entitySize * (entityLifespan - entityLifetime) / entityLifespan) * focal_length_pixels / distance_to_camera;\n"
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

  ecs_randomize_timers(world);

  ecs_app_run(world, &(ecs_app_desc_t){
    .target_fps = 60,
    .frames = 0,
    .enable_rest = true,
    .enable_stats = true,
  });

  ecs_fini(world);
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
