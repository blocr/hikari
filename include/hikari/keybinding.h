#if !defined(HIKARI_KEYBINDING_H)
#define HIKARI_KEYBINDING_H

#include <stdint.h>

struct hikari_keybinding {
  uint32_t keycode;

  void (*action)(void *);
  void *arg;
};

#endif
