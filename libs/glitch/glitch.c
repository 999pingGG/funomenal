// Before anything happens at all, the Windows API needs a crash course in staying out of the way...
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#define CVKM_NO
#define CVKM_ENABLE_FLECS
#include <cvkm.h>
#include <flecs.h>
#include <glitch.h>

#ifdef GLI_LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

typedef GLXContext (*glXCreateContextAttribsARBProc)(
  Display* display,
  GLXFBConfig config,
  GLXContext share_context,
  Bool direct,
  const int* attrib_list
);

static GLXContext context;
static Display* display;
static Window window;
static Atom wm_delete;
#elif defined(GLI_WINDOWS)
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(
  HDC device_context_handle,
  HGLRC gl_rendering_context_handle,
  const int* attribute_list
);

static HGLRC context;
static HWND window_handle;
static HDC device_context_handle;
#else
#error Unsupported platform.
#endif

#define GLI_INITIAL_WIDTH 800
#define GLI_INITIAL_HEIGHT 600

// TODO: Move this into an ECS component.
static int width = GLI_INITIAL_WIDTH;
static int height = GLI_INITIAL_HEIGHT;

static GLuint attributeless_vertex_array;

// The number of terms that we use in the ecs_query_desc_t::terms of the shader program.
#define GLI_RESERVED_TERMS 9
// This is less than the previous int because of EcsOr usage.
#define GLI_SHADER_QUERY_TERMS 8

static_assert(GLI_RESERVED_TERMS + GLI_MAX_UNIFORMS <= FLECS_TERM_COUNT_MAX, "You need to lower GLI_MAX_UNIFORMS!");

#pragma region Built-in uniforms, update those variables together!
typedef struct built_ins_t {
  vkm_mat4 model, view, projection;
  vkm_vec2 resolution;
  float time, delta_time;
} built_ins_t;

static const char* built_in_names[] = {
  "model",
  "view",
  "projection",
  "resolution",
  "time",
  "delta_time",
};

#define GLI_BUILT_IN_UNIFORMS_COUNT GLI_COUNTOF(built_in_names)

static GLuint built_ins_uniform_buffer;
#pragma endregion

typedef GLuint (*glCreateShaderProc)(GLenum shaderType);
static glCreateShaderProc glCreateShader;
typedef void (*glDeleteShaderProc)(GLuint shader);
static glDeleteShaderProc glDeleteShader;
typedef void (*glShaderSourceProc)(GLuint shader, GLsizei count, const GLchar** string, const GLint* length);
static glShaderSourceProc glShaderSource;
typedef void (*glCompileShaderProc)(GLuint shader);
static glCompileShaderProc glCompileShader;
typedef void (*glGetShaderivProc)(GLuint shader, GLenum pname, GLint* params);
static glGetShaderivProc glGetShaderiv;
typedef void (*glGetShaderInfoLogProc)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
static glGetShaderInfoLogProc glGetShaderInfoLog;
typedef GLuint (*glCreateProgramProc)(void);
static glCreateProgramProc glCreateProgram;
typedef void (*glDeleteProgramProc)(GLuint program);
static glDeleteProgramProc glDeleteProgram;
typedef void (*glAttachShaderProc)(GLuint program, GLuint shader);
static glAttachShaderProc glAttachShader;
typedef void (*glLinkProgramProc)(GLuint program);
static glLinkProgramProc glLinkProgram;
typedef void (*glGetProgramivProc)(GLuint program, GLenum pname, GLint* params);
static glGetProgramivProc glGetProgramiv;
typedef void (*glGetProgramInfoLogProc)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
static glGetProgramInfoLogProc glGetProgramInfoLog;
typedef void (*glGetActiveAttribProc)(
  GLuint program,
  GLuint index,
  GLsizei bufSize,
  GLsizei* length,
  GLint* size,
  GLenum* type,
  GLchar* name
);
static glGetActiveAttribProc glGetActiveAttrib;
typedef void (*glGetActiveUniformProc)(
  GLuint program,
  GLuint index,
  GLsizei bufSize,
  GLsizei* length,
  GLint* size,
  GLenum* type,
  GLchar* name
);
static glGetActiveUniformProc glGetActiveUniform;
typedef GLint (*glGetAttribLocationProc)(GLuint program, const GLchar* name);
static glGetAttribLocationProc glGetAttribLocation;
typedef GLint (*glGetUniformLocationProc)(GLuint program, const GLchar* name);
static glGetUniformLocationProc glGetUniformLocation;
typedef void (*glGenVertexArraysProc)(GLsizei n, GLuint* arrays);
static glGenVertexArraysProc glGenVertexArrays;
typedef void (*glDeleteVertexArraysProc)(GLsizei n, const GLuint *arrays);
static glDeleteVertexArraysProc glDeleteVertexArrays;
typedef void (*glBindVertexArrayProc)(GLuint array);
static glBindVertexArrayProc glBindVertexArray;
typedef void (*glGenBuffersProc)(GLsizei n, GLuint* buffers);
static glGenBuffersProc glGenBuffers;
typedef void (*glDeleteBuffersProc)(GLsizei n, const GLuint *buffers);
static glDeleteBuffersProc glDeleteBuffers;
typedef void (*glBindBufferProc)(GLenum target, GLuint buffer);
static glBindBufferProc glBindBuffer;
typedef void (*glBindBufferRangeProc)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
static glBindBufferRangeProc glBindBufferRange;
typedef void (*glVertexAttribPointerProc)(
  GLuint index,
  GLint size,
  GLenum type,
  GLboolean normalized,
  GLsizei stride,
  const void* pointer
);
static glVertexAttribPointerProc glVertexAttribPointer;
typedef void (*glVertexAttribIPointerProc)(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);
static glVertexAttribIPointerProc glVertexAttribIPointer;
typedef void (*glEnableVertexAttribArrayProc)(GLuint index);
static glEnableVertexAttribArrayProc glEnableVertexAttribArray;
typedef void (*glBufferDataProc)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
static glBufferDataProc glBufferData;
typedef void (*glUseProgramProc)(GLuint program);
static glUseProgramProc glUseProgram;
typedef void (*glUniform1fvProc)(GLint location, GLsizei count, const GLfloat *value);
static glUniform1fvProc glUniform1fv;
typedef void (*glUniform2fvProc)(GLint location, GLsizei count, const GLfloat *value);
static glUniform2fvProc glUniform2fv;
typedef void (*glUniform3fvProc)(GLint location, GLsizei count, const GLfloat *value);
static glUniform3fvProc glUniform3fv;
typedef void (*glUniform4fvProc)(GLint location, GLsizei count, const GLfloat *value);
static glUniform4fvProc glUniform4fv;
typedef void (*glUniform1ivProc)(GLint location, GLsizei count, const GLint *value);
static glUniform1ivProc glUniform1iv;
typedef void (*glUniform2ivProc)(GLint location, GLsizei count, const GLint *value);
static glUniform2ivProc glUniform2iv;
typedef void (*glUniform3ivProc)(GLint location, GLsizei count, const GLint *value);
static glUniform3ivProc glUniform3iv;
typedef void (*glUniform4ivProc)(GLint location, GLsizei count, const GLint *value);
static glUniform4ivProc glUniform4iv;
typedef void (*glUniform1uivProc)(GLint location, GLsizei count, const GLuint *value);
static glUniform1uivProc glUniform1uiv;
typedef void (*glUniform2uivProc)(GLint location, GLsizei count, const GLuint *value);
static glUniform2uivProc glUniform2uiv;
typedef void (*glUniform3uivProc)(GLint location, GLsizei count, const GLuint *value);
static glUniform3uivProc glUniform3uiv;
typedef void (*glUniform4uivProc)(GLint location, GLsizei count, const GLuint *value);
static glUniform4uivProc glUniform4uiv;
typedef void (*glUniformMatrix4fvProc)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static glUniformMatrix4fvProc glUniformMatrix4fv;

