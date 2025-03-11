#pragma once
#ifndef _FUNOMENAL_H_
#define _FUNOMENAL_H_

#define FUN_COUNTOF(x) (sizeof(x) / sizeof(x[0]))

void funomenalImport(ecs_world_t* world);
#endif
