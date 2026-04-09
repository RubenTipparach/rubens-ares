/*
  libco.wasm (2024)
  Emscripten/WASM fiber implementation using Asyncify
*/

#define LIBCO_C
#include "libco.h"

#include <stdlib.h>
#include <string.h>
#include <emscripten/fiber.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASYNCIFY_STACK_SIZE 65536

typedef struct {
  emscripten_fiber_t fiber;
  void (*coentry)(void);
  void *c_stack;
  void *asyncify_stack;
} cothread_struct;

static cothread_struct co_primary;
static cothread_struct *co_running = 0;
static char co_primary_asyncify_stack[ASYNCIFY_STACK_SIZE];

/* Entry trampoline: emscripten_fiber requires em_arg_callback_func signature */
static void co_thunk(void *arg) {
  cothread_struct *thread = (cothread_struct *)arg;
  thread->coentry();
  /* If coentry returns, we have nowhere to go — just loop forever.
     In practice, ares coroutines never return. */
  for(;;) emscripten_fiber_swap(&thread->fiber, &co_primary.fiber);
}

cothread_t co_active(void) {
  if(!co_running) {
    co_running = &co_primary;
    emscripten_fiber_init_from_current_context(
      &co_primary.fiber,
      co_primary_asyncify_stack,
      ASYNCIFY_STACK_SIZE
    );
  }
  return (cothread_t)co_running;
}

cothread_t co_derive(void *memory, unsigned int size, void (*coentry)(void)) {
  if(!co_running) co_active();

  cothread_struct *thread = (cothread_struct *)memory;
  if(!thread) return 0;

  unsigned char *ptr = (unsigned char *)memory + sizeof(cothread_struct);
  unsigned int remaining = size - sizeof(cothread_struct);

  /* Split remaining memory between C stack and asyncify stack */
  unsigned int asyncify_size = ASYNCIFY_STACK_SIZE;
  if(asyncify_size > remaining / 2) asyncify_size = remaining / 2;
  unsigned int c_stack_size = remaining - asyncify_size;

  void *c_stack = ptr;
  void *asyncify_stack = ptr + c_stack_size;

  thread->coentry = coentry;
  thread->c_stack = NULL;
  thread->asyncify_stack = NULL;

  emscripten_fiber_init(
    &thread->fiber,
    co_thunk,
    thread,
    c_stack,
    c_stack_size,
    asyncify_stack,
    asyncify_size
  );

  return (cothread_t)thread;
}

cothread_t co_create(unsigned int size, void (*coentry)(void)) {
  if(!co_running) co_active();

  unsigned int total = size + sizeof(cothread_struct) + ASYNCIFY_STACK_SIZE;
  cothread_struct *thread = (cothread_struct *)malloc(total);
  if(!thread) return 0;

  unsigned char *ptr = (unsigned char *)thread + sizeof(cothread_struct);

  /* C stack gets `size` bytes, asyncify stack gets ASYNCIFY_STACK_SIZE */
  void *c_stack = ptr;
  void *asyncify_stack = ptr + size;

  thread->coentry = coentry;
  thread->c_stack = c_stack;
  thread->asyncify_stack = asyncify_stack;

  emscripten_fiber_init(
    &thread->fiber,
    co_thunk,
    thread,
    c_stack,
    size,
    asyncify_stack,
    ASYNCIFY_STACK_SIZE
  );

  return (cothread_t)thread;
}

void co_delete(cothread_t cothread) {
  if(cothread) {
    free(cothread);
  }
}

void co_switch(cothread_t cothread) {
  cothread_struct *old = co_running;
  co_running = (cothread_struct *)cothread;
  emscripten_fiber_swap(&old->fiber, &co_running->fiber);
}

int co_serializable(void) {
  return 0;
}

#ifdef __cplusplus
}
#endif