ECS_COMPONENT_DECLARE(MeshData);
ECS_COMPONENT_DECLARE(Mesh);
ECS_COMPONENT_DECLARE(ShaderProgramSource);
ECS_COMPONENT_DECLARE(ShaderProgram);
ECS_COMPONENT_DECLARE(Camera2D);
ECS_COMPONENT_DECLARE(Camera3D);
ECS_COMPONENT_DECLARE(Color);
ECS_COMPONENT_DECLARE(ClearColor);

ECS_TAG_DECLARE(Uses);

ECS_CTOR(MeshData, ptr, {
  *ptr = (MeshData){ 0 };
})

ECS_MOVE(MeshData, dst, src, {
  free(dst->data);
  free(dst->indices);
  *dst = *src;
  *src = (MeshData){ 0 };
})

ECS_DTOR(MeshData, ptr, {
  free(ptr->data);
  free(ptr->indices);
  *ptr = (MeshData){ 0 };
})

ECS_CTOR(Mesh, ptr, {
  *ptr = (Mesh){ 0 };
})

ECS_MOVE(Mesh, dst, src, {
  glDeleteBuffers(1, &dst->vertex_buffer);
  glDeleteVertexArrays(1, &dst->vertex_array);
  *dst = *src;
  *src = (Mesh){ 0 };
})

ECS_DTOR(Mesh, ptr, {
  glDeleteBuffers(1, &ptr->vertex_buffer);
  glDeleteVertexArrays(1, &ptr->vertex_array);
  *ptr = (Mesh){ 0 };
})

ECS_CTOR(ShaderProgramSource, ptr, {
  *ptr = (ShaderProgramSource){ 0 };
})

ECS_MOVE(ShaderProgramSource, dst, src, {
  free(dst->vertex_shader);
  free(dst->fragment_shader);
  *dst = *src;
  *src = (ShaderProgramSource){ 0 };
})

ECS_DTOR(ShaderProgramSource, ptr, {
  free(ptr->vertex_shader);
  free(ptr->fragment_shader);
  *ptr = (ShaderProgramSource){ 0 };
})

ECS_CTOR(ShaderProgram, ptr, {
  *ptr = (ShaderProgram){ 0 };
})

ECS_MOVE(ShaderProgram, dst, src, {
  free(dst->uniforms);
  glDeleteProgram(dst->program);
  *dst = *src;
  *src = (ShaderProgram){ 0 };
})

ECS_DTOR(ShaderProgram, ptr, {
  for (int j = 0; j < ptr->uniforms_count; j++) {
    free(ptr->uniforms[j].name);
  }
  free(ptr->uniforms);
  for (int j = 0; j < ptr->attributes_count; j++) {
    free(ptr->attributes[j].name);
  }
  free(ptr->attributes);
  glDeleteProgram(ptr->program);
  *ptr = (ShaderProgram){ 0 };
})

ECS_CTOR(Camera2D, ptr, {
  *ptr = (Camera2D){
    .view = CVKM_MAT4_IDENTITY,
    .projection = CVKM_MAT4_IDENTITY,
    .zoom = 1.0f,
  };
})

ECS_CTOR(Camera3D, ptr, {
  *ptr = (Camera3D){
    .view = CVKM_MAT4_IDENTITY,
    .projection = CVKM_MAT4_IDENTITY,
    .field_of_view = 80.0f,
    .near_plane = 0.1f,
    .far_plane = 1000.0f,
  };
})

ECS_CTOR(Color, ptr, {
  *ptr = (Color){ { 0.0f, 0.0f, 0.0f, 1.0f } };
})

ECS_CTOR(ClearColor, ptr, {
  *ptr = (ClearColor){ { 0.0f, 0.0f, 0.0f, 1.0f } };
})

static GLuint compile_shader(const GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  static const char* shader_copypasta =
    "#version 330 core\n"
    "layout(std140) uniform built_ins {\n"
    "  mat4 model, view, projection;\n"
    "  vec2 resolution;\n"
    "  float time, delta_time;\n"
    "};\n"
    "#line 1\n";
  const char* sources[2] = { shader_copypasta, source };
  glShaderSource(shader, 2, sources, NULL);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[1000];
    glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
    fprintf(stderr, "Shader compilation failed: %s\n", info_log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

typedef struct gli_type_info_t {
  GLenum type;
  short vector_components, size;
} gli_type_info_t;

static gli_type_info_t type_infos[] = {
  [GLI_BYTE]   = { .type = GL_BYTE,           .vector_components = 1, .size = 1  },
  [GLI_UBYTE]  = { .type = GL_UNSIGNED_BYTE,  .vector_components = 1, .size = 1  },
  [GLI_SHORT]  = { .type = GL_SHORT,          .vector_components = 1, .size = 2  },
  [GLI_USHORT] = { .type = GL_UNSIGNED_SHORT, .vector_components = 1, .size = 2  },
  [GLI_INT]    = { .type = GL_INT,            .vector_components = 1, .size = 4  },
  [GLI_UINT]   = { .type = GL_UNSIGNED_INT,   .vector_components = 1, .size = 4  },
  [GLI_FLOAT]  = { .type = GL_FLOAT,          .vector_components = 1, .size = 4  },
  [GLI_BVEC2]  = { .type = GL_BYTE,           .vector_components = 2, .size = 2  },
  [GLI_UBVEC2] = { .type = GL_UNSIGNED_BYTE,  .vector_components = 2, .size = 2  },
  [GLI_SVEC2]  = { .type = GL_SHORT,          .vector_components = 2, .size = 4  },
  [GLI_USVEC2] = { .type = GL_UNSIGNED_SHORT, .vector_components = 2, .size = 4  },
  [GLI_IVEC2]  = { .type = GL_INT,            .vector_components = 2, .size = 8  },
  [GLI_UVEC2]  = { .type = GL_UNSIGNED_INT,   .vector_components = 2, .size = 8  },
  [GLI_VEC2]   = { .type = GL_FLOAT,          .vector_components = 2, .size = 8  },
  [GLI_BVEC3]  = { .type = GL_BYTE,           .vector_components = 3, .size = 3  },
  [GLI_UBVEC3] = { .type = GL_UNSIGNED_BYTE,  .vector_components = 3, .size = 3  },
  [GLI_SVEC3]  = { .type = GL_SHORT,          .vector_components = 3, .size = 6  },
  [GLI_USVEC3] = { .type = GL_UNSIGNED_SHORT, .vector_components = 3, .size = 6  },
  [GLI_IVEC3]  = { .type = GL_INT,            .vector_components = 3, .size = 12 },
  [GLI_UVEC3]  = { .type = GL_UNSIGNED_INT,   .vector_components = 3, .size = 12 },
  [GLI_VEC3]   = { .type = GL_FLOAT,          .vector_components = 3, .size = 12 },
  [GLI_BVEC4]  = { .type = GL_BYTE,           .vector_components = 4, .size = 4  },
  [GLI_UBVEC4] = { .type = GL_UNSIGNED_BYTE,  .vector_components = 4, .size = 4  },
  [GLI_SVEC4]  = { .type = GL_SHORT,          .vector_components = 4, .size = 8  },
  [GLI_USVEC4] = { .type = GL_UNSIGNED_SHORT, .vector_components = 4, .size = 8  },
  [GLI_IVEC4]  = { .type = GL_INT,            .vector_components = 4, .size = 16 },
  [GLI_UVEC4]  = { .type = GL_UNSIGNED_INT,   .vector_components = 4, .size = 16 },
  [GLI_VEC4]   = { .type = GL_FLOAT,          .vector_components = 4, .size = 16 },
};

static void MakeMeshes(ecs_iter_t* it) {
  const MeshData* mesh_datas = ecs_field(it, MeshData, 0);

  for (int i = 0; i < it->count; i++) {
    const MeshData* mesh_data = mesh_datas + i;

    Mesh* mesh = ecs_ensure(it->world, it->entities[i], Mesh);

    mesh->vertices_count = mesh_data->vertices_count;
    mesh->indices_count = mesh_data->indices_count;
    mesh->primitive = mesh_data->primitive;
    assert(mesh->primitive);

    if (mesh_data->data) {
      glGenVertexArrays(1, &mesh->vertex_array);
      glBindVertexArray(mesh->vertex_array);

      glGenBuffers(1, &mesh->vertex_buffer);
      glBindBuffer(GL_ARRAY_BUFFER, mesh->vertex_buffer);

      // As we iterate the vertex attributes, the buffer size serves as an offset, too.
      GLsizeiptr buffer_size = 0;
      for (int j = 0; mesh_data->vertex_attributes[j].type && j < GLI_MAX_ATTRIBUTES; j++) {
        const gli_type_info_t info = type_infos[mesh_data->vertex_attributes[j].type];

        if (info.type == GL_FLOAT || mesh_data->vertex_attributes[j].convert_to_float) {
          glVertexAttribPointer(
            j,
            info.vector_components,
            info.type,
            mesh_data->vertex_attributes[j].is_normalized,
            0,
            (const GLvoid*)buffer_size
          );
        } else {
          glVertexAttribIPointer(j, info.vector_components, info.type, 0, (const GLvoid*)buffer_size);
        }

        glEnableVertexAttribArray(j);
        buffer_size += info.size * mesh_data->vertices_count;
      }

      glBufferData(GL_ARRAY_BUFFER, buffer_size, mesh_data->data, GL_STATIC_DRAW);

      if (mesh_data->indices) {
        glGenBuffers(1, &mesh->index_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->index_buffer);
        glBufferData(
          GL_ELEMENT_ARRAY_BUFFER,
          sizeof(*mesh_data->indices) * mesh_data->indices_count,
          mesh_data->indices,
          GL_STATIC_DRAW
        );
      }
    } else {
      mesh->vertex_array = attributeless_vertex_array;
    }

    ecs_modified(it->world, it->entities[i], Mesh);
    ecs_remove(it->world, it->entities[i], MeshData);
  }
}

static void CompileShaders(ecs_iter_t* it) {
  const ShaderProgramSource* sources = ecs_field(it, ShaderProgramSource, 0);

  for (int i = 0; i < it->count; i++) {
    const ShaderProgramSource* source = sources + i;

    GLuint vertex_shader = 0, fragment_shader = 0;
    ShaderProgram shader_program;

    vertex_shader = compile_shader(GL_VERTEX_SHADER, source->vertex_shader);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, source->fragment_shader);

    if (!vertex_shader || !fragment_shader) {
      ecs_delete(it->world, it->entities[i]);
      goto cleanup;
    }

    shader_program.program = glCreateProgram();
    glAttachShader(shader_program.program, vertex_shader);
    glAttachShader(shader_program.program, fragment_shader);
    glLinkProgram(shader_program.program);

    GLint success;
    glGetProgramiv(shader_program.program, GL_LINK_STATUS, &success);
    if (!success) {
      char info_log[1000];
      glGetProgramInfoLog(shader_program.program, sizeof(info_log), NULL, info_log);
      fprintf(stderr, "Program linking failed: %s\n", info_log);
      glDeleteProgram(shader_program.program);
      ecs_delete(it->world, it->entities[i]);
      goto cleanup;
    }

    GLint max_length;
    glGetProgramiv(shader_program.program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_length);
    char* name_buffer = malloc(max_length);

    glGetProgramiv(shader_program.program, GL_ACTIVE_UNIFORMS, &shader_program.uniforms_count);
    shader_program.uniforms = malloc(shader_program.uniforms_count * sizeof(gli_shader_input_data));
    for (int j = 0, skipped = 0; j < shader_program.uniforms_count; j++) {
      gli_shader_input_data* uniform = shader_program.uniforms + j - skipped;

      GLint size;
      glGetActiveUniform(shader_program.program, j, max_length, NULL, &size, &uniform->type, name_buffer);

      // Pretend built-in uniforms don't exist here.
      for (unsigned k = 0; k < GLI_COUNTOF(built_in_names); k++) {
        if (strcmp(name_buffer, built_in_names[k]) == 0) {
          skipped++;
          goto next;
        }
      }

      uniform->name = strdup(name_buffer);
      uniform->location = glGetUniformLocation(shader_program.program, name_buffer);
      next:;
    }

    // Assuming we always insert all the built-in uniforms into the shader code.
    shader_program.uniforms_count -= GLI_BUILT_IN_UNIFORMS_COUNT;
    assert(shader_program.uniforms_count >= 0);
    if (shader_program.uniforms_count > GLI_MAX_UNIFORMS) {
      // Trim the excess uniforms in case there are too many of them.
      shader_program.uniforms_count = GLI_MAX_UNIFORMS;
    }

    // Compact memory.
    if (shader_program.uniforms_count > 0) {
      shader_program.uniforms = realloc(
        shader_program.uniforms,
        shader_program.uniforms_count * sizeof(gli_shader_input_data)
      );
    } else {
      free(shader_program.uniforms);
      shader_program.uniforms = NULL;
    }

    free(name_buffer);

    glGetProgramiv(shader_program.program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_length);
    name_buffer = malloc(max_length);

    glGetProgramiv(shader_program.program, GL_ACTIVE_ATTRIBUTES, &shader_program.attributes_count);
    shader_program.attributes = malloc(shader_program.attributes_count * sizeof(gli_shader_input_data));
    for (int j = 0; j < shader_program.attributes_count; j++) {
      GLint size;
      glGetActiveAttrib(
        shader_program.program,
        j,
        max_length,
        NULL,
        &size,
        &shader_program.attributes[j].type,
        name_buffer
      );
      shader_program.attributes[j].name = strdup(name_buffer);
      shader_program.attributes[j].location = glGetAttribLocation(shader_program.program, name_buffer);
    }

    free(name_buffer);

    // Build the query to find all entities that provide the necessary uniforms.

    // If you change the terms here, remember to update the static variable called "reserved_terms"!!
    ecs_query_desc_t query_description = {
      .terms = {
        {
          .id = ecs_id(Position2D),
          .inout = EcsIn,
          .oper = EcsOr,
        },
        {
          .id = ecs_id(Position3D),
          .inout = EcsIn,
        },
        {
          .id = ecs_id(Rotation2D),
          .inout = EcsIn,
          .oper = EcsOptional,
        },
        {
          .id = ecs_id(Rotation3D),
          .inout = EcsIn,
          .oper = EcsOptional,
        },
        {
          .id = ecs_id(Scale2D),
          .inout = EcsIn,
          .oper = EcsOptional,
        },
        {
          .id = ecs_id(Scale3D),
          .inout = EcsIn,
          .oper = EcsOptional,
        },
        {
          .first.id = ecs_id(Uses),
          .second.id = it->entities[i],
          .inout = EcsInOutNone,
        },
        {
          .first.id = ecs_id(Uses),
          .second.name = "$mesh",
          .inout = EcsInOutNone,
        },
        {
          .id = ecs_id(Mesh),
          .src.name = "$mesh",
          .inout = EcsIn,
        },
      },
      .cache_kind = EcsQueryCacheAuto,
    };

    for (int j = 0, count = shader_program.uniforms_count, skipped_uniforms = 0; j < count; j++) {
      const gli_shader_input_data* uniform = shader_program.uniforms + j - skipped_uniforms;
      uint8_t* ecs_uniform_type = shader_program.ecs_uniform_types + j - skipped_uniforms;

      const bool provided_by_entity = strncmp("entity", uniform->name, 6) == 0;
      const char* component_name = uniform->name + (provided_by_entity ? 6 : 0);

      const ecs_entity_t component = ecs_lookup_symbol(it->world, component_name, false, false);
      if (!component) {
        printf("Component %s not found.\n", component_name);
        goto invalid_component;
      }

      bool type_matches = false;
      const EcsPrimitive* primitive = ecs_get(it->world, component, EcsPrimitive);
      if (primitive) {
        switch (uniform->type) {
        case GL_FLOAT:
          type_matches = primitive->kind == EcsF32;
          *ecs_uniform_type = GLI_FLOAT;
          break;
        case GL_INT:
          type_matches = primitive->kind == EcsI32;
          *ecs_uniform_type = GLI_INT;
          break;
        case GL_UNSIGNED_INT:
          type_matches = primitive->kind == EcsU32;
          *ecs_uniform_type = GLI_UINT;
          break;
        default:
          break;
        }
      }

      if (!type_matches) {
        switch (uniform->type) {
          case GL_FLOAT_VEC2:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_vec2));
            *ecs_uniform_type = GLI_VEC2;
            break;
          case GL_FLOAT_VEC3:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_vec3));
            *ecs_uniform_type = GLI_VEC3;
            break;
          case GL_FLOAT_VEC4:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_vec4));
            *ecs_uniform_type = GLI_VEC4;
            break;
          case GL_INT_VEC2:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_ivec2));
            *ecs_uniform_type = GLI_IVEC2;
            break;
          case GL_INT_VEC3:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_ivec3));
            *ecs_uniform_type = GLI_IVEC3;
            break;
          case GL_INT_VEC4:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_ivec4));
            *ecs_uniform_type = GLI_IVEC4;
            break;
          case GL_UNSIGNED_INT_VEC2:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_uvec2));
            *ecs_uniform_type = GLI_UVEC2;
            break;
          case GL_UNSIGNED_INT_VEC3:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_uvec3));
            *ecs_uniform_type = GLI_UVEC3;
            break;
          case GL_UNSIGNED_INT_VEC4:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_uvec4));
            *ecs_uniform_type = GLI_UVEC4;
            break;
          case GL_FLOAT_MAT4:
            type_matches = ecs_has_pair(it->world, component, EcsIsA, ecs_id(vkm_mat4));
            *ecs_uniform_type = GLI_MAT4;
            break;
          default:
            break;
        }
      }

      if (!type_matches) {
        char buffer[22];
        const char* name = ecs_get_name(it->world, component);
        if (!name) {
          name = ecs_get_symbol(it->world, component);
        }
        if (!name) {
          sprintf(buffer, "#%llu", (unsigned long long)component);
        }
        printf(
          "The type of the component %s doesn't match the type the shader requires (0x%x).\n",
          name ? name : buffer,
          uniform->type
        );
        goto invalid_component;
      }

      assert(j + GLI_RESERVED_TERMS - skipped_uniforms < FLECS_TERM_COUNT_MAX);
      query_description.terms[j + GLI_RESERVED_TERMS - skipped_uniforms] = (ecs_term_t){
        .id = component,
        .src.id = provided_by_entity ? 0 : it->entities[i],
        .inout = EcsIn,
      };

      continue;

    invalid_component:
      // Delete the uniform registry.
      shader_program.uniforms_count--;
      assert(shader_program.uniforms_count >= 0);

      if (shader_program.uniforms_count > 0) {
        memmove(
          shader_program.uniforms + j - skipped_uniforms,
          shader_program.uniforms + j - skipped_uniforms + 1,
          (shader_program.uniforms_count - j + skipped_uniforms) * sizeof(gli_shader_input_data)
        );
        shader_program.uniforms = realloc(shader_program.uniforms, shader_program.uniforms_count * sizeof(gli_shader_input_data));
      } else {
        free(shader_program.uniforms);
        shader_program.uniforms = NULL;
      }

      skipped_uniforms++;
    }

    shader_program.rendered_entities_query = ecs_query_init(it->world, &query_description);

    ecs_set_id(it->world, it->entities[i], ecs_id(ShaderProgram), sizeof(ShaderProgram), &shader_program);

    cleanup:
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
  }
}

static void PreRenderFrame(ecs_iter_t* it) {
  const ClearColor* clear_color = ecs_field(it, ClearColor, 0);
  Camera2D* camera_2d = ecs_field(it, Camera2D, 1);
  const Position2D* camera_2d_position = ecs_field(it, Position2D, 2);
  Camera3D* camera_3d = ecs_field(it, Camera3D, 3);
  const Position3D* camera_3d_position = ecs_field(it, Position3D, 4);
  const Rotation3D* camera_3d_rotation = ecs_field(it, Rotation3D, 5);

  if (camera_2d) {
    // Compute 2D projection matrix.
    const float zoom_factor = 1.0f / camera_2d->zoom;
    const float half_width = (float)width * 0.5f * zoom_factor;
    const float half_height = (float)height * 0.5f * zoom_factor;
    vkm_orthogonal(
      -half_width,
      half_width,
      -half_height,
      half_height,
      -1000.f,
      1000.f,
      &camera_2d->projection
    );

    // Compute 2D view matrix.
    camera_2d->view = CVKM_MAT4_IDENTITY;
    if (camera_2d_position) {
      vkm_vec2 offset_2d;
      vkm_invert(camera_2d_position, &offset_2d);
      vkm_translate(&camera_2d->view, &offset_2d);
    }
  }

  if (camera_3d) {
    // Compute 3D projection matrix.
    vkm_perspective(
      camera_3d->field_of_view * CVKM_DEG_2_RAD_F,
      (float)width / (float)height,
      camera_3d->near_plane,
      camera_3d->far_plane,
      &camera_3d->projection
    );

    // Compute 3D view matrix.
    camera_3d->view = CVKM_MAT4_IDENTITY;
    if (camera_3d_rotation) {
      vkm_quat conjugate;
      vkm_quat_conjugate(camera_3d_rotation, &conjugate);
      vkm_mat4 rotation;
      vkm_quat_to_mat4(&conjugate, &rotation);
      vkm_mat4_mul_rotation(&camera_3d->view, &rotation, &camera_3d->view);
    }

    if (camera_3d_position) {
      vkm_vec3 offset_3d;
      vkm_invert(camera_3d_position, &offset_3d);
      vkm_translate(&camera_3d->view, &offset_3d);
    }
  }

#ifdef GLI_LINUX
  while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);
    switch (event.type) {
      case ConfigureNotify:
        glViewport(0, 0, width = event.xconfigure.width, height = event.xconfigure.height);
        break;
      case ClientMessage:
        if ((Atom)event.xclient.data.l[0] == wm_delete) {
          ecs_quit(it->world);
        }
        break;
      default:
        break;
    }
  }
#else
  MSG message;
  while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      ecs_quit(it->world);
      break;
    }
    TranslateMessage(&message);
    DispatchMessage(&message);
  }
#endif

  if (clear_color) {
    glClearColor(clear_color->r, clear_color->g, clear_color->b, clear_color->a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
}

static void Render(ecs_iter_t* it) {
  const ShaderProgram* shader_programs = ecs_field(it, ShaderProgram, 0);
  const Camera2D* camera_2d = ecs_field(it, Camera2D, 1);
  const Camera3D* camera_3d = ecs_field(it, Camera3D, 2);

  // No camera? No rendering.
  static bool warned = false;
  if (!camera_2d && !camera_3d) {
    if (!warned) {
      warned = true;
      printf("Missing both 2D and 3D cameras!\n");
    }
    return;
  }

  warned = false;

  built_ins_t built_ins = {
    .resolution = { { (float)width, (float)height } },
    .time = (float)ecs_get_world_info(it->world)->world_time_total,
    .delta_time = it->delta_time,
  };

  for (int i = 0; i < it->count; i++) {
    const ShaderProgram* shader_program = shader_programs + i;

    glUseProgram(shader_program->program);

    ecs_iter_t rendered_entities_it = ecs_query_iter(it->world, shader_program->rendered_entities_query);
    while (ecs_query_next(&rendered_entities_it)) {
      const bool is_2d = ecs_field_id(&rendered_entities_it, 0) == ecs_id(Position2D);

      // Skip the rendering of this table of entities completely if the required camera isn't present.
      if (is_2d) {
        if (!camera_2d) {
          continue;
        }

        built_ins.view = camera_2d->view;
        built_ins.projection = camera_2d->projection;
      } else {
        if (!camera_3d) {
          continue;
        }

        built_ins.view = camera_3d->view;
        built_ins.projection = camera_3d->projection;
      }

      const void* positions = ecs_table_get_id(
        it->world,
        rendered_entities_it.table,
        is_2d ? ecs_id(Position2D) : ecs_id(Position3D),
        rendered_entities_it.offset
      );

      const Rotation2D* rotations_2d = ecs_field(&rendered_entities_it, Rotation2D, 1);
      const Rotation3D* rotations_3d = ecs_field(&rendered_entities_it, Rotation3D, 2);
      const Scale2D* scales_2d = ecs_field(&rendered_entities_it, Scale2D, 3);
      const Scale3D* scales_3d = ecs_field(&rendered_entities_it, Scale3D, 4);
      const Mesh* mesh = ecs_field(&rendered_entities_it, Mesh, 7);
      const void* uniform_components[GLI_MAX_UNIFORMS] = { 0 };
      for (int j = 0; j < shader_program->uniforms_count; j++) {
        const int8_t field_index = (int8_t)(j + GLI_SHADER_QUERY_TERMS);
        uniform_components[j] = ecs_field_w_size(
          &rendered_entities_it,
          ecs_field_size(&rendered_entities_it, field_index),
          field_index
        );
      }

      glBindVertexArray(mesh->vertex_array);

      for (int j = 0; j < rendered_entities_it.count; j++) {
        built_ins.model = CVKM_MAT4_IDENTITY;

        if (is_2d) {
          const Position2D* position = (Position2D*)positions + i;
          vkm_translate(&built_ins.model, position);

          if (rotations_2d) {
            vkm_rotate(&built_ins.model, rotations_2d[j], &(vkm_vec3){ { 0.0f, 0.0f, 1.0f } });
          }

          if (scales_2d) {
            vkm_scale(&built_ins.model, &(vkm_vec3){ { scales_2d[j].x, scales_2d[j].y, 1.0f } });
          }
        } else {
          const Position3D* position = (Position3D*)positions + j;
          vkm_translate(&built_ins.model, position);

          if (rotations_3d) {
            vkm_mat4 rotation;
            vkm_quat_to_mat4(rotations_3d + j, &rotation);
            vkm_mat4_mul_rotation(&built_ins.model, &rotation, &built_ins.model);
          }

          if (scales_3d) {
            vkm_scale(&built_ins.model, scales_3d + j);
          }
        }

        glBindBuffer(GL_UNIFORM_BUFFER, built_ins_uniform_buffer);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(built_ins_t), &built_ins, GL_STREAM_DRAW);

        // Set per-entity uniforms.
        for (int k = 0; k < shader_program->uniforms_count; k++) {
          const gli_data_type_t data_type = shader_program->ecs_uniform_types[k];
          if (!data_type) {
            continue;
          }

          switch (data_type) {
            case GLI_INT:
              glUniform1iv(shader_program->uniforms[k].location, 1, (GLint*)uniform_components[k] + j);
              break;
            case GLI_UINT:
              glUniform1uiv(shader_program->uniforms[k].location, 1, (GLuint*)uniform_components[k] + j);
              break;
            case GLI_FLOAT:
              glUniform1fv(shader_program->uniforms[k].location, 1, (GLfloat*)uniform_components[k] + j);
              break;
            case GLI_IVEC2:
              glUniform2iv(shader_program->uniforms[k].location, 1, (GLint*)((vkm_ivec2*)uniform_components[k] + j));
              break;
            case GLI_UVEC2:
              glUniform2uiv(shader_program->uniforms[k].location, 1, (GLuint*)((vkm_uvec2*)uniform_components[k] + j));
              break;
            case GLI_VEC2:
              glUniform2fv(shader_program->uniforms[k].location, 1, (GLfloat*)((vkm_vec2*)uniform_components[k] + j));
              break;
            case GLI_IVEC3:
              glUniform3iv(shader_program->uniforms[k].location, 1, (GLint*)((vkm_ivec3*)uniform_components[k] + j));
              break;
            case GLI_UVEC3:
              glUniform3uiv(shader_program->uniforms[k].location, 1, (GLuint*)((vkm_uvec3*)uniform_components[k] + j));
              break;
            case GLI_VEC3:
              glUniform3fv(shader_program->uniforms[k].location, 1, (GLfloat*)((vkm_vec3*)uniform_components[k] + j));
              break;
            case GLI_IVEC4:
              glUniform4iv(shader_program->uniforms[k].location, 1, (GLint*)((vkm_ivec4*)uniform_components[k] + j));
              break;
            case GLI_UVEC4:
              glUniform4uiv(shader_program->uniforms[k].location, 1, (GLuint*)((vkm_uvec4*)uniform_components[k] + j));
              break;
            case GLI_VEC4:
              glUniform4fv(shader_program->uniforms[k].location, 1, (GLfloat*)((vkm_vec4*)uniform_components[k] + j));
              break;
            case GLI_MAT4:
              glUniformMatrix4fv(
                shader_program->uniforms[k].location,
                1,
                GL_FALSE,
                (GLfloat*)((vkm_mat4*)uniform_components[k] + j)
              );
              break;
            default:
              assert(false);
          }
        }

        if (mesh->index_buffer) {
          glDrawElements(mesh->primitive - 1, mesh->indices_count, GL_UNSIGNED_INT, NULL);
        } else {
          glDrawArrays(mesh->primitive - 1, 0, mesh->vertices_count);
        }
      }
    }
  }
}

static void PostRenderFrame(ecs_iter_t* it) {
  (void)it;
#ifdef GLI_LINUX
  glXSwapBuffers(display, window);
#else
  SwapBuffers(device_context_handle);
#endif

  GLenum error;
  while ((error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "OpenGL error 0x%x\n", error);
  }
}

#ifdef GLI_LINUX
#define gli_get_proc_address(fun) glXGetProcAddressARB((const GLubyte*)#fun)
#else
#define gli_get_proc_address(fun) wglGetProcAddress((LPCSTR)#fun)
#endif

#define GLI_LOAD_PROC_ADDRESS(fun) do {\
  fun = (fun##Proc)gli_get_proc_address(fun);\
  if (!fun){\
    fprintf(stderr, "Failed to load "#fun"\n");\
    return;\
  }\
} while (false)

static void fini(ecs_world_t* world, void* unused) {
  (void)world;
  (void)unused;
#ifdef GLI_LINUX
  glXMakeCurrent(display, None, NULL);
  glXDestroyContext(display, context);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
#else
  wglMakeCurrent(NULL, NULL);
  wglDeleteContext(context);
  ReleaseDC(window_handle, device_context_handle);
  DestroyWindow(window_handle);
#endif
}

#ifndef _MSC_VER
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
#endif
#endif

#ifdef GLI_WINDOWS
static LRESULT CALLBACK window_proc(
  const HWND handle,
  const UINT message,
  const WPARAM word_param,
  const LPARAM long_param
) {
  switch (message) {
    case WM_CLOSE:
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE:
      glViewport(0, 0, width = LOWORD(long_param), height = HIWORD(long_param));
      return 0;
    default:
      return DefWindowProc(handle, message, word_param, long_param);
  }
}
#endif

void glitchImport(ecs_world_t* world) {
  ECS_MODULE(world, glitch);

  ECS_IMPORT(world, cvkm);

  ecs_atfini(world, fini, NULL);

  ECS_COMPONENT_DEFINE(world, MeshData);
  ECS_COMPONENT_DEFINE(world, Mesh);
  ECS_COMPONENT_DEFINE(world, ShaderProgramSource);
  ECS_COMPONENT_DEFINE(world, ShaderProgram);
  ECS_COMPONENT_DEFINE(world, Camera2D);
  ecs_add_pair(world, ecs_id(Camera2D), EcsWith, ecs_id(Position2D));
  ecs_struct(world, {
    .entity = ecs_id(Camera2D),
    .members = {
      {
        .name = "view",
        .type = ecs_id(vkm_mat4),
        .offset = offsetof(Camera2D, view),
      },
      {
        .name = "projection",
        .type = ecs_id(vkm_mat4),
        .offset = offsetof(Camera2D, projection),
      },
      {
        .name = "zoom",
        .type = ecs_id(ecs_f32_t),
        .offset = offsetof(Camera2D, zoom),
      },
    },
  });
  ECS_COMPONENT_DEFINE(world, Camera3D);
  ecs_add_pair(world, ecs_id(Camera3D), EcsWith, ecs_id(Position3D));
  ecs_add_pair(world, ecs_id(Camera3D), EcsWith, ecs_id(Rotation3D));
  ecs_struct(world, {
    .entity = ecs_id(Camera3D),
    .members = {
      {
        .name = "view",
        .type = ecs_id(vkm_mat4),
        .offset = offsetof(Camera3D, view),
      },
      {
        .name = "projection",
        .type = ecs_id(vkm_mat4),
        .offset = offsetof(Camera3D, projection),
      },
      {
        .name = "field_of_view",
        .type = ecs_id(ecs_f32_t),
        .offset = offsetof(Camera3D, field_of_view),
      },
      {
        .name = "near_plane",
        .type = ecs_id(ecs_f32_t),
        .offset = offsetof(Camera3D, near_plane),
      },
      {
        .name = "far_plane",
        .type = ecs_id(ecs_f32_t),
        .offset = offsetof(Camera3D, far_plane),
      },
    },
  });
  ECS_COMPONENT_DEFINE(world, Color);
  ecs_add_pair(world, ecs_id(Color), EcsIsA, ecs_id(vkm_vec4));
  ECS_COMPONENT_DEFINE(world, ClearColor);
  ecs_add_pair(world, ecs_id(ClearColor), EcsIsA, ecs_id(Color));

  ECS_TAG_DEFINE(world, Uses);

#define GLI_SET_HOOKS(component) ecs_set_hooks(\
  world,\
  component,\
  {\
    .ctor = ecs_ctor(component),\
    .move = ecs_move(component),\
    .dtor = ecs_dtor(component),\
  }\
)

  GLI_SET_HOOKS(MeshData);
  GLI_SET_HOOKS(Mesh);
  GLI_SET_HOOKS(ShaderProgramSource);
  GLI_SET_HOOKS(ShaderProgram);
  ecs_set_hooks(world, Camera2D, { .ctor = ecs_ctor(Camera2D) });
  ecs_set_hooks(world, Camera3D, { .ctor = ecs_ctor(Camera3D) });
  ecs_set_hooks(world, Color, { .ctor = ecs_ctor(Color) });
  ecs_set_hooks(world, ClearColor, { .ctor = ecs_ctor(ClearColor) });

  static const char* window_name = "GLitch";

#ifdef GLI_LINUX
  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "Cannot open display.\n");
    return;
  }

  // Attributes for the framebuffer configuration
  static int visual_attribs[] = {
    GLX_X_RENDERABLE, True,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 24,
    GLX_STENCIL_SIZE, 0,
    GLX_DOUBLEBUFFER, True,
    None
  };

  int framebuffer_count;
  GLXFBConfig* framebuffer_config = glXChooseFBConfig(
    display,
    DefaultScreen(display),
    visual_attribs,
    &framebuffer_count
  );
  if (!framebuffer_config) {
    fprintf(stderr, "Failed to retrieve a framebuffer configuration.\n");
    return;
  }
  // Pick the first suitable framebuffer configuration.
  GLXFBConfig best_config = framebuffer_config[0];
  XFree(framebuffer_config);

  // Get a visual info from the framebuffer configuration.
  XVisualInfo* visual_info = glXGetVisualFromFBConfig(display, best_config);
  if (!visual_info) {
    fprintf(stderr, "No appropriate visual found.\n");
    return;
  }

  const Window root_window = RootWindow(display, visual_info->screen);

  // Create a colormap and set window attributes.
  XSetWindowAttributes set_window_attributes = {
    .background_pixmap = None,
    .event_mask = StructureNotifyMask | ExposureMask | KeyPressMask,
    .colormap = XCreateColormap(display, root_window, visual_info->visual, AllocNone),
  };

  window = XCreateWindow(
    display,
    root_window,
    0,
    0,
    GLI_INITIAL_WIDTH,
    GLI_INITIAL_HEIGHT,
    0,
    visual_info->depth,
    InputOutput,
    visual_info->visual,
    CWBorderPixel | CWColormap | CWEventMask,
    &set_window_attributes
  );
  XFree(visual_info);

  if (!window) {
    fprintf(stderr, "Failed to create window.\n");
    return;
  }

  wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wm_delete, 1);

  XStoreName(display, window, window_name);
  XMapWindow(display, window);

  // Get the pointer to glXCreateContextAttribsARB for modern context creation
  glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
    (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");

  if (!glXCreateContextAttribsARB) {
    fprintf(stderr, "Failed to get the address of glXCreateContextAttribsARB.\n");
    return;
  }

  // Create a OpenGL context version 3.3
  context = glXCreateContextAttribsARB(
    display,
    best_config,
    0,
    True,
    (int[]){
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 3,
      GLX_CONTEXT_PROFILE_MASK_ARB,
      GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
      None,
    }
  );
  if (!context) {
    fprintf(stderr, "Failed to create OpenGL 3.3 context.\n");
    return;
  }

  // Make the context current
  glXMakeCurrent(display, window, context);
#else
  static const char* class_name = "GLitchWindowClass";

  if (!RegisterClass(
    &(WNDCLASS) {
      .style = CS_OWNDC,
      .lpfnWndProc = window_proc,
      .hInstance = GetModuleHandle(NULL),
      .hCursor = LoadCursor(NULL, IDC_ARROW),
      .lpszClassName = class_name,
    }
  )) {
    MessageBox(NULL, "Failed to register window class.", "Error", MB_OK);
    return;
  }

  window_handle = CreateWindowEx(
    0,
    class_name,
    window_name,
    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    GLI_INITIAL_WIDTH,
    GLI_INITIAL_HEIGHT,
    NULL,
    NULL,
    GetModuleHandle(NULL),
    NULL
  );
  if (!window_handle) {
    MessageBox(NULL, "Failed to create window.", "Error", MB_OK);
    return;
  }

  device_context_handle = GetDC(window_handle);

  // Set up the pixel format descriptor.
  const PIXELFORMATDESCRIPTOR pixel_format_descriptor = {
    .nSize = sizeof(PIXELFORMATDESCRIPTOR),
    .nVersion = 1,
    .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
    .iPixelType = PFD_TYPE_RGBA,
    .cColorBits = 32,
    .cDepthBits = 24,
    .cStencilBits = 0,
    .iLayerType = PFD_MAIN_PLANE,
  };

  const int pixel_format = ChoosePixelFormat(device_context_handle, &pixel_format_descriptor);
  if (pixel_format == 0) {
    MessageBox(NULL, "Failed to choose pixel format.", "Error", MB_OK);
    return;
  }

  if (!SetPixelFormat(device_context_handle, pixel_format, &pixel_format_descriptor)) {
    MessageBox(NULL, "Failed to set pixel format.", "Error", MB_OK);
    return;
  }

  const HGLRC dummy_context_handle = wglCreateContext(device_context_handle);
  if (!dummy_context_handle) {
    MessageBox(NULL, "Failed to create dummy OpenGL context.", "Error", MB_OK);
    return;
  }

  if (!wglMakeCurrent(device_context_handle, dummy_context_handle)) {
    MessageBox(NULL, "Failed to activate dummy OpenGL context.", "Error", MB_OK);
    return;
  }

  const PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
    (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

  if (!wglCreateContextAttribsARB) {
    MessageBox(NULL, "Failed to get the address of wglCreateContextAttribsARB", "Error", MB_OK);
    return;
  }

  context = wglCreateContextAttribsARB(
    device_context_handle,
    0,
    (int[]) {
      WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
      WGL_CONTEXT_MINOR_VERSION_ARB, 3,
      WGL_CONTEXT_PROFILE_MASK_ARB,
      WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
      0,
    }
  );

  if (!context) {
    MessageBox(NULL, "Failed to create OpenGL 3.3 context.", "Error", MB_OK);
    return;
  }

  wglMakeCurrent(device_context_handle, context);
  wglDeleteContext(dummy_context_handle);
#endif

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_PROGRAM_POINT_SIZE);

  GLI_LOAD_PROC_ADDRESS(glCreateShader);
  GLI_LOAD_PROC_ADDRESS(glDeleteShader);
  GLI_LOAD_PROC_ADDRESS(glShaderSource);
  GLI_LOAD_PROC_ADDRESS(glCompileShader);
  GLI_LOAD_PROC_ADDRESS(glGetShaderiv);
  GLI_LOAD_PROC_ADDRESS(glGetShaderInfoLog);
  GLI_LOAD_PROC_ADDRESS(glCreateProgram);
  GLI_LOAD_PROC_ADDRESS(glDeleteProgram);
  GLI_LOAD_PROC_ADDRESS(glAttachShader);
  GLI_LOAD_PROC_ADDRESS(glLinkProgram);
  GLI_LOAD_PROC_ADDRESS(glGetProgramiv);
  GLI_LOAD_PROC_ADDRESS(glGetProgramInfoLog);
  GLI_LOAD_PROC_ADDRESS(glGetActiveAttrib);
  GLI_LOAD_PROC_ADDRESS(glGetActiveUniform);
  GLI_LOAD_PROC_ADDRESS(glGetAttribLocation);
  GLI_LOAD_PROC_ADDRESS(glGetUniformLocation);
  GLI_LOAD_PROC_ADDRESS(glGenVertexArrays);
  GLI_LOAD_PROC_ADDRESS(glDeleteVertexArrays);
  GLI_LOAD_PROC_ADDRESS(glBindVertexArray);
  GLI_LOAD_PROC_ADDRESS(glGenBuffers);
  GLI_LOAD_PROC_ADDRESS(glDeleteBuffers);
  GLI_LOAD_PROC_ADDRESS(glBindBuffer);
  GLI_LOAD_PROC_ADDRESS(glBindBufferRange);
  GLI_LOAD_PROC_ADDRESS(glVertexAttribPointer);
  GLI_LOAD_PROC_ADDRESS(glVertexAttribIPointer);
  GLI_LOAD_PROC_ADDRESS(glEnableVertexAttribArray);
  GLI_LOAD_PROC_ADDRESS(glBufferData);
  GLI_LOAD_PROC_ADDRESS(glUseProgram);
  GLI_LOAD_PROC_ADDRESS(glUniform1fv);
  GLI_LOAD_PROC_ADDRESS(glUniform2fv);
  GLI_LOAD_PROC_ADDRESS(glUniform3fv);
  GLI_LOAD_PROC_ADDRESS(glUniform4fv);
  GLI_LOAD_PROC_ADDRESS(glUniform1iv);
  GLI_LOAD_PROC_ADDRESS(glUniform2iv);
  GLI_LOAD_PROC_ADDRESS(glUniform3iv);
  GLI_LOAD_PROC_ADDRESS(glUniform4iv);
  GLI_LOAD_PROC_ADDRESS(glUniform1uiv);
  GLI_LOAD_PROC_ADDRESS(glUniform2uiv);
  GLI_LOAD_PROC_ADDRESS(glUniform3uiv);
  GLI_LOAD_PROC_ADDRESS(glUniform4uiv);
  GLI_LOAD_PROC_ADDRESS(glUniformMatrix4fv);

  ECS_SYSTEM(world, MakeMeshes, EcsOnLoad, [in] MeshData, [out] !Mesh);
  ecs_system(world, {
    .entity = ecs_entity(world, {
      .name = "CompileShaders",
      .add = ecs_ids(ecs_dependson(EcsOnLoad)),
    }),
    .query.expr = "[in] ShaderProgramSource, [out] !ShaderProgram",
    .callback = CompileShaders,
    .immediate = true,
  });
  ECS_SYSTEM(world, PreRenderFrame, EcsPreStore,
    [in] ?ClearColor(ClearColor),
    [inout] ?Camera2D($),
    [in] ?cvkm.Position2D(Camera2D),
    [inout] ?Camera3D($),
    [in] ?cvkm.Position3D(Camera3D),
    [in] ?cvkm.Rotation3D(Camera3D),
  );
  ECS_SYSTEM(world, Render, EcsOnStore, [in] ShaderProgram, [in] ?Camera2D(Camera2D), [in] ?Camera3D(Camera3D));
  ECS_SYSTEM(world, PostRenderFrame, EcsPostFrame, 0);

  ecs_singleton_add(world, ClearColor);
  ecs_singleton_add(world, Camera2D);
  ecs_singleton_add(world, Camera3D);

  glGenBuffers(1, &built_ins_uniform_buffer);
  glBindBuffer(GL_UNIFORM_BUFFER, built_ins_uniform_buffer);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(built_ins_t), NULL, GL_STREAM_DRAW);
  glBindBufferRange(GL_UNIFORM_BUFFER, 0, built_ins_uniform_buffer, 0, sizeof(built_ins_t));

  glGenVertexArrays(1, &attributeless_vertex_array);
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
